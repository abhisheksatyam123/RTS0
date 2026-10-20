# B audit pass 4 against 0 A.D. 0.28

Area: diplomacy, chat filtering, victory modes, resign, formation control and rank/XP UI. This pass skipped everything earlier audits already list: pass 3, Session UI pass 9, Multiplayer passes 6-7, the formations audit, Triggers passes 6-8 and the shared-folder audits. Six new differences survived checking against the code.

## Differences

1. **Exodia defeats with no chat line.**
   - 0.28: Cheat.js:56-61 calls `Defeat("%(player)s has been defeated (cheat).")`, and session.js prints it.
   - RTS0: the `CHEAT_DEFEAT_PLAYER` case never called `note_session`.
   - Severity: medium-low.
2. **Observers saw AI ally and private chat.**
   - 0.28: ChatMessageFormatPlayer.js:139-140 tests `isAddressee(sender, Engine.GetPlayerID())`, which is -1 for an observer.
   - RTS0: session_lifecycle.cpp filtered with `local_player()`, which for an observer is the player they are viewing.
   - Severity: low-medium.
3. **A typed /allies, /enemies, /observers or /msg overrode the To: dropdown.**
   - 0.28: Chat.js:88 puts the dropdown's command first, so typed commands only take effect with To: Everyone.
   - RTS0: chat_window.h parsed typed commands whatever the dropdown was set to.
   - Severity: low.
4. **AI chat was counted as "Players chat".**
   - 0.28: messages.js:120-128 gives an AI no GUID, and ChatHistory.js:96-101 needs `senderID > 0`.
   - RTS0: always passed `from_player = true`.
   - Severity: low.
5. **The chat overlay hid while the chat window was open.**
   - 0.28: ChatWindow.js:61-72 only toggles the dialog; chatPanel stays up.
   - Severity: low.
6. **"brainiac N" counted every technology, and cheat numbers had to be integers.**
   - 0.28: Cheat.js:135-178 numbers the techs as Researcher.js GetTechnologiesList does: one entry per pair, "top" or "bottom" picks within a pair, and superseded or researched techs are skipped. Cheats.js:43-46 reads numbers with `+parameter`.
   - Severity: low.

## Already matches 0.28

- **Diplomacy:**
  - mutual worsening
  - ceasefire and locked-team refusals
  - gates opening for one-sided allies
  - tribute all-or-nothing, with the shortage notice
  - spy costs and lines
  - attack request visibility
- **Chat:**
  - the addressee list, and its rules shared between host and receiver
  - order of handling: network commands, then cheats, then chat
  - the nine filters, 20 overlay lines for 30 s, the 1024-character cap
- **Victory:**
  - every mode, including the nomad fallbacks
  - wonder and relic countdowns, regicide hero rules
  - allied victory and Last Man Standing
  - every won and defeated reason string
  - population caps recomputed on defeat
- **Ceasefire:** enemies count as neutral, and the countdown lines.
- **Resign and exit:** the dialogs, the VICTORIOUS! and DEFEATED! boxes, "%(player)s has resigned.", and units going to Gaia.
- **Formations:** the panel, its tooltips and the default star, toggledefaultformation, formationwalkonly, the line drag.
- **Rank and XP:** the icon, the bar and tooltip, the status-bar options, rank selection clicks, and the promote cheat line.

## Status (2026-10-20)

All six fixed in the pass-4 PR. tools/test_victory.cpp checks the exodia notice; e2e_session_dialogs checks To: Allies; e2e_dev_overlay checks "1e3".
