// Formation animation variant tests (src/presentation/formation_variants.h): the port of
// Formation.js's AnimationVariants parsing and GetFormationAnimationVariant, run on the
// 0.28 phalanx, syntagma, testudo and anti-cavalry tables and on laid-out slots.
//
// g++ -std=c++20 -O2 -Isrc tools/test_formation_variants.cpp -o /tmp/test_formation_variants
#include "presentation/formation_variants.h"
#include "simulation/formation.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

using namespace rtso;
using namespace rtso::formvar;

static int failures = 0;
#define CHECK(cond, msg)                                              \
	do {                                                              \
		if (!(cond)) {                                                \
			std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
			++failures;                                               \
		} else {                                                      \
			std::printf("ok   %s\n", msg);                            \
		}                                                             \
	} while (0)

struct FakeController {
	uint8_t tmpl = FORMATION_NONE;
	uint8_t columnar = 0;
	uint8_t has_offsets = 1;
	std::vector<FormationSlot> slots;
};

static std::vector<FormationMember> members(int n) {
	std::vector<FormationMember> m;
	for (int i = 0; i < n; ++i)
		m.push_back(FormationMember{(uint32_t)i + 1, FCLASS_INFANTRY | FCLASS_MELEE, (float)(i % 4), (float)(i / 4), 1.0f, 1.0f});
	return m;
}

static std::map<uint32_t, uint8_t> variants_of(const FakeController &fc) {
	std::map<uint32_t, uint8_t> out;
	for_each_member_variant(fc, [&](uint32_t ent, uint8_t v) { out[ent] = v; });
	return out;
}

int main() {
	// Parsing: the tables of the four 0.28 templates that have one.
	{
		const auto &ph = formation_rects(FORMATION_PHALANX);
		CHECK(ph.size() == 3 && ph[0].variant == FV_PHALANX_FRONT && ph[2].variant == FV_PHALANX_BACK, "phalanx: three rectangles");
		CHECK(ph[2].min_row == 3 && ph[2].max_row == -1 && ph[2].min_column == 1 && ph[2].max_column == -1, "phalanx back: rows 3..-1, all columns");
		const auto &sy = formation_rects(FORMATION_SYNTAGMA);
		CHECK(sy.size() == 6 && sy[5].variant == FV_SYNTAGMA_BACK && sy[5].min_row == 7, "syntagma: six rectangles, back from row 7");
		CHECK(std::isnan(sy[5].max_row), "syntagma's '7...-1' reads its last row as NaN, as +'.-1' does in JS");
		const auto &te = formation_rects(FORMATION_TESTUDO);
		// 16 rows of 3 entries, row 15's first swallowed by row 14's ':'.
		CHECK(te.size() == 47, "testudo: 47 rectangles");
		CHECK(te[41].min_row == 14 && te[41].min_column == 1 && te[41].variant == FV_TESTUDO_LEFT, "testudo row 14 left edge keeps its own variant");
		CHECK(te[42].min_row == 15 && te[42].min_column == 8, "testudo row 15 left edge entry is dropped");
		CHECK(formation_rects(FORMATION_ANTI_CAVALRY).size() == 2, "anti-cavalry: front and back");
		CHECK(formation_rects(FORMATION_BOX).empty() && formation_rects(FORMATION_LINE).empty(), "other formations have none");
		bool named = true;
		for (uint8_t f = 0; f < FORMATION_COUNT; ++f)
			for (const Rect &r : formation_rects(f)) named = named && r.variant != FV_NONE;
		CHECK(named, "every rectangle names a known variant");
	}
	// GetFormationAnimationVariant on hand-made positions.
	{
		const auto &ph = formation_rects(FORMATION_PHALANX);
		CHECK(member_variant(ph, 1, 5, 4, 8) == FV_PHALANX_FRONT, "phalanx row 1: front");
		CHECK(member_variant(ph, 2, 1, 4, 8) == FV_PHALANX_MID, "phalanx row 2: mid");
		CHECK(member_variant(ph, 4, 8, 4, 8) == FV_PHALANX_BACK, "phalanx last row: back");
		const auto &sy = formation_rects(FORMATION_SYNTAGMA);
		CHECK(member_variant(sy, 2, 3, 9, 8) == FV_SYNTAGMA_FRONT, "syntagma rows 1-2: front");
		CHECK(member_variant(sy, 6, 3, 9, 8) == FV_SYNTAGMA_04, "syntagma row 6: 04");
		CHECK(member_variant(sy, 9, 3, 9, 8) == FV_SYNTAGMA_BACK, "syntagma row 9: back");
		const auto &te = formation_rects(FORMATION_TESTUDO);
		CHECK(member_variant(te, 1, 1, 4, 8) == FV_TESTUDO_FRONT_LEFT, "testudo front left corner");
		CHECK(member_variant(te, 1, 8, 4, 8) == FV_TESTUDO_FRONT_RIGHT, "testudo front right corner");
		CHECK(member_variant(te, 2, 4, 4, 8) == FV_TESTUDO_TOP, "testudo row 2 inside: top");
		CHECK(member_variant(te, 3, 4, 4, 8) == FV_TESTUDO_FRONT, "testudo row 3 inside: front, as 0.28 writes it");
		CHECK(member_variant(te, 15, 1, 16, 8) == FV_NONE, "testudo row 15 left edge: no variant");
		CHECK(member_variant(te, 17, 4, 17, 8) == FV_NONE, "testudo row 17: no variant");
	}
	// Laid-out formations (Formation.js ComputeFormationOffsets port).
	{
		FormationRng rng{7};
		uint8_t rows = 0;
		FakeController te;
		te.tmpl = FORMATION_TESTUDO;
		te.slots = compute_formation_offsets(formation_template(FORMATION_TESTUDO), false, members(16), 0, 0, 0, rng, &rows);
		auto v = variants_of(te);
		int tops = 0, fronts = 0, corners = 0;
		for (auto &kv : v) {
			tops += kv.second == FV_TESTUDO_TOP;
			fronts += kv.second == FV_TESTUDO_FRONT;
			corners += kv.second == FV_TESTUDO_FRONT_LEFT || kv.second == FV_TESTUDO_FRONT_RIGHT;
		}
		CHECK(rows == 2 && v.size() == 16, "testudo of 16: two rows of 8, every member has a variant");
		CHECK(corners == 2 && fronts == 6 && tops == 6, "testudo of 16: 2 front corners, 6 front, 6 top, 2 sides");
		te.columnar = 1;
		CHECK(variants_of(te).empty(), "a testudo marching in column has none");
		te.columnar = 0;
		te.has_offsets = 0;
		CHECK(variants_of(te).empty(), "nor one whose slots are not laid out");

		FakeController ph;
		ph.tmpl = FORMATION_PHALANX;
		ph.slots = compute_formation_offsets(formation_template(FORMATION_PHALANX), false, members(20), 0, 0, 0, rng, &rows);
		auto pv = variants_of(ph);
		int front = 0, mid = 0, back = 0;
		for (auto &kv : pv) {
			front += kv.second == FV_PHALANX_FRONT;
			mid += kv.second == FV_PHALANX_MID;
			back += kv.second == FV_PHALANX_BACK;
		}
		std::printf("     phalanx of 20: %d rows, %d front, %d mid, %d back\n", rows, front, mid, back);
		CHECK(pv.size() == 20 && front > 0 && mid > 0 && back > 0, "phalanx of 20: front, mid and back ranks");

		FakeController box;
		box.tmpl = FORMATION_BOX;
		box.slots = compute_formation_offsets(formation_template(FORMATION_BOX), false, members(9), 0, 0, 0, rng, &rows);
		CHECK(variants_of(box).empty(), "a box has no variants");
	}
	// Each variant names its clips.
	{
		bool ok = true;
		for (uint8_t v = 1; v < FV_COUNT; ++v) ok = ok && variant_clips(v).idle && variant_clips(v).walk;
		CHECK(ok, "every variant has an idle and a walk clip");
		CHECK(variant_clips(FV_TESTUDO_TOP).drops_weapon && !variant_clips(FV_TESTUDO_FRONT).drops_weapon, "only testudo_top sheathes the sword");
	}
	std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}
