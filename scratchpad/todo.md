## Systems

### 1. RTS Architecture (Pure C++ GDExtension)
- **Engine**: Godot 4.3 with C++ GDExtension (`librtso.so`).
- **Main Node (`RtsoMain`)**: Native C++ node attached to `scenes/main.tscn`. Controls game loop, HUD overlay, building placement ghost, selection rect, camera rig, minimap, audio, menu transitions.
- **WorldView (`RtsoWorldView`)**: Native rendering presentation for units, buildings, resources, health bars, animations.
- **Simulation (`rtso::Simulation`)**: High performance C++ simulation for units, movement, combat, resource harvesting, building construction, wave spawner.
- **Model Library (`rtso::ModelLibrary`)**: Loads and caches real 0AD GLB 3D models & textures for units, buildings, resources.
- **Animation Controller (`rtso::AnimationController`)**: Manages 0AD PSA skeleton animations.
- **Scenes**:
  - `scenes/menu/main_menu.tscn`: Main Menu
  - `scenes/menu/options.tscn`: Game Options
  - `scenes/menu/lobby.tscn`: Multiplayer Lobby
  - `scenes/session/pause_menu.tscn`: Pause Overlay
  - `scenes/main.tscn`: RTS Gameplay Scene (`RtsoMain`)

### 2. .NET / Mono Dependency Clarification
- The game project is **100% C++ native GDExtension** (no C# / .NET code required or used).
- The warning `ERROR: .NET: One of the dependent libraries is missing` printed on launch comes solely from the local Godot editor binary (`4.3.stable.mono.official`), which is a Mono-enabled build of the Godot Engine executable searching for system .NET runtime on startup.
- The game itself does **not** need or use .NET runtime.

## Tasks
- [x] Fix compilation error in `debug_overlay.h` by removing non-overridden `_notification` signature override.
- [ ] Fix headless exit / runtime crash in GDExtension during model/viewport initialization.
- [ ] Ensure full visual polish, responsive UI controls, smooth camera rig, full game loop (menus -> game -> pause -> victory/defeat).
- [ ] Verify game stability and run tests.
