# Triggers: twelfth pass against 0 A.D. 0.28, Petra diplomacy (2026-10-20)

## Method

diplomacyManager.js, gamestate.js (hasAllies, hasEnemies, hasNeutrals, isPlayerAlly), Setup.js and EndGameManager.js were read against `petra_update_diplomacy`, `petra_last_man_standing_check`, the tribute code and the victory check in `simulation.cpp`, on main at `be873c235`. Everything the ninth pass fixed (D1-D8) was checked again and still holds.

## Findings

| # | 0.28 | RTS0 before | Now |
|---|---|---|---|
| D9 | `checkEvents` runs `lastManStandingCheck` on any DiplomacyChanged, PlayerDefeated or CeasefireEnded event (DM:187-188); `update` runs it again once the lapse is up (DM:546-547). The check itself never compares the time, so an event while waiting turns the AI at once if it has 50 units (DM:315-318) | The check ran every turn and returned until its own lapse was up | A 16-bit hash of every stance, the defeated players and the ceasefire, kept in `PetraDiplomacy`'s padding, stands for the events; the check runs on a change and on the timer |
| D10 | `Setup.js:63`: allied victory is `LockTeams \|\| !LastManStanding` | Allied victory was `!last_man_standing()` alone, in Petra and in the victory check | `allied_victory()` |
| D11 | `hasAllies`, and the tribute and attack boosts to the cooperative trait (DM:134, 161, 182), use `isPlayerAlly`: our own stance | A mutual alliance (`is_ally`) | Our own stance; a tribute from a player we already ally with no longer counts against its request |
| D12 | DiplomacyChanged is handled per event | Stances toward us are compared once per AI turn | Open (low): a change and its undoing inside one 1.6 s turn go unseen |

## Left as they are

- RTS0 answers a declaration of war in kind (`petra_update_diplomacy`). `cmd_set_diplomacy` already sets the target's stance to match, as `Diplomacy.js` OnDiplomacyChanged does, so the branch never runs in play.
- The old `PetraDiplomacyRequest` map is still saved and aged. Only `test_netcode` reads it.

## Tests

`test_petra test_diplomacy`:
- Last Man Standing with teams locked: allies never fall out.
- Three allied AIs: with two of them held 1000 s on their timers, the first AI's betrayal sets off a second AI within 5 s.
