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

#include "generated/default/eatlead_pch.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(menu_trace, false, "Debug", "Development: log the menu screens opening and closing");

namespace {

uint32_t Load32(const uint8_t* base, uint32_t a) {
  return uint32_t(base[a]) << 24 | uint32_t(base[a + 1]) << 16 | uint32_t(base[a + 2]) << 8 | base[a + 3];
}

bool Readable(uint32_t addr) {
  static std::unordered_map<uint32_t, bool> pages;
  const uint32_t page = addr >> 12;
  if (const auto it = pages.find(page); it != pages.end()) return it->second;
  bool ok = false;
  auto* runtime = rex::Runtime::instance();
  if (runtime && runtime->memory()) {
    if (auto* heap = runtime->memory()->LookupHeap(addr)) {
      uint32_t protect = 0;
      ok = heap->QueryProtect(addr, &protect) && (protect & rex::memory::kMemoryProtectRead);
    }
  }
  pages.emplace(page, ok);
  return ok;
}

// Heap (the game's objects are at 0xF...), physical memory or the executable's data.
bool Pointer(uint32_t a) {
  return ((a >= 0x40000000 && a < 0x80000000) || (a >= 0x82000000 && a < 0x82600000) ||
          (a >= 0xA0000000 && a < 0xFFFF0000)) &&
         Readable(a) && Readable(a + 0xFF);
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
    if (!Pointer(p) || !seen.insert(p).second || seen.size() > 200) continue;
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
  if (Pointer(dialog) && dumped.insert(dialog).second) {
    std::set<uint32_t> seen;
    Strings(base, dialog, 0x100, 2, "", seen, text);
    text = "\n    words" + Dump(base, dialog, 0x80) + "\n    text" + text;
  }
  if (Pointer(g_object) && dumped.insert(g_object).second) {
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
