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
`unit_template_models.py --extend` adds roster rows appended since (extract_0ad_rosters.py
--append) as -1, the civ class model, without the art tree.
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
    for i, m in enumerate(re.finditer(r"^\t\t\{\"([\w/]+)\", \"(?:[^\"\\]|\\.)*\", \"(?:[^\"\\]|\\.)*\", (CIV_[A-Z]+), (UNIT_[A-Z_]+),",
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


RANK_SUFFIXES = ("a", "e")  # Promotion's advanced (rank 2) and elite (rank 3) templates


def rank_plan(names, read, assets, per_row, new):
    """{roster template id: (advanced model id or None, elite model id or None)}.

    A roster row is the basic-rank `_b` template; 0 A.D. promotes it to its
    `_a` and `_e` templates (Promotion/Entity), which mostly name their own
    actor. Each rank whose actor differs from the row's gets a model, planned
    like a roster template's (reusing a model with the same actor) and added
    to `new`."""
    by_actor = {}
    for aid, spec in assets.items():
        by_actor.setdefault(spec[0], []).append(aid)
    for mid, (_code, _kind, path) in new.items():
        by_actor.setdefault(path, []).append(mid)
    ranks = {}
    for (_i, tid, code, kind), row_model in zip(roster_rows(), per_row):
        # An advanced (_a) row, trained at that rank, still promotes to elite.
        if not tid.endswith(("_b", "_a")) or kind is None:
            continue
        base = template_actor(names, read, tid)
        got = []
        for suffix in RANK_SUFFIXES:
            rtid = tid[:-1] + suffix
            if rtid == tid:
                got.append(None)
                continue
            actor = template_actor(names, read, rtid)
            path = f"art/actors/{actor}" if actor else None
            if not actor or actor == base or path not in names:
                got.append(None)
                continue
            # Reuse the civ's class model or a template model with this actor; the
            # world view draws either (another civ's class model it cannot).
            usable = set(new) | {m for m in per_row if m} | {f"{code}_{kind}"}
            hits = [h for h in by_actor.get(path, []) if h in usable]
            pick = next((h for h in hits if h.startswith(code + "_")), None) or (hits[0] if hits else None)
            if not pick:
                pick = template_model_id(rtid)
                if pick in assets or pick in new:
                    pick += "_" + os.path.splitext(os.path.basename(path))[0]
                new[pick] = (code, mount_kind(path, kind), path)
                by_actor.setdefault(path, []).append(pick)
            got.append(pick if pick != row_model else None)
        if any(got):
            ranks[tid] = tuple(got)
    return ranks


def rank_header_lines(ranks):
    """The advanced/elite table write_header appends (models by id, so rows
    can be spliced in by hand: each id must also be in kDraw)."""
    lines = [
        "// The model an advanced (rank 2) or elite (rank 3) unit of a roster template",
        "// draws with: 0 A.D. promotes a `_b` template to its `_a` and `_e` templates,",
        "// which mostly have their own actor (tools/unit_template_models.py rank_plan).",
        "struct UnitTemplateRankModel {",
        "\tconst char *tmpl; // the roster template",
        "\tconst char *advanced; // its _a template's unit_template_draw_model or civ class model, or nullptr",
        "\tconst char *elite; // its _e template's, or nullptr",
        "};",
        "",
        f"inline constexpr int UNIT_TEMPLATE_RANK_MODEL_COUNT = {len(ranks)};",
        "",
        "inline const UnitTemplateRankModel *unit_template_rank_models() {",
        "\tstatic const UnitTemplateRankModel kRank[UNIT_TEMPLATE_RANK_MODEL_COUNT + 1] = {",
    ]
    q = lambda m: f'"{m}"' if m else "nullptr"
    for tid, (adv, eli) in sorted(ranks.items()):
        lines.append(f'\t\t{{"{tid}", {q(adv)}, {q(eli)}}},')
    lines += [
        "\t\t{nullptr, nullptr, nullptr},",
        "\t};",
        "\treturn kRank;",
        "}",
        "",
        "// The unit_template_draw_model a roster template's unit draws with at `rank`",
        "// (Promotion: 2 advanced, 3 elite), UNIT_TEMPLATE_RANK_CLASS_MODEL for the civ's",
        "// model for its UnitType (a model not in kDraw), or -1 for its basic-rank model.",
        "inline constexpr int UNIT_TEMPLATE_RANK_CLASS_MODEL = -2;",
        "",
        "inline int unit_template_rank_draw_index(uint16_t tmpl, uint8_t rank) {",
        "\tif (rank < 2 || tmpl >= CIV_UNIT_TEMPLATE_COUNT) return -1;",
        "\tstatic const std::array<std::array<int16_t, 2>, CIV_UNIT_TEMPLATE_COUNT> kLookup = [] {",
        "\t\tstd::array<std::array<int16_t, 2>, CIV_UNIT_TEMPLATE_COUNT> t;",
        "\t\tfor (auto &r : t) r = {-1, -1};",
        "\t\tauto draw_index = [](const char *id) -> int16_t {",
        "\t\t\tif (!id) return -1;",
        "\t\t\tfor (int k = 0; k < UNIT_TEMPLATE_DRAW_MODEL_COUNT; ++k)",
        "\t\t\t\tif (std::strcmp(unit_template_draw_model(k), id) == 0) return (int16_t)k;",
        "\t\t\treturn (int16_t)UNIT_TEMPLATE_RANK_CLASS_MODEL;",
        "\t\t};",
        "\t\tfor (const UnitTemplateRankModel *r = unit_template_rank_models(); r->tmpl; ++r)",
        "\t\t\tfor (uint16_t i = 0; i < CIV_UNIT_TEMPLATE_COUNT; ++i)",
        "\t\t\t\tif (std::strcmp(civ_unit_template(i).id, r->tmpl) == 0) {",
        "\t\t\t\t\tt[i] = {draw_index(r->advanced), draw_index(r->elite)};",
        "\t\t\t\t\tbreak;",
        "\t\t\t\t}",
        "\t\treturn t;",
        "\t}();",
        "\treturn kLookup[tmpl][rank >= 3 ? 1 : 0];",
        "}",
        "",
    ]
    return lines


def write_header(per_row, models, path=HEADER, ranks=None):
    """`models`: template model id -> (class model id, clip donor or None);
    `ranks`: rank_plan's table, its models also in `models`."""
    lines = [
        "// Generated by tools/extract_0ad_assets.py --templates (tools/unit_template_models.py); do not edit.",
        "// One model per civ roster template: its 0 A.D. actor (CC BY-SA 3.0, Wildfire Games).",
        "#pragma once",
        "",
        "#include \"../../simulation/sim_types.h\"",
        "",
        "#include <array>",
        "#include <cstring>",
        "",
        "namespace rtso {",
        "",
    ]
    rows = roster_rows()
    # A row draws with its civ's class model unless its actor's model differs.
    draw = sorted({mid for (_i, _t, code, kind), mid in zip(rows, per_row)
                   if mid and mid != f"{code}_{kind}"}
                  | {m for pair in (ranks or {}).values() for m in pair if m and m in models})
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
    ]
    lines += rank_header_lines(ranks or {})
    lines += [
        f"static_assert(CIV_UNIT_TEMPLATE_COUNT == {len(rows)}, \"rerun tools/extract_0ad_assets.py --templates\");",
        "",
        "} // namespace rtso",
        "",
    ]
    with open(path, "w") as fh:
        fh.write("\n".join(lines))


def extend_header(path=HEADER):
    """Roster rows added after the header was written (extract_0ad_rosters.py --append, which
    never moves a row): give each -1, so it draws its owner civ's model for its UnitType, the
    model it drew before it had a row of its own, until extract_0ad_assets.py --templates plans
    one. Rows already in the header keep their entries. Returns the rows added."""
    rows = roster_rows()
    text = open(path).read()
    start = text.index("\tstatic const int16_t kIndex[CIV_UNIT_TEMPLATE_COUNT] = {\n")
    end = text.index("\t};\n", start)
    have = len(re.findall(r"^\t\t-?\d+, // ", text[start:end], re.M))
    if have > len(rows):
        raise SystemExit(f"{path} has {have} rows, the roster {len(rows)}: rerun extract_0ad_assets.py --templates")
    added = rows[have:]
    lines = "".join(f"\t\t-1, // {tid} (no model of its own: its owner civ's {kind} model)\n" for _i, tid, _c, kind in added)
    text = text[:end] + lines + text[end:]
    text = re.sub(r"static_assert\(CIV_UNIT_TEMPLATE_COUNT == \d+,",
                  f"static_assert(CIV_UNIT_TEMPLATE_COUNT == {len(rows)},", text)
    with open(path, "w") as fh:
        fh.write(text)
    return added


if __name__ == "__main__":
    import sys
    if sys.argv[1:] != ["--extend"]:
        raise SystemExit("usage: unit_template_models.py --extend  (the full table: extract_0ad_assets.py --templates)")
    print(f"{len(extend_header())} roster rows added to {os.path.relpath(HEADER, REPO)} without a model of their own")
