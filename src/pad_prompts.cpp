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
// string on a background thread (retried every few seconds until the text file is loaded), through
// Read/WriteProcessMemory so a page that changes protection fails rather than crashes. English only so far.

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

// Guest memory is read and written through ReadProcessMemory / WriteProcessMemory, which fail instead of faulting:
// pages can change protection under us (the renderer's write tracking), and the first build, reading directly,
// crashed in the scan (2026-11-19).
bool ReadGuest(const uint8_t* base, uint64_t a, void* out, size_t size) {
#ifdef _WIN32
  SIZE_T got = 0;
  return ReadProcessMemory(GetCurrentProcess(), base + a, out, size, &got) && got == size;
#else
  (void)base, (void)a, (void)out, (void)size;
  return false;
#endif
}

bool WriteGuest(uint8_t* base, uint64_t a, const void* data, size_t size) {
#ifdef _WIN32
  SIZE_T put = 0;
  return WriteProcessMemory(GetCurrentProcess(), base + a, data, size, &put) && put == size;
#else
  (void)base, (void)a, (void)data, (void)size;
  return false;
#endif
}

bool GuestEquals(const uint8_t* base, uint32_t a, const std::vector<uint8_t>& text) {
  std::vector<uint8_t> now(text.size());
  return ReadGuest(base, a, now.data(), now.size()) && now == text;
}

// Every copy of `needle` in committed guest memory from 0x40000000 (at most 8), read 1 MB at a time.
std::vector<uint32_t> FindInGuest(const uint8_t* base, const std::vector<uint8_t>& needle) {
  std::vector<uint32_t> found;
#ifdef _WIN32
  constexpr size_t kChunk = 1 << 20;
  std::vector<uint8_t> buf(kChunk + needle.size());
  uint64_t addr = 0x40000000;
  while (addr < 0xFFFF0000ull && found.size() < 8) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(base + addr, &info, sizeof(info))) break;
    const uint64_t region_end = uint64_t(static_cast<const uint8_t*>(info.BaseAddress) - base) + info.RegionSize;
    if (region_end <= addr) break;
    if (info.State == MEM_COMMIT) {
      const uint64_t end = std::min<uint64_t>(region_end, 0xFFFF0000ull);
      for (uint64_t at = addr; at < end && found.size() < 8; at += kChunk) {
        const size_t size = size_t(std::min<uint64_t>(kChunk + needle.size() - 1, end - at));
        if (size < needle.size() || !ReadGuest(base, at, buf.data(), size)) continue;
        const auto first = buf.begin(), last = buf.begin() + ptrdiff_t(size);
        for (auto p = first; found.size() < 8;) {
          p = std::search(p, last, needle.begin(), needle.end());
          if (p == last) break;
          if (size_t(p - first) < kChunk) found.push_back(uint32_t(at + uint64_t(p - first)));
          p += ptrdiff_t(needle.size());
        }
      }
    }
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
    std::erase_if(list, [&](uint32_t a) { return !GuestEquals(base, a, xbox) && !GuestEquals(base, a, play); });
    if (list.empty()) {
      missing = true;
      continue;
    }
    if (t.applied && ps == t.last_ps) continue;
    for (uint32_t a : list) WriteGuest(base, a, (ps ? play : xbox).data(), xbox.size());
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
