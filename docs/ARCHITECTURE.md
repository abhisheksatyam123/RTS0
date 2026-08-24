# RTS0 Architecture

Deterministic RTS core (0 A.D. port) + Godot 4.3 presentation, mirroring 0ad folder structure for `diff -u` friendliness.

## Layout

```
src/
  Core/
    Maths/          → source/maths/   (Fixed 15.16, Grid, vectors, matrices, splines)
      Fixed.cs, FixedVector2D/3D.cs, Sqrt.cs, Grid.cs, MathUtil.cs
      Vector2D/3D/4D.cs, VectorUtil.cs, Matrix3D.cs, Quaternion.cs
      Bounds.cs (AABB/OBB/Sphere), Brush.cs, Frustum.cs, Plane.cs, Rect.cs, Size2D.cs
      Ease.cs, NUSpline.cs, MD5.cs
    Helpers/
      Pathfinding/  → helpers/Hierarchical/Long/VertexPathfinder, PathGoal, PriorityQueue
      Geometry.cs, Rasterize.cs, Los.cs, Selection.cs, CinemaPath.cs, MapEdgeTiles.cs, Render.cs
      Pathfinding.cs (common), PriorityQueue.cs
    Simulation/
      System/       → simulation2/system/ (ComponentManager, ParamNode, Entity, TurnManager, SimContext, Message)
      Serialization/→ simulation2/serialization/ (ISerializer, StdSerializer, HashSerializer)
      Components/   → simulation/components/*.js + CCmp* (50+ components)
        Economy: ResourceSupply, ResourceGatherer, Trader, Market, Cost, ResourceTrickle
        Combat: Attack, Health, Heal, Loot, Resistance, Capture, etc
        Movement: Position, UnitMotion, Formation, Pack, Guard
        Infra: ProductionQueue, Builder, Foundation, Repairable, TechnologyManager, etc
      GameSim.cs    → Simulation2.cpp + TurnManager + Spatial hash (deterministic 20Hz)
      SimTypes.cs   → Position.h (entity_pos_t = Fixed)
      Player.cs     → helpers/Player.h
      Commands.cs   → SimulationCommand.h
    Maths, Helpers, Pathfinding all share namespace Rts0.Core for determinism.
  App/
    CameraRig.cs, SelectionRect.cs  → input & camera (presentation logic)
  Presentation/
    WorldView.cs    → 6× MultiMesh batches + shaders, frustum culling, lerp α
  GPU/
    GpuCuller.cs    → Frustum culling (CPU) mirrors graphics/Frustum
    SeparationCompute.cs + shaders/compute/separation.glsl → GPU SeparationPass (>512 units)
  Main.cs           → glue: GameSim ↔ WorldView, fixed-step loop, commands
shaders/
  unit_thronefall.gdshader → flat ramp + outline (INSTANCE_CUSTOM x=team y=hp z=wind)
  foliage_wind.gdshader     → tree sway (VERTEX wind)
  terrain.gdshader          → 3-way splat + territory tint + LOS fog
  compute/separation.glsl   → Vulkan compute for separation

scenes/main.tscn  → DirectionalLight3D CSM + WorldEnvironment (SSAO/SSIL/fog) + Main
scenes/env.tres  → Environment (ambient+SSAO+SSIL+fog)
export_presets.cfg → Windows/Linux/Android/iOS (forward_plus Vulkan)
tools/build.py    → cross-platform driver (python) + build.sh/ps1 wrappers
```

## Determinism

- Simulation uses **only** `Fixed`/`FixedVector2D/3D`, `Grid<T>`, no `float`/`Random` except render edge (`WorldView.ToFloat()` + `alpha` lerp).
- `GameSim.FixedStep = 1/20s`, two-phase `Parallel.For` (accumulate → apply) for `SeparationPass`, GPU fallback via `SeparationCompute` when `count>=512` (`src/Core/GPU/SeparationCompute.cs:1`).
- Managers: `RangeManager` (LOS), `ObstructionManager` (pathfinding raster), `TerritoryManager` (influence), `Pathfinder` (hier+long+vertex).
- Presentation: 6 GPU batches (player/enemy units + center/house + tree/gold) + terrain plane, `INSTANCE_CUSTOM` for team/hp/wind, frustum cull `src/Core/Maths/Frustum.cs:34`.

## 0 A.D. Port Coverage (2026-08-24: 105 C# files + 4 shaders)

GPU: `src/Core/GPU/` + `shaders/*` (Thronefall flat-ramp/outline/wind, terrain splat, compute separation)

## 0 A.D. Port Coverage (2026-08-24: 105 C# files)

- **maths/** 21/21 ported (100% of relevant deterministic maths; lib/ platform code omitted)
- **helpers/** 12/12 relevant ported (Grid, PathGoal, Hierarchical, Long, Vertex, PriorityQueue, Geometry, Rasterize, Los, Selection, CinemaPath, MapEdgeTiles, Render)
- **components/** 84 JS + 9 CCmp* → 52 C# grouped files (all gameplay components)
- **system/** 6/6 core (ParamNode, Entity, ComponentManager, TurnManager, SimContext, Message)
- **serialization/** 2/2 (StdSerializer, HashSerializer)

See `docs/0AD_PORTING.md` for file-by-file map and `diff -u` usage.

## Build

- `dotnet build RTS0.sln -c ExportRelease` (CI: `csharp-build`)
- `godot --headless --path . --export-release "Windows Desktop"` etc (CI matrix)
- Vulkan `forward_plus` (`project.godot:renderer/rendering_method`), `tools/build.py android` → `build/android/RTS0.apk`
