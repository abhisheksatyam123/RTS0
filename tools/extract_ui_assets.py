#!/usr/bin/env python3
"""
Extract 0 A.D. UI textures, icons, emblems, portraits, and audio files from public.zip
into RTS0's assets folder.
"""
import os
import sys
import zipfile
import io

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow required: pip install pillow")

ZIP_PATH = os.path.join(os.path.dirname(__file__), "..", "..", "0ad",
                        "binaries", "data", "mods", "public", "public.zip")
ASSETS_DIR = os.path.join(os.path.dirname(__file__), "..", "assets")

def decode_dxt1(data, width, height):
    img = Image.new("RGBA", (width, height))
    pixels = img.load()
    offset = 0
    for y in range(0, height, 4):
        for x in range(0, width, 4):
            if offset + 8 > len(data):
                break
            c0, c1 = struct.unpack_from("<HH", data, offset)
            r0 = ((c0 >> 11) & 0x1F) * 255 // 31
            g0 = ((c0 >> 5) & 0x3F) * 255 // 63
            b0 = (c0 & 0x1F) * 255 // 31
            r1 = ((c1 >> 11) & 0x1F) * 255 // 31
            g1 = ((c1 >> 5) & 0x3F) * 255 // 63
            b1 = (c1 & 0x1F) * 255 // 31

            if c0 > c1:
                palette = [
                    (r0, g0, b0, 255),
                    (r1, g1, b1, 255),
                    ((2*r0 + r1)//3, (2*g0 + g1)//3, (2*b0 + b1)//3, 255),
                    ((r0 + 2*r1)//3, (g0 + 2*g1)//3, (b0 + 2*b1)//3, 255),
                ]
            else:
                palette = [
                    (r0, g0, b0, 255),
                    (r1, g1, b1, 255),
                    ((r0 + r1)//2, (g0 + g1)//2, (b0 + b1)//2, 255),
                    (0, 0, 0, 0),
                ]
            idx_bytes = struct.unpack_from("<I", data, offset + 4)[0]
            offset += 8
            for py in range(4):
                for px in range(4):
                    if x + px < width and y + py < height:
                        c_idx = (idx_bytes >> (2 * (py * 4 + px))) & 0x03
                        pixels[x + px, y + py] = palette[c_idx]
    return img

import struct

def decode_dds(data):
    if data[:4] != b"DDS ":
        # Try PIL directly (might be raw PNG)
        return Image.open(io.BytesIO(data)).convert("RGBA")
    height, width = struct.unpack_from("<II", data, 12)
    fourcc = data[84:88]
    body = data[128:]
    if fourcc == b"DXT1":
        return decode_dxt1(body, width, height)
    elif fourcc == b"DXT5":
        # Decode DXT5
        img = Image.new("RGBA", (width, height))
        pixels = img.load()
        offset = 0
        for y in range(0, height, 4):
            for x in range(0, width, 4):
                if offset + 16 > len(body):
                    break
                a0, a1 = struct.unpack_from("<BB", body, offset)
                a_bits = int.from_bytes(body[offset+2:offset+8], "little")
                c0, c1 = struct.unpack_from("<HH", body, offset + 8)
                r0 = ((c0 >> 11) & 0x1F) * 255 // 31
                g0 = ((c0 >> 5) & 0x3F) * 255 // 63
                b0 = (c0 & 0x1F) * 255 // 31
                r1 = ((c1 >> 11) & 0x1F) * 255 // 31
                g1 = ((c1 >> 5) & 0x3F) * 255 // 63
                b1 = (c1 & 0x1F) * 255 // 31
                c_palette = [
                    (r0, g0, b0),
                    (r1, g1, b1),
                    ((2*r0 + r1)//3, (2*g0 + g1)//3, (2*b0 + b1)//3),
                    ((r0 + 2*r1)//3, (g0 + 2*g1)//3, (b0 + 2*b1)//3),
                ]
                if a0 > a1:
                    a_table = [a0, a1,
                               (6*a0 + 1*a1)//7, (5*a0 + 2*a1)//7,
                               (4*a0 + 3*a1)//7, (3*a0 + 4*a1)//7,
                               (2*a0 + 5*a1)//7, (1*a0 + 6*a1)//7]
                else:
                    a_table = [a0, a1,
                               (4*a0 + 1*a1)//5, (3*a0 + 2*a1)//5,
                               (2*a0 + 3*a1)//5, (1*a0 + 4*a1)//5,
                               0, 255]
                idx_bytes = struct.unpack_from("<I", body, offset + 12)[0]
                offset += 16
                for py in range(4):
                    for px in range(4):
                        if x + px < width and y + py < height:
                            c_idx = (idx_bytes >> (2 * (py * 4 + px))) & 0x03
                            a_idx = (a_bits >> (3 * (py * 4 + px))) & 0x07
                            cr, cg, cb = c_palette[c_idx]
                            ca = a_table[a_idx]
                            pixels[x + px, y + py] = (cr, cg, cb, ca)
        return img
    else:
        # Fallback raw RGBA
        return Image.frombytes("RGBA", (width, height), body)

TEXTURE_MAPPING = {
    # Resources
    "art/textures/ui/session/icons/resources/food.png.cached.dds": "textures/ui/icons/food.png",
    "art/textures/ui/session/icons/resources/wood.png.cached.dds": "textures/ui/icons/wood.png",
    "art/textures/ui/session/icons/resources/stone.png.cached.dds": "textures/ui/icons/stone.png",
    "art/textures/ui/session/icons/resources/metal.png.cached.dds": "textures/ui/icons/metal.png",
    "art/textures/ui/session/icons/resources/population.png.cached.dds": "textures/ui/icons/population.png",
    "art/textures/ui/session/minimap-idle.png.cached.dds": "textures/ui/icons/idle.png",

    # Stances
    "art/textures/ui/session/icons/stances/aggressive.png.cached.dds": "textures/ui/stances/aggressive.png",
    "art/textures/ui/session/icons/stances/defensive.png.cached.dds": "textures/ui/stances/defensive.png",
    "art/textures/ui/session/icons/stances/standground.png.cached.dds": "textures/ui/stances/standground.png",
    "art/textures/ui/session/icons/stances/violent.png.cached.dds": "textures/ui/stances/violent.png",
    "art/textures/ui/session/icons/stances/passive.png.cached.dds": "textures/ui/stances/passive.png",

    # Formations
    "art/textures/ui/session/icons/formations/battle_line.png.cached.dds": "textures/ui/formations/line.png",
    "art/textures/ui/session/icons/formations/box.png.cached.dds": "textures/ui/formations/box.png",
    "art/textures/ui/session/icons/formations/column_closed.png.cached.dds": "textures/ui/formations/column.png",
    "art/textures/ui/session/icons/formations/phalanx.png.cached.dds": "textures/ui/formations/phalanx.png",
    "art/textures/ui/session/icons/formations/wedge.png.cached.dds": "textures/ui/formations/wedge.png",
    "art/textures/ui/session/icons/formations/scatter.png.cached.dds": "textures/ui/formations/scatter.png",

    # Emblems
    "art/textures/ui/session/portraits/emblems/emblem_athenians.png.cached.dds": "textures/ui/emblems/athenians.png",
    "art/textures/ui/session/portraits/emblems/emblem_spartans.png.cached.dds": "textures/ui/emblems/spartans.png",

    # Unit Portraits
    "art/textures/ui/session/portraits/units/athen/support_civilian.png.cached.dds": "textures/ui/portraits/villager.png",
    "art/textures/ui/session/portraits/units/athen/champion_infantry.png.cached.dds": "textures/ui/portraits/swordsman.png",
    "art/textures/ui/session/portraits/units/athen/champion_ranged.png.cached.dds": "textures/ui/portraits/archer.png",
    "art/textures/ui/session/portraits/units/athen/infantry_spearman.png.cached.dds": "textures/ui/portraits/spearman.png",
    "art/textures/ui/session/portraits/units/athen/cavalry_swordsman.png.cached.dds": "textures/ui/portraits/cavalry.png",

    # Structure Portraits
    "art/textures/ui/session/portraits/structures/civic_centre.png.cached.dds": "textures/ui/portraits/town_center.png",
    "art/textures/ui/session/portraits/structures/house.png.cached.dds": "textures/ui/portraits/house.png",
    "art/textures/ui/session/portraits/structures/barracks.png.cached.dds": "textures/ui/portraits/barracks.png",
    "art/textures/ui/session/portraits/structures/defense_tower.png.cached.dds": "textures/ui/portraits/tower.png",
    "art/textures/ui/session/portraits/structures/farmstead.png.cached.dds": "textures/ui/portraits/farmstead.png",
    "art/textures/ui/session/portraits/structures/market.png.cached.dds": "textures/ui/portraits/market.png",
    "art/textures/ui/session/portraits/structures/fortress.png.cached.dds": "textures/ui/portraits/fortress.png",
    "art/textures/ui/session/portraits/structures/gate.png.cached.dds": "textures/ui/portraits/gate.png",
}

AUDIO_MAPPING = {
    # Voice Lines (Greek civ female & male)
    "audio/voice/greek/civ/civ_female_what_is_it_1.ogg": "sounds/voice/select_1.ogg",
    "audio/voice/greek/civ/civ_female_what_is_it_2.ogg": "sounds/voice/select_2.ogg",
    "audio/voice/greek/civ/civ_female_my_lord_1.ogg": "sounds/voice/select_3.ogg",
    "audio/voice/greek/civ/civ_female_walk_1.ogg": "sounds/voice/move_1.ogg",
    "audio/voice/greek/civ/civ_female_march_1.ogg": "sounds/voice/move_2.ogg",
    "audio/voice/greek/civ/civ_female_attack_1.ogg": "sounds/voice/attack_1.ogg",
    "audio/voice/greek/civ/civ_male_attack_10.ogg": "sounds/voice/attack_2.ogg",
    "audio/voice/greek/civ/civ_female_gather_together_1.ogg": "sounds/voice/gather_1.ogg",
    "audio/voice/greek/civ/civ_female_build_1.ogg": "sounds/voice/build_1.ogg",
    "audio/voice/greek/civ/civ_female_repair_1.ogg": "sounds/voice/repair_1.ogg",

    # Interface & Alarms
    "audio/interface/alarm/alarm_attacked_gaia_01.ogg": "sounds/alarm_under_attack.ogg",
    "audio/interface/alarm/alarm_defeated_ally.ogg": "sounds/alarm_defeated.ogg",
}

def main():
    if not os.path.exists(ZIP_PATH):
        print(f"Error: public.zip not found at {ZIP_PATH}")
        sys.exit(1)

    print(f"[EXTRACT] Reading from {ZIP_PATH}...")
    with zipfile.ZipFile(ZIP_PATH, 'r') as z:
        # 1. Extract Textures
        for zip_entry, out_rel in TEXTURE_MAPPING.items():
            out_path = os.path.join(ASSETS_DIR, out_rel)
            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            if zip_entry in z.namelist():
                raw = z.read(zip_entry)
                try:
                    img = decode_dds(raw)
                    img.save(out_path, format="PNG")
                    print(f"  [OK] Texture: {out_rel} ({img.width}x{img.height})")
                except Exception as e:
                    print(f"  [ERR] Texture decode failed {zip_entry}: {e}")
            else:
                print(f"  [WARN] Entry not in zip: {zip_entry}")

        # 2. Extract Audio
        for zip_entry, out_rel in AUDIO_MAPPING.items():
            out_path = os.path.join(ASSETS_DIR, out_rel)
            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            if zip_entry in z.namelist():
                raw = z.read(zip_entry)
                with open(out_path, "wb") as f:
                    f.write(raw)
                print(f"  [OK] Audio: {out_rel} ({len(raw)} bytes)")
            else:
                print(f"  [WARN] Entry not in zip: {zip_entry}")

    print("[EXTRACT] Done extracting UI & Audio assets!")

if __name__ == "__main__":
    main()
