# Triggers: twentieth pass against 0 A.D. 0.28, Petra diplomacy (2026-11-03)

## Method

Read 0.28 `ai/0ad-028-petra/diplomacyManager.js` and `chatHelper.js` and compared them with RTS0's `petra_manage_diplomacy`, `petra_handle_diplomacy_request`, `petra_change_diplomacy`, `petra_cancel_attacks_against` and the `petra_say` kinds at main `34dde0c52`.

## Results

| Area | 0.28 | RTS0 | Verdict |
|---|---|---|---|
| `tributes()`: 0.3 of the surplus, 100 for weak allies, request branch when an ally has more than us by 600, 90 s / 240 s timers | diplomacyManager.js:66-126 | `petra_manage_diplomacy` | match |
| `handleDiplomacyRequest()`: odds 0.6 / 0.4 (ally) / 0.8 (neutral); tribute `max(1000, units * (ally ? 10 : 5))` | :385-447 | `petra_handle_diplomacy_request` | match |
| `update()` cadence: tributes every 30 s unless saving resources; sent-request lapse 120 + 10..100 s, then 300 + 10..100 s; 0.1 roll | :539-566 | same | match |
| `checkSentDiplomacyRequests()`: 60 s expiry, pop > 70 and not saving | :528-537 | same | match |
| `checkEvents` TributeExchanged: `wanted -= amount`, accept at zero, reset the warning on a partial tribute, cooperative trait from allies' tribute (food 1x, others 2x, 0.0001 per unit) | :128-172 | `petra_handle_*` inbox pass | match |
| `checkEvents` TributeRequest: pay at once if the amount is below what we have free, next tribute +15 s | :264-284 | same | match |
| `checkRequestedTributes()`: warn once after 60 s, then decline | :460-484 | same | match |
| `changePlayerDiplomacy()`: cancel attacks against an enemy turned ally or neutral | :449-458 | `petra_change_diplomacy` and `petra_cancel_attacks_against` | match |
| `chatHelper.js` kinds: attack, tribute, tribute request, answer kinds, new diplomacy | chatHelper.js:30-190 | `petra_say` call sites | match in kind and gating. Text is localised per kind, so wording is not ported verbatim |

## Campaigns

Campaign scripts and the campaign GUI were audited in passes 7, 10 and 13 (campaign_test_map, napata_reconstruction, the campaign pages and endings, cinema). This pass did not re-check them line by line.

No drift found in diplomacy, so no code changed.
