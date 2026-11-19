# Eat Lead: The Return of Matt Hazard: PC recompilation

A native Windows build of *Eat Lead: The Return of Matt Hazard* (Xbox 360, Vicious Cycle / D3 Publisher, 2009),
made by statically recompiling the game's code to C++ with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).
No game files are included: you need your own copy of the Xbox 360 game.

**Status: 0.1.0, the first release.** Boots to the main menu and plays the first level with the native Vulkan
renderer, at 30 or 60 FPS and up to 4K, with PlayStation button prompts for PlayStation pads.

## Installing (players)

1. Download `EatLead.exe` from the [latest release](https://github.com/thegh0sts/EAT-LEAD-X360-PORT/releases/latest).
2. Run it and pick your own Eat Lead Xbox 360 disc image (ISO). The installer reads it, installs the C++ build tools
   if your PC doesn't have them (Windows asks for permission; several GB), builds the game on your PC and makes a
   desktop shortcut. It takes a while the first time; running it again resumes where it stopped.
3. The same exe is the launcher afterwards: settings, **Save settings** and **Play**.

Needs Windows 10/11 (64-bit), a Vulkan GPU, about 25 GB free and your own copy of the game.

### Launcher settings

- **Display:** fullscreen, output resolution, render resolution (1280×720, 2560×1440, 3840×2160), FXAA, frame rate
  30 or 60 (the game's own 60 FPS mode, normal speed), FPS counter (with frame times, GPU, CPU and memory), hide
  achievement pop-ups.
- **Graphics:** brightness, texture filtering.
- **Controls:** button prompts (Auto / PlayStation only / Xbox only), extra stick dead zone, vibration strength, B
  (circle) on the main menu quits.
- **Saves**, **Achievements** (all 50, with icons and unlock dates).

### Known issues

- A decal on another decal (bullet holes on the posters) can flicker.
- Render resolutions between the whole steps (such as 1.5×, 1920×1080) break some effects, so the launcher offers
  1×, 2× and 3× only.
- Controller only: no keyboard and mouse yet.
- "Press OPTIONS to start" on the title screen is English only.
- Only the first level has been played through so far: please report problems further on (Save crash report in the
  launcher's Game files tab makes a zip to attach).

## Building from source

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
