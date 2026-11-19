# Launcher and installer

`EatLead.exe` is a single program for players:

- **Not installed yet:** shows the installer. The player picks their own Xbox 360 disc image and it builds the game
  on their PC: reads the ISO, checks it's Eat Lead (title ID 445007EB), finds or installs the C++ build tools, unpacks
  the ReXGlue SDK packed into it (with the native renderer), recompiles and builds `eatlead.exe`, and creates a
  desktop shortcut to the launcher.
- **Installed:** the settings screen, **Save settings** and **Play**.

Nothing from the game ships with it. Running the installer again resumes where it stopped.

## Settings

| Tab | Setting | How it's applied |
|---|---|---|
| Display | Fullscreen, output resolution | `--fullscreen`, `--resolution` (or `--video_mode_*` and `--window_*` windowed) |
| Display | Render resolution | `BOTS_GFX_RENDER_SCALE` 1 / 2 / 3 (the game draws 1280×720). Whole steps only: 1.5 broke effects that need exact pixel positions |
| Display | Anti-aliasing | `BOTS_GFX_FXAA` 1 / 2 (native renderer patch 0010) |
| Display | Frame rate 30 / 60 | `--vsync=false --frame_rate=30|60` (`src/fps_unlock.cpp`, `src/frame_pacing.cpp`) |
| Display | FPS counter | `--show_fps`, `--show_fps_detail` (drawn by the native renderer) |
| Display | Hide achievement pop-ups | `--achievement_popups=false` |
| Graphics | Brightness | `--brightness` (native renderer's last pass) |
| Graphics | Texture filtering | `BOTS_GFX_FORCE_ANISO` |
| Controls | Button prompts | `--pad_prompts=auto|playstation|xbox` (`src/pad_prompts.cpp`, native patch 0014) |
| Controls | Extra stick dead zone | `--left_stick_deadzone_percentage`, `--right_stick_deadzone_percentage` (the SDK's) |
| Controls | Vibration | `--vibration_strength` (`src/vibration.cpp`), plus `--vibration=false` at 0 |
| Controls | B on the main menu quits | `--menu_b_quit` (`src/menu_quit.cpp`) |
| Game files | Game language | `--user_language` |

The native renderer is the only one offered. Settings live in `%LOCALAPPDATA%\EatLeadPC\settings.json`, the install
location in `launcher.json` beside it.

## Building

Needs Python 3 on PATH, and the SDK built with `..\tools\build_native.bat` (run Eat Lead's last if you also build the
Bourne port: they share `C:\rexglue-src`). From this folder:

```
build_installer.bat
```

It gathers the project files into `payload\`, zips the SDK into it, installs PyInstaller and writes
`dist\EatLead.exe`. Change `SDK_BUNDLE` in the script when the SDK or the renderer patches change.

To run without packaging: `python eatlead_launcher.py` (after `build_installer.bat` once, so `payload\` exists).

## Files

- `eatlead_launcher.py`: entry point, settings screen, starts the game.
- `eatlead_installer.py`: install logic and install screen.
- `launcher_text.py`: the launcher's text in German, French, Spanish and Italian (from the Bourne port; strings new
  here show in English until translated).
- `xex_achievements.py`: reads the achievement list from default.xex for the Achievements tab before the first start.
- `icon.ico`: our own icon.

Adapted from the Bourne Conspiracy port's launcher (thegh0sts/bourne-conspiracy-x360-recomp).
