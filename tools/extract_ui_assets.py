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

_CANDIDATE_ZIPS = [
    os.path.join(os.path.dirname(__file__), "..", "..", "0ad",
                 "binaries", "data", "mods", "public", "public.zip"),
    "/home/abhi/project/0ad/binaries/data/mods/public/public.zip",
]
ZIP_PATH = next((p for p in _CANDIDATE_ZIPS if os.path.exists(p)), _CANDIDATE_ZIPS[0])
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
    # Formation buttons 7-12 (main_node.cpp K2: LineOpen/ColOpen/Flank/BattleLine)
    "art/textures/ui/session/icons/formations/line_open.png.cached.dds": "textures/ui/formations/line_open.png",
    "art/textures/ui/session/icons/formations/column_open.png.cached.dds": "textures/ui/formations/column_open.png",
    "art/textures/ui/session/icons/formations/flank.png.cached.dds": "textures/ui/formations/flank.png",
    "art/textures/ui/session/icons/formations/battle_line.png.cached.dds": "textures/ui/formations/battle_line.png",

    # Phase emblems (session HUD)
    "art/textures/ui/session/portraits/technologies/village_phase.png.cached.dds": "textures/ui/phases/village.png",
    "art/textures/ui/session/portraits/technologies/town_phase.png.cached.dds": "textures/ui/phases/town.png",
    "art/textures/ui/session/portraits/technologies/city_phase.png.cached.dds": "textures/ui/phases/city.png",

    # Session command icons used by HUD / selection
    "art/textures/ui/session/icons/construction.png.cached.dds": "textures/ui/icons/construction.png",
    "art/textures/ui/session/icons/repair.png.cached.dds": "textures/ui/icons/repair.png",
    "art/textures/ui/session/icons/stop.png.cached.dds": "textures/ui/icons/stop.png",
    "art/textures/ui/session/icons/garrison.png.cached.dds": "textures/ui/icons/garrison.png",
    "art/textures/ui/session/icons/patrol.png.cached.dds": "textures/ui/icons/patrol.png",
    "art/textures/ui/session/icons/heal.png.cached.dds": "textures/ui/icons/heal.png",
    "art/textures/ui/session/icons/health_fg.png.cached.dds": "textures/ui/icons/health_fg.png",
    "art/textures/ui/session/icons/health_bg.png.cached.dds": "textures/ui/icons/health_bg.png",

    # Inspector stat bar icons (0 A.D. session icon parity)
    "art/textures/ui/session/icons/capture_bar.png.cached.dds": "textures/ui/icons/insp_capture_bar.png",
    "art/textures/ui/session/icons/experience_fg.png.cached.dds": "textures/ui/icons/insp_experience_fg.png",
    "art/textures/ui/session/icons/health_bg.png.cached.dds": "textures/ui/icons/insp_health_bg.png",
    "art/textures/ui/session/icons/health_fg.png.cached.dds": "textures/ui/icons/insp_health_fg.png",
    "art/textures/ui/session/icons/stamina_fg.png.cached.dds": "textures/ui/icons/insp_stamina_fg.png",

    # Emblems (all playable civs)
    "art/textures/ui/session/portraits/emblems/emblem_athenians.png.cached.dds": "textures/ui/emblems/athenians.png",
    "art/textures/ui/session/portraits/emblems/emblem_spartans.png.cached.dds": "textures/ui/emblems/spartans.png",
    "art/textures/ui/session/portraits/emblems/emblem_persians.png.cached.dds": "textures/ui/emblems/persians.png",
    "art/textures/ui/session/portraits/emblems/emblem_romans.png.cached.dds": "textures/ui/emblems/romans.png",
    "art/textures/ui/session/portraits/emblems/emblem_carthaginians.png.cached.dds": "textures/ui/emblems/carthaginians.png",
    "art/textures/ui/session/portraits/emblems/emblem_macedonians.png.cached.dds": "textures/ui/emblems/macedonians.png",
    "art/textures/ui/session/portraits/emblems/emblem_britons.png.cached.dds": "textures/ui/emblems/britons.png",
    "art/textures/ui/session/portraits/emblems/emblem_celts.png.cached.dds": "textures/ui/emblems/gauls.png",
    "art/textures/ui/session/portraits/emblems/emblem_iberians.png.cached.dds": "textures/ui/emblems/iberians.png",
    "art/textures/ui/session/portraits/emblems/emblem_mauryas.png.cached.dds": "textures/ui/emblems/mauryans.png",
    "art/textures/ui/session/portraits/emblems/emblem_kushites.png.cached.dds": "textures/ui/emblems/kushites.png",
    "art/textures/ui/session/portraits/emblems/emblem_ptolemies.png.cached.dds": "textures/ui/emblems/ptolemies.png",
    "art/textures/ui/session/portraits/emblems/emblem_seleucids.png.cached.dds": "textures/ui/emblems/seleucids.png",

    # Unit portraits — 0 A.D. session art (generic Hellenic + unique civ units)
    "art/textures/ui/session/portraits/units/athen/support_civilian.png.cached.dds": "textures/ui/portraits/villager.png",
    "art/textures/ui/session/portraits/units/hele/support_female_citizen.png.cached.dds": "textures/ui/portraits/female_citizen.png",
    "art/textures/ui/session/portraits/units/hele/support_healer.png.cached.dds": "textures/ui/portraits/healer.png",
    "art/textures/ui/session/portraits/units/hele/support_trader.png.cached.dds": "textures/ui/portraits/trader.png",
    "art/textures/ui/session/portraits/units/athen/champion_infantry.png.cached.dds": "textures/ui/portraits/swordsman.png",
    "art/textures/ui/session/portraits/units/hele/infantry_archer.png.cached.dds": "textures/ui/portraits/archer.png",
    "art/textures/ui/session/portraits/units/athen/infantry_spearman.png.cached.dds": "textures/ui/portraits/spearman.png",
    "art/textures/ui/session/portraits/units/athen/cavalry_swordsman.png.cached.dds": "textures/ui/portraits/cavalry.png",
    "art/textures/ui/session/portraits/units/athen/hero_pericles.png.cached.dds": "textures/ui/portraits/hero.png",
    "art/textures/ui/session/portraits/units/hele/ship_ram.png.cached.dds": "textures/ui/portraits/trireme.png",
    "art/textures/ui/session/portraits/units/hele/ship_fishing.png.cached.dds": "textures/ui/portraits/fishing_boat.png",
    "art/textures/ui/session/portraits/units/hele/ship_merchant.png.cached.dds": "textures/ui/portraits/merchant_ship.png",
    "art/textures/ui/session/portraits/units/athen/siege_ram.png.cached.dds": "textures/ui/portraits/ram.png",
    "art/textures/ui/session/portraits/units/hele/siege_lithobolos.png.cached.dds": "textures/ui/portraits/catapult.png",
    "art/textures/ui/session/portraits/units/hele/siege_oxybeles.png.cached.dds": "textures/ui/portraits/bolt_shooter.png",
    "art/textures/ui/session/portraits/units/cart/champion_elephant.png.cached.dds": "textures/ui/portraits/elephant.png",
    "art/textures/ui/session/portraits/units/brit/champion_chariot.png.cached.dds": "textures/ui/portraits/chariot.png",
    "art/textures/ui/session/portraits/units/brit/war_dog.png.cached.dds": "textures/ui/portraits/war_dog.png",
    "art/textures/ui/session/portraits/units/gaul/champion_swordsman_carnyx.png.cached.dds": "textures/ui/portraits/carnyx.png",
    "art/textures/ui/session/portraits/units/maur/support_elephant.png.cached.dds": "textures/ui/portraits/worker_elephant.png",
    "art/textures/ui/session/portraits/units/maur/elephant_archer.png.cached.dds": "textures/ui/portraits/maur_elephant.png",
    "art/textures/ui/session/portraits/units/maur/champion_maiden_archer.png.cached.dds": "textures/ui/portraits/archer_maiden.png",
    "art/textures/ui/session/portraits/units/kush/camel_swordsman.png.cached.dds": "textures/ui/portraits/camel.png",
    "art/textures/ui/session/portraits/units/sele/champion_chariot.png.cached.dds": "textures/ui/portraits/scythed_chariot.png",

    # Structure portraits
    "art/textures/ui/session/portraits/structures/civic_centre.png.cached.dds": "textures/ui/portraits/town_center.png",
    "art/textures/ui/session/portraits/structures/house.png.cached.dds": "textures/ui/portraits/house.png",
    "art/textures/ui/session/portraits/structures/barracks.png.cached.dds": "textures/ui/portraits/barracks.png",
    "art/textures/ui/session/portraits/structures/storehouse.png.cached.dds": "textures/ui/portraits/storehouse.png",
    "art/textures/ui/session/portraits/structures/defense_tower.png.cached.dds": "textures/ui/portraits/tower.png",
    "art/textures/ui/session/portraits/structures/farmstead.png.cached.dds": "textures/ui/portraits/farmstead.png",
    "art/textures/ui/session/portraits/structures/market.png.cached.dds": "textures/ui/portraits/market.png",
    "art/textures/ui/session/portraits/structures/fortress.png.cached.dds": "textures/ui/portraits/fortress.png",
    "art/textures/ui/session/portraits/structures/gate.png.cached.dds": "textures/ui/portraits/gate.png",
    "art/textures/ui/session/portraits/structures/temple.png.cached.dds": "textures/ui/portraits/temple.png",
    "art/textures/ui/session/portraits/structures/wonder.png.cached.dds": "textures/ui/portraits/wonder.png",
    "art/textures/ui/session/portraits/structures/dock.png.cached.dds": "textures/ui/portraits/dock.png",
    "art/textures/ui/session/portraits/structures/wall.png.cached.dds": "textures/ui/portraits/wall.png",
    "art/textures/ui/session/portraits/structures/corral.png.cached.dds": "textures/ui/portraits/corral.png",
}

def dump_hud_textures(zf, assets_dir):
    names = set(zf.namelist())
    for zip_entry, out_rel in TEXTURE_MAPPING.items():
        out_path = os.path.join(assets_dir, out_rel)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        if zip_entry not in names:
            print(f"  [WARN] Entry not in zip: {zip_entry}")
            continue
        try:
            img = decode_dds(zf.read(zip_entry))
            img.save(out_path, format="PNG")
            print(f"  [OK] Texture: {out_rel} ({img.width}x{img.height})")
        except Exception as e:
            print(f"  [ERR] Texture decode failed {zip_entry}: {e}")

    # Inspector icon convenience aliases
    insp_aliases = [
        ("art/textures/ui/session/icons/health_fg.png.cached.dds", "textures/ui/icons/insp_health.png"),
        ("art/textures/ui/session/icons/capture_bar.png.cached.dds", "textures/ui/icons/insp_capture.png"),
        ("art/textures/ui/session/icons/experience_fg.png.cached.dds", "textures/ui/icons/insp_experience.png"),
        ("art/textures/ui/session/icons/experience_fg.png.cached.dds", "textures/ui/icons/insp_xp.png"),
        ("art/textures/ui/session/icons/stamina_fg.png.cached.dds", "textures/ui/icons/insp_stamina.png"),
    ]
    for zip_entry, out_rel in insp_aliases:
        if zip_entry not in names:
            continue
        try:
            img = decode_dds(zf.read(zip_entry))
            out_path = os.path.join(assets_dir, out_rel)
            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            img.save(out_path, format="PNG")
            print(f"  [OK] Texture: {out_rel} ({img.width}x{img.height})")
        except Exception as e:
            print(f"  [ERR] Texture decode failed {zip_entry}: {e}")

    # Ship formation glyphs: 0 A.D. ships no dedicated ship-formation icons
    # (public.zip has only land formations) -> reuse line/column closed.
    # Drives main_node.cpp ship_line/ship_column buttons.
    ship_formations = {
        "art/textures/ui/session/icons/formations/line_closed.png.cached.dds":
            "textures/ui/formations/ship_line.png",
        "art/textures/ui/session/icons/formations/column_closed.png.cached.dds":
            "textures/ui/formations/ship_column.png",
    }
    for zip_entry, out_rel in ship_formations.items():
        if zip_entry not in names:
            print(f"  [WARN] Entry not in zip: {zip_entry}")
            continue
        try:
            img = decode_dds(zf.read(zip_entry))
            out_path = os.path.join(assets_dir, out_rel)
            os.makedirs(os.path.dirname(out_path), exist_ok=True)
            img.save(out_path, format="PNG")
            print(f"  [OK] Texture: {out_rel} ({img.width}x{img.height})")
        except Exception as e:
            print(f"  [ERR] Texture decode failed {zip_entry}: {e}")


def dump_session_archive(zf, assets_dir):
    """Remaining art/textures/ui/session/**/*.cached.dds -> textures/ui/archive/."""
    prefix = "art/textures/ui/session/"
    mapped = set(TEXTURE_MAPPING)
    ok = skip = 0
    for name in zf.namelist():
        if not name.startswith(prefix) or not name.endswith(".cached.dds"):
            continue
        if name in mapped:
            continue
        rel = name[len(prefix):-len(".cached.dds")]
        stem, ext = os.path.splitext(rel)
        out_rel = "textures/ui/archive/" + stem + ".png"
        out_path = os.path.join(assets_dir, out_rel)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        try:
            img = decode_dds(zf.read(name))
            img.save(out_path, format="PNG")
            ok += 1
        except Exception as e:
            skip += 1
            print(f"  [ERR] archive texture {name}: {e}")
    print(f"  [OK] session archive {ok} pngs ({skip} failed)")


def main():
    if not os.path.exists(ZIP_PATH):
        print(f"Error: public.zip not found at {ZIP_PATH}")
        sys.exit(1)

    print(f"[EXTRACT] Reading from {ZIP_PATH}...")
    with zipfile.ZipFile(ZIP_PATH, 'r') as z:
        dump_hud_textures(z, ASSETS_DIR)
        dump_session_archive(z, ASSETS_DIR)

    print("[EXTRACT] Done extracting UI textures (audio is tools/extract_media.py)")

if __name__ == "__main__":
    main()
