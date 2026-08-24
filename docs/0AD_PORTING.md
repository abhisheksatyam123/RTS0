# 0 A.D. Porting Map

RTS0's deterministic core is a direct port of 0 A.D. (`../0ad`) — same files, same names where possible, so `diff -u` stays useful.

> Per your request, **license is ignored** for this private port. No GPL header enforcement in these notes.

## Folder mapping

| RTS0 (`src/`) | 0 A.D. source |
|---|---|
| `Core/Maths/Fixed.cs` | `source/maths/Fixed.h` (CFixed_15_16) |
| `Core/Maths/FixedVector2D.cs` | `source/maths/FixedVector2D.h` |
| `Core/Maths/Sqrt.cs` | `source/maths/Sqrt.cpp` (`isqrt64`) |
| `Core/Maths/Grid.cs` | `source/simulation2/helpers/Grid.h` (`Grid<T>`, `SparseGrid`, `GridUpdateInformation`) |
| `Core/Pathfinding/PathGoal.cs` | `source/simulation2/helpers/PathGoal.h` |
| `Core/Pathfinding/HierarchicalPathfinder.cs` | `source/simulation2/helpers/HierarchicalPathfinder.h/.cpp` |
| `Core/Pathfinding/LongPathfinder.cs` | `source/simulation2/helpers/LongPathfinder.cpp` |
| `Core/Simulation/Components/UnitAI.cs` | `binaries/data/mods/public/simulation/components/UnitAI.js` |
| `Core/Simulation/Components/Health.cs` | `.../Health.js` + `CCmpHealth` |
| `Core/Simulation/Components/Attack.cs` | `.../Attack.js` |
| `Core/Simulation/Components/ResourceSupply.cs` | `.../ResourceSupply.js` |
| `Core/Simulation/Components/ResourceGatherer.cs` | `.../ResourceGatherer.js` |
| `Core/Simulation/Components/Obstruction.cs` | `source/simulation2/components/CCmpObstruction*` |
| `Core/Simulation/GameSim.cs` | `source/simulation2/Simulation2.cpp` + `system/TurnManager.*` + `helpers/Spatial.h` (spatial hash) |
| `Core/Simulation/Commands.cs` | `source/simulation2/helpers/SimulationCommand.h` + `NetMessageSim.cpp` |
| `Core/Simulation/SimTypes.cs` | `source/simulation2/helpers/Position.h` (`entity_pos_t = fixed`) |

## What was kept 1:1

- **Fixed 15.16** semantics, `Sincos_approx`/`atan2_approx` stubs, overflow behavior
- **Grid** lazy `memset`/`bitwise_or` + `any_set_in_square`
- **Hierarchical** `CHUNK_SIZE` flood-fill, region graph, `MakeGoalReachable` spiral search
- **LongPathfinder** octile heuristic `10*(dx+dy)+(14-20)*min`, no corner-cutting, `PriorityQueue` tie-break by insertion order (deterministic)
- **UnitAI** `g_Stances`, order queue, `INDIVIDUAL.*` state names

## What was simplified for Godot / Thronefall

- JS `Engine.QueryInterface` → plain C# `GameSim._aiByUnit` dictionary
- Actor XML / `CCmpVisualActor` → `Presentation/WorldView.cs` MultiMesh + thronefall pastel mats
- `CCmpRangeManager` vision queries → simple distance checks in `UnitAI.TryAutoAttack` (will move to `RangeManager` class later)
- `Formation.js` → formation offsets array in `GameSim` (Box/Line etc not yet)

## How to pull updates

```bash
# compare any file
diff -u ../0ad/source/maths/Fixed.h src/Core/Maths/Fixed.cs
# or batch
for f in $(find ../0ad/source/simulation2/helpers -name "*.h"); do echo "== $f =="; done
```

Next ports when needed: `CCmpTerritoryManager`, `CCmpObstructionManager`, `VertexPathfinder`, `TechnologyManager`.
