Second release of the Eat Lead: The Return of Matt Hazard PC recompilation.

**Updating from 0.1.0?** Download `EatLead.exe` and run it: it shows that your game was built with 0.1.0; press
**Reinstall now** to rebuild the game with this version's SDK. Your settings and saves are kept.

## What's new

- **New launcher layout** (as in the 007 port's launcher): each setting's name with a short note under it, its control
  on the right. On/off settings are Enabled | Disabled buttons, choices are button groups (720p / 1080p / 1440p / 4K,
  30 / 60, Auto / PlayStation / Xbox, ...), pages scroll when they don't fit, and a new **Credits** tab.
- **Shader translator fix** (SDK patch 0013, found in the 007 port): a texture's exponent bias was read from the
  wrong word of its fetch constant. Eat Lead looked right without it; it's there so textures that use the bias come
  out right.

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
