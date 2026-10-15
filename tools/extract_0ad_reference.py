#!/usr/bin/env python3
"""Writes data/reference/templates.json: the text 0 A.D.'s reference pages
(gui/reference/civinfo, structree and viewer) show for each entity and that
RTS0's simulation tables don't carry: Identity/Tooltip, Identity/History,
VisibleClasses and Builder/Entities for every roster unit and structure,
technology descriptions, and each civilization's History.

Numbers (health, costs, attacks, armour) are not copied here; the reference
pages read them from the game's own tables through RtsoReference, so they
always show what the simulation plays.

0 A.D. text is CC BY-SA 3.0, Wildfire Games (see docs/0AD_ATTRIBUTION.md).

Usage:
  python3 -I tools/extract_0ad_reference.py --0ad PATH/TO/0ad [--previous-0ad PATH] [--check]

--previous-0ad: roster rows and technologies RTS0 still has that the --0ad release
dropped (a retired roster row kept for saves, Flanking Maneuvers) keep that tree's text.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_0ad_rosters import (BUILDING_BITS, CIVS, UNIQUE_BUILDINGS,  # noqa: E402
                                 TemplateLoader)

import re  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "data", "reference", "templates.json")
ROSTERS = os.path.join(REPO, "data", "civs", "rosters.json")
TECH_DATA = os.path.join(REPO, "src", "simulation", "tech_data.h")

# 0 A.D. structure file stems for RTS0's generic BuildingTypes, first match wins
# (structures/<civ>/<stem>.xml, then the generic template_structure_* parent).
GENERIC_STEMS = {
    "BLD_TOWN_CENTER": (["civil_centre"], "template_structure_civic_civil_centre"),
    "BLD_HOUSE": (["house"], "template_structure_civic_house"),
    "BLD_BARRACKS": (["barracks"], "template_structure_military_barracks"),
    "BLD_STOREHOUSE": (["storehouse"], "template_structure_economic_storehouse"),
    "BLD_TOWER": (["defense_tower"], "template_structure_defensive_tower_stone"),
    "BLD_FARMSTEAD": (["farmstead"], "template_structure_economic_farmstead"),
    "BLD_MARKET": (["market"], "template_structure_economic_market"),
    "BLD_FORTRESS": (["fortress"], "template_structure_military_fortress"),
    "BLD_GATE": (["wall_gate"], "template_structure_defensive_wall_gate"),
    "BLD_TEMPLE": (["temple"], "template_structure_civic_temple"),
    "BLD_WONDER": (["wonder"], "template_structure_wonder"),
    "BLD_DOCK": (["dock"], "template_structure_military_dock"),
    "BLD_WALL_SEGMENT": (["wall_long"], "template_structure_defensive_wall_long"),
    "BLD_CORRAL": (["corral"], "template_structure_economic_corral"),
    "BLD_FIELD": (["field"], "template_structure_resource_field"),
    "BLD_STABLE": (["stable"], "template_structure_military_stable"),
    "BLD_RANGE": (["range"], "template_structure_military_range"),
    "BLD_FORGE": (["forge"], "template_structure_military_forge"),
    "BLD_ARSENAL": (["arsenal", "workshop"], "template_structure_military_arsenal"),
    "BLD_SENTRY_TOWER": (["sentry_tower"], "template_structure_defensive_tower_sentry"),
    "BLD_OUTPOST": (["outpost"], "template_structure_defensive_outpost"),
    "BLD_PALISADE": (["palisades_long"], "template_structure_defensive_palisade"),
    "BLD_WALL_TOWER": (["wall_tower"], "template_structure_defensive_wall_tower"),
    "BLD_ELEPHANT_STABLE": (["elephant_stable"], "template_structure_military_elephant_stable"),
    "BLD_THEATER": (["theater"], "template_structure_special_theater"),
    "BLD_EMBASSY": (["embassy_celtic"], "template_structure_military_embassy"),  # Iberian, Italic: own types
}
# A wall set builds its segments, towers and gates (0 A.D. WallSet).
WALLSETS = {
    "wallset_stone": ("BLD_WALL_SEGMENT", "BLD_WALL_TOWER", "BLD_GATE"),
    "wallset_palisade": ("BLD_PALISADE",),
}
STEMS = dict(GENERIC_STEMS)
for _enum, _stems, _model, _portrait in UNIQUE_BUILDINGS:
    STEMS[_enum] = (_stems, None)


def text_of(t, path):
    v = t.value(path, "")
    return " ".join(v.split()) if v else ""


def _n(x):
    """A template number as JSON keeps it short: 2 not 2.0, 0.35 not 0.35000000001."""
    x = round(float(x), 4)
    return int(x) if x == int(x) else x


def _rates(node):
    return {k: _n(c.text or 0) for k, c in node.children.items() if c.text and float(c.text)} if node else {}


def effects(node):
    """Capture, Damage and ApplyStatus of an attack or its splash (Templates.js getAttackEffects)."""
    e = {}
    if node.get("Capture") is not None:
        e["capture"] = _n(node.num("Capture"))
    if node.get("Damage") is not None:
        e["damage"] = {k: _n(c.text or 0) for k, c in node.get("Damage").children.items()}
    if node.get("ApplyStatus") is not None:
        st = {}
        for code, c in node.get("ApplyStatus").children.items():
            x = {"name": STATUS_NAMES.get(code, (code, ""))[0], "tooltip": STATUS_NAMES.get(code, (code, ""))[1]}
            if c.get("Damage") is not None:
                x["damage"] = {k: _n(v.text or 0) for k, v in c.get("Damage").children.items()}
            for key, path in (("interval", "Interval"), ("duration", "Duration")):
                if c.num(path):
                    x[key] = _n(c.num(path) / 1000.0)
            x["stackability"] = c.value("Stackability", "")
            st[code] = x
        e["status"] = st
    return e


# data/status_effects/*.json statusName and applierTooltip, by code (collect() fills it).
STATUS_NAMES = {}


def viewer(t):
    """What gui/common/tooltips.js shows of a template that RTS0's tables don't keep:
    attack names, ranges, splash and status effects, speed, garrison size, turrets,
    dropsite, supply, trickle, upkeep, treasure, gather rates, capture and status
    resistance and the rank (GetTemplateDataHelper and TemplateParser.getEntity)."""
    v = {}
    atk = t.get("Attack")
    if atk is not None:
        attacks = []
        for kind, node in atk.children.items():
            if kind == "Slaughter":
                continue  # used to kill animals, never shown
            a = {"type": kind, "name": node.value("AttackName", kind),
                 "min_range": _n(node.num("MinRange")), "max_range": _n(node.num("MaxRange")),
                 "y_origin": _n(node.num("Origin/Y")), "repeat_time": _n(node.num("RepeatTime") / 1000.0)}
            a.update(effects(node))
            sp = node.get("Splash")
            if sp is not None:
                a["splash"] = dict(effects(sp), shape=sp.value("Shape", ""),
                                   friendly_fire=sp.value("FriendlyFire", "true") != "false")
            attacks.append(a)
        v["attacks"] = attacks
    if t.get("Heal") is not None:
        v["heal"] = {"health": _n(t.num("Heal/Health")), "range": _n(t.num("Heal/Range")),
                     "interval": _n(t.num("Heal/Interval") / 1000.0)}
    if t.get("GarrisonHolder") is not None:
        v["garrison"] = {"capacity": _n(t.num("GarrisonHolder/Max")), "buff_heal": _n(t.num("GarrisonHolder/BuffHeal"))}
    if t.get("Garrisonable") is not None:
        v["garrison_size"] = _n(t.num("Garrisonable/Size", 1))
    if t.get("TurretHolder/TurretPoints") is not None:
        v["turrets"] = len(t.get("TurretHolder/TurretPoints").children)
    if t.get("UnitMotion") is not None:
        walk = t.num("UnitMotion/WalkSpeed")
        v["speed"] = {"walk": _n(walk), "run": _n(walk * t.num("UnitMotion/RunMultiplier", 1)),
                      "acceleration": _n(t.num("UnitMotion/Acceleration"))}
    rg = t.get("ResourceGatherer/Rates")
    if rg is not None:
        base = t.num("ResourceGatherer/BaseSpeed", 1)
        v["gather"] = {k: _n(float(c.text) * base) for k, c in rg.children.items()
                       if c.text and float(c.text) > 0 and not k.endswith(".ruins")}
    if t.tokens("ResourceDropsite/Types"):
        v["dropsite"] = t.tokens("ResourceDropsite/Types")
    if t.get("ResourceSupply") is not None:
        mx = t.value("ResourceSupply/Max", "")
        v["supply"] = {"type": t.value("ResourceSupply/Type", "").split(".")[0],
                       "amount": "Infinity" if mx == "Infinity" else _n(t.num("ResourceSupply/Max"))}
    for key, path in (("trickle", "ResourceTrickle"), ("upkeep", "Upkeep")):
        rates = _rates(t.get(path + "/Rates"))
        if rates:
            v[key] = {"interval": _n(t.num(path + "/Interval") / 1000.0), "rates": rates}
    treasure = _rates(t.get("Treasure/Resources"))
    if treasure:
        v["treasure"] = treasure
    res = t.get("Resistance/Entity")
    if res is not None and res.get("Capture") is not None:
        v["capture_resistance"] = _n(res.num("Capture"))
    if res is not None and res.get("ApplyStatus") is not None:
        v["status_resistance"] = {k: {"block": _n(c.num("BlockChance")), "duration": _n(c.num("Duration", 1))}
                                  for k, c in res.get("ApplyStatus").children.items()}
    if t.value("Identity/Rank"):
        v["rank"] = t.value("Identity/Rank")
    return v


def auras_of(sim_root, t):
    """A structure's Auras as getAurasTooltip lists them: auraName, auraDescription and,
    for a range aura, its radius (0.28 metres)."""
    out = []
    for a in t.tokens("Auras"):
        path = os.path.join(sim_root, "data", "auras", a + ".json")
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as f:
            j = json.load(f)
        out.append({"name": j.get("auraName", a), "description": j.get("auraDescription", ""),
                    "radius": _n(float(j.get("radius", 0))) if j.get("type") == "range" else 0})
    return out


def identity(t):
    return {
        "generic_name": text_of(t, "Identity/GenericName"),
        "specific_name": text_of(t, "Identity/SpecificName"),
        "tooltip": t.value("Identity/Tooltip", "").strip(),
        "history": text_of(t, "Identity/History"),
        "classes": t.tokens("Identity/VisibleClasses"),
        "icon": t.value("Identity/Icon", ""),
    }


def structure_template(loader, civ, enum):
    stems, generic = STEMS[enum]
    for st in stems:
        for name in ("structures/%s/%s" % (civ, st), "structures/" + st):
            if loader.exists(name):
                return name
    return generic if generic and loader.exists(generic) else None


def build_list(loader, t, civ):
    """Builder/Entities mapped to RTS0 BuildingType ids (0 A.D. structures this unit builds)."""
    out = []
    for e in t.tokens("Builder/Entities"):
        e = e.replace("{civ}", civ).replace("{native}", civ)
        if not loader.exists(e):
            continue  # template_unit lists every civ's structures; only existing ones count
        stem = e.rsplit("/", 1)[-1]
        for i, enum in enumerate(BUILDING_BITS):
            if (stem in STEMS.get(enum, ([], None))[0] or enum in WALLSETS.get(stem, ())) and i not in out:
                out.append(i)
    return sorted(out)


def sim_root_of(zad):
    return os.path.join(zad, "binaries", "data", "mods", "public", "simulation")


def tech_text(path):
    with open(path, encoding="utf-8") as f:
        j = json.load(f)
    if "description" not in j and "tooltip" not in j:
        return None
    return {
        "tooltip": j.get("tooltip", ""),
        "description": j.get("description", ""),
        "requirements_tooltip": j.get("requirementsTooltip", ""),
        "icon": j.get("icon", ""),
    }


def tech_reqs(civ, op, val):
    """globalscripts/Technologies.js InterpretTechRequirements, kept to what decides
    whether a civ can research at all: a list (maybe empty) when it can, else False."""
    if op == "civ":
        return [] if not civ or civ == val else False
    if op == "notciv":
        return False if civ == val else []
    if op == "entity":
        n = val.get("number") or val.get("numberOfTypes") or 0
        return [{"entities": [val.get("class")]}] if n > 0 else []
    if op == "tech":
        return [{"techs": [val]}]
    reqs = []
    if op == "all":
        permitted = None
        for sub in val:
            sop, sval = next(iter(sub.items()))
            res = tech_reqs(civ, sop, sval)
            if sop == "civ":
                if res is not False:
                    permitted = True
                elif permitted is not True:
                    permitted = False
            elif sop == "notciv":
                if res is False:
                    return False
            else:
                if res is False:
                    if sop == "any":
                        return False
                    null = tech_reqs(None, sop, sval)
                    if not null:
                        permitted = False
                    continue
                reqs += res
        return False if permitted is False else reqs
    if op == "any":
        permitted = False
        for sub in val:
            sop, sval = next(iter(sub.items()))
            res = tech_reqs(civ, sop, sval)
            if sop == "civ":
                if res is not False:
                    return []
            elif sop == "notciv":
                if res is False:
                    return False
                permitted = True
            else:
                if res is False:
                    if sop == "any":
                        if not tech_reqs(None, sop, sval):
                            continue
                        return False
                    continue
                if sop in ("any", "all"):
                    permitted = True
                reqs += res
        return False if not permitted and not reqs else reqs
    return reqs


def civ_specific(j, civ):
    """0.28 Subsection.getTechnologyCaptions: researchable by this civ but not by "anyciv"."""
    req = j.get("requirements")
    if not req:
        return False
    op, val = next(iter(req.items()))
    return tech_reqs(civ, op, val) is not False and tech_reqs("anyciv", op, val) is False


def tech_name(j, civ):
    n = j.get("specificName", j.get("genericName", ""))
    if isinstance(n, dict):
        n = n.get(civ, j.get("genericName", ""))
    g = j.get("genericName", "")
    return {"generic": g if isinstance(g, str) else "", "specific": n if n != g else ""}


COST_KEYS = ("food", "wood", "stone", "metal")


def _classes(t):
    return set(t.tokens("Identity/Classes")) | set(t.tokens("Identity/VisibleClasses"))


def _matches(classes, affects):
    """MatchesClassList: any list whose classes all match ("!X" excludes X)."""
    return any(all((c[1:] not in classes) if c.startswith("!") else (c in classes) for c in lst) for lst in affects)


def civ_modifiers(sim_root, civ, auto_techs, player):
    """0.28 TemplateLoader.deriveModifications: auto-researched techs the civ may
    research, its civ-bonus auras and the team bonuses that also affect the player."""
    out = []
    def add(j):
        base = [a.split() for a in j.get("affects", [])] or [[]]
        for m in j.get("modifications", []):
            aff = [lst + m["affects"].split() for lst in base] if m.get("affects") else base
            out.append((m["value"], m, aff))
    for _rel, j in auto_techs:
        req = j.get("requirements")
        if req:
            op, val = next(iter(req.items()))
            if tech_reqs(civ, op, val) is False:
                continue
        add(j)
    for a in player.tokens("Auras"):
        path = os.path.join(sim_root, "data", "auras", a + ".json")
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                j = json.load(f)
            if any(x in j.get("affectedPlayers", ["Player"]) for x in ("Player", "Ally", "MutualAlly")):
                add(j)
    return out


def _modified(mods, path, value, classes):
    for p, m, aff in mods:
        if p != path or not _matches(classes, aff):
            continue
        if "replace" in m:
            value = m["replace"]
        elif "multiply" in m:
            value *= m["multiply"]
        elif "add" in m:
            value += m["add"]
    return value


def civ_cost_mults(loader, mods, structures, units):
    """Cost and Researcher/TechCostMultiplier as the civ's own bonuses leave them
    (TemplateParser.getEntity under the civ's modifiers): food, wood, stone, metal
    and time factors, only where one differs from 1."""
    def factors(t, prefix, base):
        cl = _classes(t)
        f = []
        for k in COST_KEYS + ("time",):
            path = prefix + ("BuildTime" if k == "time" and prefix == "Cost/" else (k if prefix != "Cost/" else "Resources/" + k))
            b = base(t, path)
            v = _modified(mods, path, b, cl)
            f.append(round(v / b, 4) if b else 1.0)
        return f if any(abs(x - 1) > 1e-6 for x in f) else None
    def tval(t, path):
        return float(t.value(path, "1") or 1)
    def cval(t, path):
        return float(t.value(path, "0") or 0)
    tech, cost = {}, {}
    for i, d in structures.items():
        t = loader.load(d["template"])
        f = factors(t, "Researcher/TechCostMultiplier/", tval)
        if f:
            tech[i] = f
        f = factors(t, "Cost/", cval)
        if f:
            cost["s" + i] = f
    for uid, name in units:
        f = factors(loader.load(name), "Cost/", cval)
        if f:
            cost[uid] = f
    return {"tech_cost_mult": tech, "cost_mult": cost}


def civ_gameplay(loader, sim_root, civ, auto_techs, all_techs, structures):
    """0.28 civinfo GameplaySection lists: the civ bonuses that are an auto-researched
    civ tech or a player aura (BonusesSubsection), team bonuses, and which techs and
    structures are civ specific (Subsection.getTechnologyCaptions, "CivSpecific Structure")."""
    bonuses = []
    for rel, j in auto_techs:
        if civ_specific(j, civ):
            n = tech_name(j, civ)
            bonuses.append({"name": n["specific"] or n["generic"], "info": j.get("description", ""),
                            "description": j.get("tooltip", "")})
    team = []
    for a in loader.load("special/players/" + civ).tokens("Auras"):
        path = os.path.join(sim_root, "data", "auras", a + ".json")
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as f:
            j = json.load(f)
        affected = j.get("affectedPlayers", ["Player"])
        entry = {"name": j.get("auraName", ""), "description": j.get("auraDescription", "")}
        (team if any(x in affected for x in ("MutualAlly", "ExclusiveMutualAlly")) else bonuses).append(entry)
    techs = sorted(rel for rel, j in all_techs if civ_specific(j, civ))
    specific = []
    for i, d in structures.items():
        t = loader.load(d["template"])
        classes = set(t.tokens("Identity/Classes")) | set(t.tokens("Identity/VisibleClasses"))
        if {"CivSpecific", "Structure"} <= classes:
            specific.append(int(i))
    # structree PhaseIdent.drawPhaseIcon: phase_X_<civ>, else phase_X_generic, else phase_X.
    by_name = dict(all_techs)
    phases = []
    for ph in ("phase_village", "phase_town", "phase_city"):
        j = by_name.get(ph + "_" + civ) or by_name.get(ph + "_generic") or by_name.get(ph, {})
        n = tech_name(j, civ)
        phases.append({"generic_name": n["generic"], "specific_name": n["specific"], "icon": j.get("icon", "")})
    return {"bonuses": bonuses, "team_bonuses": team, "specific_techs": techs,
            "specific_structures": sorted(specific), "phases": phases}


def collect(zad, prev=None):
    sim_root = sim_root_of(zad)
    loader = TemplateLoader(sim_root)
    sdir = os.path.join(sim_root, "data", "status_effects")
    for fn in sorted(os.listdir(sdir)) if os.path.isdir(sdir) else []:
        if fn.endswith(".json"):
            with open(os.path.join(sdir, fn), encoding="utf-8") as f:
                j = json.load(f)
            STATUS_NAMES[j["code"]] = (j.get("statusName", j["code"]), j.get("applierTooltip", ""))
    prev_loader = TemplateLoader(sim_root_of(prev)) if prev else None
    with open(ROSTERS, encoding="utf-8") as f:
        rosters = json.load(f)
    units = {}
    unit_names = {}  # civ -> [(roster id, 0.28 template)] for the civ's cost modifiers
    for r in rosters["units"]:
        name = "units/" + r["id"]
        ld = loader
        if not ld.exists(name):
            ld = prev_loader  # a row the release dropped, kept for saves
            if ld is None or not ld.exists(name):
                continue  # RTS0-only rows (rts0_*) have no 0 A.D. template
        t = ld.load(name)
        d = identity(t)
        d["builds"] = build_list(ld, t, r["civ"])
        d["viewer"] = viewer(t)
        units[r["id"]] = d
        if ld is loader:
            unit_names.setdefault(r["civ"], []).append((r["id"], name))
    structures = {}
    for civ in CIVS:
        rows = {}
        for i, enum in enumerate(BUILDING_BITS):
            name = structure_template(loader, civ, enum)
            if name is None:
                continue
            t = loader.load(name)
            d = identity(t)
            d["template"] = name
            d["viewer"] = viewer(t)
            auras = auras_of(sim_root, t)
            if auras:
                d["auras"] = auras
            rows[str(i)] = d
        structures[civ] = rows
    # Gaia entities the selection panel shows: animals, treasures, ruins, and the generic
    # tree, fruit, mine and rock templates (every gaia/tree and gaia/fruit file inherits their Icon).
    gaia = {}
    gdir = os.path.join(sim_root, "templates", "gaia")
    names = ["template_gaia_" + g for g in ("tree", "fruit", "ore", "rock", "ruins", "treasure")]
    names += ["gaia/" + fn[:-4] for fn in sorted(os.listdir(gdir)) if fn.startswith("fauna_") and fn.endswith(".xml")]
    for sub in ("treasure", "ruins"):
        names += ["gaia/%s/%s" % (sub, fn[:-4]) for fn in sorted(os.listdir(os.path.join(gdir, sub))) if fn.endswith(".xml")]
    for name in names:
        if loader.exists(name):
            d = identity(loader.load(name))
            del d["history"]
            gaia[name] = d
    techs = {}
    all_techs = []  # (name, json) for every 0.28 technology, in listFiles order
    tdir = os.path.join(sim_root, "data", "technologies")
    for root, _dirs, files in os.walk(tdir):
        for fn in sorted(files):
            if not fn.endswith(".json"):
                continue
            rel = os.path.relpath(os.path.join(root, fn), tdir)[:-5].replace(os.sep, "/")
            with open(os.path.join(root, fn), encoding="utf-8") as f:
                all_techs.append((rel, json.load(f)))
            d = tech_text(os.path.join(root, fn))
            if d is not None:
                techs[rel] = d
    if prev:
        # Technologies tech_data.h still plays that the release dropped.
        with open(TECH_DATA, encoding="utf-8") as f:
            ids = re.findall(r'"([a-z0-9_/]+)"\},\s*(?://.*)?$', f.read(), re.M)
        pdir = os.path.join(sim_root_of(prev), "data", "technologies")
        for tid in ids:
            p = os.path.join(pdir, tid + ".json")
            if tid not in techs and os.path.exists(p):
                d = tech_text(p)
                if d is not None:
                    techs[tid] = d
    all_techs.sort()
    auto_techs = [(rel, j) for rel, j in all_techs if j.get("autoResearch")]
    civs = {}
    for civ in CIVS:
        p = loader.load("special/players/" + civ)
        civs[civ] = {"name": text_of(p, "Identity/GenericName"), "history": text_of(p, "Identity/History")}
        civs[civ].update(civ_gameplay(loader, sim_root, civ, auto_techs, all_techs, structures[civ]))
        mods = civ_modifiers(sim_root, civ, auto_techs, p)
        civs[civ].update(civ_cost_mults(loader, mods, structures[civ], unit_names.get(civ, [])))
    return {
        "generator": "tools/extract_0ad_reference.py",
        "license": "0 A.D. data, CC BY-SA 3.0, Wildfire Games",
        "civs": civs,
        "units": units,
        "structures": structures,
        "gaia": gaia,
        "techs": dict(sorted(techs.items())),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--0ad", dest="zad", required=True, help="0 A.D. source checkout")
    ap.add_argument("--previous-0ad", dest="prev", help="older 0 A.D. tree for rows and techs the release dropped")
    ap.add_argument("--check", action="store_true", help="fail if the checked-in file differs")
    args = ap.parse_args()
    text = json.dumps(collect(args.zad, args.prev), indent=1, ensure_ascii=False) + "\n"
    if args.check:
        with open(OUT, encoding="utf-8") as f:
            if f.read() != text:
                sys.exit("data/reference/templates.json is stale; rerun tools/extract_0ad_reference.py")
        print("templates.json up to date")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    d = json.loads(text)
    print("wrote %s: %d units, %d civs of structures, %d techs" % (
        OUT, len(d["units"]), len(d["structures"]), len(d["techs"])))


if __name__ == "__main__":
    main()
