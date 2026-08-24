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

## Architecture
```
scripts/
  Main.cs                    Glue: input gestures -> serializable commands -> GameSim; fixed-step loop
  core/
    GameSim.cs               Pure simulation (no Godot Nodes): movement, spatial hash, parallel separation, economy
    Commands.cs              Serializable player commands (Move, BuildHouse) with binary encode/decode -> netcode-ready
    SimTypes.cs              Plain sim data (SimUnit, SimBuilding)
  presentation/
    WorldView.cs             Reads sim state only; MultiMesh batched unit rendering, selection ring pool
  CameraRig.cs               RTS camera: pan/rotate/zoom rig with child perspective Camera3D
  SelectionRect.cs           Draws the drag-selection rectangle (Control._Draw)
scenes/
  main.tscn                  Root node only; everything is constructed in code for now
RTS0.csproj/.sln             .NET project files
docs/
  MULTIPLAYER.md             Netcode model, platform matrix, architecture rules
  PERFORMANCE.md             Multi-core + GPU strategy and hot-path rules
```

Key invariants (enforced by structure):
- `core/` never references Godot Nodes -> threadable, testable off-engine, lockstep-friendly.
- Every player action is a serializable `GameCommand` applied via `GameSim.Apply` -> the same entry point the network layer will use.
- Units render as one GPU `MultiMesh` draw call regardless of count.

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
