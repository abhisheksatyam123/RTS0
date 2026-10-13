#!/usr/bin/env python3
"""Generate RTS0's combat sound data from 0 A.D.'s templates and sound groups.

0 A.D. plays a unit's combat sounds from its template's Sound/SoundGroups:
  * attack_melee / attack_ranged / attack_slaughter: the actor's attack animation
    fires the group named after the attack type on each swing or shot (BuildingAI.js
    FireArrows plays a structure's attack_ranged itself, once per round of arrows);
    Slaughter is the attack on domestic animals. Animals (gaia/fauna_*) have their own
    attack_melee.
  * attack_impact_ranged: Attack.js PerformAttack hands it to DelayedDamage.js,
    which plays it where the projectile lands, whatever it hits.
  * death: Health.js plays it as the entity dies; every structure's is
    attack/destruction/building_collapse_large.xml (template_structure.xml).
A sound group (audio/**/*.xml) lists its Sound files under Path and picks one at
random (RandOrder) at Gain, between PitchLower and PitchUpper when RandPitch is set.

One row per civ roster template (same order as civ_roster_data.h), one per RTS0
UnitType for units without a roster template, one per BuildingType and one per
FaunaType (extract_0ad_fauna.py's order), each naming its groups by index into the
group table.

Usage:
  python3 -I tools/extract_0ad_attack_sounds.py --0ad PATH/TO/0ad --audio PATH/TO/audio [--check]

--0ad is a tree with binaries/data/mods/public/simulation; --audio is that tree's
public/audio folder (only the *.xml sound groups are read). Every Sound file must
be in assets/audio/. Writes src/presentation/attack_sound_data.h. --check fails if
the checked-in file differs.
"""

import argparse
import importlib.util
import json
import os
import sys
import xml.etree.ElementTree as ET

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_HEADER = os.path.join(REPO, "src", "presentation", "attack_sound_data.h")
ROSTERS = os.path.join(REPO, "data", "civs", "rosters.json")
ASSETS_AUDIO = os.path.join(REPO, "assets", "audio")

_spec = importlib.util.spec_from_file_location("extract_0ad_attack",
                                               os.path.join(REPO, "tools", "extract_0ad_attack.py"))
atk = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(atk)
ex = atk.ex

UNIT_GROUPS = ("attack_melee", "attack_ranged", "attack_impact_ranged", "attack_slaughter")
BUILDING_GROUPS = ("attack_ranged", "attack_impact_ranged", "death", "")


def fauna_stems(loader):
    """gaia/fauna_* in FaunaType order (extract_0ad_fauna.py fauna_rows)."""
    import glob
    sys.path.insert(0, os.path.join(REPO, "tools"))
    from extract_0ad_fauna import FIRST
    stems = sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(loader.root, "gaia", "fauna_*.xml")))
    return FIRST + [s for s in stems if s not in FIRST]


def load_group(audio_root, path):
    """audio/<path> -> (dir under assets/audio, [files], gain, pitch_lo, pitch_hi)."""
    root = ET.parse(os.path.join(audio_root, path)).getroot()

    def num(tag, default):
        e = root.find(tag)
        try:
            return float(e.text) if e is not None and e.text else default
        except ValueError:
            return default

    d = (root.findtext("Path") or "").strip().strip("/")
    if d.startswith("audio/"):
        d = d[len("audio/"):]
    files = [s.text.strip() for s in root.findall("Sound") if s.text and s.text.strip()]
    missing = [f for f in files if not os.path.isfile(os.path.join(ASSETS_AUDIO, d, f))]
    if missing:
        sys.exit("%s: %s missing from assets/audio/%s" % (path, ", ".join(missing), d))
    lo, hi = 1.0, 1.0
    if num("RandPitch", 0) > 0:
        lo, hi = num("PitchLower", 1.0), num("PitchUpper", 1.0)
    return d, files, num("Gain", 1.0), lo, hi


def sounds_of(t, names):
    return [(t.value("Sound/SoundGroups/" + n) or "") if n else "" for n in names]


def collect(sim_root):
    loader = ex.TemplateLoader(sim_root)
    rosters = json.load(open(ROSTERS))
    units = []
    for u in rosters["units"]:
        name = "units/" + u["id"]
        if not loader.exists(name.split("|")[-1]):
            name = ex.GENERIC[u["class"]]  # RTS0-only rows (the male villager)
        units.append((u["id"], sounds_of(loader.load(name), UNIT_GROUPS)))
    generic = []
    for cls in atk.UNIT_TYPES:
        generic.append((cls, ex.GENERIC[cls] or "no template", sounds_of(ex.generic_template(loader, cls), UNIT_GROUPS)))
    buildings = []
    for bld, name in atk.BUILDINGS + atk.civ_buildings():
        buildings.append((bld, name, sounds_of(loader.load(name), BUILDING_GROUPS)))
    buildings.append(("default", "template_structure", sounds_of(loader.load("template_structure"), BUILDING_GROUPS)))
    fauna = [(stem, (loader.load("gaia/" + stem).value("Sound/SoundGroups/attack_melee") or "")) for stem in fauna_stems(loader)]
    return units, generic, buildings, fauna


def f(x):
    s = ("%.3f" % x).rstrip("0")
    return (s + "0" if s.endswith(".") else s) + "f"


def emit(groups, units, generic, buildings, fauna):
    index = {p: i for i, (p, _) in enumerate(groups)}

    def ids(paths):
        return ", ".join(str(index[p]) if p else "ATTACK_SOUND_NONE" for p in paths)

    out = []
    w = out.append
    w("// Generated by tools/extract_0ad_attack_sounds.py from 0 A.D.'s templates (Sound/SoundGroups)")
    w("// and sound groups (audio/**/*.xml); do not edit. 0 A.D. data is CC BY-SA 3.0 / GPL-2.0-or-later")
    w("// (see docs/0AD_ATTRIBUTION.md). Included by session_audio.h inside namespace rtso.")
    w("#pragma once")
    w("")
    w("// A 0 A.D. sound group: Sound files under assets/audio/<dir>/, picked at random")
    w("// at Gain and a pitch between pitch_lo and pitch_hi.")
    w("struct AttackSoundGroup {")
    w("\tconst char *dir;")
    w("\tconst char *const *files; // nullptr-terminated")
    w("\tfloat gain, pitch_lo, pitch_hi;")
    w("};")
    w("")
    w("inline constexpr uint8_t ATTACK_SOUND_NONE = 255;")
    w("inline constexpr uint8_t ATTACK_SOUND_GROUP_COUNT = %d;" % len(groups))
    w("")
    w("inline const AttackSoundGroup &attack_sound_group(uint8_t g) {")
    for i, (p, (d, files, gain, lo, hi)) in enumerate(groups):
        w("\tstatic const char *const kFiles%d[] = {%s, nullptr}; // %s" % (i, ", ".join('"%s"' % x for x in files), p))
    w("\tstatic const AttackSoundGroup kGroups[ATTACK_SOUND_GROUP_COUNT] = {")
    for i, (p, (d, files, gain, lo, hi)) in enumerate(groups):
        w('\t\t{"%s", kFiles%d, %s, %s, %s},' % (d, i, f(gain), f(lo), f(hi)))
    w("\t};")
    w("\treturn kGroups[g < ATTACK_SOUND_GROUP_COUNT ? g : 0];")
    w("}")
    w("")
    w("// attack_melee, attack_ranged, attack_impact_ranged and attack_slaughter of a unit")
    w("// template, or attack_ranged, attack_impact_ranged and death of a structure.")
    w("struct EntityAttackSounds {")
    w("\tuint8_t a, b, c, d;")
    w("};")
    w("")
    w("inline constexpr uint16_t ATTACK_SOUND_ROSTER_ROWS = %d;" % len(units))
    w("inline constexpr uint8_t ATTACK_SOUND_GENERIC_ROWS = %d;" % len(generic))
    w("inline constexpr uint8_t ATTACK_SOUND_BUILDING_ROWS = %d;" % len(buildings[:-1]))
    w("inline constexpr uint8_t ATTACK_SOUND_FAUNA_ROWS = %d;" % len(fauna))
    w("")
    w("// Melee, ranged and impact groups of a civ roster row (civ_unit_template_table order).")
    w("inline const EntityAttackSounds *roster_attack_sounds() {")
    w("\tstatic const EntityAttackSounds kRows[ATTACK_SOUND_ROSTER_ROWS] = {")
    for uid, paths in units:
        w("\t\t{%s}, // %s" % (ids(paths), uid))
    w("\t};")
    w("\treturn kRows;")
    w("}")
    w("")
    w("// The same for a unit without a roster row, by UnitType.")
    w("inline const EntityAttackSounds *generic_attack_sounds() {")
    w("\tstatic const EntityAttackSounds kRows[ATTACK_SOUND_GENERIC_ROWS] = {")
    for cls, name, paths in generic:
        w("\t\t{%s}, // %s: %s" % (ids(paths), cls, name))
    w("\t};")
    w("\treturn kRows;")
    w("}")
    w("")
    w("// Ranged, impact and death groups of a BuildingType; a type past the table")
    w("// (added since the last run) takes template_structure's.")
    w("inline const EntityAttackSounds &building_attack_sounds(uint8_t type) {")
    w("\tstatic const EntityAttackSounds kRows[ATTACK_SOUND_BUILDING_ROWS + 1] = {")
    for bld, name, paths in buildings:
        w("\t\t{%s}, // %s: %s" % (ids(paths), bld, name))
    w("\t};")
    w("\treturn kRows[type < ATTACK_SOUND_BUILDING_ROWS ? type : ATTACK_SOUND_BUILDING_ROWS];")
    w("}")
    w("")
    w("// An animal's attack_melee group by FaunaType (ATTACK_SOUND_NONE past the table).")
    w("inline uint8_t fauna_attack_sound(uint8_t type) {")
    w("\tstatic const uint8_t kRows[ATTACK_SOUND_FAUNA_ROWS] = {")
    for stem, path in fauna:
        w("\t\t%s, // %s" % (ids([path]), stem))
    w("\t};")
    w("\treturn type < ATTACK_SOUND_FAUNA_ROWS ? kRows[type] : ATTACK_SOUND_NONE;")
    w("}")
    w("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zad", required=True)
    ap.add_argument("--audio", required=True)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    sim_root = os.path.join(args.zad, "binaries", "data", "mods", "public", "simulation")
    units, generic, buildings, fauna = collect(sim_root)
    paths = sorted({p for rows in (units, generic) for _, *rest in rows for p in rest[-1] if p} |
                   {p for _, _, ps in buildings for p in ps if p} | {p for _, p in fauna if p})
    groups = [(p, load_group(args.audio, p)) for p in paths]
    if len(groups) >= 255:
        sys.exit("more than 254 sound groups")
    text = emit(groups, units, generic, buildings, fauna)
    if args.check:
        old = open(OUT_HEADER).read() if os.path.exists(OUT_HEADER) else ""
        if old != text:
            sys.exit("attack_sound_data.h is out of date")
        print("attack_sound_data.h is up to date")
        return
    with open(OUT_HEADER, "w") as fh:
        fh.write(text)
    print("wrote %s (%d sound groups, %d roster rows, %d generic, %d building types, %d animals)" % (
        OUT_HEADER, len(groups), len(units), len(generic), len(buildings) - 1, len(fauna)))


if __name__ == "__main__":
    main()
