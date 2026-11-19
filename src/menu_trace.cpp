// eatlead - --menu_trace (development): which menu screen is open, to make B quit from the main menu.
//
// The menus are scripted: a screen opens and closes through two script actions, "UI Show Dialog"
// (gcDoUIShowDialog, vtable 0x820137A0, 164-byte object, made by sub_82214960) and "UI Hide Dialog"
// (gcDoUIHideDialog, vtable 0x820135C0, 136 bytes, sub_82214440). Which of their methods does the work, and
// where the dialog's name sits, aren't known yet, so this logs every call of the methods the two classes
// don't share with other actions (vtable slots 4, 5, 11, 16 and 17), with any text the object leads to
// within two pointers (the dialog's name, e.g. DG_Options). Run with --menu_trace=true, open the main menu,
// go into Options and back, press B on the main menu, start a level, then read eatlead.log.

#include "generated/default/eatlead_pch.h"

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

// Heap, physical memory or the executable's data.
bool Pointer(uint32_t a) {
  return ((a >= 0x40000000 && a < 0x80000000) || (a >= 0x82000000 && a < 0x82600000) ||
          (a >= 0xA0000000 && a < 0xE0000000)) &&
         Readable(a) && Readable(a + 63);
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

void Log(const char* what, int slot, const PPCContext& ctx, uint8_t* base) {
  if (!REXCVAR_GET(menu_trace)) return;
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  const uint32_t obj = ctx.r3.u32;
  if (slot >= 16) {  // small methods, possibly called every frame: once per object
    static std::set<uint64_t> logged;
    if (!logged.insert(uint64_t(slot) << 32 | obj).second) return;
  }
  std::string text;
  if (Pointer(obj)) {
    std::set<uint32_t> seen;
    Strings(base, obj, what[0] == 'S' ? 164 : 136, 2, "", seen, text);
  }
  REXLOG_INFO("menu_trace: {} slot {} obj {:08X} r4 {:08X} r5 {:08X}{}", what, slot, obj, ctx.r4.u32, ctx.r5.u32,
              text);
}

}  // namespace

#define MENU_HOOK(addr, what, slot)                \
  REX_EXTERN(__imp__sub_##addr);                   \
  extern "C" REX_FUNC(sub_##addr) {                \
    Log(what, slot, ctx, base);                    \
    __imp__sub_##addr(ctx, base);                  \
  }

MENU_HOOK(821F2BA0, "Show", 4)
MENU_HOOK(821F2C98, "Show", 5)
MENU_HOOK(821F2E30, "Show", 11)
MENU_HOOK(821F2E20, "Show", 16)
MENU_HOOK(821F2E10, "Show", 17)
MENU_HOOK(821F1290, "Hide", 4)
MENU_HOOK(821F1358, "Hide", 5)
MENU_HOOK(821F1498, "Hide", 11)
MENU_HOOK(821F1488, "Hide", 16)
MENU_HOOK(821F1478, "Hide", 17)
