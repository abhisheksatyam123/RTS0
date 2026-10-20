# Petra parity audit: RTS0 (main a8ac431f2) vs 0 A.D. 0.28 -- cadence, difficulty, behaviour

Paths:
- 0.28 Petra: `/mnt/project-files/ai/0ad-028-petra/`
- 0.28 sim components and data: `/mnt/project-files/d-civ-rosters/0ad-028-src/simulation/`
- RTS0: `src/simulation/` (`simulation.cpp`, `petra_ai.h`, `simulation.h`)

Save format: `PetraConfig` is 88 bytes and has `int reserved_[6]`. None of the fixes below need a resize. Where a fix needs saved state, the finding says so.

## Turn length and cadence base (verified matching)

- **0.28.** `_petrabot.js:110` plays when `!playedTurn || (turn + player) % 8 == 5`. DEFAULT_TURN_LENGTH is 200 ms, so the AI plays one turn every 1.6 s.
  - The engine constant is not in the provided tree. This value is from knowledge.
- **RTS0 matches.**
  - `simulation.h:3769-3776`: `PETRA_TURN_TURNS=8`, `PETRA_TURN_TICKS=32`, `PETRA_TURN_S=1.6`.
  - `simulation.cpp:30546-30557`: `petra_turn_due` and `petra_played_turn`, with the per-player stagger.
  - `turn_manager.h:30-31`: `TURN_DURATION 0.2`.

## Ranked findings

### 1. `getPopulationMax()` is read as the housing limit instead of the population cap (HIGH, confirmed and measured)

In 0.28, `getPopulationMax()` returns the **cap** and `getPopulationLimit()` returns min(cap, houses):
- `GameState.getPopulationMax` reads `Player.GetMaxPopulation`, which is GuiInterface `popMax` (`Player.js:172-185`, `GuiInterface.js:106-107`).
- The default cap is 300 (`population_capacities.json`).

RTS0's names are the other way round:
- `pop_max(pid)` (`simulation.cpp:801`) is the housing-limited value.
- `pop_limit(pid)` (`simulation.cpp:787`) is the cap.

RTS0 calls `pop_max` at the Petra sites that use `getPopulationMax` in 0.28:

| Site | 0.28 | RTS0 |
|---|---|---|
| Max-pop special case | `attackPlan.js:504-508`: `if (gameState.getPopulationMax() - gameState.getPopulation() < 5) { let lengthMin = 16; if (gameState.getPopulationMax() < 300) lengthMin -= Math.floor(8 * (300 - gameState.getPopulationMax()) / 300); if (this.canStart() \|\| this.unitCollection.length > lengthMin) this.assignUnits(gameState); else { Engine.ProfileStop(); return 0; } }` (returns 0, which is PREPARATION_FAILED) | `simulation.cpp:26313-26340`: `const bool capped = pop_cap - pop_current(petra_pid_) < 5;` with `pop_cap = pop_max(...)`, and `length_min` at 26337. A comment says RTS0 keeps mustering instead of failing. |
| Concurrent attack plans | `attackManager.js:392`: `Math.min(2, 1 + Math.round(gameState.getPopulationMax() / 100))`; `:395`: `gameState.getPopulationMax() - gameState.getPopulation() > 12` | `simulation.cpp:26174-26187`: `pop_cap = pop_max(petra_pid_)`; `started >= min(2, 1+lround(pop_cap/100)) \|\| (started > 0 && pop_cap - pop_current <= 12)` |
| Wonder | `headquarters.js:2315`: `gameState.getPopulation() > gameState.getPopulationMax() * 0.9` | `simulation.cpp:24812`: `(float)pop > 0.9f * (float)pop_max(petra_pid_)` |
| Victory guards | `victoryManager.js:346` (getPopulationMax) | `simulation.cpp:29896` (pop_max) |

**Impact.**
- Early in the game the housing limit is reached all the time, so RTS0 hits the "capped" special case constantly.
- Rushes launch below target. In seated Medium mirrors at half 128, aggressive behaviour, the baseline first rush always left at about 300 s with 11 units against a target of 18. That size is lengthMin 10 at limit 50.
- The concurrent-plan limit sees cap 50 → 1 plan, and pop room ≤ 12 blocks a second plan.
- The wonder fires at 90% of housing, not 90% of the cap. With phase and %30 gates this is inferred to be rare.

**Measured (variant C, all four sites switched to `pop_limit`).**
- Rushes leave with 19-25 units.
- Aggressive town times drop from 614-1074 s to 507-669 s.
- Balanced mirrors reach the city in all 3 seeds (1198-1294 s). The baseline reached it at 1217 s, never, and 1318 s.

**Fix.**
- Use `pop_limit()` (the cap; 10000 when unlimited, as 0.28) at the four sites.
- Restore the 0.28 fail branch at 26333-26340: when `capped && !can_start && size <= length_min`, abort the plan.

**Save format.** No change.

### 2. The default AI behaviour is "balanced", not 0.28's "random" (HIGH for the observed timings, confirmed)

- **0.28.** `simulation/data/settings/player_defaults.json`: `"AIBehavior": "random"`, `"AIDiff": 3`. With random, `config.js` draws the personality (aggressive, cooperative, defensive) randomly.
- **RTS0.** Defaults to balanced (2) in four places:
  - `scenes/menu/match_settings.gd:86`: `var ai_behavior: int = 2`
  - `gamesetup.gd:2640`
  - `net_types.h:434`: `behavior = 2`
  - the `PetraConfig` default

**Impact.**
- Over 12 seeds (probe2, half 128, 24 min), random drew aggressive ≥ 0.77 in 4 of 12 seeds (22, 5, 23, 37).
  - Aggressive > 0.7 switches on popPhase2 50 and popForBarracks1 12 (`config.js:233-320`).
  - Those seeds reached the town phase at 479-564 s, against 778-823 s for balanced.
- City times overlap: 900-1118 s for both.
- A "default" RTS0 game is therefore never an aggressive or defensive Petra. 0.28 is one about a third of the time.

**Fix.** Default `ai_behavior` to random (0) in the four places.

**Save format.** No change; the behaviour is already serialized.

### 3. Queue funding latency: an item is paid one or more manager cadences late (MEDIUM, mechanism confirmed and measured)

**0.28** (`queueManager.js:396-421`). `update()` runs at the end of every played turn, in this order:
1. `checkPausedQueues`
2. `distributeResources` (`:227-321`). `maxAccountWanted` is the first plan plus 0.6 × the second (`queue.js:72-84`). Priority is halved once the first item is funded.
3. `startNextItems` (`:371-394`)

A plan queued by a manager earlier in the same turn is funded and started that turn. Plans persist, so `trainMoreWorkers` (%4) just keeps the queue full.

**RTS0** has no persistent plan objects. Each queue has a single "want" (`petra_ai.h:180`, `WANT_TTL = 10.0f`).
- `petra_try_pay` (`simulation.cpp:27535-27565`) succeeds only if the queue's account already holds the cost.
- `petra_refresh_queue_accounts` (`27390-27491`) runs once at the **start** of the player turn (`30657`).
- A new want fails its first try_pay (account empty). It is funded at the start of the next turn, and paid only when the manager runs on its own cadence again.
- Workers (%4 at `19711`) therefore went out on played turns 8, 16, 24 and so on: one batch per 12.8 s instead of per 6.4 s.
- Distribution also lacks the 0.6 × second-plan term and the priority halving.

**Measured (baseline, Petra player 1, Continental).**
- The CC was idle for 39% of the first 5 minutes.
- Variant F refreshes with priorities on a failed pay and retries once in the same turn, which emulates 0.28's same-turn start. It cut CC idle to 30%.
  - Variant F changed spending; for example, Spartan helot_economy research moved from 439 s to 0 s.
  - Variant F did **not** advance phases: town mean 814 vs 816 s, city 1037 vs 991 s.
- Variant E (refresh at the end of the turn) had no effect, because payment still waits for the manager's cadence.
- Houses (%4), research (%5) and training buildings (%3) use the same path. That they get the same delay is **inferred**; I measured only workers.

**Fix.**
- Fund and start in the same turn as the request: a variant F-style retry, or a short-lived plan list per queue.
- Add the 0.6 × second-item term.

**Save format.** None for the F style. Persistent plans would need new saved state.

### 4. setWorkersIdleByPriority: no phase gate, different period, one switch globally (MEDIUM, confirmed and measured)

**0.28.**
- `baseManager.js:1155-1157` runs only when `gameState.currentPhase() > 1 || gameState.ai.HQ.phasing == 2`, and only on an anchored base.
- It makes one switch **per base** per call.
- `baseManager.js:604`: `this.timeNextIdleCheck = gameState.ai.elapsedTime + 8`. With `elapsed > next` at a 1.6 s cadence, this fires every 6 turns (9.6 s). The initial value is 0 (`:45`).

**RTS0.**
- `simulation.cpp:20413-20470`. Line 20420: `if ((int)(petra_timer_ / 8.0f) != (int)((petra_timer_ - petra_turn_dt_) / 8.0f)) {`. That is about every 8 s.
- There is no phase gate, and only one switch globally (`bool done`, 20457).

**Measured (variant D adds the phase gate).**
- RTS0 gets **slower**: town mean 852 vs 816 s, city 1068 vs 991 s.
- So the extra village-phase rebalancing currently masks a growth deficit somewhere else. Fixing this alone would move RTS0 further from 0.28's timings.
- Apply this fix only together with finding 3 or 1, or after the deficit is found.

**Fix.**
- Add the phase gate.
- Use `played_turn % 6`. An exact per-base `timeNextIdleCheck` would need saved state; `% 6` does not.
- Allow one switch per base.

**Save format.** None with `% 6`.

### 5. Lower-impact differences (confirmed unless marked)

| # | 0.28 | RTS0 | Impact and fix |
|---|---|---|---|
| a | `garrisonManager.js:134`: unloads at most once per holder every 3 s | `simulation.cpp:28773-28797`: checks every turn, with no per-holder throttle | Low; units ungarrison slightly earlier. Fix: `played_turn % 2` (no save change), or a per-holder timestamp (saved state). |
| b | Raid plans can relaunch immediately | `simulation.cpp:27024`: `if (plan.state_timer < 6.0f) return;` (RTS0-only wait) | Low. Remove it, or document it as a deliberate deviation. |
| c | `tradeManager.js:698`: `%20` with `this.traders.length >= 2` (live traders only) | `simulation.cpp:22096`: `traders >= 2`, where `traders` includes queued ones | Low; the second trade route is evaluated slightly early. |
| d | `queueManager.js:424-489`: `workersMin = Math.min(Math.max(12, 24*popScaling), popPhase2)`, not floored | `simulation.cpp:27365`: `(int)(24*scaling)` | Very low; only at Easy (popScaling 0.7) does 16.8 become 16. |
| e | `victoryManager.js`: relic lapse checked on the %10 cadence | `simulation.cpp:25349-25350`: 2 s steps | Very low granularity difference. |
| f | Opening dropsite on the first played turn | `simulation.cpp:27212`: `if (petra_opening_timer_ < 0.8f) return;` delays it one AI turn | 1.6 s at the start. Fix: drop the timer, or use a `>=` first-turn check. |
| g | `config.js`: `maxPop` set once in setConfig | `simulation.cpp:25486-25501`: `petra_config_max_pop` is recomputed live as `pop_cap` × civ bonus | Differs only under World Population or changed caps. Freezing it would need 4 bytes of saved state, which fits in `reserved_`. |
| h | none | `simulation.cpp:24585-24593`: an RTS0-only "wood-starved" clause in `petra_phasing` | Deviation; check whether it is intended. |
| i | none | `simulation.cpp:18965-19100`: RTS0-only scouting with idle cavalry | Deviation; costs early cavalry time. |
| j | `trainMoreWorkers` queues the batch regardless of pop room (the queue pauses) | `simulation.cpp:19715`, `19470`: `room = petra_pop_room()` caps the batch, and `petra_pop_room() >= count` is required | Smaller batches near the housing limit. This compounds with finding 3. |
| k | `TechnologyManager.js:41` and `Researcher.js:272`: AI Cheat `Cost/BuildTime` does not apply to technologies | `simulation.cpp:4171-4174`: `tech_price` matches this, but the `petra_ai.h:61` comment says "(units, structures, technologies)" | The comment is stale; the code is correct. |

## Difficulty and behaviour: verified matching

Each item was checked against both sources.

**Config (`config.js` setConfig 233-320 vs `simulation.cpp:23667-23707` and `24499-24510`).**
- popScaling: 0.5 below Easy, 0.7 at Easy.
- popPhase2 and popForBarracks1 when aggressive > 0.7 from Medium.
- The healer rule.
- targetNumWorkers: 40, maxPop/2, maxPop/3.
- targetNumTraders: 2 + difficulty.
- workPhase3 is Infinity below Easy.
- Sentry towers, extraTowers and extraFortresses (`headquarters.js:62-63`).
- Tower and fortress lapse multiplied by (1.1 - 0.2·defensive).
- Cheat rate and time multipliers (`petra_ai.h` recompute: kPop, kRate, kTime, kEmerg, kRoots).
- supportRatio.
- provisionFields.
- Personality draws.
- Emergency factors.

**Headquarters %N gates (`headquarters.js:2238-2350`).**
- trainMoreWorkers 4/0, buildMoreHouses 4/1, buildFarmstead 4/2, manageCorral 4/3.
- research 5/1 (`24837`).
- checkBaseExpansion 10/7 with phase > 1 (`23404`).
- market, forge and temple %3 (`24775`).
- wonder %30.
- training buildings %3 (`23458`).
- defenses %3 with the Sandbox gate (`23506`).
- The advanced-buildings list (`23511`).

**Research and phases.**
- checkPhase runs every turn and is blocked by a queued majorTech, civil centre or wonder.
- Research update is blocked while the minor or major queues are busy.
- Phase thresholds.

**Queue priorities.**
- `petra_queue_base_priority` (`27303-27331`) matches, including healer, defenseBuilding, and the plan × 0.4 / 0.8 factors.

**Bases.**
- checkResourceLevels: next check +20 / +10 (`baseManager.js:480-541`).
- neededDefenders (`baseManager.js:40` vs `18878-18879`).
- Decaying structures are not destroyed below Easy (`headquarters.js:266` vs `28699`).

**Attacks.**
- Difficulty variation and priority (`attackPlan.js:183-217`).
- Siege targetSize (`436-440`).
- End-game fasten %5 (`488`).
- Workers join only above Easy (`765` vs `26247-26251`).
- The %5 stuck checks.
- Bombers when difficulty > VERY_EASY and %5 (`attackManager.js:439` vs `25983`).
- The attack manager is off at Sandbox.
- Can-expand ≠ SANDBOX (`startingStrategy.js:40`).

**Trade.**
- Gate ≤ VERY_EASY (`tradeManager.js:684` vs `21969`).
- %5, %20 and %60 cadences (apart from item 5c).
- Ship treasure %5.

**Other managers.**
- Naval %3 (`navalManager.js:873`).
- Defense manager cadences (`defenseManager.js:209`, `321`) and the checkEnemyUnits rotation.
- Victory: relic count 240 - 30(d-3) (`victoryManager.js:578`), and the `:585` and `:590` %10 checks.
- Diplomacy timers: 90 and 240 initial values.
- Worker cadences (`worker.js:282`, `384`).

## Method and measurements

- **Tools.** Scratch C++ probes linked against copies of the RTS0 objects, with `#define private public`. No repository files were modified.
  - `p6/petra6_probe.cpp`, `petra6_probe2.cpp` (behaviour argument) and `petra6_atk.cpp` (seated Medium mirror that logs attack launches).
  - Variants generated by `p6/petra6_variants*.py` (B, C, D, E, F, BCD).
  - Runs by `p6/petra6_runs.sh`, summarised by `p6/petra6_sum.py`.
  - Results in `p6/res*.txt`, `p6/atk_res.txt` and `p6/beh.txt`.
- **Probe setup.** Continental, seeds 42, 7, 1234, 22 and 99, half 64 and 128, 24 min. Petra player 1; p0 is unseated, as in the bench.
- **Baseline (A).** Town 794-845 s (mean 816), city 910-1120 s (mean 991).
  - This setup differs from the one in PR #1159, which gave about 882 s. Use the deltas, not the absolute values.

| Variant | Town mean | City mean |
|---|---|---|
| A (baseline) | 816 | 991 |
| B (try_pay tops up from unaccounted resources) | 818 | 1052 |
| C (pop cap fix, finding 1) | 842 | 993 |
| D (idle-switch phase gate) | 852 | 1068 |
| E (refresh at end of turn) | 824 | 1022 |
| F (same-turn fund and retry) | 814 | 1037 |
| BCD | 833 | 1022 |

- **What the numbers say.**
  - In the unseated, non-fighting bench, no single cadence fix explains the city gap. RTS0's timings fall within or ahead of 0.28's 14-16 min.
  - The differences that matter in real skirmishes are 1 (under-sized rushes and the plan limit) and 2 (default behaviour). Their effect shows up in the seated mirrors (`atk_res.txt`).
  - Finding 4 shows that RTS0's extra village-phase rebalancing hides a slower underlying economy. That deficit was not located in this pass (inferred).

## Status
- 1 fixed: `petra_population_max()` at the four getPopulationMax sites. The plan still keeps mustering at the true cap instead of failing (rare at 300).
- 2 fixed: autostart, the net defaults and match settings default to random. Game Setup already used gui.gamesetup.aibehavior "random".
- 3 and 4: handed to the economy owner (C).
