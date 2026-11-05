# Changelog

All notable changes to RTS0 are recorded here. Versions follow [Semantic Versioning](https://semver.org/),
and the version in `project.godot` (`config/version`) and in `export_presets.cfg` must match.

## [0.1.0] - 2026-11-04

First packaged release, a Godot 4.3 and C++ GDExtension port of 0 A.D. 0.28.

### Gameplay
- Simulation ported from 0 A.D. 0.28 in a deterministic structure-of-arrays core, with lockstep multiplayer.
- Unit motion, pathfinding, formations, garrisons, capture, combat, veterancy and loot follow 0 A.D.'s behaviour.
- Petra AI ported for economy, building, research, training, defence and attacks.
- Random maps, the 0 A.D. map set in its converted form, and the setup and session menus.
- Parity status against 0 A.D. is tracked row by row in [docs/REMAINING_GAPS.md](docs/REMAINING_GAPS.md).

### Presentation
- New 3D rendering on Godot 4.3, with animation, sound, minimap and in-match HUD.
- Touch controls and phone layouts; safe-area insets for notched screens.
- Options, hotkeys, language selection and a credits screen.

### Packaging
- Export presets for Windows, Linux, Android, iOS and macOS.
- Application icons and a splash screen.
- `LICENSE`, `assets/LICENSE` and `docs/0AD_ATTRIBUTION.md` are included in every export.

### Known limits
- Multiplayer matches start once every seated player has loaded.
- Replays cannot be saved during playback.
- iOS builds need macOS and Xcode.
