// eatlead - PlayStation button prompts with a PlayStation pad.
//
// The game's prompts cut their buttons out of its own textures (ui_x360buttons: A B X Y, BACK / START, LB RB RT LT;
// ui_buttoncombo_360: stick + button). With a DualShock or DualSense connected, the native renderer paints the
// PlayStation buttons over the Xbox ones as it loads those textures (native-plugin/patches/0014-pad-prompts.patch,
// described by assets/prompts/playstation.txt, our own art in assets/prompts). Which pad is connected comes from the
// SDK (tools/sdk-patches/0010-pad-style.patch); this passes the choice on through --pad_prompts_active, read by the
// renderer each frame (an int, so it can be read while it's written).
//
// --pad_prompts=auto         PlayStation buttons while a PlayStation pad is the one connected (default)
// --pad_prompts=xbox         always the game's own
// --pad_prompts=playstation  always PlayStation's
// With no pad connected (one unplugged before the next is plugged in) the last pad's buttons stay.

#include "generated/default/eatlead_pch.h"

#include <cstdint>
#include <string>

#include <rex/cvar.h>
#include <rex/input/pad_style.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(pad_prompts, "auto", "Input",
                      "Controller button prompts: auto (PlayStation buttons with a PlayStation pad), xbox, playstation");
REXCVAR_DEFINE_INT32(pad_prompts_active, 0, "Input",
                     "The buttons the prompts show now, 0 Xbox, 1 PlayStation; set by the game for the renderer");

namespace eatlead::pad_prompts {

// Called on every controller poll (menu_trace.cpp's XInputGetState hook).
void Update() {
  static rex::input::PadStyle last_pad = rex::input::PadStyle::kXbox;
  if (const auto style = rex::input::ConnectedPadStyle(); style != rex::input::PadStyle::kNone) last_pad = style;
  const std::string mode = REXCVAR_GET(pad_prompts);
  const bool ps = mode == "playstation" || (mode == "auto" && last_pad == rex::input::PadStyle::kPlayStation);
  const int32_t active = ps ? 1 : 0;
  if (REXCVAR_GET(pad_prompts_active) != active) {
    REXCVAR_SET(pad_prompts_active, active);
    REXLOG_INFO("pad_prompts: {} buttons", ps ? "PlayStation" : "Xbox");
  }
}

bool PlayStation() { return REXCVAR_GET(pad_prompts_active) == 1; }

}  // namespace eatlead::pad_prompts
