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
//
// Text: the title screen's "Press the START button" is a string in the game's text file (Games/<lang>/
// 0000000100000001.loc, UTF-16 big-endian, length-prefixed), drawn with the game's font. With PlayStation prompts
// it's rewritten in guest memory, where the file is loaded, to "Press OPTIONS to start": the same length, so the
// string table needs no other change; switching back restores it. Found once by scanning guest memory for the
// string (retried every few seconds until the text file is loaded). English only so far.

#include "generated/default/eatlead_pch.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <algorithm>
#include <string>

#include <rex/cvar.h>
#include <rex/input/pad_style.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(pad_prompts, "auto", "Input",
                      "Controller button prompts: auto (PlayStation buttons with a PlayStation pad), xbox, playstation");
REXCVAR_DEFINE_INT32(pad_prompts_active, 0, "Input",
                     "The buttons the prompts show now, 0 Xbox, 1 PlayStation; set by the game for the renderer");

namespace {

std::vector<uint8_t> Utf16Be(const char* text) {
  std::vector<uint8_t> out;
  for (const char* c = text; *c; ++c) out.insert(out.end(), {0, uint8_t(*c)});
  return out;
}

// Original and PlayStation text, the same length.
struct TextSwap {
  const char* xbox;
  const char* playstation;
};

constexpr TextSwap kTexts[] = {
    {"Press the START button", "Press OPTIONS to start"},
};
constexpr size_t kTextCount = sizeof(kTexts) / sizeof(kTexts[0]);

// Whether [a, a + size) of guest memory is committed and readable now.
bool Readable(const uint8_t* base, uint32_t a, size_t size) {
#ifdef _WIN32
  for (uint64_t p = a & ~0xFFFull; p < uint64_t(a) + size; p += 0x1000) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(base + p, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
      return false;
  }
  return true;
#else
  (void)base, (void)a, (void)size;
  return false;
#endif
}

// Every copy of `needle` in committed guest memory from 0x40000000 (at most 8).
std::vector<uint32_t> FindInGuest(const uint8_t* base, const std::vector<uint8_t>& needle) {
  std::vector<uint32_t> found;
#ifdef _WIN32
  uint64_t addr = 0x40000000;
  while (addr < 0xFFFF0000ull && found.size() < 8) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(base + addr, &info, sizeof(info))) break;
    const uint64_t region_end = uint64_t(static_cast<const uint8_t*>(info.BaseAddress) - base) + info.RegionSize;
    const bool readable = info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                          (info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
    if (readable && region_end > addr) {
      const uint8_t* begin = base + addr;
      const uint8_t* end = base + std::min<uint64_t>(region_end, 0xFFFF0000ull);
      const auto it_end = end;
      for (const uint8_t* p = begin; p < it_end && found.size() < 8;) {
        p = std::search(p, it_end, needle.begin(), needle.end());
        if (p == it_end) break;
        found.push_back(uint32_t(p - base));
        p += needle.size();
      }
    }
    if (region_end <= addr) break;
    addr = region_end;
  }
#else
  (void)base;
  (void)needle;
#endif
  return found;
}

// The scan runs on its own thread (it reads hundreds of MB), started at most every 3 s and 10 times in all, until the
// text file is loaded.
struct Texts {
  std::mutex mutex;
  std::vector<uint32_t> addresses[kTextCount];
  bool scanning = false;
  int scans = 0;
  std::chrono::steady_clock::time_point next{};
  bool applied = false, last_ps = false;
};

Texts& T() {
  static Texts t;
  return t;
}

void SwapTexts(uint8_t* base, bool ps) {
  Texts& t = T();
  std::lock_guard<std::mutex> lock(t.mutex);
  if (t.scanning) return;
  bool missing = false;
  for (size_t i = 0; i < kTextCount; ++i) {
    const auto xbox = Utf16Be(kTexts[i].xbox), play = Utf16Be(kTexts[i].playstation);
    auto& list = t.addresses[i];
    // Drop copies that are neither text any more (freed or reused).
    std::erase_if(list, [&](uint32_t a) {
      return !Readable(base, a, xbox.size()) || std::memcmp(base + a, xbox.data(), xbox.size()) != 0 && std::memcmp(base + a, play.data(), play.size()) != 0;
    });
    if (list.empty()) {
      missing = true;
      continue;
    }
    if (t.applied && ps == t.last_ps) continue;
    for (uint32_t a : list) std::memcpy(base + a, (ps ? play : xbox).data(), xbox.size());
  }
  t.applied = true;
  t.last_ps = ps;
  const auto now = std::chrono::steady_clock::now();
  if (missing && ps && t.scans < 10 && now >= t.next) {
    t.scanning = true;
    ++t.scans;
    t.next = now + std::chrono::seconds(3);
    std::thread([base] {
      std::vector<uint32_t> found[kTextCount];
      for (size_t i = 0; i < kTextCount; ++i) found[i] = FindInGuest(base, Utf16Be(kTexts[i].xbox));
      Texts& t = T();
      std::lock_guard<std::mutex> lock(t.mutex);
      for (size_t i = 0; i < kTextCount; ++i) {
        for (uint32_t a : found[i]) {
          REXLOG_INFO("pad_prompts: \"{}\" at {:08X}", kTexts[i].xbox, a);
          if (std::find(t.addresses[i].begin(), t.addresses[i].end(), a) == t.addresses[i].end())
            t.addresses[i].push_back(a);
        }
      }
      t.applied = false;  // write the new copies on the next poll
      t.scanning = false;
    }).detach();
  }
}

}  // namespace

namespace eatlead::pad_prompts {

// Called on every controller poll (menu_trace.cpp's XInputGetState hook).
void Update(uint8_t* base) {
  static rex::input::PadStyle last_pad = rex::input::PadStyle::kXbox;
  if (const auto style = rex::input::ConnectedPadStyle(); style != rex::input::PadStyle::kNone) last_pad = style;
  const std::string mode = REXCVAR_GET(pad_prompts);
  const bool ps = mode == "playstation" || (mode == "auto" && last_pad == rex::input::PadStyle::kPlayStation);
  const int32_t active = ps ? 1 : 0;
  if (REXCVAR_GET(pad_prompts_active) != active) {
    REXCVAR_SET(pad_prompts_active, active);
    REXLOG_INFO("pad_prompts: {} buttons", ps ? "PlayStation" : "Xbox");
  }
  SwapTexts(base, ps);
}

bool PlayStation() { return REXCVAR_GET(pad_prompts_active) == 1; }

}  // namespace eatlead::pad_prompts
