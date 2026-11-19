// eatlead - controller vibration strength (--vibration_strength=0..100, the launcher's Controls tab).
//
// The game rumbles through one function, sub_82268C60, its XInputSetState(user, XINPUT_VIBRATION*): two big-endian
// u16 motor speeds, left then right, which it passes on to XamInputSetState. Wrapped here: the caller's speeds are
// scaled for the call and put back after (the struct is the game's own, which it may keep and send again). 100
// leaves them as the game sets them; at 0 the launcher also passes --vibration=false, the runtime's own switch.

#include "generated/default/eatlead_pch.h"

#include <algorithm>
#include <cstdint>

#include <rex/cvar.h>

REXCVAR_DEFINE_INT32(vibration_strength, 100, "Input",
                     "Controller vibration strength in percent (100 = as the game sets it)")
    .range(0, 100);

REX_EXTERN(__imp__sub_82268C60);

extern "C" REX_FUNC(sub_82268C60) {
  const int strength = std::clamp(REXCVAR_GET(vibration_strength), 0, 100);
  const uint32_t vibration = ctx.r4.u32;
  if (strength >= 100 || !vibration) {
    __imp__sub_82268C60(ctx, base);
    return;
  }
  const uint32_t left = REX_LOAD_U16(vibration + 0), right = REX_LOAD_U16(vibration + 2);
  REX_STORE_U16(vibration + 0, uint16_t(left * uint32_t(strength) / 100));
  REX_STORE_U16(vibration + 2, uint16_t(right * uint32_t(strength) / 100));
  __imp__sub_82268C60(ctx, base);
  REX_STORE_U16(vibration + 0, uint16_t(left));
  REX_STORE_U16(vibration + 2, uint16_t(right));
}
