# Petra AI Parity vs 0 A.D. `ai/petra`

> **Status:** Native C++ port. Config and state types are in `src/simulation/petra_ai.h`. Behaviour is the `Simulation::petra_*` methods in `src/simulation/simulation.cpp`, driven each tick from `Simulation::tick_petra_ai` (`simulation.cpp:7415`).
> Parity percentages: `docs/REMAINING_GAPS.md` (G10). Remaining work: `docs/PLAN.md` (P3).

## Fidelity table

Status key (grep, 2026-10-08): **Present** = code exists and is called from `tick_petra_ai`. Fidelity to 0 A.D. is not re-audited. **Absent** = no matching code found. **Not re-checked** = not verified this pass.

| 0 A.D. module (`ai/petra/`) | RTS0 location | Status |
|---|---|---|
| `config.js`, `difficultyLevel.js` | `PetraConfig` in `petra_ai.h:46`; difficulty and behaviour tables in `recompute()` | Present; hard-coded tables, no external data file |
| `headquarters.js`, `baseManager.js` | `update_petra_bases` (`simulation.cpp:4460`); `PetraBase` (`simulation.h:378`) | Present as per-base anchors; no HQ class |
| `attackManager.js`, `attackPlan.js` | `petra_manage_tactical_attacks` (`simulation.cpp:6186`), `petra_manage_raid` (`:6656`); `AttackPlan` (`simulation.h:405`) | Present |
| `defenseManager.js` | `petra_manage_defense` (`simulation.cpp:7067`); `PetraDefenseArmy` (`petra_ai.h:187`) | Present |
| `queueManager.js`, `queue.js` | `PetraQueueManager` (`petra_ai.h:133`, per-queue accounts); queue priorities in `PetraConfig` | Present |
| `mapModule.js`, `borderMap.js`, `accessibility.js`, `territoryMap.js` | `update_petra_threat_map` (`simulation.cpp:4334`); threat grid `THREAT_GRID_RES = 64` (`simulation.h:645`) | Threat grid only. Absent: obstruction map, accessibility |
| `economy.js`, `economicManager.js` | `petra_manage_economy` (`simulation.cpp:5057`) | Present |
| `buildingManager.js`, `buildingPlacement.js`, `startingStrategy.js` | `petra_manage_buildings` (`simulation.cpp:5586`); `petra_manage_opening` (`:6774`) | Present |
| `militaryManager.js`, `unitManager.js` | `petra_manage_military_production` (`simulation.cpp:4897`) | Present |
| `navalManager.js`, `transportPlan.js` | `petra_manage_naval` (`simulation.cpp:5346`), `petra_manage_transport` (`:6992`); per-sea fleet registry `Petra` (`petra_ai.h:204`) | Present |
| `tradeManager.js` | `petra_manage_trading` (`simulation.cpp:5243`) | Present |
| `diplomacyManager.js` | `petra_manage_diplomacy` (`simulation.cpp:7302`); `PetraDiplomacyRequest` (`petra_ai.h:166`) | Present |
| `victoryManager.js`, `wonderManager.js` | `petra_manage_victory` (`simulation.cpp:7192`) | Present |
| `chatHelper.js` | `PetraChatLine` (`petra_ai.h:179`); `petra_say` (`simulation.cpp:6902`) | Present |
| `statistics.js` | none found | Not re-checked |
| `workerManager.js`, `trainingManager.js` | worker roles are in `petra_manage_economy`; training is inside `petra_manage_military_production` and `petra_manage_economy`. RTS0's own threat evacuation (`petra_manage_workers`) is gone: 0.28 has none, and `petra_garrison_attacked_unit` ports defenseManager garrisonAttackedUnit | Present; no separate manager class |
| `researchManager.js` | `petra_manage_research` (`simulation.cpp:5930`) | Present |
| `emergencyManager.js` | `petra_manage_emergency` (`simulation.cpp:7364`); `PetraEmergency` (`petra_ai.h:196`) | Present |
| `scoutManager.js` (not in the 0 A.D. list above) | `petra_manage_scouting` (`simulation.cpp:4625`) | Present |

## Current wiring

`Simulation::tick_petra_ai` (`simulation.cpp:7415`) runs these steps in order. All of them mutate `Simulation` state directly. There are no per-manager classes.

```
tick_petra_ai(dt)
  trickle: ai_food/wood/gold/stone += rate * dt * trickle_mult
  petra_refresh_queue_accounts()
  1   update_petra_threat_map        (timer 0.4 s, simulation.cpp:4336)
  2   update_petra_bases
  3   petra_manage_scouting
  5   petra_manage_military_production
  6   petra_manage_economy
  7   petra_manage_trading
  8   petra_manage_naval
  9   petra_manage_opening, petra_manage_buildings
  10  petra_manage_research
  11  petra_manage_tactical_attacks, petra_manage_raid
  11b petra_manage_transport
  12  petra_manage_defense
  13  petra_manage_victory
  14  petra_manage_diplomacy
  15  petra_manage_emergency
```

`petra_ai.h` holds only the types: `PetraConfig`, queue accounts, transport plan, diplomacy requests, chat lines, defense army, emergency state, and the sea fleet registry. It does not run any logic.

## Known gaps (grep, 2026-10-08)

- No HQ or BaseManager class. Base logic lives in `Simulation` (`simulation.h:377`).
- No `obstructionMap`, accessibility or border map. Placement does not consult them.
- Threat map is 64x64 (`simulation.h:645`). 0 A.D. uses larger influence maps.
- No external config file. Difficulty and behaviour values are constants in `PetraConfig::recompute()`.

## References

- `src/simulation/petra_ai.h` — config, queue, transport, diplomacy, chat, defense, emergency and sea-fleet types.
- `src/simulation/simulation.h:369-725` — Petra state members (`petra_threat_map_`, `PetraBase`, `AttackPlan`) and `petra_*` method declarations.
- `src/simulation/simulation.cpp` — `tick_petra_ai` at `:7415`; `Simulation::petra_*` methods as listed in the table above.
- Progress and remaining work: `docs/REMAINING_GAPS.md` (G10), `docs/PLAN.md` (P3).
