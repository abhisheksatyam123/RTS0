# RTS0

A real-time strategy game modeled on [0 A.D.](https://play0ad.com), built with **Godot 4.3 (Pure C++ GDExtension / SoA Simulation)**.

**Cross-platform multiplayer target: Windows, Linux, Android, iOS.**
See `docs/MULTIPLAYER.md` for the netcode model, platform matrix, and the architecture rules (sim/presentation split, command pattern) that all gameplay code must follow.

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
