import zipfile
import xml.etree.ElementTree as ET
import json
import struct
import math
import os

zip_path = "/home/abhi/project/0ad/binaries/data/mods/public/public.zip"

def extract_pmp_heights(raw_pmp, target_res=64, height_scale=0.20):
    magic = raw_pmp[:4].decode('ascii', errors='ignore')
    if magic != "PSMP":
        return [0.0] * (target_res * target_res), 0.0
    patches = struct.unpack('<I', raw_pmp[12:16])[0]
    vert_side = patches * 16 + 1
    num_heights = vert_side * vert_side
    offset = 16
    raw_h = struct.unpack(f'<{num_heights}H', raw_pmp[offset:offset + num_heights * 2])
    
    # 0 A.D. maps raw 0..65535 to 0..512m
    # We apply height_scale (e.g. 0.20 -> 0..25m hills)
    heights = []
    for r in range(target_res):
        for c in range(target_res):
            src_r = int(r * (vert_side - 1) / (target_res - 1))
            src_c = int(c * (vert_side - 1) / (target_res - 1))
            idx = src_r * vert_side + src_c
            raw_val = raw_h[idx]
            val_m = (raw_val / 128.0) * height_scale
            heights.append(round(val_m, 2))
            
    return heights

def port_map(map_id, xml_rel, pmp_rel, world_size_m=256.0, biome="mediterranean", height_scale=0.20):
    print(f"Porting map: {map_id} ({xml_rel})...")
    with zipfile.ZipFile(zip_path, 'r') as z:
        xml_data = z.read(xml_rel).decode('utf-8')
        root = ET.fromstring(xml_data)
        
        # Script settings
        s_elem = root.find("ScriptSettings")
        settings = json.loads(s_elem.text) if s_elem is not None and s_elem.text else {}
        name = settings.get("Name", map_id.replace("_", " ").title())
        desc = settings.get("Description", "")
        
        # Terrain heights from PMP
        raw_pmp = z.read(pmp_rel)
        heightmap = extract_pmp_heights(raw_pmp, target_res=64, height_scale=height_scale)
        
        # Water settings
        water_height = -1.0
        w_elem = root.find(".//WaterBody/Height")
        if w_elem is not None:
            try:
                raw_w = float(w_elem.text)
                water_height = round(raw_w * height_scale, 2)
            except Exception:
                pass
                
        # Scan raw entity bounds
        entities_elem = root.find("Entities")
        raw_entities = []
        min_x, max_x = float('inf'), float('-inf')
        min_z, max_z = float('inf'), float('-inf')
        
        if entities_elem is not None:
            for e in entities_elem.findall("Entity"):
                tpl = e.find("Template").text if e.find("Template") is not None else ""
                ply = int(e.find("Player").text) if e.find("Player") is not None else 0
                pos = e.find("Position")
                if pos is None: continue
                x = float(pos.get("x", 0))
                z = float(pos.get("z", 0))
                min_x, max_x = min(min_x, x), max(max_x, x)
                min_z, max_z = min(min_z, z), max(max_z, z)
                raw_entities.append((tpl, ply, x, z))
                
        center_x = (min_x + max_x) / 2.0
        center_z = (min_z + max_z) / 2.0
        raw_span = max(max_x - min_x, max_z - min_z, 1.0)
        scale = (world_size_m * 0.90) / raw_span
        
        # Convert entities into RTS0 format
        resources = []
        players = {}
        structures = []
        units = []
        
        for tpl, ply, rx, rz in raw_entities:
            # Map coordinates from 0AD space to centered RTS0 space [-world_size_m/2, world_size_m/2]
            wx = round((rx - center_x) * scale, 2)
            wz = round((rz - center_z) * scale, 2)
            
            ltpl = tpl.lower()
            if ply == 0:
                # Gaia resource / nature
                if "tree" in ltpl or "palm" in ltpl or "cedar" in ltpl or "cypress" in ltpl or "oak" in ltpl:
                    res_type = "palm" if ("palm" in ltpl or "date" in ltpl) else ("pine" if ("pine" in ltpl or "cedar" in ltpl or "cypress" in ltpl) else "tree")
                    resources.append({"type": res_type, "x": wx, "z": wz, "amount": 250})
                elif "stone" in ltpl or "rock" in ltpl:
                    resources.append({"type": "stone", "x": wx, "z": wz, "amount": 1000})
                elif "metal" in ltpl or "gold" in ltpl or "silver" in ltpl or "iron" in ltpl:
                    resources.append({"type": "gold", "x": wx, "z": wz, "amount": 1000})
            else:
                # Player base / units
                if ply not in players:
                    players[ply] = {"id": ply, "team": ply - 1, "start_x": wx, "start_z": wz}
                
                if "civil_centre" in ltpl or "town_center" in ltpl or "civic" in ltpl:
                    players[ply]["start_x"] = wx
                    players[ply]["start_z"] = wz
                    structures.append({"type": "town_center", "player": ply, "x": wx, "z": wz})
                elif "barracks" in ltpl or "military" in ltpl:
                    structures.append({"type": "barracks", "player": ply, "x": wx, "z": wz})
                elif "house" in ltpl:
                    structures.append({"type": "house", "player": ply, "x": wx, "z": wz})
                elif "storehouse" in ltpl or "mill" in ltpl or "farmstead" in ltpl:
                    structures.append({"type": "storehouse", "player": ply, "x": wx, "z": wz})
                elif "tower" in ltpl or "outpost" in ltpl or "wall" in ltpl:
                    structures.append({"type": "tower", "player": ply, "x": wx, "z": wz})
                elif "infantry_melee" in ltpl or "swordsman" in ltpl:
                    units.append({"type": "swordsman", "player": ply, "x": wx, "z": wz})
                elif "spearman" in ltpl or "phalanx" in ltpl:
                    units.append({"type": "spearman", "player": ply, "x": wx, "z": wz})
                elif "ranged" in ltpl or "archer" in ltpl or "slinger" in ltpl or "skirmisher" in ltpl:
                    units.append({"type": "archer", "player": ply, "x": wx, "z": wz})
                elif "cavalry" in ltpl or "horse" in ltpl:
                    units.append({"type": "cavalry", "player": ply, "x": wx, "z": wz})
                elif "villager" in ltpl or "female" in ltpl or "citizen" in ltpl:
                    units.append({"type": "villager", "player": ply, "x": wx, "z": wz})
                    
        map_data = {
            "id": map_id,
            "name": name,
            "desc": desc,
            "biome": biome,
            "world_size": world_size_m,
            "water_height": water_height,
            "heightmap_res": 64,
            "heightmap": heightmap,
            "players": list(players.values()),
            "resources": resources,
            "structures": structures,
            "units": units
        }
        
        out_path = f"data/maps/{map_id}.json"
        with open(out_path, "w") as f:
            json.dump(map_data, f, indent=2)
        print(f"  -> Generated {out_path}: {len(resources)} resources, {len(structures)} structures, {len(units)} units, {len(players)} players (Water={water_height}m).")

maps_list = [
    ("acropolis_bay", "maps/skirmishes/acropolis_bay_2p.xml", "maps/skirmishes/acropolis_bay_2p.pmp", 256.0, "mediterranean", 0.20),
    ("corinthian_isthmus", "maps/skirmishes/corinthian_isthmus_4p.xml", "maps/skirmishes/corinthian_isthmus_4p.pmp", 384.0, "mediterranean", 0.20),
    ("atlas_valleys", "maps/skirmishes/atlas_valleys_8p.xml", "maps/skirmishes/atlas_valleys_8p.pmp", 384.0, "savanna", 0.20),
    ("bactria", "maps/skirmishes/bactria_2p.xml", "maps/skirmishes/bactria_2p.pmp", 256.0, "desert", 0.15),
    ("battle_for_the_tiber", "maps/scenarios/battle_for_the_tiber.xml", "maps/scenarios/battle_for_the_tiber.pmp", 320.0, "temperate", 0.20)
]

for mid, xrel, prel, size, bome, hscale in maps_list:
    port_map(mid, xrel, prel, size, bome, hscale)

