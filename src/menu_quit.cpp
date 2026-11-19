// eatlead - B on the main menu closes the game.
//
// On the console, B does nothing on the main menu (New / Options / Select Map / Credits): there's no "back" from it.
// On PC that leaves no way out but the window's close button, so B there quits.
//
// How the main menu is told apart (from --menu_trace runs, 2026-11-19; menu_trace.cpp):
// - Menu screens open and close through the "UI Show Dialog" / "UI Hide Dialog" script actions
//   (sub_82259630 / sub_82259258, hooked in menu_trace.cpp, which call OnShow / OnHide here).
// - The main menu's Show action is the object at 0xF3AC0958, and the loading screen's is 0xF5D8B13C: the same
//   addresses in every run, at the first start and when quitting back from a level (the game allocates them in the
//   same order each time). Between the main menu's Show and the next loading screen we're in the front end.
// - In the front end, B in a submenu (Options, Continue's screen) closes it: a Hide runs in the same frame. On the
//   main menu itself B closes nothing. So: B pressed in the front end, and no Show or Hide within 150 ms -> quit.
// If the addresses ever differ (another build of the game), the front end is never recognised and B does what it
// always did: nothing is lost. --menu_b_quit=false turns it off.
//
// The hint: while the main menu is the screen on top (front end, and every screen opened since it has closed again,
// for 0.4 s so it doesn't flash during transitions), --menu_hint is set to 1 (b_quit, the first of --menu_hint_images) and the native renderer draws
// assets/hints/b_quit.png ("B Quit", our own art) in the bottom-right corner (native-plugin patch 0013,
// --menu_hint_pos in 1080p pixels).

#include "generated/default/eatlead_pch.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

REXCVAR_DEFINE_BOOL(menu_b_quit, true, "UI", "B on the main menu closes the game");
// Read by the native renderer each frame (native-plugin/patches/0013-menu-hint.patch); set here, not by hand.
REXCVAR_DEFINE_INT32(menu_hint, 0, "UI", "Menu hint the renderer draws: 0 none, N the Nth of menu_hint_images; set by the game");
REXCVAR_DEFINE_STRING(menu_hint_images, "b_quit", "UI", "The menu hints' images, hints/<name>.png, comma-separated");
REXCVAR_DEFINE_STRING(menu_hint_pos, "1702,982", "UI", "Where the menu hint goes: x,y in 1080p pixels of the picture");

namespace {

constexpr uint32_t kMainMenuShow = 0xF3AC0958;
constexpr uint32_t kLoadingScreenShow = 0xF5D8B13C;
constexpr auto kWait = std::chrono::milliseconds(150);

using Clock = std::chrono::steady_clock;

struct State {
  std::mutex mutex;
  bool front_end = false;  // the main menu has shown since the last loading screen
  bool submenu = false;    // a screen opened on top of it and hasn't closed yet
  Clock::time_point changed;
  bool pending = false;    // B pressed there; quit unless a screen opens or closes first
  Clock::time_point pressed;
  bool quitting = false;
};

State& S() {
  static State s;
  return s;
}

void RequestQuit() {
  REXLOG_INFO("menu_b_quit: B on the main menu, closing the game");
  auto* runtime = rex::Runtime::instance();
  if (!runtime) return;
  auto* window = runtime->display_window();
  auto* app = runtime->app_context();
  if (window && app) app->CallInUIThread([window] { window->RequestClose(); });
}

}  // namespace

namespace eatlead::menu_quit {

void OnShow(uint32_t action) {
  State& s = S();
  std::lock_guard<std::mutex> lock(s.mutex);
  s.pending = false;
  s.changed = Clock::now();
  if (action == kMainMenuShow) {
    if (!s.front_end) REXLOG_INFO("menu_b_quit: main menu");
    s.front_end = true;
    s.submenu = false;
  } else if (action == kLoadingScreenShow) {
    s.front_end = false;
  } else if (s.front_end) {
    s.submenu = true;
  }
}

void OnHide(uint32_t) {
  State& s = S();
  std::lock_guard<std::mutex> lock(s.mutex);
  s.pending = false;
  s.changed = Clock::now();
  s.submenu = false;
}

// Each poll of the controller: `pressed` are the buttons newly down since the last poll.
void OnPad(uint16_t pressed) {
  if (!REXCVAR_GET(menu_b_quit)) {
    if (REXCVAR_GET(menu_hint) != 0) REXCVAR_SET(menu_hint, 0);
    return;
  }
  State& s = S();
  bool quit = false;
  {
    std::lock_guard<std::mutex> lock(s.mutex);
    const auto now = Clock::now();
    const bool on_top = s.front_end && !s.submenu && !s.quitting && now - s.changed >= std::chrono::milliseconds(400);
    const int32_t hint = on_top ? 1 : 0;
    if (REXCVAR_GET(menu_hint) != hint) REXCVAR_SET(menu_hint, hint);
    if (s.pending && now - s.pressed >= kWait && !s.quitting) {
      s.pending = false;
      s.quitting = quit = true;
    }
    if ((pressed & 0x2000) && s.front_end && !s.quitting) {  // B
      s.pending = true;
      s.pressed = now;
    }
  }
  if (quit) RequestQuit();
}

}  // namespace eatlead::menu_quit
