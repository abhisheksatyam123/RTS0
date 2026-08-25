## Systems

### 1. Existing C# Architecture (`src/`)
- Total ~11k LOC across 113 C# files.
- `Main.cs`: Scene root Node3D. Instantiates `GameSim`, `WorldView`, `DebugOverlay`, UI elements, music, handles mouse/keyboard input (selection, drag rect, building placement mode `B`, movement order `RMB`).
- `Presentation/`:
  - `WorldView.cs`: Visual representation of entities (units, buildings, resources, ghosts, footprints), interpolation, selection highlights, camera tracking.
  - `DebugOverlay.cs`: Performance statistics overlay.
  - `MiniMapView.cs`: Minimap widget.
- `App/`:
  - `CameraRig.cs`: RTS camera movement, zoom, pan.
  - `SelectionRect.cs`: UI Control drawing drag-selection rectangle.
- `Core/Assets/`: `Assets.cs`, `Procedural.cs` (mesh generation, materials, shaders).
- `Core/Diagnostics/`: `Logger.cs`, `PerfTracker.cs`, `CrashHandler.cs`, `BugReporter.cs`.
- `Core/GPU/`: `SeparationCompute.cs`, `GpuCuller.cs`.
- `Core/Helpers/`: `Geometry.cs`, `Render.cs`, `Pathfinding.cs`, `Selection.cs`, `Rasterize.cs`, `Los.cs`, `CinemaPath.cs`, `PriorityQueue.cs`, `MapEdgeTiles.cs`.
- `Core/Maths/`: Vector2D, Vector3D, Matrix3D, Fixed, FixedVector, Grid, Frustum, Bounds, Brush, Sqrt, MD5, NUSpline, Quaternion, Plane, Rect, Ease.
- `Core/Pathfinding/`: `VertexPathfinder.cs`, `HierarchicalPathfinder.cs`, `LongPathfinder.cs`, `PathGoal.cs`.
- `Core/Serialization/`: `Serializer.cs`, `HashSerializer.cs`.
- `Core/Simulation/`: `GameSim.cs`, `Player.cs`, `SimTypes.cs`, `Commands.cs`, 40+ simulation components (Position, Health, UnitMotion, Pathfinder, Attack, ResourceSupply, ResourceGatherer, Builder, Market, Turret, Wall, Dropsite, Upkeep, Auras, etc.).
- `Core/System/`: `Entity.cs`, `ComponentManager.cs`, `SimContext.cs`, `TurnManager.cs`, `Message.cs`.

### 2. Existing C++ GDExtension Setup (`native/`)
- GDExtension binding for Godot 4.3 using `godot-cpp` (v4.3 branch).
- `SConstruct`: Cross-platform SCons build script supporting Linux, Windows, macOS, Android, iOS, Web.
- `rts0.gdextension`: Configured for multi-platform shared libraries in `native/bin/`.
- `native/src/`:
  - `register_types.h / .cpp`: Entry symbol `rtso_library_init`. Registers GDExtension classes.
  - `sim/simulation.h / .cpp`: Cache-friendly SoA high-performance C++ simulation (`rtso::Simulation`).
  - `core/`: `arena.h` (64B-aligned bump allocator, double-buffered `FrameArena`), `pool.h` (ABA-guarded pool), `soa_storage.h` (SoA arrays for units and resources).
  - `nodes/sim_node.h / .cpp`: GDExtension `RtsoSimNode` extending `Node3D`. Exposes simulation bindings to Godot (spawn_initial, tick, cmd_move, cmd_build_house, get_gold, get_unit_count, etc.).

### 3. Removal & Porting Strategy
- Port all simulation, rendering/presentation, diagnostics, maths, pathfinding, assets, and main node logic into C++ GDExtension classes (and Godot native C++ APIs).
- Replace `src/Main.cs` with GDExtension node `RtsoMain` (or `RtsoSimNode` extended to handle scene presentation and input) registered with Godot's `ClassDB`.
- Update `scenes/main.tscn` to attach to the native `RtsoMain` class instead of `res://src/Main.cs`.
- Delete all C# files (`src/`), `.csproj`, `.sln`, `.godot/mono/`.
- Remove `.NET` / `mono` configurations from `project.godot`, `export_presets.cfg`, `tools/build.py`, `tools/build.sh`, `.gitignore`.
- Verify compilation of C++ GDExtension module across debug/release and ensure project imports and runs in Godot without C# / Mono.

## Tasks
- [ ] Inspect remaining C# classes in detail to ensure no logic or functionality is missed in C++ native.
- [ ] Expand native C++ codebase in `native/src/` to cover all game simulation components, math/pathfinding helpers, assets/rendering presentation, camera rig, selection rect, debug overlay, and main node logic (`RtsoMain`).
- [ ] Update GDExtension bindings (`register_types.cpp`, class registrations, methods, signals, properties).
- [ ] Test build native C++ extension using SCons (`cd native && scons`).
- [ ] Update `scenes/main.tscn` to use native GDExtension `RtsoMain` node.
- [ ] Update `project.godot`, `export_presets.cfg`, `tools/build.py` to remove all .NET/Mono references.
- [ ] Remove all `.cs` files, `RTS0.csproj`, `RTS0.sln`.
- [ ] Verify full project build and run using `godot --headless` / test execution.
