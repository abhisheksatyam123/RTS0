# RTS0

Gray-box RTS starter (Age of Empires gameplay direction, Thronefall-style art direction) built with **Godot 4.3 (.NET / C#)**.

**Cross-platform multiplayer target: Windows, Linux, Android, iOS.**
See `docs/MULTIPLAYER.md` for the netcode model, platform matrix, and the architecture rules (sim/presentation split, command pattern) that all gameplay code must follow.

## Requirements
- Godot 4.3+ **.NET edition** (not the standard build): https://godotengine.org/download
- .NET SDK 6.0+ (the editor will prompt/restore automatically)
- For device testing later: Android export templates + keystore; iOS requires macOS/Xcode

## Run
1. Open Godot (.NET edition) -> Import -> select this folder's `project.godot`.
2. First launch builds the C# solution (MSBuild). Press **F5**.

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
See **`docs/BUILD.md`** — export presets for Windows/Linux/Android/iOS, `tools/build.sh` + `tools/build.ps1` wrappers, and GitHub Actions that auto-export Windows+Linux on every push (Android via manual dispatch).

**Current status & priorities: see `docs/NEXT_STEPS.md`.**

## Architecture — mirrors 0 A.D. (`../0ad/source/...`)

```
src/
  Main.cs                    Glue: input gestures -> serializable commands -> GameSim; fixed-step loop
  App/
    CameraRig.cs             RTS camera rig (thronefall: soft shadows, pastel sky)
    SelectionRect.cs         Drag-selection rectangle (Control._Draw)
  Core/
    Maths/                   Deterministic math — ported from 0 A.D.
      Fixed.cs               source/maths/Fixed.h          (CFixed_15_16, 15.16)
      FixedVector2D.cs       source/maths/FixedVector2D.h
      Sqrt.cs                source/maths/Sqrt.cpp (isqrt64)
      Grid.cs                source/simulation2/helpers/Grid.h (Grid/SparseGrid)
    Pathfinding/             Ported from simulation2/helpers/
      PathGoal.cs            PathGoal.h — point/circle/square goals
      HierarchicalPathfinder.cs  HierarchicalPathfinder.h/.cpp — chunk regions + MakeGoalReachable
      LongPathfinder.cs      LongPathfinder.cpp — A* 8-dir (10/14), PriorityQueue, Deterministic
    Simulation/              Deterministic sim (no Godot Nodes) — host-authoritative now, lockstep-ready
      SimTypes.cs            SimUnit/SimBuilding/SimResource + Team enum (helpers/Position.h)
      GameSim.cs             simulation2/Simulation2.cpp + TurnManager — tick, spatial hash, waves
      Commands.cs            helpers/SimulationCommand.h — binary serializable Move/Build/Gather/Attack
      Components/
        UnitAI.cs            mods/public/simulation/components/UnitAI.js (6652 lines → condensed FSM, stances)
        Health.cs            Health.js — Hp/regen, TakeDamage
        Attack.cs            Attack.js — damage types, range, bonuses
        ResourceSupply.cs    ResourceSupply.js — amount / Infinite
        ResourceGatherer.cs  ResourceGatherer.js — rates/capacities/Carry
        Obstruction.cs       CCmpObstruction — grid footprint blocking
  Presentation/              Visual only — never writes sim state (ICmpVisual rule)
    WorldView.cs             Reads sim only; dual MultiMesh (player blue / enemy red) + resource visuals
scenes/
  main.tscn                  Root node only (res://src/Main.cs); everything constructed in code
RTS0.csproj/.sln
docs/
  0AD_PORTING.md             What was copied from ../0ad and why
  MULTIPLAYER.md             Host-authoritative model, platform matrix
  PERFORMANCE.md             Multi-core (Parallel.For) + GPU (MultiMesh, compute) plan
  BUILD.md / NEXT_STEPS.md
```

Key invariants (from 0 A.D.):
- `Core/` never references Godot Nodes -> threadable, testable off-engine, lockstep-friendly.
- Every player action is a serializable `GameCommand` via `GameSim.Apply` -> network `TurnManager` will replay identically.
- Units = GPU `MultiMesh` (1–2 draw calls) regardless of count — thronefall look via clean lighting, not extra geometry.

## Roadmap (suggested order)
1. **Pathfinding** — A*/flow-field on the sim grid inside `GameSim` (deterministic; NOT `NavigationAgent3D`, which is engine-stateful and lockstep-hostile).
2. **Unit state machine** — `enum State { Idle, Move, Gather, Return, Attack }` in `core/`.
3. **Economy** — resource nodes (gold/wood), villager gather loop, drop-off at town center.
4. **Buildings** — production queues, rally points, population cap via houses.
5. **Combat** — HP, attack range/cooldown, projectiles, auto-aggro.
6. **Netcode M2/M3** — host/join UI, ENet command sync (see `docs/MULTIPLAYER.md`).
7. **Touch controls** — tap/drag/pinch scheme feeding the same command pipeline.
8. **Art pass (Thronefall look)** — swap primitives for low-poly assets:
   - Free: Kenney.nl, Quaternius.com; Paid: Synty Studios
   - Pastel palette, flat shading, filmic tonemap, soft shadows, subtle fog/glow in `Environment`
9. **Juice** — AnimationPlayer, sounds, minimap (`SubViewport`), health bars.

See `docs/PERFORMANCE.md` for the multi-core/GPU plan (staggered AI, off-thread ticks, compute shaders) and hot-path rules.
