#!/usr/bin/env python3
"""Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games); the
templates and actors it reads are CC BY-SA 3.0 (see docs/0AD_ATTRIBUTION.md).

One model per civ roster template. The world view used to draw every unit by
civ and RTS0 UnitType (`{civ}_{model}`), so templates that 0 A.D. draws with
different actors (champions, heroes, ranks of a class) shared one model.

This module reads the roster (src/simulation/civ_roster_data.h), finds each
template's 0 A.D. actor (VisualActor/Actor through its parents), and plans
the models:
  * a template whose actor some extracted `{civ}_{model}` GLB already uses
    keeps that GLB;
  * any other template gets its own GLB, named after the template
    (`athen/champion_infantry` -> `athen_champion_infantry`).

extract_0ad_assets.py --templates registers the new models and writes
src/graphics/assets/unit_template_models.h, the table the world view draws by.
"""

import os
import re
import xml.etree.ElementTree as ET

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROSTER_H = os.path.join(REPO, "src", "simulation", "civ_roster_data.h")
SIM_TYPES_H = os.path.join(REPO, "src", "simulation", "sim_types.h")
HEADER = os.path.join(REPO, "src", "graphics", "assets", "unit_template_models.h")


def _enum(text, name):
    body = re.search(r"enum %s\b[^{]*\{(.*?)\};" % name, text, re.S).group(1)
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"(\w+)\s*=\s*(\d+)", body)}


def roster_rows():
    """[(index, template id, civ code, RTS0 model kind)] in roster order."""
    sim = open(SIM_TYPES_H).read()
    civs = _enum(sim, "CivId") if re.search(r"enum CivId\b", sim) else None
    if civs is None:
        civs = {m.group(1): int(m.group(2)) for m in re.finditer(r"\b(CIV_[A-Z]+)\s*=\s*(\d+)", sim)}
    codes = {}
    for m in re.finditer(r"\{(CIV_[A-Z]+), \"(\w+)\", \"", sim):
        codes[m.group(1)] = m.group(2)
    utypes = _enum(sim, "UnitType")
    table = re.search(r"unit_type_info\(uint8_t type\) \{.*?kTable\[UNIT_TYPE_COUNT\] = \{(.*?)\n\t\};", sim, re.S).group(1)
    models = re.findall(r"^\s*\{\"[^\"]*\", \"(\w+)\"", table, re.M)
    by_type = {name: models[idx] for name, idx in utypes.items() if idx < len(models)}
    rows = []
    src = open(ROSTER_H).read()
    for i, m in enumerate(re.finditer(r"^\t\t\{\"([a-z_]+/[\w/]+)\", \"[^\"]*\", \"[^\"]*\", (CIV_[A-Z]+), (UNIT_[A-Z_]+),",
                                      src, re.M)):
        rows.append((i, m.group(1), codes[m.group(2)], by_type.get(m.group(3))))
    return rows


def template_actor(names, read, tid):
    """The VisualActor/Actor of `units/<tid>`, looking through its parents
    (`a|b|c` layers a on b on c; plain names are mixins)."""
    seen = set()

    def look(path):
        if path in seen:
            return None
        seen.add(path)
        for cand in (f"simulation/templates/{path}.xml", f"simulation/templates/mixins/{path}.xml"):
            if cand in names:
                root = ET.fromstring(read(cand))
                el = root.find("VisualActor/Actor")
                if el is not None and el.text and el.text.strip():
                    return el.text.strip()
                for parent in (root.get("parent") or "").split("|"):
                    if parent:
                        got = look(parent)
                        if got:
                            return got
                return None
        return None

    return look(f"units/{tid}")


def template_model_id(tid):
    return tid.replace("/", "_")


MOUNTED_KINDS = ("cavalry", "elephant", "camel", "chariot", "scythed_chariot", "worker_elephant")


def mount_kind(actor, kind):
    """The extractor kind (skeleton and clips) for a template's actor: mounted
    heroes and champions are drawn like the class they ride, not as infantry."""
    if kind in MOUNTED_KINDS:
        return kind
    base = os.path.basename(actor)
    if "elephant" in base:
        return "elephant"
    if "chariot" in base:
        return "chariot"
    if "camel" in base:
        return "camel"
    # "_m" marks a mounted actor, except 0.28's mercenary infantry
    # (carthaginians/infantry_spearman_b_m.xml, the Italic spearman).
    if (base.endswith("_m.xml") and not base.startswith("infantry_")) or "cavalry" in base:
        return "cavalry"
    return kind


def plan(names, read, assets):
    """(per-row model id or None, {new model id: (code, model kind, actor path)}).

    `assets` is the extractor's registry (id -> (actor, ...)); a template
    reuses any `{code}_{kind}` model whose actor is its own."""
    by_actor = {}
    for aid, spec in assets.items():
        by_actor.setdefault(spec[0], []).append(aid)
    per_row, new = [], {}
    for _i, tid, code, kind in roster_rows():
        actor = template_actor(names, read, tid)
        if not actor or kind is None:
            per_row.append(None)
            continue
        path = f"art/actors/{actor}"
        if path not in names:
            per_row.append(None)
            continue
        own = f"{code}_{kind}"
        hits = by_actor.get(path, [])
        if own in hits:
            per_row.append(own)
            continue
        if hits:
            # Same civ first, then any civ's model of the same kind, then a
            # template model planned earlier for the same actor.
            pick = next((h for h in hits if h.startswith(code + "_")), None) \
                or next((h for h in hits if h.endswith("_" + kind)), None) \
                or next((h for h in hits if h in new), None)
            if pick:
                per_row.append(pick)
                continue
        if tid.endswith("_packed"):
            # Packed siege draws through the packed wagon models (packed_siege_models.h).
            per_row.append(None)
            continue
        mid = template_model_id(tid)
        if mid in assets or mid in new:
            # The id names another actor's model already (Ashoka on foot).
            mid += "_" + os.path.splitext(os.path.basename(path))[0]
        new[mid] = (code, mount_kind(path, kind), path)
        by_actor.setdefault(path, []).append(mid)
        per_row.append(mid)
    return per_row, new


def write_header(per_row, models, path=HEADER):
    """`models`: template model id -> (class model id, clip donor or None)."""
    lines = [
        "// Generated by tools/extract_0ad_assets.py --templates (tools/unit_template_models.py); do not edit.",
        "// One model per civ roster template: its 0 A.D. actor (CC BY-SA 3.0, Wildfire Games).",
        "#pragma once",
        "",
        "#include \"../../simulation/sim_types.h\"",
        "",
        "#include <cstring>",
        "",
        "namespace rtso {",
        "",
    ]
    rows = roster_rows()
    # A row draws with its civ's class model unless its actor's model differs.
    draw = sorted({mid for (_i, _t, code, kind), mid in zip(rows, per_row)
                   if mid and mid != f"{code}_{kind}"})
    index = {mid: k for k, mid in enumerate(draw)}
    lines += [
        "// The models roster templates draw with besides the civ class models.",
        f"inline constexpr int UNIT_TEMPLATE_DRAW_MODEL_COUNT = {len(draw)};",
        "",
        "inline const char *unit_template_draw_model(int k) {",
        "\tstatic const char *const kDraw[UNIT_TEMPLATE_DRAW_MODEL_COUNT] = {",
    ]
    for mid in draw:
        lines.append(f'\t\t"{mid}",')
    lines += [
        "\t};",
        "\treturn k >= 0 && k < UNIT_TEMPLATE_DRAW_MODEL_COUNT ? kDraw[k] : nullptr;",
        "}",
        "",
        "// The unit_template_draw_model a civ roster template draws with (its 0 A.D.",
        "// actor), or -1 to draw the civ's model for the unit's UnitType.",
        "inline int unit_template_draw_index(uint16_t tmpl) {",
        "\tstatic const int16_t kIndex[CIV_UNIT_TEMPLATE_COUNT] = {",
    ]
    for (_i, tid, code, kind), mid in zip(rows, per_row):
        k = index.get(mid, -1) if mid else -1
        lines.append(f"\t\t{k}, // {tid}")
    lines += [
        "\t};",
        "\treturn tmpl < CIV_UNIT_TEMPLATE_COUNT ? kIndex[tmpl] : -1;",
        "}",
        "",
        "struct UnitTemplateModel {",
        "\tconst char *id;",
        "\tconst char *class_model; // the civ's model for the unit's kind: sizes the template model",
        "\tconst char *clip_donor; // a mesh-only model plays this model's clips (same bones), or nullptr",
        "};",
        "",
        f"inline constexpr int UNIT_TEMPLATE_MODEL_COUNT = {len(models)};",
        "",
        "inline const UnitTemplateModel *unit_template_models() {",
        "\tstatic const UnitTemplateModel kTable[UNIT_TEMPLATE_MODEL_COUNT] = {",
    ]
    for mid, (cls, donor) in sorted(models.items()):
        d = f'"{donor}"' if donor else "nullptr"
        lines.append(f'\t\t{{"{mid}", "{cls}", {d}}},')
    lines += [
        "\t};",
        "\treturn kTable;",
        "}",
        "",
        "// The template model entry for a model id, or nullptr for any other model.",
        "inline const UnitTemplateModel *find_unit_template_model(const char *id) {",
        "\tif (!id) return nullptr;",
        "\tconst UnitTemplateModel *t = unit_template_models();",
        "\tfor (int i = 0; i < UNIT_TEMPLATE_MODEL_COUNT; ++i)",
        "\t\tif (std::strcmp(t[i].id, id) == 0) return &t[i];",
        "\treturn nullptr;",
        "}",
        "",
        f"static_assert(CIV_UNIT_TEMPLATE_COUNT == {len(rows)}, \"rerun tools/extract_0ad_assets.py --templates\");",
        "",
        "} // namespace rtso",
        "",
    ]
    with open(path, "w") as fh:
        fh.write("\n".join(lines))
