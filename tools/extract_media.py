#!/usr/bin/env python3
"""Dump every zip audio file, alias RTS0 play paths, and extract UI textures.

Replaces the curated copies in extract_ui_assets.py / extract_combat_sfx.py.
"""
import os
import sys
import zipfile

_CANDIDATE_ZIPS = [
    os.path.join(os.path.dirname(__file__), "..", "..", "0ad",
                 "binaries", "data", "mods", "public", "public.zip"),
    "/home/abhi/project/0ad/binaries/data/mods/public/public.zip",
]
ZIP_PATH = next((p for p in _CANDIDATE_ZIPS if os.path.exists(p)), _CANDIDATE_ZIPS[0])
ASSETS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "assets"))

# zip path -> one or more assets-relative destinations (RTS0 C++ / HUD names)
SOUND_ALIASES = [
    # Combat
    ("audio/attack/weapon/swordhit_10.ogg", "sounds/combat/sword_hit_1.ogg"),
    ("audio/attack/weapon/swordhit_12.ogg", "sounds/combat/sword_hit_2.ogg"),
    ("audio/attack/weapon/swordhit_14.ogg", "sounds/combat/sword_hit_3.ogg"),
    ("audio/attack/weapon/bow_attack_01.ogg", "sounds/combat/bow_shoot_1.ogg"),
    ("audio/attack/weapon/bow_attack_03.ogg", "sounds/combat/bow_shoot_2.ogg"),
    ("audio/attack/weapon/arrowfly_01.ogg", "sounds/combat/arrow_fly.ogg"),
    ("audio/attack/impact/arrow_wood_01.ogg", "sounds/combat/arrow_hit.ogg"),
    ("audio/attack/destruction/explode_debris_20.ogg", "sounds/combat/catapult_blast.ogg"),
    ("audio/attack/weapon/spear_attack_01.ogg", "sounds/combat/spear_hit_1.ogg"),
    ("audio/attack/weapon/spear_attack_02.ogg", "sounds/combat/spear_hit_2.ogg"),
    # Buildings
    ("audio/interface/complete/building/complete_civ_center.ogg", "sounds/building/complete_tc.ogg"),
    ("audio/interface/complete/building/complete_barracks.ogg", "sounds/building/complete_barracks.ogg"),
    ("audio/interface/complete/building/complete_house.ogg", "sounds/building/complete_house.ogg"),
    ("audio/interface/complete/building/complete_tower.ogg", "sounds/building/complete_tower.ogg"),
    ("audio/attack/destruction/building_collapse_large_01.ogg", "sounds/building/building_collapse.ogg"),
    ("audio/attack/fire/sp_11.ogg", "sounds/building/building_fire.ogg"),
    # Alarms
    ("audio/interface/alarm/alarmattackplayer_1.ogg", "sounds/alarm_under_attack.ogg"),
    ("audio/interface/alarm/alarm_attacked_gaia_01.ogg", "sounds/alarm_under_attack.ogg"),
    ("audio/interface/alarm/alarmresearchtech_1.ogg", "sounds/alarm_research_done.ogg"),
    ("audio/interface/alarm/alarmvictory_1.ogg", "sounds/alarm_victory.ogg"),
    ("audio/interface/alarm/alarmdefeat_1.ogg", "sounds/alarm_defeated.ogg"),
    ("audio/interface/alarm/alarm_defeated_ally.ogg", "sounds/alarm_defeated.ogg"),
    # UI holes (C++ plays these; zip names differ)
    ("audio/interface/alarm/alarmalert0.ogg", "sounds/ui/alarm.ogg"),
    ("audio/interface/ui/chat_alert.ogg", "sounds/ui/chat.ogg"),
    ("audio/interface/ui/gamesetup_join.ogg", "sounds/ui/diplomacy.ogg"),
    ("audio/interface/select/building/sel_universal_1.ogg", "sounds/ui/cancel.ogg"),
    ("audio/interface/select/building/sel_universal_1.ogg", "sounds/ui/bonus.ogg"),
    # Generic command voices
    ("audio/voice/greek/civ/civ_female_what_is_it_1.ogg", "sounds/voice/select_1.ogg"),
    ("audio/voice/greek/civ/civ_female_what_is_it_2.ogg", "sounds/voice/select_2.ogg"),
    ("audio/voice/greek/civ/civ_female_my_lord_1.ogg", "sounds/voice/select_3.ogg"),
    ("audio/voice/greek/civ/civ_female_walk_1.ogg", "sounds/voice/move_1.ogg"),
    ("audio/voice/greek/civ/civ_female_march_1.ogg", "sounds/voice/move_2.ogg"),
    ("audio/voice/greek/civ/civ_female_attack_1.ogg", "sounds/voice/attack_1.ogg"),
    ("audio/voice/greek/civ/civ_male_attack_10.ogg", "sounds/voice/attack_2.ogg"),
    ("audio/voice/greek/civ/civ_female_gather_together_1.ogg", "sounds/voice/gather_1.ogg"),
    ("audio/voice/greek/civ/civ_female_build_1.ogg", "sounds/voice/build_1.ogg"),
    ("audio/voice/greek/civ/civ_female_repair_1.ogg", "sounds/voice/repair_1.ogg"),
    # Hellenic display names (zip has no audio/voice/hellenes/)
    ("audio/voice/greek/civ/civ_female_what_is_it_1.ogg", "sounds/voice/hellenes/khaire.ogg"),
    ("audio/voice/greek/civ/civ_female_what_is_it_2.ogg", "sounds/voice/hellenes/ti_estin.ogg"),
    ("audio/voice/greek/civ/civ_female_what_is_it_3.ogg", "sounds/voice/hellenes/hetoimos.ogg"),
    ("audio/voice/greek/civ/civ_female_walk_1.ogg", "sounds/voice/hellenes/poreuomai.ogg"),
    ("audio/voice/greek/civ/civ_female_march_1.ogg", "sounds/voice/hellenes/baino.ogg"),
    ("audio/voice/greek/civ/civ_female_attack_1.ogg", "sounds/voice/hellenes/epi_tous_polemious.ogg"),
    ("audio/voice/greek/civ/civ_female_go_out_against_1.ogg", "sounds/voice/hellenes/eis_makhen.ogg"),
    ("audio/voice/greek/civ/civ_female_build_1.ogg", "sounds/voice/hellenes/poieso.ogg"),
    ("audio/voice/greek/civ/civ_female_work_land_1.ogg", "sounds/voice/hellenes/ergon.ogg"),
    # Napatan zip has no hello and no male what_is_it
    ("audio/voice/napatan/civ/civ_female_what_is_it_01.ogg",
     "sounds/voice/napatan/civ_female_hello_01.ogg"),
    ("audio/voice/napatan/civ/civ_female_what_is_it_01.ogg",
     "sounds/voice/napatan/civ_male_what_is_it_01.ogg"),
    # Ambient + music RTS0 loads
    ("audio/ambient/dayscape/day_temperate_gen_01.ogg", "sounds/ambient/nature_ambient.ogg"),
    ("audio/ambient/weather/windleaves_10.ogg", "sounds/ambient/wind_ambient.ogg"),
    ("audio/ambient/water/wave_21.ogg", "sounds/ambient/water_coast.ogg"),
    ("audio/ambient/water/river_med_21.ogg", "sounds/ambient/water_river.ogg"),
    ("audio/music/In_the_Shadow_of_Olympus.ogg", "music/theme.ogg"),
    ("audio/music/In_the_Shadow_of_Olympus.ogg", "music/peace_1.ogg"),
    ("audio/music/You_are_Victorious!.ogg", "music/victory.ogg"),
    ("audio/music/Dried_Tears.ogg", "music/defeat.ogg"),
    ("audio/music/Alba_Longa_Burns.ogg", "music/battle_1.ogg"),
    ("audio/music/Helen_Leaves_Sparta.ogg", "music/peace_2.ogg"),
    ("audio/music/Mediterranean_Waves.ogg", "music/peace_3.ogg"),
]


def _copy_zip(zf, src, dst_abs):
    os.makedirs(os.path.dirname(dst_abs), exist_ok=True)
    with open(dst_abs, "wb") as f:
        f.write(zf.read(src))


def dump_all_audio(zf, assets_dir):
    names = zf.namelist()
    n = 0
    for src in names:
        if not src.startswith("audio/") or not src.endswith(".ogg"):
            continue
        dst = os.path.join(assets_dir, src)
        _copy_zip(zf, src, dst)
        n += 1
    print(f"[audio] dumped {n} oggs -> assets/audio/")
    return n


def alias_rts0_audio(zf, assets_dir):
    names = set(zf.namelist())
    copied = 0
    missing = 0
    written = set()
    for src, dst_rel in SOUND_ALIASES:
        dst = os.path.join(assets_dir, dst_rel)
        if src not in names:
            if dst_rel not in written:
                print(f"[alias] MISSING zip {src} -> {dst_rel}")
                missing += 1
            continue
        _copy_zip(zf, src, dst)
        written.add(dst_rel)
        copied += 1
    # Voice folders: audio/voice/<lang>/civ/*.ogg -> sounds/voice/<lang>/
    for src in zf.namelist():
        if not src.startswith("audio/voice/") or not src.endswith(".ogg"):
            continue
        parts = src.split("/")
        if len(parts) < 4:
            continue
        lang = parts[2]
        fname = parts[-1]
        dst_rel = f"sounds/voice/{lang}/{fname}"
        if dst_rel in written:
            continue
        _copy_zip(zf, src, os.path.join(assets_dir, dst_rel))
        written.add(dst_rel)
        copied += 1
    still_missing = [d for s, d in SOUND_ALIASES if d not in written]
    print(f"[alias] wrote {copied} RTS0 sound/music paths ({len(still_missing)} dests missing)")
    for d in still_missing:
        print(f"[alias] MISSING dest {d}")
    return len(still_missing)


def dump_ui(zf, assets_dir):
    from extract_ui_assets import dump_hud_textures, dump_session_archive
    dump_hud_textures(zf, assets_dir)
    dump_session_archive(zf, assets_dir)


def main(zip_path=None, assets_dir=None):
    if zip_path is None or assets_dir is None:
        import argparse
        ap = argparse.ArgumentParser()
        ap.add_argument("--zip", default=ZIP_PATH)
        ap.add_argument("--out", default=ASSETS_DIR)
        args = ap.parse_args()
        zip_path = zip_path or args.zip
        assets_dir = assets_dir or args.out
    zip_path = zip_path
    assets_dir = os.path.abspath(assets_dir)
    if not os.path.exists(zip_path):
        sys.exit(f"public.zip not found at {zip_path}")
    print(f"[media] zip={zip_path}")
    print(f"[media] out={assets_dir}")
    with zipfile.ZipFile(zip_path) as zf:
        dump_all_audio(zf, assets_dir)
        miss = alias_rts0_audio(zf, assets_dir)
        dump_ui(zf, assets_dir)
    if miss:
        sys.exit(f"[media] {miss} alias destinations missing")
    print("[media] done")


if __name__ == "__main__":
    main()
