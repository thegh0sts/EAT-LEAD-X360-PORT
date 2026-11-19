// eatlead - load the graphics plugin before the command line is read.
//
// The runtime reads command-line settings (and eatlead.toml) at startup, but only loads the
// graphics plugin (rexgpu-<name>*.dll) later. Settings that belong to the plugin, such as
// --resolution_scale, --vsync and --swap_post_effect, weren't registered yet at that
// point and were dropped, so the game always rendered at 1280x720. Loading the plugin
// here, during static initialization (before main), registers its settings first.
// The runtime's own load later finds the DLL already loaded and reuses it.
//
// Only the plugin named by --gpu_plugin (default xenos) is loaded: the plugins define
// some of the same settings, and loading two would register them twice.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <cwctype>
#include <string>
#include <string_view>

#pragma comment(lib, "shell32.lib")

namespace {

// --gpu_plugin=<name> or --gpu_plugin <name>; "native" when absent (EatleadApp::OnPreSetup).
std::wstring PluginName() {
  std::wstring name = L"native";
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) {
    return name;
  }
  constexpr std::wstring_view kFlag = L"--gpu_plugin";
  for (int i = 1; i < argc; ++i) {
    std::wstring_view arg(argv[i]);
    if (arg == kFlag && i + 1 < argc) {
      name = argv[++i];
    } else if (arg.size() > kFlag.size() && arg.starts_with(kFlag) && arg[kFlag.size()] == L'=') {
      name = std::wstring(arg.substr(kFlag.size() + 1));
    }
  }
  LocalFree(argv);
  // A plugin name is a plain word; anything else is left to the runtime to reject.
  for (wchar_t c : name) {
    if (!std::iswalnum(c) && c != L'_' && c != L'-') {
      return L"native";
    }
  }
  return name.empty() ? L"native" : name;
}

// Native renderer defaults for this game, set before the plugin loads unless the environment already has them.
// BOTS_GFX_DEPTH_BIAS_SCALE: the game nudges decals (the notice board's posters, ...) towards the camera with a depth
// bias; converted to the host's 32-bit float depth it was too small and they z-fought (whole triangles flickering)
// at any render scale. 4 was the smallest of 4 / 8 / 16 that stopped it (2026-11-19); 0 made it worse.
void NativeRendererDefaults() {
  char value[8];
  if (!GetEnvironmentVariableA("BOTS_GFX_DEPTH_BIAS_SCALE", value, sizeof(value))) {
    _putenv_s("BOTS_GFX_DEPTH_BIAS_SCALE", "4");  // the CRT's copy and the process's (the plugin reads getenv)
  }
}

HMODULE PreloadGpuPlugin() {
  NativeRendererDefaults();
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    return nullptr;
  }
  std::wstring dir(buf, n);
  dir.resize(dir.find_last_of(L"\\/") + 1);
  const std::wstring base = dir + L"rexgpu-" + PluginName();
  // Build-type suffixes: rd = RelWithDebInfo, none = Release, d = Debug.
  for (const wchar_t* suffix : {L"rd.dll", L".dll", L"d.dll"}) {
    std::wstring path = base + suffix;
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
      return LoadLibraryW(path.c_str());
    }
  }
  return nullptr;
}

const HMODULE g_gpu_plugin = PreloadGpuPlugin();

}  // namespace
#endif  // _WIN32
