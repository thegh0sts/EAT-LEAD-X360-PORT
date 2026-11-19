First release of the Eat Lead: The Return of Matt Hazard PC recompilation.

`EatLead.exe` is the installer and launcher in one: pick your own Xbox 360 disc image and it builds the game on your PC
(no game files are included or downloaded), then it's the settings screen and Play button.

## What's in it

- The game recompiled to native code, with Skullburglar's native Vulkan renderer.
- **60 FPS** at normal speed (the engine's own unused 60 FPS mode), or the original 30.
- **Up to 4K:** render resolution 1280×720, 2560×1440 or 3840×2160, with the game's tiled frames and deferred lighting
  drawn right at every step; FXAA and anisotropic texture filtering.
- **PlayStation button prompts** with a DualShock or DualSense (or forced on / off in the launcher), including
  "Press OPTIONS to start" on the title screen (English).
- **B (circle) on the main menu quits**, with a "Quit" hint in the corner.
- Achievement pop-ups in the picture (fullscreen too) and an Achievements tab with all 50.
- FPS counter with frame times, GPU, CPU and memory; vibration strength; extra stick dead zone; brightness.

## Known issues

- A decal on another decal (bullet holes on the posters) can flicker.
- Render resolutions between whole steps (1.5×) break some effects; the launcher offers 1×, 2×, 3× only.
- Controller only for now.
- Only the first level has been played through: please report anything further on with the launcher's Save crash
  report (Game files tab).

## Credits

[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (built on Xenia's work), [Skullburglar](https://github.com/nlwstein/skullburglar)
by nlwstein (native renderer, BSD 3-Clause), and the [Bourne Conspiracy port](https://github.com/thegh0sts/bourne-conspiracy-x360-recomp)'s
launcher and tooling.
