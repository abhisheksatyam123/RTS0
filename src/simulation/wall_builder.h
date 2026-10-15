#pragma once
// rmgen-common wall_builder.js (0.28): the wall styles random maps lay (g_WallStyles), their
// elements (getWallElement) and how they line up one after the next (getWallAlignment). The
// placers that spawn them are Simulation members (place_wall, place_linear_wall, ...).
//
// Units are wall_builder's: lengths and indents in terrain tiles (one tile is one RTS0 unit),
// angles in radians as rmgen counts them (0 faces +x, growing counter-clockwise; the entity's
// Position angle is pi/2 less, RandomMap.exportEntityList).
#include "sim_types.h"
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace rtso {

// g_WallStyles (loadWallsetsFromCivData), in its key order: the civs as rmgen's g_CivData lists
// them (loadCivFiles reads simulation/data/civs/*.json in file name order: athen, brit, cart,
// gaul, germ, han, iber, kush, mace, maur, pers, ptol, rome, sele, spart), each civ's WallSets in
// its JSON's order, a style only the first time it comes (structures/wallset_palisade is
// "palisade", structures/{civ}/wallset_{x} "{civ}/{x}"). Han list no palisade, so Han's bamboo
// stockade is not a style.
enum WallStyle : uint8_t {
	WALL_STYLE_PALISADE,    // structures/wallset_palisade (athen.json lists it first)
	WALL_STYLE_ATHEN_STONE,
	WALL_STYLE_BRIT_STONE,
	WALL_STYLE_CART_SHORT,  // cart/wallset_short, the Low Wall (s_wall_*)
	WALL_STYLE_CART_STONE,
	WALL_STYLE_GAUL_STONE,
	WALL_STYLE_GERM_STONE,
	WALL_STYLE_HAN_STONE,
	WALL_STYLE_IBER_STONE,
	WALL_STYLE_KUSH_STONE,
	WALL_STYLE_MACE_STONE,
	WALL_STYLE_MAUR_STONE,
	WALL_STYLE_PERS_STONE,
	WALL_STYLE_PTOL_STONE,
	WALL_STYLE_ROME_STONE,
	WALL_STYLE_ROME_SIEGE,  // rome/wallset_siege
	WALL_STYLE_SELE_STONE,
	WALL_STYLE_SPART_STONE,
	WALL_STYLE_COUNT
};

// A style's templates, in metres: WallPiece/Length of its Tower, Gate and WallLong/Medium/Short,
// its MinTowerOverlap, and its Fort (WallSet/Templates/Fort, else structures/{civ}/fortress as
// GetTemplateDataHelper falls back) with that template's WallPiece Length and Indent.
struct WallStyleInfo {
	const char *key;        // the g_WallStyles key ("athen/stone")
	uint8_t civ;            // the pieces' template civ (palisades are Gaia's)
	uint8_t structure_civ;  // the civ of the "structures/{civ}/X" elements (g_CivData[civ] or its first civ)
	bool civ_style;         // the style's prefix is a civ (entryTower: its defense_tower, else palisades_watchtower)
	uint8_t kind;           // the WallSetKind RTS0 lays its pieces as
	double tower, gate, lengths[3], min_overlap;
	uint8_t fort_type;      // the Fort's BuildingType
	double fort_length, fort_indent;
	bool palisade_extras;   // WallEnd (palisades_end) and WallCurves (palisades_curve)
};

inline const WallStyleInfo &wall_style_info(uint8_t style) {
	// structures/{civ}/wall_tower, wall_gate, wall_long/medium/short and fortress WallPiece values;
	// wallset_stone.xml MinTowerOverlap (template_wallset's 0.05 where it has none).
	static const WallStyleInfo kStyles[WALL_STYLE_COUNT] = {
		{"palisade", CIV_GAIA, CIV_ATHENIANS, false, WALLSET_PALISADE, 3, 14, {14, 9, 4}, 0.05, BLD_SENTRY_TOWER, 8, 0, true},
		{"athen/stone", CIV_ATHENIANS, CIV_ATHENIANS, true, WALLSET_STONE, 7, 36, {36, 24, 12}, 0.05, BLD_FORTRESS, 24, 7.6, false},
		{"brit/stone", CIV_BRITONS, CIV_BRITONS, true, WALLSET_STONE, 10, 36, {36, 24, 12}, 0.05, BLD_FORTRESS, 28, 0, false},
		// s_wall_tower 3, s_wall_gate 14, s_wall_long/medium/short 14, 9, 4; no Fort, so cart/fortress.
		{"cart/short", CIV_CARTHAGINIANS, CIV_CARTHAGINIANS, true, WALLSET_PALISADE, 3, 14, {14, 9, 4}, 0.05, BLD_FORTRESS, 25, 6.4, false},
		{"cart/stone", CIV_CARTHAGINIANS, CIV_CARTHAGINIANS, true, WALLSET_STONE, 11, 36, {36, 24, 12}, 0.05, BLD_FORTRESS, 25, 6.4, false},
		{"gaul/stone", CIV_GAULS, CIV_GAULS, true, WALLSET_STONE, 8, 36, {36, 24, 12}, 0.05, BLD_FORTRESS, 16.8, 5.8, false},
		{"germ/stone", CIV_GERMANS, CIV_GERMANS, true, WALLSET_STONE, 9, 37.5, {36, 24, 12}, 0.05, BLD_FORTRESS, 28, 0, false},
		{"han/stone", CIV_HAN, CIV_HAN, true, WALLSET_STONE, 10, 36, {36, 24, 12}, 0.10, BLD_FORTRESS, 34, 0, false},
		{"iber/stone", CIV_IBERIANS, CIV_IBERIANS, true, WALLSET_STONE, 10, 36, {36, 24, 12}, 0.20, BLD_FORTRESS, 24.5, 0, false},
		{"kush/stone", CIV_KUSHITES, CIV_KUSHITES, true, WALLSET_STONE, 8, 36, {36, 24, 12}, 0.05, BLD_FORTRESS, 28.5, 6.7, false},
		{"mace/stone", CIV_MACEDONIANS, CIV_MACEDONIANS, true, WALLSET_STONE, 7, 36, {36, 24, 12}, 0.05, BLD_FORTRESS, 24, 7.6, false},
		{"maur/stone", CIV_MAURYANS, CIV_MAURYANS, true, WALLSET_STONE, 7, 36, {36, 24, 12}, 0.05, BLD_FORTRESS, 22, 0, false},
		{"pers/stone", CIV_PERSIANS, CIV_PERSIANS, true, WALLSET_STONE, 7, 36, {36, 24, 12}, 0.05, BLD_FORTRESS, 24.5, 7.6, false},
		{"ptol/stone", CIV_PTOLEMIES, CIV_PTOLEMIES, true, WALLSET_STONE, 9, 39, {38, 25, 15}, 0.15, BLD_FORTRESS, 24, 8.7, false},
		{"rome/stone", CIV_ROMANS, CIV_ROMANS, true, WALLSET_STONE, 10, 36, {36, 24, 12}, 0.10, BLD_FORTRESS, 25, 8.4, false},
		// siege_wall_tower 6, siege_wall_gate 36, siege_wall_long/medium/short 36, 24, 12; Fort rome/army_camp.
		{"rome/siege", CIV_ROMANS, CIV_ROMANS, true, WALLSET_SIEGE, 6, 36, {36, 24, 12}, 0.05, BLD_ARMY_CAMP, 29.5, 8, false},
		{"sele/stone", CIV_SELEUCIDS, CIV_SELEUCIDS, true, WALLSET_STONE, 7, 34, {34, 21, 11}, 0.05, BLD_FORTRESS, 23, 7.8, false},
		{"spart/stone", CIV_SPARTANS, CIV_SPARTANS, true, WALLSET_STONE, 7, 36, {36, 24, 12}, 0.05, BLD_FORTRESS, 24, 7.6, false},
	};
	return kStyles[style < WALL_STYLE_COUNT ? style : (uint8_t)WALL_STYLE_PALISADE];
}

// The style a g_WallStyles key names, WALL_STYLE_COUNT for none.
inline uint8_t wall_style_by_key(std::string_view key) {
	for (uint8_t s = 0; s < WALL_STYLE_COUNT; ++s)
		if (key == wall_style_info(s).key) return s;
	return WALL_STYLE_COUNT;
}

// validateStyle for a player: "{civ}/stone" of the player's civ, else the first style.
inline uint8_t wall_style_of_civ(uint8_t civ) {
	for (uint8_t s = 0; s < WALL_STYLE_COUNT; ++s) {
		const WallStyleInfo &w = wall_style_info(s);
		if (w.civ == civ && w.kind == WALLSET_STONE) return s;
	}
	return WALL_STYLE_PALISADE;
}

// getWallElement's element strings: long, medium, short, start, end, cornerIn, cornerOut, tower,
// fort, gate, entry, entryTower, entryFort, gap_{x}, turn_{x}, and any other name a
// structures/{civ}/{name} set into the wall (a house, barracks, outpost, ...).
enum WallElementType : uint8_t {
	WALL_EL_LONG, WALL_EL_MEDIUM, WALL_EL_SHORT, WALL_EL_START, WALL_EL_END, WALL_EL_CORNER_IN, WALL_EL_CORNER_OUT,
	WALL_EL_TOWER, WALL_EL_FORT, WALL_EL_GATE, WALL_EL_ENTRY, WALL_EL_ENTRY_TOWER, WALL_EL_ENTRY_FORT, WALL_EL_GAP,
	WALL_EL_TURN, WALL_EL_STRUCTURE, WALL_EL_UNKNOWN,
};

struct WallElementName {
	uint8_t type = WALL_EL_TOWER;   // WallElementType
	uint8_t building = BLD_TYPE_COUNT; // a WALL_EL_STRUCTURE's BuildingType
	bool outward = false;           // a WALL_EL_STRUCTURE set outside the wall ("outpost", "*_tower")
	double value = 0.0;             // gap_{x}'s length, turn_{x}'s fraction of pi
};

// The structures/{civ}/{name} templates RTS0 has a BuildingType for.
inline uint8_t wall_structure_building(std::string_view name) {
	static const struct { const char *name; uint8_t type; } kNames[] = {
		{"house", BLD_HOUSE}, {"barracks", BLD_BARRACKS}, {"outpost", BLD_OUTPOST}, {"defense_tower", BLD_TOWER},
		{"sentry_tower", BLD_SENTRY_TOWER}, {"fortress", BLD_FORTRESS}, {"stable", BLD_STABLE}, {"range", BLD_RANGE},
		{"temple", BLD_TEMPLE}, {"market", BLD_MARKET}, {"storehouse", BLD_STOREHOUSE}, {"farmstead", BLD_FARMSTEAD},
		{"forge", BLD_FORGE}, {"arsenal", BLD_ARSENAL}, {"civil_centre", BLD_TOWN_CENTER}, {"corral", BLD_CORRAL},
		{"wonder", BLD_WONDER}, {"dock", BLD_DOCK},
	};
	for (const auto &n : kNames)
		if (name == n.name) return n.type;
	return BLD_TYPE_COUNT;
}

inline WallElementName wall_element_name(std::string_view s) {
	static const struct { const char *name; uint8_t type; } kNames[] = {
		{"long", WALL_EL_LONG}, {"medium", WALL_EL_MEDIUM}, {"short", WALL_EL_SHORT}, {"start", WALL_EL_START},
		{"end", WALL_EL_END}, {"cornerIn", WALL_EL_CORNER_IN}, {"cornerOut", WALL_EL_CORNER_OUT}, {"tower", WALL_EL_TOWER},
		{"fort", WALL_EL_FORT}, {"gate", WALL_EL_GATE}, {"entry", WALL_EL_ENTRY}, {"entryTower", WALL_EL_ENTRY_TOWER},
		{"entryFort", WALL_EL_ENTRY_FORT},
	};
	WallElementName n;
	for (const auto &k : kNames)
		if (s == k.name) {
			n.type = k.type;
			return n;
		}
	auto number = [](std::string_view v) {
		double d = 0.0;
#if defined(_LIBCPP_VERSION) && !defined(__cpp_lib_to_chars)
		// Older libc++ (Android NDK, Apple) has no floating from_chars; strtod is
		// correctly rounded too, and native code runs in the "C" locale.
		std::string z(v);
		d = std::strtod(z.c_str(), nullptr);
#else
		std::from_chars(v.data(), v.data() + v.size(), d);
#endif
		return d;
	};
	if (s.starts_with("gap_")) {
		n.type = WALL_EL_GAP;
		n.value = number(s.substr(4));
	} else if (s.starts_with("turn_")) {
		n.type = WALL_EL_TURN;
		n.value = number(s.substr(5));
	} else if ((n.building = wall_structure_building(s)) != BLD_TYPE_COUNT) {
		n.type = WALL_EL_STRUCTURE;
		n.outward = s == "outpost" || s.ends_with("_tower");
	} else {
		n.type = WALL_EL_UNKNOWN; // "Unrecognized wall element": a tower
	}
	return n;
}

inline std::vector<WallElementName> wall_element_names(std::initializer_list<const char *> names) {
	std::vector<WallElementName> out;
	out.reserve(names.size());
	for (const char *n : names) out.push_back(wall_element_name(n));
	return out;
}

// What RTS0 lays for an element: nothing (no templateName), a WallSet piece of the style's kind
// or a structure.
enum WallPieceKind : uint8_t {
	WALL_PIECE_NONE, WALL_PIECE_SEGMENT, WALL_PIECE_TOWER, WALL_PIECE_GATE,
	WALL_PIECE_END,       // palisades_end, a post across the wall's end
	WALL_PIECE_CURVE,     // palisades_curve, its obstruction's long side across its Position angle
	WALL_PIECE_STRUCTURE, // a building (a fort, a house, a defense tower)
};

// An element as getWallElement returns it (angle, length, indent and bend in tiles and radians),
// with the piece RTS0 lays for its templateName.
template <class T>
struct WallElementT {
	T angle = 0, length = 0, indent = 0, bend = 0;
	uint8_t piece = WALL_PIECE_NONE;
	uint8_t building = BLD_TYPE_COUNT;
	uint8_t civ = CIV_GAIA;
	T half_w = 0; // a WallSet piece's half length (RTS0 units)
	bool placed() const { return piece != WALL_PIECE_NONE; }
};
using WallElement = WallElementT<double>;

template <class T> constexpr T kWallPi = (T)3.14159265358979323846;
// A quarter turn. The float one is the 1.5707963f place_custom_fortress has always laid its
// fortresses with, so their pieces stay where they were.
template <class T> constexpr T kWallQuarter = (T)1.5707963267948966;
template <> inline constexpr float kWallQuarter<float> = 1.5707963f;

// GetTemplateDataHelper's WallPiece angle default (Orientation 1, pi), in tiles (TERRAIN_TILE_SIZE 4 m).
template <class T>
WallElementT<T> wall_template_element(uint8_t piece, uint8_t building, uint8_t civ, double length_m, double indent_m = 0.0,
		double orientation = 1.0, double bend = 0.0) {
	WallElementT<T> e;
	e.angle = (T)orientation * kWallPi<T>;
	e.length = (T)length_m / (T)4;
	e.indent = (T)indent_m / (T)4;
	e.bend = (T)bend * kWallPi<T>;
	e.piece = piece;
	e.building = building;
	e.civ = civ;
	e.half_w = (T)0.5 * e.length;
	return e;
}

// getWallElement(element, style).
template <class T>
WallElementT<T> wall_element(uint8_t style, const WallElementName &n) {
	const WallStyleInfo &s = wall_style_info(style);
	const uint8_t seg = s.kind == WALLSET_PALISADE ? (uint8_t)BLD_PALISADE : s.kind == WALLSET_SIEGE ? (uint8_t)BLD_SIEGE_WALL : (uint8_t)BLD_WALL_SEGMENT;
	const uint8_t gate = s.kind == WALLSET_PALISADE ? (uint8_t)BLD_PALISADE_GATE : s.kind == WALLSET_SIEGE ? (uint8_t)BLD_SIEGE_WALL_GATE : (uint8_t)BLD_GATE;
	const uint8_t tower_type = s.kind == WALLSET_STONE ? (uint8_t)BLD_WALL_TOWER : seg;
	const T tower_half = s.kind == WALLSET_PALISADE ? (T)PALISADE_TOWER_HALF : s.kind == WALLSET_SIEGE ? (T)SIEGE_WALL_TOWER_HALF : (T)1;
	WallElementT<T> tower = wall_template_element<T>(WALL_PIECE_TOWER, tower_type, s.civ, s.tower);
	tower.half_w = tower_half;
	const WallElementT<T> gate_el = wall_template_element<T>(WALL_PIECE_GATE, gate, s.civ, s.gate);
	// palisades_end (Orientation 1.5, Length 0.8, Obstruction 1.5 m across the wall) and
	// palisades_curve (Orientation 0.75, Length 8, Indent 2.8, Bend 0.5, Obstruction 3 by 9 m).
	WallElementT<T> end_el = wall_template_element<T>(WALL_PIECE_END, BLD_PALISADE, s.civ, 0.8, 0.0, 1.5);
	end_el.half_w = (T)(1.5 / 4.0 * 0.5);
	WallElementT<T> curve = wall_template_element<T>(WALL_PIECE_CURVE, BLD_PALISADE, s.civ, 8.0, 2.8, 0.75, 0.5);
	curve.half_w = (T)(9.0 / 4.0 * 0.5);
	const WallElementT<T> fort = wall_template_element<T>(WALL_PIECE_STRUCTURE, s.fort_type,
			s.fort_type == BLD_FORTRESS ? s.structure_civ : s.civ, s.fort_length, s.fort_indent);
	const T quarter = kWallQuarter<T>;
	WallElementT<T> ret = tower;
	switch (n.type) {
		case WALL_EL_LONG: case WALL_EL_MEDIUM: case WALL_EL_SHORT:
			return wall_template_element<T>(WALL_PIECE_SEGMENT, seg, s.civ, s.lengths[n.type - WALL_EL_LONG]);
		case WALL_EL_TOWER: return tower;
		case WALL_EL_GATE: return gate_el;
		case WALL_EL_FORT: return fort;
		case WALL_EL_CORNER_IN:
			if (s.palisade_extras) return curve; // the curve that bends a quarter
			ret.angle += kWallPi<T> / (T)4;
			ret.indent = ret.length / (T)4;
			ret.length = 0;
			ret.bend = quarter;
			return ret;
		case WALL_EL_CORNER_OUT:
			if (s.palisade_extras) {
				ret = curve;
				ret.angle += kWallPi<T> / (T)2;
				ret.indent -= ret.indent * (T)2;
			} else {
				ret.angle -= kWallPi<T> / (T)4;
				ret.indent = -ret.length / (T)4;
				ret.length = 0;
			}
			ret.bend = -quarter;
			return ret;
		case WALL_EL_ENTRY:
			ret.piece = WALL_PIECE_NONE;
			ret.length = gate_el.length;
			return ret;
		case WALL_EL_ENTRY_TOWER:
			// structures/{civ}/defense_tower, or palisades_watchtower for the palisade style.
			if (s.civ_style) {
				ret.piece = WALL_PIECE_STRUCTURE;
				ret.building = BLD_TOWER;
				ret.civ = s.structure_civ;
			} else {
				ret.civ = CIV_GAIA;
			}
			ret.indent = ret.length * (T)-3;
			ret.length = gate_el.length;
			return ret;
		case WALL_EL_ENTRY_FORT:
			ret = fort;
			ret.angle -= kWallPi<T>;
			ret.length *= (T)1.5;
			ret.indent = ret.length;
			return ret;
		case WALL_EL_START:
			if (s.palisade_extras) {
				ret = end_el;
				ret.angle += kWallPi<T>;
			}
			return ret;
		case WALL_EL_END:
			return s.palisade_extras ? end_el : ret;
		case WALL_EL_GAP:
			ret.piece = WALL_PIECE_NONE;
			ret.angle = 0;
			ret.length = (T)n.value;
			return ret;
		case WALL_EL_TURN:
			ret.piece = WALL_PIECE_NONE;
			ret.bend = (T)n.value * kWallPi<T>;
			ret.length = 0;
			return ret;
		case WALL_EL_STRUCTURE:
			// structures/{civ}/{name}: no length, 3.5 towers inside the wall line (an outpost or a
			// *_tower 3 outside).
			ret.indent = ret.length * (n.outward ? (T)-3 : (T)3.5);
			ret.piece = WALL_PIECE_STRUCTURE;
			ret.building = n.building;
			ret.civ = s.structure_civ;
			ret.length = 0;
			return ret;
		default:
			return ret; // "Unrecognized wall element": a tower
	}
}

// The style's overlap: MinTowerOverlap times its tower's length.
template <class T>
T wall_overlap(uint8_t style) {
	return (T)wall_style_info(style).min_overlap * wall_element<T>(style, WallElementName{}).length;
}

// getWallLength: the sum of the elements' lengths less the overlap each (no bends).
template <class T>
T wall_length(uint8_t style, const std::vector<WallElementName> &wall) {
	const T overlap = wall_overlap<T>(style);
	T length = 0;
	for (const WallElementName &e : wall) length += wall_element<T>(style, e).length - overlap;
	return length;
}

// One element of getWallAlignment: where it stands, the wall's orientation there (before its own
// bend) and its angle (orientation + the element's angle).
template <class T>
struct WallAlignT {
	T x, y, orientation, angle;
	WallElementT<T> element;
};

// getWallAlignment(position, wall, style, orientation) for elements already resolved: each one
// set back by its indent, the next a half of each length on (less the overlap), turned by the
// element's bend. With 0.28's Vector2D: rotate(a) turns clockwise, perpendicular() a quarter
// counter-clockwise, so the wall runs along perpendicular(rotate((1, 0), -orientation)).
template <class T>
std::vector<WallAlignT<T>> wall_alignment(T x, T y, const std::vector<WallElementT<T>> &wall, T overlap, T orientation) {
	struct V { T x, z; };
	auto rot = [](V v, T a) { return V{v.x * std::cos(a) + v.z * std::sin(a), -v.x * std::sin(a) + v.z * std::cos(a)}; };
	const size_t n = wall.size();
	std::vector<WallAlignT<T>> al(n);
	V pos{x, y};
	T o = orientation;
	for (size_t i = 0; i < n; ++i) {
		const WallElementT<T> &e = wall[i];
		const V in = rot({e.indent, (T)0}, -o);
		al[i] = {pos.x - in.x, pos.z - in.z, o, o + e.angle, e};
		if (i + 1 == n) break;
		o += e.bend;
		T d = (T)0.5 * (e.length + wall[i + 1].length) - overlap;
		if (e.bend != (T)0 && e.indent != (T)0) {
			d += e.indent * std::sin(e.bend); // indent correction to adjust the distance
			const V back = rot({e.indent, (T)0}, -o);
			pos = {pos.x + back.x, pos.z + back.z};
		}
		const V step = rot({d, (T)0}, -o);
		pos = {pos.x - step.z, pos.z + step.x};
	}
	return al;
}

template <class T>
std::vector<WallElementT<T>> wall_elements(uint8_t style, const std::vector<WallElementName> &wall) {
	std::vector<WallElementT<T>> out;
	out.reserve(wall.size());
	for (const WallElementName &e : wall) out.push_back(wall_element<T>(style, e));
	return out;
}

// createDefaultFortressTypes: "tiny", "small", "medium", "normal", "large", "veryLarge" and
// "giant" (a quarter of the outline four times), and each one's "...Palisades" with a tower
// between any two of its walls, ends and corners. Empty for any other name.
inline std::vector<WallElementName> wall_fortress_type(std::string_view type) {
	static const struct { const char *type; const char *quarter[14]; } kTypes[] = {
		{"tiny", {"gate", "tower", "short", "cornerIn", "short", "tower"}},
		{"small", {"gate", "tower", "medium", "cornerIn", "medium", "tower"}},
		{"medium", {"gate", "tower", "long", "cornerIn", "long", "tower"}},
		{"normal", {"gate", "tower", "medium", "cornerIn", "medium", "cornerOut", "medium", "cornerIn", "medium", "tower"}},
		{"large", {"gate", "tower", "long", "cornerIn", "long", "cornerOut", "long", "cornerIn", "long", "tower"}},
		{"veryLarge", {"gate", "tower", "medium", "cornerIn", "medium", "cornerOut", "long", "cornerIn", "long", "cornerOut",
				"medium", "cornerIn", "medium", "tower"}},
		{"giant", {"gate", "tower", "long", "cornerIn", "long", "cornerOut", "long", "cornerIn", "long", "cornerOut", "long",
				"cornerIn", "long", "tower"}},
	};
	const bool palisades = type.ends_with("Palisades");
	const std::string_view base = palisades ? type.substr(0, type.size() - 9) : type;
	for (const auto &k : kTypes) {
		if (base != k.type) continue;
		std::vector<WallElementName> quarter, wall;
		for (const char *e : k.quarter)
			if (e) quarter.push_back(wall_element_name(e));
		for (int r = 0; r < 4; ++r) wall.insert(wall.end(), quarter.begin(), quarter.end());
		if (!palisades) return wall;
		auto fills = [](const WallElementName &e) {
			return e.type == WALL_EL_SHORT || e.type == WALL_EL_MEDIUM || e.type == WALL_EL_LONG || e.type == WALL_EL_START ||
			       e.type == WALL_EL_END || e.type == WALL_EL_CORNER_IN || e.type == WALL_EL_CORNER_OUT;
		};
		std::vector<WallElementName> out;
		for (size_t j = 0; j < wall.size(); ++j) {
			out.push_back(wall[j]);
			if (j + 1 < wall.size() && fills(wall[j]) && fills(wall[j + 1])) out.push_back(WallElementName{});
		}
		return out;
	}
	return {};
}

} // namespace rtso
