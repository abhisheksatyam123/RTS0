// Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games).
//
// Formation animation variants (Formation.js GetFormationAnimationVariant). A
// formation template lists which actor variant each member slot takes, by row and
// column ("1..1,1..-1: phalanx_front; ..."); UnitAI's FORMATIONMEMBER state switches a
// member's actor to it while it stands or walks in formation, and back on leaving.
// The variants (art/variants/biped/formations/*.xml) swap the Idle, Walk and Run
// animations; an actor that does not list the variant keeps its own.
#ifndef RTSO_FORMATION_VARIANTS_H
#define RTSO_FORMATION_VARIANTS_H

#include "../simulation/sim_types.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace rtso::formvar {

enum Variant : uint8_t {
	FV_NONE = 0,
	FV_ANTI_CAVALRY_BACK,
	FV_ANTI_CAVALRY_FRONT,
	FV_PHALANX_BACK,
	FV_PHALANX_FRONT,
	FV_PHALANX_MID,
	FV_SYNTAGMA_01,
	FV_SYNTAGMA_02,
	FV_SYNTAGMA_03,
	FV_SYNTAGMA_04,
	FV_SYNTAGMA_BACK,
	FV_SYNTAGMA_FRONT,
	FV_TESTUDO_FRONT,
	FV_TESTUDO_FRONT_LEFT,
	FV_TESTUDO_FRONT_RIGHT,
	FV_TESTUDO_LEFT,
	FV_TESTUDO_RIGHT,
	FV_TESTUDO_TOP,
	FV_COUNT
};
static_assert(FV_COUNT <= 32, "variant masks are 32-bit");

inline const char *variant_name(uint8_t v) {
	static const char *const kNames[FV_COUNT] = {
		"", "anti_cavalry_back", "anti_cavalry_front", "phalanx_back", "phalanx_front", "phalanx_mid",
		"syntagma_01", "syntagma_02", "syntagma_03", "syntagma_04", "syntagma_back", "syntagma_front",
		"testudo_front", "testudo_front_left", "testudo_front_right", "testudo_left", "testudo_right",
		"testudo_top",
	};
	return v < FV_COUNT ? kNames[v] : "";
}

inline uint8_t variant_by_name(const std::string &name) {
	for (uint8_t v = 1; v < FV_COUNT; ++v)
		if (name == variant_name(v)) return v;
	return FV_NONE;
}

// The animation files each variant names (art/variants/biped/formations/*.xml), as
// assets/animations PSA names. 0 A.D. reads animation names case-blind, so
// syntagma_back's "walk" and "run" are its Walk and Run. A variant without a Run
// clip (testudo, anti-cavalry) keeps walking with its Walk. Anti-cavalry's Attack
// never plays: members fight outside FORMATIONMEMBER, back on their own variant.
struct VariantClips {
	const char *idle;
	const char *walk;
	const char *run;
	bool drops_weapon; // testudo_top: <prop actor="" attachpoint="weapon_R"/>
};

inline const VariantClips &variant_clips(uint8_t v) {
	static const VariantClips kNone{nullptr, nullptr, nullptr, false};
	static const VariantClips kClips[FV_COUNT] = {
		kNone,
		{"biped_formations_anti_cavalry_back_idle", "biped_infantry_spearman_walk_relax", nullptr, false},
		{"biped_formations_anti_cavalry_front_idle", "biped_infantry_spearman_walk_relax", nullptr, false},
		{"biped_infantry_hoplite_idle_ready_back", "biped_infantry_hoplite_walk_ready", "biped_infantry_hoplite_jog_ready", false},
		{"biped_infantry_hoplite_idle_ready_front", "biped_infantry_hoplite_walk_ready_front", "biped_infantry_hoplite_run_ready", false},
		{"biped_infantry_hoplite_idle_ready_01", "biped_infantry_hoplite_walk_ready", "biped_infantry_hoplite_run_ready", false},
		{"biped_formations_syntagma_idle_ready_01", "biped_formations_syntagma_walk_ready_01", "biped_infantry_pikeman_run_ready", false},
		{"biped_formations_syntagma_idle_ready_02", "biped_formations_syntagma_walk_ready_02", "biped_infantry_pikeman_run_ready", false},
		{"biped_formations_syntagma_idle_ready_03", "biped_formations_syntagma_walk_ready_03", "biped_infantry_pikeman_run_ready", false},
		{"biped_formations_syntagma_idle_ready_04", "biped_formations_syntagma_walk_ready_04", "biped_infantry_pikeman_run_ready", false},
		{"biped_formations_syntagma_idle_ready_back", "biped_formations_syntagma_walk_relax_back", "biped_infantry_pikeman_jog_ready", false},
		{"biped_formations_syntagma_idle_ready_front", "biped_infantry_pikeman_walk_ready", "biped_infantry_pikeman_run_ready", false},
		{"biped_formations_testudo_front_idle", "biped_formations_testudo_front_walk", nullptr, false},
		{"biped_formations_testudo_front_left_idle", "biped_formations_testudo_front_left_walk", nullptr, false},
		{"biped_formations_testudo_front_right_idle", "biped_formations_testudo_front_right_walk", nullptr, false},
		{"biped_formations_testudo_left_idle", "biped_formations_testudo_left_walk", nullptr, false},
		{"biped_formations_testudo_right_idle", "biped_formations_testudo_right_walk", nullptr, false},
		{"biped_formations_testudo_top_idle", "biped_formations_testudo_top_walk", nullptr, true},
	};
	return v < FV_COUNT ? kClips[v] : kNone;
}

// Formation/AnimationVariants of the 0.28 templates (special/formations/*.xml), as
// written there (testudo's row 14 ends in ':' where ';' was meant, which Formation.js
// reads as dropping the rest of that entry).
inline const char *animation_variants(uint8_t formation) {
	switch (formation) {
		case FORMATION_PHALANX:
		case FORMATION_SHIELD_WALL: // shield_wall.xml: the phalanx variants
			return "1..1,1..-1: phalanx_front; 2..2,1..-1: phalanx_mid; 3..-1,1..-1: phalanx_back";
		case FORMATION_SYNTAGMA:
			return "1..2,1..-1: syntagma_front; 3..3,1..-1: syntagma_01; 4..4,1..-1: syntagma_02; "
				   "5..5,1..-1: syntagma_03; 6..6,1..-1: syntagma_04; 7...-1,1..-1: syntagma_back";
		case FORMATION_TESTUDO:
			return "1..1,2..7: testudo_front; 1..1,1..1: testudo_front_left; 1..1,8..8: testudo_front_right; "
				   "2..2,2..7: testudo_top; 2..2,8..8: testudo_right; 2..2,1..1: testudo_left; "
				   "3..3,1..1: testudo_top; 3..3,8..8: testudo_top; 3..3,2..7: testudo_front; "
				   "4..4,8..8: testudo_right; 4..4,2..7: testudo_top; 4..4,1..1: testudo_left; "
				   "5..5,1..1: testudo_top; 5..5,8..8: testudo_top; 5..5,2..7: testudo_front; "
				   "6..6,8..8: testudo_right; 6..6,2..7: testudo_top; 6..6,1..1: testudo_left; "
				   "7..7,1..1: testudo_top; 7..7,8..8: testudo_top; 7..7,2..7: testudo_front; "
				   "8..8,8..8: testudo_right; 8..8,2..7: testudo_top; 8..8,1..1: testudo_left; "
				   "9..9,1..1: testudo_top; 9..9,8..8: testudo_top; 9..9,2..7: testudo_front; "
				   "10..10,8..8: testudo_right; 10..10,2..7: testudo_top; 10..10,1..1: testudo_left; "
				   "11..11,1..1: testudo_top; 11..11,8..8: testudo_top; 11..11,2..7: testudo_front; "
				   "12..12,8..8: testudo_right; 12..12,2..7: testudo_top; 12..12,1..1: testudo_left; "
				   "13..13,1..1: testudo_top; 13..13,8..8: testudo_top; 13..13,2..7: testudo_front; "
				   "14..14,8..8: testudo_right; 14..14,2..7: testudo_top; 14..14,1..1: testudo_left: "
				   "15..15,1..1: testudo_top; 15..15,8..8: testudo_top; 15..15,2..7: testudo_front; "
				   "16..16,8..8: testudo_right; 16..16,2..7: testudo_top; 16..16,1..1: testudo_left";
		case FORMATION_ANTI_CAVALRY:
			return "1..1,1..-1: anti_cavalry_front; 2..2,1..-1: anti_cavalry_back";
		default:
			return nullptr;
	}
}

struct Rect {
	double min_row, max_row, min_column, max_column; // NaN where Formation.js's +"..." is NaN
	uint8_t variant;
};

namespace detail {
inline std::string trim(const std::string &s) {
	const size_t a = s.find_first_not_of(" \t\r\n");
	return a == std::string::npos ? std::string() : s.substr(a, s.find_last_not_of(" \t\r\n") + 1 - a);
}
// JS split(/\s*sep\s*/) on text whose ends are already trimmed: the pieces, trimmed.
inline std::vector<std::string> js_split(const std::string &s, const std::string &sep) {
	std::vector<std::string> out;
	size_t start = 0;
	for (;;) {
		const size_t at = s.find(sep, start);
		out.push_back(trim(s.substr(start, at == std::string::npos ? std::string::npos : at - start)));
		if (at == std::string::npos) break;
		start = at + sep.size();
	}
	return out;
}
// JS unary plus on a string: surrounding whitespace ignored, "" is 0, junk is NaN.
inline double js_number(const std::string &s) {
	const std::string t = trim(s);
	if (t.empty()) return 0.0;
	char *end = nullptr;
	const double v = std::strtod(t.c_str(), &end);
	return end && *end == '\0' ? v : NAN;
}
} // namespace detail

// Formation.js Init: the AnimationVariants text into rectangles, in order.
inline std::vector<Rect> parse_animation_variants(const char *text) {
	std::vector<Rect> out;
	if (!text || !*text) return out;
	for (const std::string &entry : detail::js_split(detail::trim(text), ";")) {
		const std::vector<std::string> parts = detail::js_split(entry, ":");
		const std::vector<std::string> rc = detail::js_split(parts[0], ",");
		const std::vector<std::string> rows = detail::js_split(rc[0], "..");
		const std::vector<std::string> cols = detail::js_split(rc.size() > 1 ? rc[1] : std::string(), "..");
		Rect r;
		r.min_row = detail::js_number(rows[0]);
		r.max_row = rows.size() > 1 ? detail::js_number(rows[1]) : NAN;
		r.min_column = detail::js_number(cols[0]);
		r.max_column = cols.size() > 1 ? detail::js_number(cols[1]) : NAN;
		r.variant = parts.size() > 1 ? variant_by_name(parts[1]) : FV_NONE;
		out.push_back(r);
	}
	return out;
}

inline const std::vector<Rect> &formation_rects(uint8_t formation) {
	static const std::vector<Rect> *kRects = [] {
		auto *t = new std::vector<Rect>[FORMATION_COUNT];
		for (uint8_t f = 0; f < FORMATION_COUNT; ++f) t[f] = parse_animation_variants(animation_variants(f));
		return t;
	}();
	static const std::vector<Rect> kEmpty;
	return formation < FORMATION_COUNT ? kRects[formation] : kEmpty;
}

// Formation.js GetFormationAnimationVariant for the member at 1-based (row, column),
// with the formation's row count and that row's column count (maxRowsUsed,
// maxColumnsUsed[row]). Comparisons against NaN never skip a rectangle, as in JS.
inline uint8_t member_variant(const std::vector<Rect> &rects, int row, int column, int rows_used, int columns_used) {
	for (const Rect &r : rects) {
		double min_row = r.min_row;
		if (min_row < 0) min_row += rows_used + 1;
		if (row < min_row) continue;
		double max_row = r.max_row;
		if (max_row < 0) max_row += rows_used + 1;
		if (row > max_row) continue;
		double min_column = r.min_column;
		if (min_column < 0) min_column += columns_used + 1;
		if (column < min_column) continue;
		double max_column = r.max_column;
		if (max_column < 0) max_column += columns_used + 1;
		if (column > max_column) continue;
		return r.variant;
	}
	return FV_NONE;
}

// Every member's variant in one formation controller (Simulation::FormationController):
// none while it marches in column (Formation.js this.columnar) or before its slots are
// laid out. Rows and columns used are the slots' largest, which is Formation.js's
// maxRowsUsed and, for every column range the 0.28 templates write, maxColumnsUsed.
template <class Controller, class Fn>
inline void for_each_member_variant(const Controller &fc, Fn &&fn) {
	if (!fc.has_offsets || fc.columnar) return;
	const std::vector<Rect> &rects = formation_rects(fc.tmpl);
	if (rects.empty() || fc.slots.empty()) return;
	int rows_used = 0;
	std::vector<int> columns_used;
	for (const auto &s : fc.slots) {
		rows_used = s.row > rows_used ? s.row : rows_used;
		if ((int)columns_used.size() <= s.row) columns_used.resize(s.row + 1, 0);
		if (s.column > columns_used[s.row]) columns_used[s.row] = s.column;
	}
	for (const auto &s : fc.slots) {
		const uint8_t v = member_variant(rects, s.row, s.column, rows_used, columns_used[s.row]);
		if (v != FV_NONE) fn(s.ent, v);
	}
}

} // namespace rtso::formvar

#endif
