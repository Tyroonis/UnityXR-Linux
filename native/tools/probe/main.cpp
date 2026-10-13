// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Tyroonis
// linuxxr-probe - tests the OpenXR chain (WiVRn/Monado) without Unity.
//
// Reproduces exactly the sequence the Unity plugin uses in the Editor:
//   PreInit (Vulkan extensions, GPU) -> own VkInstance/VkDevice like Unity ->
//   OpenXR instance/session only afterwards ("Play") -> frame loop with a solid color per eye.

#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "core/log.h"
#include "core/runtime_select.h"
#include "core/vk_preinit.h"
#include "core/xr_context.h"
#include "core/xr_input.h"
#include "core/xr_math.h"
#include "probe_vulkan.h"

using namespace lxr;

namespace {

std::atomic<int> g_sigint{0};
void onSigint(int) { g_sigint++; }

struct Options {
  std::string command = "help";
  std::string runtime = "auto";
  int seconds = 30;
  TrackingOrigin origin = TrackingOrigin::Floor;
  bool haptics = true;
  bool quiet = false;
};

void usage() {
  std::printf(
      "linuxxr-probe - test the OpenXR chain without Unity\n\n"
      "Commands:\n"
      "  runtimes   Show the OpenXR runtimes found and whether they are reachable\n"
      "  info       Print OpenXR instance/system/extensions/Vulkan requirements\n"
      "  preinit    Simulate Unity's PreInit (Vulkan extensions + GPU choice)\n"
      "  run        Start a session, show solid colors on the headset, print poses/buttons\n\n"
      "Options:\n"
      "  --runtime X      auto | wivrn | monado | active | /path/manifest.json  (default: auto)\n"
      "  --seconds N      Duration for 'run' (default 30, 0 = until Ctrl+C)\n"
      "  --origin O       floor | device  (default floor)\n"
      "  --no-haptics     No vibration on A/X\n"
      "  --quiet          No per-second status line\n"
      "  --debug          Verbose log (= LINUXXR_LOG=debug)\n\n"
      "During 'run': trigger brightens the eye color, A/X turns it green, B/Y blue-white,\n"
      "grip vibrates. Ctrl+C exits cleanly.\n");
}

bool parse(int argc, char** argv, Options& o) {
  if (argc >= 2) o.command = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s requires a value\n", name);
        return nullptr;
      }
      return argv[++i];
    };
    if (a == "--runtime") {
      auto v = next("--runtime");
      if (!v) return false;
      o.runtime = v;
    } else if (a == "--seconds") {
      auto v = next("--seconds");
      if (!v) return false;
      o.seconds = std::atoi(v);
    } else if (a == "--origin") {
      auto v = next("--origin");
      if (!v) return false;
      o.origin = std::strcmp(v, "device") == 0 ? TrackingOrigin::Device : TrackingOrigin::Floor;
    } else if (a == "--no-haptics") {
      o.haptics = false;
    } else if (a == "--quiet") {
      o.quiet = true;
    } else if (a == "--debug") {
      setLogLevel(LogLevel::Debug);
    } else {
      std::fprintf(stderr, "Unknown option: %s\n", a.c_str());
      return false;
    }
  }
  return true;
}

int cmdRuntimes(const Options& o) {
  auto list = listRuntimeCandidates(o.runtime);
  if (list.empty()) {
    std::printf("No OpenXR runtime found. WiVRn: sudo apt install wivrn-server wivrn-dashboard\n");
    return 1;
  }
  std::printf("Candidates (order = priority for --runtime %s):\n", o.runtime.c_str());
  for (size_t i = 0; i < list.size(); ++i) {
    auto& c = list[i];
    std::printf("  %s %-8s %-18s %s\n     -> %s\n", i == 0 ? "*" : " ", runtimeKindName(c.kind), c.source.c_str(),
                c.manifestPath.c_str(), runtimeReachabilityHint(c).c_str());
  }
  return 0;
}

int cmdInfo(const Options& o) {
  XrContext ctx;
  XrContextConfig cfg;
  cfg.appName = "linuxxr-probe";
  cfg.runtimePreference = o.runtime;
  cfg.origin = o.origin;
  if (!ctx.createInstance(cfg)) return 1;
  std::printf("%s\n", ctx.describe().c_str());
  std::printf("Loader:       %s\n", openXRLoaderPath().c_str());
  std::printf("\nEnabled extensions:\n");
  for (auto& e : ctx.enabledExtensions()) std::printf("  %s\n", e.c_str());
  std::printf("\nAvailable extensions:\n");
  for (auto& e : ctx.availableExtensions()) std::printf("  %s\n", e.c_str());
  VulkanRequirements req;
  if (ctx.queryVulkanRequirements(req)) {
    std::printf("\nRuntime Vulkan instance extensions: %s\n", joinExtensions(req.instanceExtensions).c_str());
    std::printf("Runtime Vulkan device extensions:   %s\n", joinExtensions(req.deviceExtensions).c_str());
  }
  XrVersion minV = 0, maxV = 0;
  if (ctx.vulkanGraphicsRequirements(minV, maxV)) {
    std::printf("Vulkan API: %u.%u - %u.%u\n", XR_VERSION_MAJOR(minV), XR_VERSION_MINOR(minV), XR_VERSION_MAJOR(maxV),
                XR_VERSION_MINOR(maxV));
  }
  return 0;
}

int cmdPreinit(const Options& o) {
  VulkanPreInit pre;
  pre.prepare(o.runtime);
  std::printf("Runtime:        %s (%s)\n", pre.runtime().manifestPath.c_str(),
              pre.runtimeWasQueried() ? "queried" : "not reachable");
  std::printf("Source:         %s\n", pre.requirements().source.c_str());
  std::string inst = pre.instanceExtensionString();

  probe::ProbeVulkan vkp;
  if (!vkp.createInstance(inst, VK_API_VERSION_1_1)) return 1;
  VkPhysicalDevice pd = pre.choosePhysicalDevice(vkp.instance);
  std::string dev = pre.deviceExtensionString();
  pre.finish();
  std::printf("Instance ext.:  %s\n", inst.c_str());
  std::printf("GPU:            %s\n", pd ? pre.chosenDeviceInfo().name.c_str() : "(none)");
  std::printf("Device ext.:    %s\n", dev.c_str());
  if (!pre.droppedDeviceExtensions().empty())
    std::printf("Unsupported:    %s\n", joinExtensions(pre.droppedDeviceExtensions()).c_str());
  bool ok = vkp.createDevice(pd, dev);
  std::printf("vkCreateDevice: %s\n", ok ? "ok" : "FAILED");
  vkp.destroy();
  return ok ? 0 : 1;
}

const char* shortProfile(const std::string& p) {
  if (p.empty()) return "-";
  auto pos = p.rfind('/');
  return pos == std::string::npos ? p.c_str() : p.c_str() + pos + 1;
}

std::string buttons(const ControllerState& c) {
  std::string s;
  if (c.primaryButton) s += "P";
  if (c.secondaryButton) s += "S";
  if (c.thumbstickClick) s += "K";
  if (c.menuButton) s += "M";
  if (c.triggerTouch) s += "t";
  if (c.thumbrestTouch) s += "r";
  return s.empty() ? "·" : s;
}

int cmdRun(const Options& o) {
  signal(SIGINT, onSigint);
  signal(SIGTERM, onSigint);

  // 1) PreInit as at Editor startup
  VulkanPreInit pre;
  pre.prepare(o.runtime);
  probe::ProbeVulkan vkp;
  if (!vkp.createInstance(pre.instanceExtensionString(), VK_API_VERSION_1_1)) return 1;
  VkPhysicalDevice pd = pre.choosePhysicalDevice(vkp.instance);
  if (!vkp.createDevice(pd, pre.deviceExtensionString())) return 1;
  pre.finish();
  LXR_INFO("Vulkan ready on %s", vkp.deviceName.c_str());

  // 2) "Play": OpenXR instance + session
  XrContext ctx;
  XrContextConfig cfg;
  cfg.appName = "linuxxr-probe";
  cfg.runtimePreference = o.runtime;
  cfg.origin = o.origin;
  if (!ctx.createInstance(cfg)) {
    vkp.destroy();
    return 1;
  }
  VulkanBinding b{vkp.instance, vkp.physicalDevice, vkp.device, vkp.queueFamily, 0};
  if (!ctx.createSession(b)) {
    ctx.destroy();
    vkp.destroy();
    return 1;
  }

  XrInput input;
  input.init(ctx);

  const auto& vc = ctx.viewConfig(0);
  SwapchainInfo sc;
  const std::vector<int64_t> formats = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM,
                                        VK_FORMAT_B8G8R8A8_UNORM};
  if (!ctx.createSwapchain(formats, vc.recommendedImageRectWidth, vc.recommendedImageRectHeight, 2, 1,
                           XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                               XR_SWAPCHAIN_USAGE_SAMPLED_BIT,
                           sc)) {
    ctx.destroy();
    vkp.destroy();
    return 1;
  }

  auto start = std::chrono::steady_clock::now();
  auto lastPrint = start;
  uint64_t lastFrames = 0;
  bool exitRequested = false;
  bool hapticLatch[2] = {false, false};
  ControllerState ctrl[2];
  int rc = 0;

  std::printf("Running. Put on the headset - left red, right blue. Ctrl+C quits.\n");
  while (true) {
    if (g_sigint > 0 && !exitRequested) {
      exitRequested = true;
      std::printf("\nEnding session ...\n");
      ctx.requestExit();
      if (!ctx.sessionRunning()) break;
    }
    if (g_sigint > 1) break;
    if (o.seconds > 0 && !exitRequested &&
        std::chrono::steady_clock::now() - start > std::chrono::seconds(o.seconds)) {
      exitRequested = true;
      ctx.requestExit();
      if (!ctx.sessionRunning()) break;
    }

    ctx.pollEvents();
    if (ctx.exitRequested()) break;
    if (ctx.lossPending()) {
      LXR_WARN("Lost connection to the runtime");
      rc = 2;
      break;
    }
    if (!ctx.sessionRunning()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }

    XrFrameState fs;
    if (!ctx.waitFrame(fs)) continue;
    if (!ctx.beginFrame()) continue;

    // Inputs for exactly this frame
    input.sync();
    for (int h = 0; h < 2; ++h) input.readController(static_cast<Hand>(h), fs.predictedDisplayTime, ctrl[h]);
    if (o.haptics) {
      for (int h = 0; h < 2; ++h) {
        bool on = ctrl[h].squeeze > 0.8f;
        if (on && !hapticLatch[h]) input.vibrate(static_cast<Hand>(h), 0.6f, 0.08f);
        hapticLatch[h] = on;
      }
    }

    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                              {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    const XrCompositionLayerBaseHeader* layers[1] = {reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
    uint32_t layerCount = 0;

    if (fs.shouldRender) {
      ViewPair views;
      if (ctx.locateViews(fs.predictedDisplayTime, ctx.appSpace(), views) && views.valid() && ctx.acquireImage(sc)) {
        float colors[2][4];
        for (int e = 0; e < 2; ++e) {
          const ControllerState& c = ctrl[e];
          float boost = 0.25f + 0.75f * c.trigger;
          float base[3] = {e == 0 ? 0.6f : 0.05f, 0.05f, e == 0 ? 0.05f : 0.6f};
          if (c.primaryButton) base[0] = 0.05f, base[1] = 0.7f, base[2] = 0.05f;
          if (c.secondaryButton) base[0] = 0.5f, base[1] = 0.6f, base[2] = 0.9f;
          for (int i = 0; i < 3; ++i) colors[e][i] = std::fmin(1.0f, base[i] * boost * 1.6f);
          colors[e][3] = 1.0f;
        }
        vkp.clearImageLayers(sc.images[sc.acquiredIndex], colors, 2);
        ctx.releaseImage(sc);
        for (int e = 0; e < 2; ++e) {
          pv[e].pose = views.views[e].pose;
          pv[e].fov = views.views[e].fov;
          pv[e].subImage.swapchain = sc.handle;
          pv[e].subImage.imageRect = {{0, 0}, {static_cast<int32_t>(sc.width), static_cast<int32_t>(sc.height)}};
          pv[e].subImage.imageArrayIndex = static_cast<uint32_t>(e);
        }
        layer.space = ctx.appSpace();
        layer.viewCount = 2;
        layer.views = pv;
        layerCount = 1;
      }
    }
    ctx.endFrame(fs.predictedDisplayTime, layers, layerCount);

    auto now = std::chrono::steady_clock::now();
    if (!o.quiet && now - lastPrint >= std::chrono::seconds(1)) {
      double dt = std::chrono::duration<double>(now - lastPrint).count();
      uint64_t frames = ctx.framesSubmitted();
      double fps = (frames - lastFrames) / dt;
      lastFrames = frames;
      lastPrint = now;
      XrPosef head;
      Vec3 hp{};
      if (ctx.locateHead(fs.predictedDisplayTime, head)) hp = toUnity(head.position);
      std::printf(
          "[%-12s] %5.1f fps | Head %+.2f %+.2f %+.2f | L %s T%.2f G%.2f S%+.1f/%+.1f %s | R %s T%.2f G%.2f "
          "S%+.1f/%+.1f %s\n",
          XrContext::sessionStateName(ctx.sessionState()), fps, hp.x, hp.y, hp.z,
          ctrl[0].active ? shortProfile(input.currentProfile(kLeft)) : "-", ctrl[0].trigger, ctrl[0].squeeze,
          ctrl[0].thumbstick.x, ctrl[0].thumbstick.y, buttons(ctrl[0]).c_str(),
          ctrl[1].active ? shortProfile(input.currentProfile(kRight)) : "-", ctrl[1].trigger, ctrl[1].squeeze,
          ctrl[1].thumbstick.x, ctrl[1].thumbstick.y, buttons(ctrl[1]).c_str());
      std::fflush(stdout);
    }
  }

  std::printf("Frames submitted: %llu\n", static_cast<unsigned long long>(ctx.framesSubmitted()));
  ctx.destroySwapchain(sc);
  input.shutdown();
  ctx.destroy();
  vkp.destroy();
  return rc;
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  if (!parse(argc, argv, o)) {
    usage();
    return 2;
  }
  if (o.command == "runtimes") return cmdRuntimes(o);
  if (o.command == "info") return cmdInfo(o);
  if (o.command == "preinit") return cmdPreinit(o);
  if (o.command == "run") return cmdRun(o);
  usage();
  return o.command == "help" || o.command == "--help" || o.command == "-h" ? 0 : 2;
}
