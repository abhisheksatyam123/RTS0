# RTS0

A real-time strategy game modeled on [0 A.D.](https://play0ad.com), built with **Godot 4.3 (Pure C++ GDExtension / SoA Simulation)**.

**Cross-platform multiplayer target: Windows, Linux, macOS, Android, iOS.**
See `docs/MULTIPLAYER.md` for the netcode model, platform matrix, and the architecture rules (sim/presentation split, command pattern) that all gameplay code must follow.

Version 0.1.0. See [CHANGELOG.md](CHANGELOG.md) for what is in it.

## Install and play

### Playing a release build
Release archives are built per platform by `tools/build.py <platform> --package` (see [docs/BUILD.md](docs/BUILD.md)). Each one is a self-contained game:

- **Windows:** unzip `RTS0-windows-x86_64.zip` and run `RTS0.exe`.
- **Linux:** unzip `RTS0-linux-x86_64.zip` and run `./RTS0.x86_64`.
- **macOS:** unzip `RTS0-macos-universal.zip` and open `RTS0.app`.
- **Android:** install `RTS0-android-arm64.apk` (allow installs from your browser or file manager).
- **iOS:** built from macOS/Xcode only; see [docs/BUILD.md](docs/BUILD.md).

Each archive ships with `LICENSE`, `assets/LICENSE` and `docs/0AD_ATTRIBUTION.md`. The in-game credits screen is under the main menu's **Credits** button.

### Playing from source
Follow **Requirements** and **Run / Build** below, then start the game with `./build/linux/RTS0.x86_64` (or the equivalent file for your platform).

### Starting a match quickly
From the main menu, open **Single-player** and choose **Skirmish Arena** for a quick match, or start a new game from the setup screen. For scripted starts, use the 0 A.D.-style autostart options shown under **Run / Build**.

### Saving and multiplayer
- Press Esc in a match to open the session menu and save. Saves go to Godot's user data folder (`user://saves/`, shown in the save dialog).
- Multiplayer matches start once every seated player has loaded.

## Requirements
- Godot 4.3+ **Standard edition** (no .NET SDK or Mono required): https://godotengine.org/download
- C++20 compiler (`gcc` 11+, `clang` 14+, or MSVC 2022)
- SCons (for compiling the native C++ GDExtension library)
- Standard Godot 4.3 export templates
- For device testing later: Android export templates + keystore; iOS requires macOS/Xcode

## Run / Build
The playable file is **`build/linux/RTS0.x86_64`**. Refresh it every ship (not only the `.so`):

```bash
make                    # or: python3 tools/build.py linux
./build/linux/RTS0.x86_64
# 0 A.D.'s -autostart options skip the menus (see scenes/menu/autostart.gd):
./build/linux/RTS0.x86_64 -- -autostart=random/mainland -autostart-civ=2:brit -autostart-ai=2:petra
```

`scons platform=linux target=template_release` now copies `bin/librtso.linux.template_release.x86_64.so` into `build/linux/` and, if `godot` is on `PATH` (or `GODOT=`), re-exports that binary. Use `scons skip_export=1` for library-only. Editor iteration: `make debug` then F5.

## Controls
| Input | Action |
|---|---|
| WASD | Pan camera |
| Q / E | Rotate camera |
| Mouse wheel | Zoom |
| LMB click / drag | Select unit / box-select |
| RMB | Move order (spread formation) |
| B, LMB, Shift | Build mode; place; keep placing |
| Esc | Cancel build / clear selection |

Passive gold trickles from your workers each second (placeholder economy).

## Building & CI
See **[docs/BUILD.md](docs/BUILD.md)** for export presets, local build wrappers and what the GitHub Actions workflows run.

## Documentation

Start at **[docs/README.md](docs/README.md)**, which lists every doc and the one topic it owns. The most used ones:

- [docs/PLAN.md](docs/PLAN.md): what to work on next, in order.
- [docs/REMAINING_GAPS.md](docs/REMAINING_GAPS.md): how close each subsystem is to 0 A.D.
- [docs/MIGRATION.md](docs/MIGRATION.md): status of the vendored 0 A.D. code in `src/0ad/`.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): code layout and the rules new code must follow.

## License

RTS0's code is **GPL-2.0-or-later** (`LICENSE`), because it copies and ports code from
[0 A.D.](https://play0ad.com). Art, audio and maps from 0 A.D. are **CC BY-SA 3.0**
(`assets/LICENSE`). Details: `docs/0AD_ATTRIBUTION.md`.

Distributed builds are under the same licences. The complete corresponding source for any build is this repository (https://github.com/abhisheksatyam123/RTS0) at the tag or commit the build was made from. Credits are shown in the game and listed in `docs/0AD_ATTRIBUTION.md`.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).
