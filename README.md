# Eat Lead: The Return of Matt Hazard: PC recompilation

A native Windows build of *Eat Lead: The Return of Matt Hazard* (Xbox 360, Vicious Cycle / D3 Publisher, 2009),
made by statically recompiling the game's code to C++ with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).
No game files are included: you need your own copy of the Xbox 360 game.

**Status: early.** It boots to the main menu and plays the first level with the native Vulkan renderer, at the
game's own 30 FPS or at 60 (`--vsync=false --frame_rate=60`, using the engine's own unused 60 FPS mode).
No installer or launcher yet.

## Building

Needs Windows, Visual Studio 2022 or newer with the C++ build tools, Clang and CMake, and the ReXGlue SDK v0.10.1
built with this project's patches:

1. Build the SDK with the native renderer, in an *x64 Native Tools Command Prompt for VS*:
   ```
   tools\build_native.bat
   ```
   (SDK v0.10.1 plus `tools\sdk-patches`, and Skullburglar's native renderer plus `native-plugin\patches`,
   into `C:\rexglue-src`.)
2. Put the game's files (the extracted disc, with `default.xex`) in `eatlead-game\`.
3. Generate the code and build:
   ```
   C:\rexglue-src\rexglue-sdk\out\install\win-amd64\bin\rexglue.exe codegen eatlead_manifest.toml
   cmake --preset win-amd64-relwithdebinfo -DCMAKE_PREFIX_PATH=C:/rexglue-src/rexglue-sdk/out/install/win-amd64
   cmake --build out\build\win-amd64-relwithdebinfo
   ```
4. Run:
   ```
   out\build\win-amd64-relwithdebinfo\eatlead.exe --game_data_root eatlead-game
   ```
   The native renderer is the default; `--gpu_plugin xenos` uses the SDK's own.

## Notes

- `default_functions.toml` and `default_functions_scan.toml` list function starts the recompiler's analysis
  misses (mostly small stubs called through pointers).
- `native-plugin\patches\0012-tiled-resolve-offset.patch` lets the native renderer draw the game's tiled frames
  (its deferred G-buffer is drawn in two side-by-side tiles).
- `HANDOFF.md` has the project's state and next steps.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), built on Xenia's work.
- [Skullburglar](https://github.com/nlwstein/skullburglar) by nlwstein (BSD 3-Clause): the native Vulkan renderer,
  SDK patches, crash reporting and the leak scanner.
- Shared tooling from the [Bourne Conspiracy recompilation](https://github.com/thegh0sts/bourne-conspiracy-x360-recomp).
