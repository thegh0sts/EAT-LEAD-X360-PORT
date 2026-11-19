// eatlead - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/cvar.h>
#include <rex/rex_app.h>
#include <rex/ui/overlay/achievement_notification.h>

#include "crash_win.h"

// Read by the native renderer (native-plugin/patches/0006-fps-counter.patch), which draws the counter and the
// achievement pop-ups into the picture itself, so they show in fullscreen. Same as the Bourne port.
REXCVAR_DEFINE_BOOL(show_fps, false, "UI", "Show a small frame rate counter in the corner (native renderer)");
REXCVAR_DEFINE_BOOL(show_fps_detail, false, "UI",
                    "With show_fps: also frame time, GPU, CPU and memory (native renderer)");
REXCVAR_DEFINE_BOOL(achievement_popups, true, "UI", "Show a pop-up when an achievement unlocks");
REXCVAR_DEFINE_DOUBLE(brightness, 1.0, "GPU", "Picture brightness, native renderer (1 = the game's)")
    .range(0.5, 2.0);

namespace eatlead {
// The log's first lines: the release, the build time and the launch options (version_log.cpp).
void LogVersion();
}  // namespace eatlead

class EatleadApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<EatleadApp>(new EatleadApp(ctx, "eatlead",
        PPCImageConfig));
  }

  void OnPostInitLogging() override {
    eatlead::crash::Install();
    eatlead::LogVersion();
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // The runtime loads no graphics plugin unless --gpu_plugin names one, and then the game
    // runs with a black window. Default to Skullburglar's native Vulkan renderer
    // (native-plugin/), as gpu_preload.cpp does; --gpu_plugin=xenos for the SDK's own.
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "native";
    }
    native_renderer_ = config.gpu_plugin == "native";
  }

  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog() override {
    // None with the native renderer, which draws the pop-up itself (native-plugin/patches/0006), or with
    // --achievement_popups=false.
    if (!REXCVAR_GET(achievement_popups) || native_renderer_) return nullptr;
    return rex::ReXApp::CreateAchievementNotificationDialog();
  }

 private:
  bool native_renderer_ = true;

 public:

  // Other hooks available for customization:
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
