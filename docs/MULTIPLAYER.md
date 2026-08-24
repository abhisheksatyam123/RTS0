# Multiplayer & Cross-Platform Plan

Targets: **Windows, Linux, Android, iOS** — online multiplayer.

## Platform matrix

| Platform | Renderer | Notes |
|---|---|---|
| Windows | Forward+ | Full support |
| Linux | Forward+ | Full support (+ headless dedicated-server export) |
| Android | Mobile | .NET export uses **AOT**; expect ~30 MB size increase; test early |
| iOS | Mobile | .NET export support is newer/experimental — **verify against your pinned Godot version before locking C# in** |

Rules of thumb:
- Pin one Godot version for the whole team; test Android builds weekly from day one.
- Avoid Forward+-only features (SDFGI, volumetric fog) — Mobile renderer must look acceptable.
- No right-click assumptions anywhere in UI/input code.

## Netcode model

### Chosen: Host-authoritative (client-server)
One peer hosts (or a dedicated Linux server later).

```
Client A/B/C          Host (= player 0 or dedicated)
    |  commands (small, reliable)   ^
    |------------------------------>|  simulate world at fixed tick
    |        snapshots (unreliable) |  broadcast compressed state
    |<------------------------------|
```

Why not deterministic lockstep (the classic AoE approach):
- Cross-platform float determinism (x86 vs ARM) forces fixed-point math for the entire simulation.
- Lockstep punishes mobile network jitter (everyone waits for slowest peer).
- Our scope (co-op/wave defense, hundreds of units — not 1000s) fits snapshots fine.

If we ever add ranked 1v1 PvP, revisit lockstep then — design the sim boundary below so that door stays open.

## Architecture rules (start NOW, cheap to do today, painful to retrofit)

1. **Simulation / presentation split**
   - `core/` — pure game logic: unit state, economy, combat. No `Node3D`, no rendering, deterministic-friendly (fixed timestep, integer or grid coords where possible).
   - `presentation/` — visuals, audio, particles, camera. Reads sim state, never mutates it.
2. **Everything a player does becomes a serializable command**
   - `MoveCommand(unitIds[], Vector3 target)`, `BuildCommand(type, cell)`, etc.
   - Local input -> command -> (send to host) -> host validates & applies. Replay/anticheat/debug come almost free.
3. **Fixed timestep simulation** (e.g., 20 Hz), render decoupled with interpolation.
4. **IDs, not node references**, inside the sim (units, buildings, resources). Maps id -> entity on the presentation side.
5. **Input abstraction**: gameplay code consumes intents (`Select`, `OrderMove`), never raw mouse/touch events. Desktop and touch schemes both feed the same intents.

## Transport
- LAN/desktop: `ENetMultiplayerPeer` (UDP).
- Mobile/carrier NAT: same ENet usually OK with host having open port; fallback WebSocket relay later if needed.
- PC storefronts: GodotSteam for lobbies/overlay.
- Cross-play between stores: your own session service (even a tiny room-list HTTP API) keeps it store-agnostic.

## Mobile UX adaptations (plan early)
- Tap = select single; drag = box select; second-finger or virtual cursor for orders while dragging.
- Pinch zoom + one-finger drag pan (when nothing selected) or edge zones disabled.
- Build/train actions via bottom bar buttons, not hotkeys (hotkeys stay as desktop shortcuts).
- Target: playable one-handed portrait? No — landscape only, lock orientation.

## Milestones insertion
- M1 ✅ (done): command pattern + core/presentation split exist (`core/GameSim.cs`, `core/Commands.cs`; all input routes through serializable commands applied via `GameSim.Apply`).
  - Note: parallelism inside the tick is deterministic-safe (two-phase force pass), so this does NOT block future lockstep.
- M2: local-only game fully driven through commands (netcode-ready).
- M3: host/join UI + ENet sync of commands, host simulates, clients render snapshots.
- M4: mobile touch scheme + Android device testing.
- M5: iOS verification gate -> decide C# stays or hot paths move.
