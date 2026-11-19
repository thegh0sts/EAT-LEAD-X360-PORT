# Eat Lead: The Return of Matt Hazard: handoff

Xbox 360 static recompilation of *Eat Lead: The Return of Matt Hazard* (Vicious Cycle / D3 Publisher,
2009), built with the same ReXGlue SDK build and native renderer as The Bourne Conspiracy port
(`thegh0sts/bourne-conspiracy-x360-recomp`; see its `docs/HANDOFF.md` for the shared routine).
Update this file at the end of each working session. Started 2026-11-19.

## Where things are

| What | Where |
|---|---|
| Project folder | `%USERPROFILE%\source\repos\EAT-LEAD-X360-PORT` |
| Game files (extracted disc) | `EAT-LEAD-X360-PORT\eatlead-game` (5.6 GB): `default.xex`, `Cinematics`, `Configurations`, `Games`, `LoadingScreens`, `Maps`, `Movies` (.wmv), `Regions`, `RenderConfig`, `Sounds`, `nxeart`, `$SystemUpdate` |
| GitHub repository | thegh0sts/EAT-LEAD-X360-PORT |
| SDK | same as Bourne: ReXGlue v0.10.1 (`bd833a2`) with Bourne's `tools/sdk-patches` 0001-0011, built by Bourne's `tools/build_sdk.bat` / `build_native.bat` into `C:\rexglue-src\rexglue-sdk\out\install\win-amd64` |
| Native renderer | Skullburglar at `67aacb28670077e839f84931dee9b162526bbdb2` with Bourne's `native-plugin/patches` 0001-0011 (to be copied into this project) |

## The game

- `default.xex`: title ID **445007EB**, media ID 4B85F016, version 6, disc 1 of 1.
- Image base `0x82000000`, entry point `0x82269C10`, default stack `0xD0000`.
- Encrypted and compressed (normal retail XEX). Imports only the system libraries (xam, xboxkrnl).
- Original PE name `default.exe`.
- Engine: Vicious Cycle's own Vicious Engine, **not Unreal Engine 3**. Bourne's UE3-specific work
  (Kismet, `Coalesced.ini`, takedowns, menus) doesn't carry over; the SDK, renderer, launcher,
  installer, translations and release routine do.
- Languages on the disc: eng, fre, ger, ita, spa (per-language folders under each asset folder).
- Videos are `.wmv` (Bourne's were Bink); the SDK's video path for them is untested.

## Status (2026-11-19, evening)

**Boots to the main menu and into the first level with the native renderer, full screen.**

- **Code generation:** clean. `default_functions.toml` has 2 hand-checked starts; `default_functions_scan.toml`
  has 85 found by scanning the decrypted code for stretches no function covers (`gapscan.py` + `classify.py`,
  repeated after each code generation until nothing new appears: rows of thunks and virtual-call forwarders
  otherwise only show one per run). The first runs crashed on two of them (0x821A9300, 0x822977A0).
- **Tiled rendering (native renderer patch 0012, `0012-tiled-resolve-offset.patch`).** Eat Lead is a deferred
  renderer: four G-buffer colour targets plus depth at 1280x720 (about 18 MB, more than the 360's 10 MB EDRAM),
  so it draws the scene in **two side-by-side tiles, 704 and 576 wide** (one sample per pixel: not MSAA, so
  nothing to switch off as in Bourne). Each tile is drawn at the top-left of EDRAM and resolved with
  RB_COPY_DEST_BASE pointing inside the full texture (tile 2: +0x16000 = 22 blocks of 32x32 texels = x 704).
  The renderer filed resolve textures by exact address, so tile 2 went to a texture nothing read and only the
  left 704 px showed. Patch 0012 finds the enclosing texture (same format and size) and copies the tile there
  (colour and depth). Draws were already right: the SDK's viewport/scissor helpers apply the window offset.
- **60 FPS works** (`src/fps_unlock.cpp`, `src/frame_pacing.cpp`): `--vsync=false --frame_rate=60`. The 30 FPS
  cap is the presentation interval (two vblanks); with the SDK's guest vsync off the vblank runs at 1 kHz and a
  host limiter in D3D Swap (sub_822765C0, the caller of VdSwap) paces frames (Bourne's frame pacing). The engine
  steps a fixed time per frame, so 60 alone ran at double speed; it has its own unused 60 FPS mode:
  sub_820A19F0 (settings object in r3, field +16 == 1) stores 1/59.94 s (0x82001E58) and 600/60 = 10, else
  1/29.97 s (0x82001E50) and 20, into the frame time global 0x824114A8 (double) and 0x824B4C40 (int). With
  `--frame_rate=60` the 60 values are stored after that function and before every Swap. Only 30 and 60 are
  right; other rates run the game at rate/30 speed. Confirmed in game: normal speed at 60.
- **FPS counter**: `--show_fps` (and `--show_fps_detail`, `--brightness`, `--achievement_popups`), drawn by the
  native renderer as in Bourne.
- **Repository**: thegh0sts/EAT-LEAD-X360-PORT (first commit from the cloud workspace, 2026-11-19).
- **Xenos** (`--gpu_plugin xenos`): shows the menu, then a blank screen with audio still playing; no errors in
  the log. Not looked into yet.
- **Build:** the project folder builds the shared SDK in `C:\rexglue-src` with its own `native-plugin\patches`
  (0001-0012) via `tools\build_native.bat` (needs `tools\fix_symlinks.sh`, now copied). Bourne's next
  `build_native.bat` puts Bourne's 11 back; 0012 is generic and could go into Bourne too.
- **Lessons:** stage every file copy to the PC in a new folder (reusing a staging path once left an old file on
  the PC); check the renderer DLL's date next to the exe after an SDK rebuild.

## Next steps

0. Done today: steps 1-4 below, plus 60 FPS. Now: **launcher and installer** (adapted from Bourne's
   `installer/`: install from the player's disc image, build, settings incl. frame rate 30/60, renderer,
   resolution, FXAA, texture filtering, and the FPS counter as Off / Simple / Detailed with everything
   the native overlay draws: frame rate and times, GPU, CPU, RAM/VRAM, clock, frame graph, achievement pop-ups). Later: keyboard and mouse, play further (controls, `.wmv` intros,
   saving, other levels), the Xenos blank screen.

1. **Code generation on the PC** (x64 Native Tools prompt, in the project folder):
   ```
   C:\rexglue-src\rexglue-sdk\out\install\win-amd64\bin\rexglue.exe codegen eatlead_manifest.toml
   ```
2. **Build:**
   ```
   cmake --preset win-amd64-relwithdebinfo -DCMAKE_PREFIX_PATH=C:/rexglue-src/rexglue-sdk/out/install/win-amd64
   cmake --build out\build\win-amd64-relwithdebinfo
   ```
3. **First run**, native renderer (the default), then Xenos to compare:
   ```
   out\build\win-amd64-relwithdebinfo\eatlead.exe --game_data_root eatlead-game
   out\build\win-amd64-relwithdebinfo\eatlead.exe --game_data_root eatlead-game --gpu_plugin xenos
   ```
   Crashes on a missing function show an address: check it with `xexdump.py`/`ppcdis.py` and add it
   to `default_functions.toml`, as for Bourne.
4. **Create the GitHub repository** once it builds; the leak-scan hooks keep game files out
   (`git config core.hooksPath .githooks`).

## Open questions

- Whether the game's `.wmv` videos play under the SDK.
- The Xenos blank screen after the menu (native works, so not urgent).
