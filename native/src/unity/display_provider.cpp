// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
// Display subsystem "LinuxXR Display": Unity renders, the image goes to the headset via OpenXR.
//
// Per frame (graphics thread):
//   PopulateNextFrameDesc  pollEvents -> xrWaitFrame -> xrBeginFrame -> xrLocateViews (VIEW and app space)
//                          -> render pass(es) for Unity: eye pose relative to the head + FOV
//   (Unity renders into its own textures - a texture array for single-pass instanced)
//   SubmitCurrentFrame     AccessTexture (barrier -> TRANSFER_SRC) -> AccessQueue(flush=true):
//                          xrAcquire/WaitSwapchainImage -> own command buffer copies the Unity texture
//                          into the swapchain image -> xrReleaseSwapchainImage -> xrEndFrame (projection layer)
//
// Why copy instead of rendering straight into the swapchain? Wrapping foreign VkImages in Unity textures
// is undocumented for Vulkan; the copy is robust and costs <0.5 ms on the GPU.
// Why AccessQueue? OpenXR runtimes submit work on the supplied queue during acquire/release.
// Nobody except Unity may touch Unity's queue outside AccessQueue - and flush=true guarantees that
// Unity's render commands for this frame have been submitted beforehand.

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>

#include "IUnityGraphics.h"
#include "IUnityGraphicsVulkan.h"
#include "core/log.h"
#include "core/vk_api.h"
#include "core/xr_math.h"
#include "unity/provider_context.h"

namespace lxr::unity {
namespace {

using Clock = std::chrono::steady_clock;
float msSince(Clock::time_point t) {
  return std::chrono::duration<float, std::milli>(Clock::now() - t).count();
}

constexpr int kQueueEventId = 0x4C58;  // 'LX'
constexpr int kRingSize = 3;

// ---- Vulkan copy resources on Unity's device -------------------------------------
struct CopyRing {
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd[kRingSize] = {};
  VkFence fence[kRingSize] = {};
  int next = 0;

  bool create(const UnityVulkanInstance& ui) {
    if (pool) return true;
    device = ui.device;
    queue = ui.graphicsQueue;
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = ui.queueFamilyIndex;
    if (!LXR_VK_OK(vk().vkCreateCommandPool(device, &pci, nullptr, &pool))) return false;
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = kRingSize;
    if (!LXR_VK_OK(vk().vkAllocateCommandBuffers(device, &ai, cmd))) return false;
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (auto& f : fence)
      if (!LXR_VK_OK(vk().vkCreateFence(device, &fci, nullptr, &f))) return false;
    return true;
  }

  void destroy() {
    if (!device) return;
    for (auto& f : fence) {
      if (f) {
        vk().vkWaitForFences(device, 1, &f, VK_TRUE, 1000000000ull);
        vk().vkDestroyFence(device, f, nullptr);
      }
      f = VK_NULL_HANDLE;
    }
    if (pool) vk().vkDestroyCommandPool(device, pool, nullptr);
    *this = CopyRing{};
  }
};

// ---- Per-frame data: filled in Populate, completed in the queue callback -----------
struct FrameJob {
  bool begun = false;
  bool render = false;
  XrTime displayTime = 0;
  XrFovf fov[2]{};
  XrPosef eyeInHead[2]{};
  XrPosef viewsApp[2]{};
  bool viewsAppValid = false;
  XrPosef head{};
  bool headValid = false;
  // Sources (Unity textures)
  VkImage src[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
  uint32_t srcLayer[2] = {0, 0};
  VkFormat srcFormat = VK_FORMAT_UNDEFINED;
  VkExtent3D srcExtent{0, 0, 1};
};

struct DisplayState {
  UnitySubsystemHandle handle = nullptr;
  uint64_t populateCalls = 0, submitCalls = 0, framesRendered = 0;

  SwapchainInfo swapchain;
  UnityXRRenderTextureId tex[2] = {0, 0};
  void* nativeTex[2] = {nullptr, nullptr};
  int texCount = 0;
  bool singlePass = true;
  bool srgb = true;
  uint32_t width = 0, height = 0;

  CopyRing ring;
  FrameJob job;
  FrameJob pending;

  std::mutex endMutex;
  std::condition_variable endCv;
  bool endPending = false;

  std::atomic<UnityXRRenderTextureId> mirrorTex0{0}, mirrorTex1{0};
  std::atomic<bool> mirrorArray{true};

  bool flipY = false;
  bool directSubmit = false;
  bool warnedAccess = false;

  // Statistics helpers (graphics thread only)
  Clock::time_point lastPopulate{};
  Clock::time_point lastEnd{};
  Clock::time_point submitStart{};
  XrTime lastPredicted = 0;
};
DisplayState g;

UnityXRPose toUnityPose(const XrPosef& p) {
  Pose u = toUnity(p);
  UnityXRPose r;
  r.position = {u.position.x, u.position.y, u.position.z};
  r.rotation = {u.rotation.x, u.rotation.y, u.rotation.z, u.rotation.w};
  return r;
}

UnityXRProjection toUnityProjection(const XrFovf& fov) {
  UnityXRProjection p{};
  p.type = kUnityXRProjectionTypeHalfAngles;
  TanAngles t = tanAngles(fov);
  p.data.halfAngles.left = t.left;
  p.data.halfAngles.right = t.right;
  p.data.halfAngles.top = t.up;
  p.data.halfAngles.bottom = t.down;
  return p;
}

int mirrorModeToUnity(int mode) {
  switch (mode) {
    case 1: return kUnityXRMirrorBlitRightEye;
    case 2: return kUnityXRMirrorBlitSideBySide;
    case 3: return kUnityXRMirrorBlitNone;
    default: return kUnityXRMirrorBlitLeftEye;
  }
}

PluginSettings currentSettings() {
  auto& c = context();
  std::lock_guard<std::mutex> l(c.settingsMutex);
  return c.settings;
}

// ---- Resources --------------------------------------------------------------------

void destroyTextures() {
  auto& c = context();
  for (int i = 0; i < 2; ++i) {
    if (g.tex[i] && g.handle && c.display) c.display->DestroyTexture(g.handle, g.tex[i]);
    g.tex[i] = 0;
    g.nativeTex[i] = nullptr;
  }
  g.texCount = 0;
  g.mirrorTex0 = 0;
  g.mirrorTex1 = 0;
}

void destroyResources() {
  auto& c = context();
  {
    std::unique_lock<std::mutex> l(g.endMutex);
    g.endCv.wait_for(l, std::chrono::milliseconds(500), [] { return !g.endPending; });
  }
  if (g.ring.device) vk().vkDeviceWaitIdle(g.ring.device);
  g.ring.destroy();
  if (g.swapchain.handle) c.xr.destroySwapchain(g.swapchain);
  destroyTextures();
  g.width = g.height = 0;
  g.job = {};
}

void cleanupHook() { destroyResources(); }

bool ensureResources(const UnityXRFrameSetupHints* hints) {
  auto& c = context();
  if (!g.ring.device) {
    if (auto* vkIf = unityVulkan()) {
      UnityVulkanInstance ui = vkIf->Instance();
      if (!ui.device || !g.ring.create(ui)) return false;
    } else {
      return false;
    }
  }
  PluginSettings s = currentSettings();
  const auto& vc = c.xr.viewConfig(0);
  float scale = s.renderScale * (hints && hints->appSetup.textureResolutionScale > 0 ? hints->appSetup.textureResolutionScale : 1.0f);
  uint32_t w = static_cast<uint32_t>(std::lround(vc.recommendedImageRectWidth * scale));
  uint32_t h = static_cast<uint32_t>(std::lround(vc.recommendedImageRectHeight * scale));
  if (vc.maxImageRectWidth) w = std::min(w, vc.maxImageRectWidth);
  if (vc.maxImageRectHeight) h = std::min(h, vc.maxImageRectHeight);
  w = std::max<uint32_t>(w, 64);
  h = std::max<uint32_t>(h, 64);
  bool singlePass = s.singlePass && (!hints || hints->appSetup.singlePassRendering);
  bool srgb = !hints || hints->appSetup.sRGB;

  // Recreate the swapchain on size or sRGB change (format must match the Unity texture -> direct copy)
  bool sizeChanged = (w != g.width || h != g.height || !g.swapchain.handle || srgb != g.srgb);
  bool texChanged = sizeChanged || singlePass != g.singlePass || srgb != g.srgb || g.texCount == 0;
  if (!texChanged) return true;

  if (sizeChanged) {
    if (g.swapchain.handle) {
      if (g.ring.device) vk().vkDeviceWaitIdle(g.ring.device);
      c.xr.destroySwapchain(g.swapchain);
    }
    std::vector<int64_t> formats = srgb ? std::vector<int64_t>{VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
                                                               VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM}
                                        : std::vector<int64_t>{VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
                                                               VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB};
    if (!c.xr.createSwapchain(formats, w, h, 2, 1,
                              XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                                  XR_SWAPCHAIN_USAGE_SAMPLED_BIT,
                              g.swapchain)) {
      c.setError("Could not create swapchain");
      return false;
    }
  }

  destroyTextures();
  int count = singlePass ? 1 : 2;
  for (int i = 0; i < count; ++i) {
    UnityXRRenderTextureDesc d{};
    d.colorFormat = kUnityXRRenderTextureFormatRGBA32;
    d.color.nativePtr = reinterpret_cast<void*>(kUnityXRRenderTextureIdDontCare);
    d.depthFormat = kUnityXRDepthTextureFormat24bitOrGreater;
    d.depth.nativePtr = reinterpret_cast<void*>(kUnityXRRenderTextureIdDontCare);
    d.width = w;
    d.height = h;
    d.textureArrayLength = singlePass ? 2 : 0;
    d.flags = srgb ? kUnityXRRenderTextureFlagsSRGB : 0;
    if (c.display->CreateTexture(g.handle, &d, &g.tex[i]) != kUnitySubsystemErrorCodeSuccess) {
      LXR_ERROR("CreateTexture failed (%ux%u, array %u)", w, h, d.textureArrayLength);
      destroyTextures();
      return false;
    }
  }
  g.texCount = count;
  g.singlePass = singlePass;
  g.srgb = srgb;
  g.width = w;
  g.height = h;
  g.mirrorArray = singlePass;
  g.mirrorTex0 = g.tex[0];
  g.mirrorTex1 = singlePass ? g.tex[0] : g.tex[1];
  c.stats.eyeWidth = w;
  c.stats.eyeHeight = h;
  c.stats.singlePass = singlePass ? 1 : 0;
  c.stats.renderScale = scale;
  c.stats.swapchainFormat = g.swapchain.format;
  LXR_INFO("Eye textures: %ux%u, %s, %s (render scale %.2f)", w, h,
           singlePass ? "Single-Pass-Instanced (Array)" : "Multi-Pass", srgb ? "sRGB" : "linear", scale);
  return true;
}

// ---- Frame completion (in queue context) ------------------------------------------

void imageBarrier(VkCommandBuffer cmd, VkImage image, uint32_t layers, VkImageLayout from, VkImageLayout to,
                  VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                  VkPipelineStageFlags dstStage) {
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
  b.oldLayout = from;
  b.newLayout = to;
  b.srcAccessMask = srcAccess;
  b.dstAccessMask = dstAccess;
  vk().vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

bool copyToSwapchain(const FrameJob& job, VkImage dst) {
  CopyRing& r = g.ring;
  int i = r.next;
  r.next = (r.next + 1) % kRingSize;
  if (vk().vkWaitForFences(r.device, 1, &r.fence[i], VK_TRUE, 1000000000ull) != VK_SUCCESS) {
    LXR_WARN("Copy fence timed out");
    return false;
  }
  vk().vkResetFences(r.device, 1, &r.fence[i]);
  VkCommandBuffer cmd = r.cmd[i];
  vk().vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vk().vkBeginCommandBuffer(cmd, &bi);

  imageBarrier(cmd, dst, 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

  const uint32_t w = g.swapchain.width, h = g.swapchain.height;
  const bool sameSize = job.srcExtent.width == w && job.srcExtent.height == h;
  const bool sameFormat = static_cast<int64_t>(job.srcFormat) == g.swapchain.format;
  for (uint32_t e = 0; e < 2; ++e) {
    if (sameSize && sameFormat && !g.flipY) {
      VkImageCopy region{};
      region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, job.srcLayer[e], 1};
      region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, e, 1};
      region.extent = {w, h, 1};
      vk().vkCmdCopyImage(cmd, job.src[e], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    } else {
      VkImageBlit blit{};
      blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, job.srcLayer[e], 1};
      blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, e, 1};
      int32_t sw = static_cast<int32_t>(job.srcExtent.width), sh = static_cast<int32_t>(job.srcExtent.height);
      blit.srcOffsets[0] = {0, g.flipY ? sh : 0, 0};
      blit.srcOffsets[1] = {sw, g.flipY ? 0 : sh, 1};
      blit.dstOffsets[0] = {0, 0, 0};
      blit.dstOffsets[1] = {static_cast<int32_t>(w), static_cast<int32_t>(h), 1};
      vk().vkCmdBlitImage(cmd, job.src[e], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                          sameSize ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
    }
  }

  imageBarrier(cmd, dst, 2, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  vk().vkEndCommandBuffer(cmd);

  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  return LXR_VK_OK(vk().vkQueueSubmit(r.queue, 1, &si, r.fence[i]));
}

void finishFrame() {
  auto& c = context();
  FrameJob& job = g.pending;
  XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  XrCompositionLayerProjectionView views[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                               {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  const XrCompositionLayerBaseHeader* layers[1] = {reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
  uint32_t layerCount = 0;

  if (job.render && g.swapchain.handle && g.ring.device && c.xr.acquireImage(g.swapchain)) {
    bool copied = copyToSwapchain(job, g.swapchain.images[g.swapchain.acquiredIndex]);
    c.xr.releaseImage(g.swapchain);
    if (copied) {
      for (int e = 0; e < 2; ++e) {
        // Pose Unity rendered with: head (from the input subsystem) * eye relative to the head.
        views[e].pose = job.headValid ? poseMul(job.head, job.eyeInHead[e])
                                      : (job.viewsAppValid ? job.viewsApp[e] : job.eyeInHead[e]);
        views[e].fov = job.fov[e];
        views[e].subImage.swapchain = g.swapchain.handle;
        views[e].subImage.imageRect = {{0, 0},
                                       {static_cast<int32_t>(g.swapchain.width),
                                        static_cast<int32_t>(g.swapchain.height)}};
        views[e].subImage.imageArrayIndex = static_cast<uint32_t>(e);
      }
      layer.layerFlags = XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
      layer.space = c.xr.appSpace();
      layer.viewCount = 2;
      layer.views = views;
      layerCount = 1;
    }
  }
  bool ended = c.xr.endFrame(job.displayTime, layers, layerCount);
  if (layerCount) g.framesRendered++;
  if (ended) {
    auto& st = c.stats;
    (layerCount ? st.framesRendered : st.framesEmpty)++;
    auto now = Clock::now();
    if (g.lastEnd != Clock::time_point{}) {
      float dt = std::chrono::duration<float>(now - g.lastEnd).count();
      if (dt > 0.0f && dt < 1.0f) FrameStats::ema(st.headsetFps, 1.0f / dt);
    }
    g.lastEnd = now;
    if (g.submitStart != Clock::time_point{}) FrameStats::ema(st.submitMs, msSince(g.submitStart));
  }

  {
    std::lock_guard<std::mutex> l(g.endMutex);
    g.endPending = false;
  }
  g.endCv.notify_all();
}

void UNITY_INTERFACE_API OnQueueAccess(int eventId, void*) {
  if (eventId != kQueueEventId) return;
  finishFrame();
}

void waitForPendingEnd() {
  std::unique_lock<std::mutex> l(g.endMutex);
  if (!g.endCv.wait_for(l, std::chrono::milliseconds(250), [] { return !g.endPending; })) {
    LXR_WARN("Previous frame did not complete (AccessQueue callback missing?) - "
             "try LINUXXR_DIRECT_SUBMIT=1");
    g.endPending = false;
  }
}

// ---- Graphics thread ------------------------------------------------------------------

UnitySubsystemErrorCode UNITY_INTERFACE_API Gfx_Start(UnitySubsystemHandle, void*, UnityXRRenderingCapabilities* caps) {
  caps->noSinglePassRenderingSupport = false;
  caps->invalidateRenderStateAfterEachCallback = true;
  caps->skipPresentToMainScreen = false;
  g.flipY = std::getenv("LINUXXR_FLIP_Y") && std::atoi(std::getenv("LINUXXR_FLIP_Y")) != 0;
  g.directSubmit = std::getenv("LINUXXR_DIRECT_SUBMIT") && std::atoi(std::getenv("LINUXXR_DIRECT_SUBMIT")) != 0;
  if (auto* vkIf = unityVulkan()) {
    UnityVulkanInstance ui = vkIf->Instance();
    if (ui.device && context().sessionReady) g.ring.create(ui);
  }
  LXR_INFO("Display (gfx thread) started%s%s", g.flipY ? ", Y flipped" : "",
           g.directSubmit ? ", direct submit" : "");
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Gfx_PopulateNextFrameDesc(UnitySubsystemHandle, void*,
                                                                       const UnityXRFrameSetupHints* hints,
                                                                       UnityXRNextFrameDesc* next) {
  auto& c = context();
  PluginSettings s = currentSettings();
  next->renderPassesCount = 0;
  next->mirrorBlitMode = mirrorModeToUnity(s.mirrorMode);
  {
    auto now = Clock::now();
    if (g.lastPopulate != Clock::time_point{}) {
      float dt = std::chrono::duration<float>(now - g.lastPopulate).count();
      if (dt > 0.0f && dt < 1.0f) FrameStats::ema(c.stats.appFps, 1.0f / dt);
    }
    g.lastPopulate = now;
  }
  if (g.populateCalls++ == 0) {
    LXR_INFO("First frame: singlePass=%d sRGB=%d zNear=%.3f zFar=%.1f scale=%.2f",
             hints->appSetup.singlePassRendering, hints->appSetup.sRGB, hints->appSetup.zNear,
             hints->appSetup.zFar, hints->appSetup.textureResolutionScale);
  }
  if (!c.sessionReady) return kUnitySubsystemErrorCodeSuccess;
  if (c.xr.lossPending()) {
    c.setError("Lost connection to the headset - exit Play Mode, check WiVRn/Quest and press Play again");
    return kUnitySubsystemErrorCodeSuccess;
  }

  waitForPendingEnd();

  int origin = c.pendingOrigin.exchange(-1);
  if (origin >= 0) c.xr.setTrackingOrigin(origin == 1 ? TrackingOrigin::Floor : TrackingOrigin::Device);

  c.xr.pollEvents();
  if (g.job.begun) {  // Unity never submitted the last frame -> end it empty
    c.xr.endFrame(g.job.displayTime, nullptr, 0);
    g.job.begun = false;
    c.stats.framesDropped++;
  }
  if (!c.xr.sessionRunning()) return kUnitySubsystemErrorCodeSuccess;

  XrFrameState fs;
  auto waitStart = Clock::now();
  if (!c.xr.waitFrame(fs)) return kUnitySubsystemErrorCodeSuccess;
  FrameStats::ema(c.stats.waitFrameMs, msSince(waitStart));
  if (fs.predictedDisplayPeriod > 0) {
    c.stats.displayPeriodMs = static_cast<float>(fs.predictedDisplayPeriod) / 1.0e6f;
    if (g.lastPredicted > 0 && fs.predictedDisplayTime > g.lastPredicted) {
      // Each skipped display interval between two of our frames is a missed frame on the headset.
      double intervals = static_cast<double>(fs.predictedDisplayTime - g.lastPredicted) /
                         static_cast<double>(fs.predictedDisplayPeriod);
      long skipped = std::lround(intervals) - 1;
      if (skipped > 0 && skipped < 1000) c.stats.framesMissed += static_cast<uint64_t>(skipped);
    }
  }
  g.lastPredicted = fs.predictedDisplayTime;
  if (!c.xr.beginFrame()) return kUnitySubsystemErrorCodeSuccess;
  c.stats.framesBegun++;
  g.job = FrameJob{};
  g.job.begun = true;
  g.job.displayTime = fs.predictedDisplayTime;
  if (!fs.shouldRender) {
    c.stats.framesNoRender++;
    return kUnitySubsystemErrorCodeSuccess;
  }

  ViewPair inHead;
  if (!c.xr.locateViews(fs.predictedDisplayTime, c.xr.viewSpace(), inHead) || !inHead.valid())
    return kUnitySubsystemErrorCodeSuccess;
  ViewPair inApp;
  g.job.viewsAppValid = c.xr.locateViews(fs.predictedDisplayTime, c.xr.appSpace(), inApp) && inApp.valid();
  for (int e = 0; e < 2; ++e) {
    g.job.fov[e] = inHead.views[e].fov;
    g.job.eyeInHead[e] = inHead.views[e].pose;
    g.job.viewsApp[e] = inApp.views[e].pose;
  }
  {
    std::lock_guard<std::mutex> l(c.head.mutex);
    c.head.eyeInHead[0] = g.job.eyeInHead[0];
    c.head.eyeInHead[1] = g.job.eyeInHead[1];
    g.job.head = c.head.pose;
    g.job.headValid = c.head.valid;
  }

  if (!ensureResources(hints)) return kUnitySubsystemErrorCodeSuccess;

  // Culling: midpoint between the eyes, enclosing FOV
  XrPosef mid = g.job.eyeInHead[0];
  mid.position.x = 0.5f * (g.job.eyeInHead[0].position.x + g.job.eyeInHead[1].position.x);
  mid.position.y = 0.5f * (g.job.eyeInHead[0].position.y + g.job.eyeInHead[1].position.y);
  mid.position.z = 0.5f * (g.job.eyeInHead[0].position.z + g.job.eyeInHead[1].position.z);
  float dx = g.job.eyeInHead[1].position.x - g.job.eyeInHead[0].position.x;
  float dy = g.job.eyeInHead[1].position.y - g.job.eyeInHead[0].position.y;
  float dz = g.job.eyeInHead[1].position.z - g.job.eyeInHead[0].position.z;
  next->cullingPasses[0].deviceAnchorToCullingPose = toUnityPose(mid);
  next->cullingPasses[0].projection = toUnityProjection(fovUnion(g.job.fov[0], g.job.fov[1]));
  next->cullingPasses[0].separation = std::sqrt(dx * dx + dy * dy + dz * dz);

  auto fillParams = [&](UnityXRNextFrameDesc::UnityXRRenderPass::UnityXRRenderParams& p, int eye, int slice) {
    p.deviceAnchorToEyePose = toUnityPose(g.job.eyeInHead[eye]);
    p.projection = toUnityProjection(g.job.fov[eye]);
    p.occlusionMeshId = 0;
    p.textureArraySlice = slice;
    p.viewportRect = {0.0f, 0.0f, 1.0f, 1.0f};
  };

  if (g.singlePass) {
    next->renderPassesCount = 1;
    auto& pass = next->renderPasses[0];
    pass.textureId = g.tex[0];
    pass.cullingPassIndex = 0;
    pass.renderParamsCount = 2;
    fillParams(pass.renderParams[0], 0, 0);
    fillParams(pass.renderParams[1], 1, 1);
  } else {
    next->renderPassesCount = 2;
    for (int e = 0; e < 2; ++e) {
      auto& pass = next->renderPasses[e];
      pass.textureId = g.tex[e];
      pass.cullingPassIndex = 0;
      pass.renderParamsCount = 1;
      fillParams(pass.renderParams[0], e, 0);
    }
  }
  g.job.render = true;
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Gfx_SubmitCurrentFrame(UnitySubsystemHandle, void*) {
  auto& c = context();
  g.submitCalls++;
  if (!g.job.begun) return kUnitySubsystemErrorCodeSuccess;
  g.submitStart = Clock::now();

  FrameJob job = g.job;
  g.job.begun = false;
  IUnityGraphicsVulkan* vkIf = unityVulkan();

  if (job.render && vkIf) {
    vkIf->EnsureOutsideRenderPass();
    UnityVulkanImage img[2]{};
    for (int i = 0; i < g.texCount && job.render; ++i) {
      if (!g.nativeTex[i]) {
        UnityXRRenderTextureDesc d{};
        if (c.display->QueryTextureDesc(g.handle, g.tex[i], &d) == kUnitySubsystemErrorCodeSuccess)
          g.nativeTex[i] = d.color.nativePtr;
      }
      if (!g.nativeTex[i] ||
          !vkIf->AccessTexture(g.nativeTex[i], UnityVulkanWholeImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                               kUnityVulkanResourceAccess_PipelineBarrier, &img[i])) {
        if (!g.warnedAccess) LXR_ERROR("Unity eye texture %d not accessible (AccessTexture)", i);
        g.warnedAccess = true;
        job.render = false;
      }
    }
    if (job.render) {
      job.srcFormat = img[0].format;
      job.srcExtent = img[0].extent;
      if (g.singlePass) {
        job.src[0] = job.src[1] = img[0].image;
        job.srcLayer[0] = 0;
        job.srcLayer[1] = 1;
      } else {
        job.src[0] = img[0].image;
        job.src[1] = img[1].image;
      }
      if (g.framesRendered == 0) {
        LXR_INFO("First image: Unity format %d %ux%u, swapchain format %lld %ux%u -> %s", job.srcFormat,
                 job.srcExtent.width, job.srcExtent.height, static_cast<long long>(g.swapchain.format),
                 g.swapchain.width, g.swapchain.height,
                 static_cast<int64_t>(job.srcFormat) == g.swapchain.format ? "copy" : "blit");
      }
    }
  }

  {
    std::lock_guard<std::mutex> l(g.endMutex);
    g.endPending = true;
  }
  g.pending = job;
  if (vkIf && !g.directSubmit) {
    vkIf->AccessQueue(OnQueueAccess, kQueueEventId, nullptr, true);
  } else {
    finishFrame();
  }
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Gfx_Stop(UnitySubsystemHandle, void*) {
  LXR_INFO("Display (gfx thread) stopped: %llu frames requested, %llu sent to the headset",
           static_cast<unsigned long long>(g.populateCalls), static_cast<unsigned long long>(g.framesRendered));
  auto& c = context();
  if (g.job.begun && c.sessionReady) {
    c.xr.endFrame(g.job.displayTime, nullptr, 0);
    g.job.begun = false;
  }
  destroyResources();
  return kUnitySubsystemErrorCodeSuccess;
}

// ---- Main thread -----------------------------------------------------------------

UnitySubsystemErrorCode UNITY_INTERFACE_API Main_UpdateDisplayState(UnitySubsystemHandle, void*,
                                                                     UnityXRDisplayState* state) {
  auto& c = context();
  state->focusLost = !(c.sessionReady && c.xr.sessionRunning());
  state->displayIsTransparent = false;
  state->contentProtectionEnabled = false;
  state->reprojectionMode = kUnityXRReprojectionModePositionAndOrientation;
  state->nativePtr = nullptr;
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Main_QueryMirrorViewBlitDesc(UnitySubsystemHandle, void*,
                                                                          const UnityXRMirrorViewBlitInfo info,
                                                                          UnityXRMirrorViewBlitDesc* desc) {
  desc->nativeBlitAvailable = false;
  desc->nativeBlitInvalidStates = false;
  desc->blitParamsCount = 0;
  UnityXRRenderTextureId t0 = g.mirrorTex0, t1 = g.mirrorTex1;
  if (!t0) return kUnitySubsystemErrorCodeSuccess;
  bool array = g.mirrorArray;
  int mode = info.mirrorBlitMode;
  if (mode == kUnityXRMirrorBlitDefault) mode = mirrorModeToUnity(currentSettings().mirrorMode);

  auto add = [&](int eye, UnityXRRectf dest) {
    auto& p = desc->blitParams[desc->blitParamsCount++];
    p.srcTexId = eye == 0 ? t0 : t1;
    p.srcTexArraySlice = array ? eye : 0;
    p.srcRect = {0.0f, 0.0f, 1.0f, 1.0f};
    p.destRect = dest;
  };
  switch (mode) {
    case kUnityXRMirrorBlitRightEye: add(1, {0, 0, 1, 1}); break;
    case kUnityXRMirrorBlitSideBySide:
    case kUnityXRMirrorBlitSideBySideOcclusionMesh:
      add(0, {0.0f, 0.0f, 0.5f, 1.0f});
      add(1, {0.5f, 0.0f, 0.5f, 1.0f});
      break;
    case kUnityXRMirrorBlitNone: break;
    default: add(0, {0, 0, 1, 1}); break;
  }
  return kUnitySubsystemErrorCodeSuccess;
}

// ---- Lifecycle ----------------------------------------------------------------

UnitySubsystemErrorCode UNITY_INTERFACE_API Lifecycle_Initialize(UnitySubsystemHandle handle, void*) {
  auto& c = context();
  g.handle = handle;
  g.populateCalls = g.submitCalls = g.framesRendered = 0;
  g.warnedAccess = false;
  g.lastPopulate = g.lastEnd = g.submitStart = Clock::time_point{};
  g.lastPredicted = 0;

  UnityXRDisplayGraphicsThreadProvider gfx{};
  gfx.userData = nullptr;
  gfx.Start = Gfx_Start;
  gfx.SubmitCurrentFrame = Gfx_SubmitCurrentFrame;
  gfx.PopulateNextFrameDesc = Gfx_PopulateNextFrameDesc;
  gfx.Stop = Gfx_Stop;
  gfx.BlitToMirrorViewRenderTarget = nullptr;
  if (c.display->RegisterProviderForGraphicsThread(handle, &gfx) != kUnitySubsystemErrorCodeSuccess) {
    LXR_ERROR("RegisterProviderForGraphicsThread failed");
    return kUnitySubsystemErrorCodeFailure;
  }

  UnityXRDisplayProvider main{};
  main.userData = nullptr;
  main.UpdateDisplayState = Main_UpdateDisplayState;
  main.QueryMirrorViewBlitDesc = Main_QueryMirrorViewBlitDesc;
  if (c.display->RegisterProvider(handle, &main) != kUnitySubsystemErrorCodeSuccess) {
    LXR_ERROR("RegisterProvider (Display) failed");
    return kUnitySubsystemErrorCodeFailure;
  }
  setSwapchainCleanup(cleanupHook);

  c.displayState = 1;
  LXR_INFO("Display subsystem initialized");
  return kUnitySubsystemErrorCodeSuccess;
}

UnitySubsystemErrorCode UNITY_INTERFACE_API Lifecycle_Start(UnitySubsystemHandle, void*) {
  auto& c = context();
  if (!acquireSession(0)) {
    if (c.xr.unavailable())
      LXR_WARN("VR not started (no headset / runtime) - Play Mode runs without VR.");
    else
      LXR_ERROR("Display failed to start - see the previous message (Vulkan? WiVRn running? Quest connected?).");
    releaseSession(0);
    return kUnitySubsystemErrorCodeFailure;
  }
  c.displayState = 2;
  LXR_INFO("Display subsystem started - image goes to %s", c.xr.systemName().c_str());
  return kUnitySubsystemErrorCodeSuccess;
}

void UNITY_INTERFACE_API Lifecycle_Stop(UnitySubsystemHandle, void*) {
  context().displayState = 1;
  LXR_INFO("Display subsystem stopped");
}

void UNITY_INTERFACE_API Lifecycle_Shutdown(UnitySubsystemHandle, void*) {
  releaseSession(0);
  context().displayState = 0;
  g.handle = nullptr;
  LXR_INFO("Display subsystem shut down");
}

}  // namespace

void registerDisplayLifecycle() {
  auto& c = context();
  if (!c.display) {
    LXR_ERROR("IUnityXRDisplayInterface not available");
    return;
  }
  static UnityLifecycleProvider lp{};
  lp.userData = nullptr;
  lp.Initialize = Lifecycle_Initialize;
  lp.Start = Lifecycle_Start;
  lp.Stop = Lifecycle_Stop;
  lp.Shutdown = Lifecycle_Shutdown;
  if (c.display->RegisterLifecycleProvider("LinuxXR", "LinuxXR Display", &lp) != kUnitySubsystemErrorCodeSuccess)
    LXR_WARN("Display subsystem not registered - Unity has not read LinuxXR's UnitySubsystemsManifest.json yet. "
             "Restart the Editor once (Window > XR > LinuxXR Setup).");
}

}  // namespace lxr::unity
