import zipfile, os

ZIP_PATH = "/home/abhi/project/0ad/binaries/data/mods/public/public.zip"
OUT_BASE = "/home/abhi/project/RTS0/assets/sounds"

sfx_map = {
    "audio/attack/weapon/swordhit_10.ogg": "combat/sword_hit_1.ogg",
    "audio/attack/weapon/swordhit_12.ogg": "combat/sword_hit_2.ogg",
    "audio/attack/weapon/swordhit_14.ogg": "combat/sword_hit_3.ogg",
    "audio/attack/weapon/bow_attack_01.ogg": "combat/bow_shoot_1.ogg",
    "audio/attack/weapon/bow_attack_03.ogg": "combat/bow_shoot_2.ogg",
    "audio/attack/weapon/arrowfly_01.ogg": "combat/arrow_fly.ogg",
    "audio/attack/impact/arrow_wood_01.ogg": "combat/arrow_hit.ogg",
    "audio/interface/complete/building/complete_civ_center.ogg": "building/complete_tc.ogg",
    "audio/interface/complete/building/complete_barracks.ogg": "building/complete_barracks.ogg",
    "audio/interface/complete/building/complete_house.ogg": "building/complete_house.ogg",
    "audio/interface/complete/building/complete_tower.ogg": "building/complete_tower.ogg",
    "audio/interface/alarm/alarmattackplayer_1.ogg": "alarm_under_attack.ogg",
    "audio/interface/alarm/alarmresearchtech_1.ogg": "alarm_research_done.ogg",
    "audio/interface/alarm/alarmvictory_1.ogg": "alarm_victory.ogg",
    "audio/interface/alarm/alarmdefeat_1.ogg": "alarm_defeated.ogg",
}

with zipfile.ZipFile(ZIP_PATH, 'r') as z:
    for src, dst in sfx_map.items():
        if src in z.namelist():
            dst_path = os.path.join(OUT_BASE, dst)
            os.makedirs(os.path.dirname(dst_path), exist_ok=True)
            with open(dst_path, "wb") as f:
                f.write(z.read(src))
            print(f"Extracted {src} -> {dst}")
        else:
            print(f"Warning: {src} not found in zip")
print("Done extracting combat & building SFX!")
