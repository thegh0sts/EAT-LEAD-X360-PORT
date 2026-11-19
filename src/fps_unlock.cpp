// eatlead - frame rate unlock.
//
// The game runs at 30 FPS on the console: its frames wait for two vblanks (Direct3D's presentation interval,
// with the SDK's imitated 60 Hz vblank). With the graphics plugin's guest vsync off (--vsync=false), the vblank
// is imitated at 1 kHz and that wait no longer holds the frame rate; the D3D Swap function (sub_822765C0, the one
// that calls VdSwap) is then wrapped to wait for each frame's deadline first (frame_pacing.cpp, from the Bourne
// Conspiracy port).
//
// --frame_rate=30   the limiter at 30 (the original rate)
// --frame_rate=60   60 FPS
// --frame_rate=N    any other N > 0
// --frame_rate=0    no limit
//
// All of these need --vsync=false. With guest vsync on (the SDK's default), the original vblank pacing is used,
// the limiter stays off and the game runs at its own 30.
//
// Game speed. The engine advances a fixed time per frame, so 60 frames a second alone ran everything at double
// speed. It has its own 60 FPS mode, never switched on in the release: sub_820A19F0 (reached through a pointer,
// with a settings object in r3) checks a field (+16 == 1) and stores either
//   1/59.94 s (the double at 0x82001E58) and 600 / 60 = 10, or
//   1/29.97 s (the double at 0x82001E50) and 600 / 30 = 20
// into the frame time global (0x824114A8, a double) and a frame-count global (0x824B4C40).
// With --frame_rate=60 the 60 branch's values are stored after the function runs, and again before every Swap in
// case it isn't called (both globals start with the 30 values). Other rates keep the game's 30 Hz step, so they
// run the game at rate / 30 speed: only 30 and 60 are right.

#include "generated/default/eatlead_pch.h"

#include <rex/cvar.h>

#include <cstring>
#include <string>

#include "frame_pacing.h"

REXCVAR_DEFINE_INT32(frame_rate, 60, "Game",
                     "Frame rate with --vsync=false: 30 (original), 60, another limit, or 0 for no limit");

REX_EXTERN(__imp__sub_822765C0);
REX_EXTERN(__imp__sub_820A19F0);

namespace {
// The plugin's guest vsync setting; read on first use (the plugin is loaded by then).
bool GuestVsyncOn() {
  static const bool on = [] {
    const std::string v = rex::cvar::GetFlagByName("vsync");
    const bool result = !(v == "false" || v == "0");
    REXLOG_INFO("Frame pacing: guest vsync {} -> {}", v.empty() ? "(unset)" : v,
                result ? "vblank pacing (30 FPS)" : "host frame limiter");
    return result;
  }();
  return on;
}
constexpr uint32_t kFrameTime = 0x824114A8;   // double: seconds per frame
constexpr uint32_t kFrameCount = 0x824B4C40;  // int32: 600 / frames per second
constexpr double kFrameTime60 = 0.016683350016683352;  // 1/59.94, as the game's own constant at 0x82001E58

bool Want60() { return !GuestVsyncOn() && REXCVAR_GET(frame_rate) == 60; }

void Apply60(uint8_t* base) {
  uint64_t bits;
  std::memcpy(&bits, &kFrameTime60, sizeof(bits));
  REX_STORE_U64(kFrameTime, bits);
  REX_STORE_U32(kFrameCount, 10);
}
}  // namespace

// The engine's frame rate setting (30 or 60, see above).
extern "C" REX_FUNC(sub_820A19F0) {
  __imp__sub_820A19F0(ctx, base);
  if (Want60()) {
    Apply60(base);
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXLOG_INFO("frame_rate=60: the engine's 60 FPS step (1/59.94 s)");
    }
  }
}

// D3D Swap (calls VdSwap): once per frame.
extern "C" REX_FUNC(sub_822765C0) {
  if (Want60()) Apply60(base);
  eatlead::pacing::BeforeSwap(GuestVsyncOn() ? 0 : REXCVAR_GET(frame_rate));
  __imp__sub_822765C0(ctx, base);
}
