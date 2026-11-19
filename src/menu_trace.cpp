// eatlead - --menu_trace (development): which menu screen is open, to make B quit from the main menu.
//
// The menus are scripted: a screen opens and closes through two script actions, "UI Show Dialog"
// (gcDoUIShowDialog, vtable 0x820137A0, 164-byte object, made by sub_82214960) and "UI Hide Dialog"
// (gcDoUIHideDialog, vtable 0x820135C0, 136 bytes, sub_82214440). Which of their methods does the work, and
// where the dialog's name sits, aren't fully known. First run (2026-11-19): slots 5 and 11 run only while a
// region loads (reading and setting up the actions) and slots 4, 16, 17 never did, so the work is in slot 13:
// sub_82259630 (show) and sub_82259258 (hide). Both resolve their dialog through sub_822459A8 (a reference at
// +76 in the show action). This logs each run of slot 13 (once per action per second: they may run every
// frame while active), the dialog that reference resolves to, and the dialog's first words and any text it
// leads to. Run with --menu_trace=true, open the main menu, go into Options and back, press B on the main
// menu, start a level, then read eatlead.log.
//
// The controller too: sub_82268C50 is the game's XInputGetState(user, state*), calling XamInputGetState(user, 1,
// state); each change of the buttons is logged with their names (B = 0x2000), so a press that opens or closes
// nothing still shows.
//
// And the files the game opens through sub_82269408(path) (an ANSI path, NtCreateFile): which Regions\*.rgn is
// loaded tells the front end from a level.

#include "generated/default/eatlead_pch.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(menu_trace, false, "Debug", "Development: log the menu screens opening and closing");

namespace {

uint32_t Load32(const uint8_t* base, uint32_t a) {
  return uint32_t(base[a]) << 24 | uint32_t(base[a + 1]) << 16 | uint32_t(base[a + 2]) << 8 | base[a + 3];
}

// Whether the host page behind a guest address can be read. The guest heaps' own protection said yes for
// pages that weren't committed on the host (third run crashed reading 0x41F979C4), so ask Windows directly,
// every time (pages come and go).
bool Readable(const uint8_t* base, uint32_t addr) {
#ifdef _WIN32
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(base + addr, &info, sizeof(info))) return false;
  constexpr DWORD kRead = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                          PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  return info.State == MEM_COMMIT && (info.Protect & kRead) && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS));
#else
  (void)base;
  (void)addr;
  return false;
#endif
}

// Heap (the game's objects are at 0xF...), physical memory or the executable's data.
bool Pointer(const uint8_t* base, uint32_t a) {
  return ((a >= 0x40000000 && a < 0x80000000) || (a >= 0x82000000 && a < 0x82600000) ||
          (a >= 0xA0000000 && a < 0xFFFF0000)) &&
         Readable(base, a) && Readable(base, a + 0xFF);
}

// Text at a: 3+ printable characters then a 0 (8-bit), or the same as UTF-16.
std::string Text(const uint8_t* base, uint32_t a) {
  std::string s;
  for (uint32_t i = 0; i < 63; ++i) {
    const uint8_t c = base[a + i];
    if (!c) return s.size() >= 3 ? s : std::string();
    if (c < 0x20 || c > 0x7E) break;
    s += char(c);
  }
  s.clear();
  for (uint32_t i = 0; i + 1 < 64; i += 2) {
    const uint16_t c = uint16_t(base[a + i] << 8 | base[a + i + 1]);
    if (!c) return s.size() >= 3 ? s : std::string();
    if (c < 0x20 || c > 0x7E) break;
    s += char(c);
  }
  return {};
}

void Strings(const uint8_t* base, uint32_t at, uint32_t size, int depth, std::string path, std::set<uint32_t>& seen,
             std::string& out) {
  char buf[32];
  for (uint32_t i = 0; i < size; i += 4) {
    const uint32_t p = Load32(base, at + i);
    if (!Pointer(base, p) || !seen.insert(p).second || seen.size() > 200) continue;
    std::snprintf(buf, sizeof(buf), "%s+%X", path.c_str(), i);
    if (const std::string t = Text(base, p); !t.empty()) {
      out += " ";
      out += buf;
      out += "=\"" + t + "\"";
    } else if (depth > 0 && (p < 0x82000000 || p >= 0x82600000)) {  // data, not code or vtables
      Strings(base, p, 64, depth - 1, buf, seen, out);
    }
  }
}

std::string Dump(const uint8_t* base, uint32_t a, uint32_t size) {
  std::string out;
  char buf[16];
  for (uint32_t i = 0; i < size; i += 4) {
    std::snprintf(buf, sizeof(buf), " %08X", Load32(base, a + i));
    out += buf;
  }
  return out;
}

std::mutex& Mutex() {
  static std::mutex m;
  return m;
}

thread_local const char* g_action = nullptr;  // "Show" / "Hide" while slot 13 runs
thread_local uint32_t g_object = 0;

void Run(const char* what, const PPCContext& ctx) {
  if (!REXCVAR_GET(menu_trace)) return;
  using clock = std::chrono::steady_clock;
  static std::unordered_map<uint32_t, clock::time_point> last;
  std::lock_guard<std::mutex> lock(Mutex());
  const uint32_t obj = ctx.r3.u32;
  const auto now = clock::now();
  auto it = last.find(obj);
  const bool fresh = it == last.end() || now - it->second > std::chrono::seconds(1);
  last[obj] = now;
  if (fresh) REXLOG_INFO("menu_trace: {} runs, action {:08X}", what, obj);
}

void Resolved(uint32_t dialog, uint8_t* base) {
  static std::set<uint64_t> pairs;
  static std::set<uint32_t> dumped;
  std::lock_guard<std::mutex> lock(Mutex());
  if (!pairs.insert(uint64_t(g_object) << 32 | dialog).second) return;
  std::string text;
  if (Pointer(base, dialog) && dumped.insert(dialog).second) {
    std::set<uint32_t> seen;
    Strings(base, dialog, 0x100, 2, "", seen, text);
    text = "\n    words" + Dump(base, dialog, 0x80) + "\n    text" + text;
  }
  if (Pointer(base, g_object) && dumped.insert(g_object).second) {
    std::set<uint32_t> seen;
    std::string atext;
    Strings(base, g_object, 0xA4, 2, "", seen, atext);
    text += "\n    action words" + Dump(base, g_object, 0xA4) + "\n    action text" + atext;
  }
  REXLOG_INFO("menu_trace: {} action {:08X} -> dialog {:08X}{}", g_action, g_object, dialog, text);
}

}  // namespace

REX_EXTERN(__imp__sub_82259630);
REX_EXTERN(__imp__sub_82259258);
REX_EXTERN(__imp__sub_822459A8);

// "UI Show Dialog", vtable slot 13.
extern "C" REX_FUNC(sub_82259630) {
  Run("Show", ctx);
  const char* prev = g_action;
  const uint32_t prev_obj = g_object;
  g_action = "Show";
  g_object = ctx.r3.u32;
  __imp__sub_82259630(ctx, base);
  g_action = prev;
  g_object = prev_obj;
}

// "UI Hide Dialog", vtable slot 13.
extern "C" REX_FUNC(sub_82259258) {
  Run("Hide", ctx);
  const char* prev = g_action;
  const uint32_t prev_obj = g_object;
  g_action = "Hide";
  g_object = ctx.r3.u32;
  __imp__sub_82259258(ctx, base);
  g_action = prev;
  g_object = prev_obj;
}

// A script reference resolving to its object; logged only inside the two actions above.
extern "C" REX_FUNC(sub_822459A8) {
  __imp__sub_822459A8(ctx, base);
  if (g_action && REXCVAR_GET(menu_trace)) Resolved(ctx.r3.u32, base);
}

namespace {
std::string ButtonNames(uint16_t b) {
  static constexpr struct { uint16_t bit; const char* name; } kNames[] = {
      {0x0001, "Up"},   {0x0002, "Down"}, {0x0004, "Left"},  {0x0008, "Right"}, {0x0010, "Start"}, {0x0020, "Back"},
      {0x0040, "LS"},   {0x0080, "RS"},   {0x0100, "LB"},    {0x0200, "RB"},    {0x1000, "A"},     {0x2000, "B"},
      {0x4000, "X"},    {0x8000, "Y"}};
  std::string out;
  for (const auto& n : kNames) {
    if (!(b & n.bit)) continue;
    if (!out.empty()) out += "+";
    out += n.name;
  }
  return out.empty() ? "none" : out;
}
}  // namespace

REX_EXTERN(__imp__sub_82268C50);

// XInputGetState(user, state*): the XINPUT_STATE is big-endian, buttons at +4.
extern "C" REX_FUNC(sub_82268C50) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t state = ctx.r4.u32;
  __imp__sub_82268C50(ctx, base);
  if (!REXCVAR_GET(menu_trace) || user > 3 || !state || ctx.r3.u32 != 0) return;  // 0 = ERROR_SUCCESS
  static uint16_t last[4] = {};
  const uint16_t buttons = uint16_t(base[state + 4] << 8 | base[state + 5]);
  std::lock_guard<std::mutex> lock(Mutex());
  if (buttons == last[user]) return;
  const uint16_t pressed = buttons & ~last[user];
  last[user] = buttons;
  if (pressed) REXLOG_INFO("menu_trace: pad {} pressed {} (held {})", user, ButtonNames(pressed), ButtonNames(buttons));
}

REX_EXTERN(__imp__sub_82269408);

// The game's file open by path (r3: a 0-terminated ANSI path).
extern "C" REX_FUNC(sub_82269408) {
  if (REXCVAR_GET(menu_trace)) {
    const uint32_t p = ctx.r3.u32;
    std::string path;
    if (Readable(base, p))
      for (uint32_t i = 0; i < 260 && base[p + i]; ++i) path += char(base[p + i]);
    REXLOG_INFO("menu_trace: open {}", path);
  }
  __imp__sub_82269408(ctx, base);
}
