# Triggers: fifteenth pass against 0 A.D. 0.28, skirmish map settings (2026-10-21)

## Method

Every 0.28 skirmish map's ScriptSettings (maps/skirmishes/*.xml, 77 maps) was diffed against its data/maps/*.json and data/map_types.json, and each key against the gamesettings attribute that reads it when a map is picked (gamesettings/attributes/*.js onMapChange). On a skirmish 0.28's simulation receives what Game Setup worked out, not the XML's ScriptSettings (MapReader.cpp:152-161), so those handlers decide.

Keys present: Name, Description, Preview, Keywords, CircularMap and PlayerData in all 77; VictoryConditions 74, LockTeams 16, RevealMap 15, AISeed 12, AllyView 12, Size, Seed, BaseHeight, BaseTerrain and TriggerScripts in one or two. PlayerData: Team 41, Name 21, Civ 6, Color 6, Resources 5, StartingCamera 4.

## Findings

| # | 0.28 | RTS0 before | Now |
|---|---|---|---|
| SK1 | PlayerCiv.js onMapChange/_set: a map whose PlayerData has a Civ resets every seat, the map's civs locked, on any map type | `_map_player` answered only scenarios: Egypt's seats showed Random or your pick, and the sim swapped civs at start (AI names from the pre-swap civ) | `_map_seat` for any non-random map; Egypt's civs are set and locked, the rest Random |
| SK2 | LockedTeams.js onMapChange: setEnabled(map's LockTeams), still available on skirmishes | Lock Teams kept its last value | extract_0ad_map_types.py writes "lock_teams" for skirmishes (Egypt); a map change sets the box from it |
| SK3 | PlayerName.js _set: PlayerData Name is the seat's default; pickRandomItems leaves a non-default name, so an AI keeps it | The porter dropped Name; AIs always took civ AI names | port_0ad_maps.py keeps a non-default "name" (--player-data-only refreshed 27 maps); `_player_names` uses it for AI and other seats |
| SK4 | PlayerColor.js: the map's colours join the colour list | Nearest palette colour | Kept |

## Matching

Name, Description, Preview (the two maps without one have none in 0.28 either), Keywords, CircularMap and the map type agree for all 77. PlayerData Civ, Color and StartingCamera values are correct; the Gaia placeholder offset and player counts agree. VictoryConditions, RevealMap, ExploreMap, AllyView, Ceasefire, PopulationCap, StartingResources and PlayerData Resources are left to Game Setup on a skirmish in both (VictoryConditions.js:41, MapExploration.js:32, Ceasefire.js:24, Population.js:45, StartingResources.js:38); Size, Seed, AISeed, BaseHeight and BaseTerrain are ignored by both. Skirmish PlayerData teams being ignored is already documented. Gallic Fields' trigger script is ported.
