#!/usr/bin/env python3
"""Audit the size every entity is drawn at.

ModelLibrary::target_height() picks the height an entity is drawn at, and
ModelLibrary::load() turns it into a scale factor over the model's own size.
Two things go wrong there, and this script finds both:

1. The rules match on a substring, so the rule that names the actor's kind
   has to come before the rule that only shares a word with it.  A rule
   placed too low sizes the entity by the wrong kind entirely: the cargo
   ship ("athen_merchant_ship") was drawn at the trader's 1.8 m, half its
   size, because the trader rule sits above the ship rule.

2. A model drawn far from its own size looks wrong even when the rule that
   matched was the right one: the extracted model is a partial actor, or the
   target is calibrated for a different class.

The rules are read out of src/graphics/assets/model_library.cpp, so their
order and their heights are the ones the game uses; this script only mirrors
how each condition matches an id.  A condition it does not know about is an
error, so a new rule cannot slip past unnoticed.

An entity is a "class model" when the civ's unit table names it (it is
drawn at target_height) and a "template model" when unit_template_models.h
gives it a class model (it is drawn at the class model's scale factor, and
target_height does not apply to it at all).

Usage:  python3 tools/audit_entity_scale.py
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIM = ROOT / "src" / "simulation" / "sim_types.h"
TPL = ROOT / "src" / "graphics" / "assets" / "unit_template_models.h"
LIB = ROOT / "src" / "graphics" / "assets" / "model_library.cpp"
MANIFEST = ROOT / "assets" / "models" / "manifest.json"

# A model drawn this far from its own size looks wrong: the extracted actor
# is a partial one, or the target is calibrated for another class.
SCALE_LOW, SCALE_HIGH = 0.40, 2.60

# Entities known to be out of their class, and why. Each needs the 0 A.D. art
# to re-extract, so they are listed rather than silently re-calibrated.
KNOWN_OUTLIERS = {
    # The German fishing boat's actor (boat, oars, basket and fisherman) came
    # out 0.70 m tall against the other fishing boats' 1.2 m, so the class's
    # 2.0 m target blows it up 2.9x. Re-extract the actor to fix it.
    "germ_fishing_boat": "actor extracted 0.70 m tall against the class's 1.2 m",
}


def sub(cid, text):
    return text in cid


def is_(cid, base, text):
    return base == text


# How each condition of ModelLibrary::target_height matches an id, and how
# specific that condition is (a bigger number wins when several match).
# Keyed by the condition's exact text in the C++ source.
CONDITIONS = {
    'id.contains("champion_infantry")':
        (lambda c, b: sub(c, "champion_infantry"), 40),
    'base == "infantry" || id.contains("infantry_")':
        (lambda c, b: is_(c, b, "infantry") or sub(c, "infantry_"), 10),
    'id.contains("support_female_citizen") || id.contains("female_citizen") || id.contains("female")':
        (lambda c, b: sub(c, "female"), 50),
    'id.contains("archer_maiden")':
        (lambda c, b: sub(c, "archer_maiden"), 55),
    'id.contains("priestess") || id.contains("priest") || id.contains("healer")':
        (lambda c, b: sub(c, "priest") or sub(c, "healer"), 60),
    'id.contains("hero_cav") || id.contains("_cav_")':
        (lambda c, b: sub(c, "hero_cav") or sub(c, "_cav_"), 65),
    'id.contains("champion_cavalry")':
        (lambda c, b: sub(c, "champion_cavalry"), 64),
    'base == "cavalry" || id.contains("cavalry")':
        (lambda c, b: is_(c, b, "cavalry") or sub(c, "cavalry"), 20),
    'base == "camel" || id.contains("camel")':
        (lambda c, b: is_(c, b, "camel") or sub(c, "camel"), 20),
    'id.contains("hero_elephant")':
        (lambda c, b: sub(c, "hero_elephant"), 70),
    'id.contains("worker_elephant") || id.contains("elephant")':
        (lambda c, b: sub(c, "elephant"), 30),
    'id.contains("hero_") || id.contains("_hero")':
        (lambda c, b: sub(c, "hero_") or sub(c, "_hero"), 25),
    'id.contains("fishing_boat") || id.contains("fishing_ship")':
        (lambda c, b: sub(c, "fishing_boat") or sub(c, "fishing_ship"), 35),
    'id.contains("merchant_ship") || id.contains("trireme") || id.contains("ship_merchant") || id.contains("warship") || id.contains("bireme") || id.contains("ship")':
        (lambda c, b: "ship" in c or "trireme" in c or "bireme" in c, 15),
    'id.contains("trader") || id.contains("merchant")':
        (lambda c, b: sub(c, "trader") or sub(c, "merchant"), 5),
    'id.contains("scythed_chariot") || id.contains("chariot")':
        (lambda c, b: sub(c, "chariot"), 15),
    '!id.contains("ship") && (base == "ram" || base == "siege_ram" || id.ends_with("_ram"))':
        (lambda c, b: "ship" not in c and (is_(c, b, "ram") or is_(c, b, "siege_ram")
                                           or c.endswith("_ram")), 12),
    'id.contains("siege_tower")':
        (lambda c, b: sub(c, "siege_tower"), 25),
    'id.contains("bolt_shooter") || id.contains("ballista") || id.contains("oxybeles")':
        (lambda c, b: "bolt_shooter" in c or "ballista" in c or "oxybeles" in c, 25),
    'id.contains("catapult") || id.contains("onager") || id.contains("lithobolos") || id.contains("mangonel")':
        (lambda c, b: any(k in c for k in ("catapult", "onager", "lithobolos", "mangonel")), 25),
    'id.contains("war_dog") || id.contains("dog")':
        (lambda c, b: sub(c, "dog"), 20),
    'id.contains("carnyx")':
        (lambda c, b: sub(c, "carnyx"), 20),
    'id == "bolt_projectile"':
        (lambda c, b: c == "bolt_projectile", 25),
    'id == "arrow_projectile"':
        (lambda c, b: c == "arrow_projectile", 25),
    'id == "stone_projectile"':
        (lambda c, b: c == "stone_projectile", 25),
}


def read_rules():
    """The ordered rules of target_height's "units" branch, from the C++."""
    src = LIB.read_text()
    a = src.find("float ModelLibrary::target_height(")
    seg = src[a:]
    seg = seg[:seg.find('if (category == "buildings")')]
    # Drop the comments: they mention the rules but are not rules.
    seg = re.sub(r"//[^\n]*", "", seg)
    rules = []
    for m in re.finditer(r"if \(([^;]*?)\)\s*return\s+([^;]+);", seg, re.S):
        cond = re.sub(r"\s+", " ", m.group(1)).strip()
        val = re.sub(r"\s+", " ", m.group(2)).strip()
        if cond not in CONDITIONS:
            if "fauna_type_info" in cond or "is_packed_siege_model" in cond or "wagon" in cond:
                continue  # the models that keep their own size
            raise SystemExit(f"audit does not know this rule: if ({cond}) return {val};")
        rules.append((cond, val))
    return rules


def height_of(value, natural):
    if value == "natural_h":
        return natural
    return float(value.rstrip("f"))


def base_of(cid):
    us = cid.find("_")
    return cid[us + 1:] if 0 < us < len(cid) - 1 else cid


def parse_civs(text):
    return re.findall(r'\{CIV_[A-Z_]+,\s*"([a-z]+)"', text)


def parse_table(text, marker, end_marker):
    a = text.find(marker)
    b = text.find(end_marker, a)
    return re.findall(r'\{"[^"]+",\s*"([a-z_]+)",', text[a:b])


def parse_templates():
    text = TPL.read_text()
    seg = text[text.find("static const UnitTemplateModel kTable"):]
    out = {}
    for m in re.finditer(r'\{"([a-z0-9_]+)",\s*"([a-z0-9_]+)",\s*(?:nullptr|"[a-z0-9_]+")\}', seg):
        out[m.group(1)] = m.group(2)
    return out


def main():
    rules = read_rules()
    sim = SIM.read_text()
    civs = parse_civs(sim)
    unit_models = parse_table(sim, "static const UnitTypeInfo kTable[UNIT_TYPE_COUNT]",
                              "inline const BuildingTypeInfo &building_type_info")
    templates = parse_templates()
    models = json.loads(MANIFEST.read_text())["models"]

    # Every id the game draws as a unit: the civ class models, and the roster
    # template models that have no class model of their own (they are their
    # own kind, and target_height applies to them).
    ids = sorted({f"{c}_{m}" for c in civs for m in unit_models} |
                 {i for i in templates if i not in templates})

    print(f"rules read from model_library.cpp: {len(rules)}")
    print(f"entity ids audited: {len(ids)}")

    problems, known = [], []
    for cid in ids:
        b = base_of(cid)
        natural = models.get(cid, {}).get("size_meters", [0, 0, 0])[1]
        won_cond = won_h = None
        best = None
        for cond, val in rules:
            fn, spec = CONDITIONS[cond]
            if not fn(cid, b):
                continue
            h = height_of(val, natural)
            if won_cond is None:
                won_cond, won_h = cond, h
            if best is None or spec > best[1]:
                best = (cond, spec, h)
        if won_cond is None:
            continue
        if best and best[0] != won_cond:
            problems.append((cid, won_h, best[2], won_cond, best[0]))
            continue
        if natural > 0.05 and won_h is not None:
            scale = won_h / natural
            if scale < SCALE_LOW or scale > SCALE_HIGH:
                if cid in KNOWN_OUTLIERS:
                    known.append((cid, scale, natural, KNOWN_OUTLIERS[cid]))
                else:
                    problems.append((cid, won_h, None, won_cond,
                                     f"drawn at {scale:.2f}x its own {natural:.2f} m"))

    for cid, scale, natural, why in known:
        print(f"  known: {cid:<28} drawn at {scale:.2f}x its own {natural:.2f} m ({why})")
    if not problems:
        print("PASS: every entity is sized by its most specific rule and close to its own size")
        return 0
    print(f"FAIL: {len(problems)} entity id(s) sized wrong\n")
    for cid, h, bh, cond, why in problems:
        if bh is None:
            print(f"  {cid:<36} {h:5.2f} m: {why}")
        else:
            print(f"  {cid:<36} {h:5.2f} m  (rule: {cond})")
            print(f"  {'':<36} should be {bh:5.2f} m  (rule: {why})")
    return 1


if __name__ == "__main__":
    sys.exit(main())
