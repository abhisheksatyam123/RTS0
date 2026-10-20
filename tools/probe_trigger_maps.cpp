// Headless probe of the maps that carry a trigger script, and of the scenario maps: loads or
// generates the map the way the game does (session_setup.cpp load_map_seeded for
// data/maps/*.json, generate_random_map for random maps), seats players, runs a few game
// minutes at FIXED_STEP and logs what the triggers do: timers that fire, notifications,
// dialogs, script-spawned entities (and whether they land off the map, in water, or never
// move), defeats, wins and the end of the match. Run it from the repository root.
//
//   g++ -std=c++20 -O2 -Isrc -c src/simulation/simulation.cpp -o /tmp/p3/sim.o   (and rms_generator.cpp -> rms.o)
//   g++ -std=c++20 -O2 -Isrc tools/probe_trigger_maps.cpp /tmp/p3/sim.o /tmp/p3/rms.o \
//       src/core/job_system.cpp src/network/turn_manager.cpp src/network/net_host.cpp -lpthread -o /tmp/p3/probe
//   /tmp/p3/probe --json treasure_islands --minutes 8
//   /tmp/p3/probe --random flood --players 2 --size 256 --minutes 6
//
// Options: --json NAME | --random MAPID, --minutes M, --seed S, --players N, --size TILES*4,
//   --humans MASK (bit p: player p+1 is an idle human; default 1 = player 1), --no-ai,
//   --victory MASK (override), --water MODE (0 shallow, 1 rising, 2 deep), --rise MIN,
//   --difficulty D (trigger difficulty 1-5), --nomad, --summary SECONDS, --quiet.
#include <map>
#include <set>
#define private public
#include "simulation/simulation.h"
#undef private
#include "simulation/rms_generator.h"
#include "simulation/gaia_data.h"
#include "simulation/cinema.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace rtso;

// --- just enough JSON (tools/test_civ_roster.cpp) ---------------------------------------
struct Json {
	enum Kind { NUL, NUM, STR, ARR, OBJ, BOOL } kind = NUL;
	double num = 0.0;
	std::string str;
	std::vector<Json> arr;
	std::vector<std::pair<std::string, Json>> obj;
	const Json *get(const char *key) const {
		for (const auto &kv : obj)
			if (kv.first == key) return &kv.second;
		return nullptr;
	}
	bool has(const char *key) const { return get(key) != nullptr; }
	double number(const char *key, double fallback) const {
		const Json *v = get(key);
		return v && (v->kind == NUM || v->kind == BOOL) ? v->num : fallback;
	}
	std::string string(const char *key, const char *fallback = "") const {
		const Json *v = get(key);
		return v && v->kind == STR ? v->str : std::string(fallback);
	}
	const std::vector<Json> &array(const char *key) const {
		static const std::vector<Json> empty;
		const Json *v = get(key);
		return v && v->kind == ARR ? v->arr : empty;
	}
};

struct JsonReader {
	const char *p;
	void ws() { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p; }
	std::string text() {
		std::string out;
		for (++p; *p && *p != '"'; ++p) {
			if (*p == '\\' && p[1]) {
				++p;
				if (*p == 'u') { out += '?'; p += 4; continue; }
				out += *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;
			} else {
				out += *p;
			}
		}
		if (*p) ++p;
		return out;
	}
	Json value() {
		ws();
		Json v;
		if (*p == '{') {
			v.kind = Json::OBJ;
			++p;
			for (ws(); *p && *p != '}'; ws()) {
				std::string key = text();
				ws();
				++p;
				v.obj.emplace_back(std::move(key), value());
				ws();
				if (*p == ',') ++p;
			}
			if (*p) ++p;
		} else if (*p == '[') {
			v.kind = Json::ARR;
			++p;
			for (ws(); *p && *p != ']'; ws()) {
				v.arr.push_back(value());
				ws();
				if (*p == ',') ++p;
			}
			if (*p) ++p;
		} else if (*p == '"') {
			v.kind = Json::STR;
			v.str = text();
		} else if (*p == 't' || *p == 'f') {
			v.kind = Json::BOOL;
			v.num = *p == 't';
			p += *p == 't' ? 4 : 5;
		} else if (*p == 'n') {
			p += 4;
		} else {
			v.kind = Json::NUM;
			char *end = nullptr;
			v.num = std::strtod(p, &end);
			p = end && end != p ? end : p + 1;
		}
		return v;
	}
};

static bool read_json(const std::string &path, Json &out) {
	FILE *f = std::fopen(path.c_str(), "rb");
	if (!f) return false;
	std::string buf;
	char chunk[65536];
	size_t n;
	while ((n = std::fread(chunk, 1, sizeof chunk, f)) > 0) buf.append(chunk, n);
	std::fclose(f);
	JsonReader r{buf.c_str()};
	out = r.value();
	return out.kind == Json::OBJ;
}

// --- session_setup.cpp's map loader, without Godot ----------------------------------------
static uint8_t map_building_type(const std::string &n) {
	if (n == "town_center" || n == "civic" || n == "civil_centre") return BLD_TOWN_CENTER;
	if (n == "house" || n == "apartment" || n == "hut" || n == "longhouse") return BLD_HOUSE;
	if (n == "barracks" || n == "hall" || n == "arsenal" || n == "elephant_stable") return BLD_BARRACKS;
	if (n == "stable") return BLD_STABLE;
	if (n == "range") return BLD_RANGE;
	if (n == "storehouse" || n == "forge") return BLD_STOREHOUSE;
	if (n == "farmstead" || n == "field" || n == "mill" || n == "rotarymill") return BLD_FARMSTEAD;
	if (n == "market") return BLD_MARKET;
	if (n == "tower" || n == "defense_tower") return BLD_TOWER;
	if (n == "sentry_tower") return BLD_SENTRY_TOWER;
	if (n == "outpost") return BLD_OUTPOST;
	if (n == "fortress" || n == "palisades_fort" || n == "keep") return BLD_FORTRESS;
	if (n == "gate" || n == "wall_gate") return BLD_GATE;
	if (n == "palisade_gate" || n == "palisades_gate") return BLD_PALISADE_GATE;
	if (n == "siege_wall" || n == "wallset_siege") return BLD_SIEGE_WALL;
	if (n == "siege_wall_gate") return BLD_SIEGE_WALL_GATE;
	if (n == "temple" || n == "tophet" || n == "shrine" || n == "pyramid") return BLD_TEMPLE;
	if (n == "wonder") return BLD_WONDER;
	if (n == "dock") return BLD_DOCK;
	if (n == "wall" || n == "wall_segment") return BLD_WALL_SEGMENT;
	if (n == "palisade") return BLD_PALISADE;
	if (n == "wall_tower") return BLD_WALL_TOWER;
	if (n == "corral") return BLD_CORRAL;
	if (n == "field_plot") return BLD_FIELD;
	if (n == "mercenary_camp") return BLD_MERCENARY_CAMP;
	return BLD_TYPE_COUNT;
}

static uint8_t map_building_template(const std::string &tmpl) {
	std::string n = tmpl.substr(tmpl.rfind('/') == std::string::npos ? 0 : tmpl.rfind('/') + 1);
	if (n.rfind("default_", 0) == 0) n = n.substr(8);
	if (n == "house_5" || n == "house_10") return BLD_HOUSE;
	static const struct { const char *name; uint8_t type; } kNames[] = {
		{"civil_centre", BLD_TOWN_CENTER}, {"house", BLD_HOUSE}, {"barracks", BLD_BARRACKS},
		{"storehouse", BLD_STOREHOUSE}, {"farmstead", BLD_FARMSTEAD}, {"field", BLD_FIELD},
		{"market", BLD_MARKET}, {"temple", BLD_TEMPLE}, {"forge", BLD_FORGE},
		{"outpost", BLD_OUTPOST}, {"sentry_tower", BLD_SENTRY_TOWER}, {"defense_tower", BLD_TOWER},
		{"arsenal", BLD_ARSENAL}, {"stable", BLD_STABLE}, {"range", BLD_RANGE},
		{"dock", BLD_DOCK}, {"fortress", BLD_FORTRESS}, {"wonder", BLD_WONDER},
		{"corral", BLD_CORRAL},
	};
	for (const auto &k : kNames)
		if (n == k.name) return k.type;
	for (uint8_t t = BLD_GYMNASIUM; t < BLD_TYPE_COUNT; ++t)
		if (n == building_type_info(t).model_id) return t;
	return BLD_TYPE_COUNT;
}

static UnitType map_unit_type(const std::string &n) {
	static const struct { const char *name; UnitType type; } kTypes[] = {
		{"swordsman", UNIT_SWORDSMAN}, {"spearman", UNIT_SPEARMAN}, {"archer", UNIT_ARCHER},
		{"cavalry", UNIT_CAVALRY}, {"trader", UNIT_TRADER}, {"catapult", UNIT_CATAPULT},
		{"hero", UNIT_HERO}, {"trireme", UNIT_TRIREME}, {"elephant", UNIT_ELEPHANT},
		{"siege_ram", UNIT_SIEGE_RAM}, {"healer", UNIT_HEALER}, {"chariot", UNIT_CHARIOT},
		{"war_dog", UNIT_WAR_DOG}, {"bolt_shooter", UNIT_BOLT_SHOOTER}, {"fishing_boat", UNIT_FISHING_BOAT},
		{"merchant_ship", UNIT_MERCHANT_SHIP}, {"female_citizen", UNIT_FEMALE_CITIZEN}, {"carnyx", UNIT_CARNYX},
		{"worker_elephant", UNIT_WORKER_ELEPHANT}, {"archer_maiden", UNIT_ARCHER_MAIDEN}, {"camel", UNIT_CAMEL},
		{"scythed_chariot", UNIT_SCYTHED_CHARIOT}, {"minister", UNIT_MINISTER},
		{"catafalque", UNIT_CATAFALQUE}, {"wagon", UNIT_WAGON},
	};
	for (const auto &t : kTypes)
		if (n == t.name) return t.type;
	return UNIT_VILLAGER;
}

static uint32_t spawn_map_wall(Simulation &sim, const Json &b, uint8_t player) {
	const std::string ws = b.string("wallset", "stone"), piece = b.string("piece", "wall");
	const uint8_t kind = ws == "palisade" ? WALLSET_PALISADE : ws == "siege" ? WALLSET_SIEGE : WALLSET_STONE;
	const uint8_t p = piece == "tower" ? Simulation::MAP_WALL_TOWER : piece == "gate" ? Simulation::MAP_WALL_GATE : Simulation::MAP_WALL_SEGMENT;
	const uint32_t id = sim.spawn_map_wall_piece(player, kind, p, (float)b.number("x", 0), (float)b.number("z", 0),
			(float)b.number("angle", 0), (float)b.number("length", 9.0));
	if (id && b.number("no_decay", 0) != 0) sim.set_structure_no_decay(id); // session_setup.cpp spawn_map_wall
	if (id && b.number("no_capture", 0) != 0) sim.set_structure_uncapturable(id);
	return id;
}

struct MapPlayerOverrides {
	int8_t civ[MAX_PLAYERS];
	bool has_res[MAX_PLAYERS] = {};
	int res[MAX_PLAYERS][4] = {};
	int pop[MAX_PLAYERS] = {};
	uint8_t team[MAX_PLAYERS] = {};
	bool has_teams = false;
	int pop_limit = -1;
	int players = 0;
	MapPlayerOverrides() { for (auto &c : civ) c = -1; }
};

static std::vector<std::string> g_unported_scripts;

static bool load_json_map(Simulation &sim, const std::string &map_id, uint32_t seed, MapPlayerOverrides &ov) {
	Json d;
	if (!read_json("data/maps/" + map_id + ".json", d)) {
		std::fprintf(stderr, "map not found: data/maps/%s.json\n", map_id.c_str());
		return false;
	}
	sim.set_sim_seed(seed);
	sim.reset();
	ov.pop_limit = (int)d.number("pop_limit", -1);
	for (const Json &p : d.array("players")) {
		const int id = (int)p.number("id", 1);
		const uint8_t team = id <= 1 ? 0 : (uint8_t)(id - 1);
		if (team >= MAX_PLAYERS) continue;
		ov.players = std::max(ov.players, (int)team + 1);
		const int c = (int)p.number("civ", -1);
		if (is_playable_civ(c)) ov.civ[team] = (int8_t)c;
		if (const Json *r = p.get("resources")) {
			const char *keys[4] = {"food", "wood", "stone", "metal"};
			for (int k = 0; k < 4; ++k) ov.res[team][k] = (int)r->number(keys[k], 300);
			ov.has_res[team] = true;
		}
		if (p.has("pop_limit")) ov.pop[team] = std::max(0, (int)p.number("pop_limit", 0));
		if (p.has("team")) {
			const int t = (int)p.number("team", 0);
			ov.team[team] = (uint8_t)(t >= 0 && t < MAX_PLAYERS ? t + 1 : 0);
			if (t != id - 1) ov.has_teams = true;
		}
	}
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
		if (ov.civ[p] >= 0) sim.set_team_civ(p, (uint8_t)ov.civ[p]);

	const std::string name = d.string("name", map_id.c_str()), biome = d.string("biome", "mediterranean");
	const float world_size = (float)d.number("world_size", 256.0);
	const float water_h = (float)d.number("water_height", -1.0);
	const int h_res = (int)d.number("heightmap_res", 64);
	sim.set_map_half(world_size * 0.5f);
	sim.set_circular_map(d.number("circular", 0) != 0);
	sim.set_map_info(map_id, name, biome);
	std::vector<float> heights;
	for (const Json &h : d.array("heightmap")) heights.push_back((float)h.num);
	sim.set_terrain_data(heights, h_res, water_h);
	if (const Json *t = d.get("terrain"); t && t->kind == Json::OBJ) {
		std::vector<std::string> names;
		for (const Json &n : t->array("names")) names.push_back(n.str);
		sim.set_terrain_tiles(Simulation::terrain_tiles_from_rle((int)t->number("res", 0), std::move(names), t->string("rle")));
	}
	for (const Json &r : d.array("resources")) {
		const std::string rt = r.string("type", "tree");
		int amt = (int)r.number("amount", 600);
		uint8_t rtype = RES_WOOD, rsub = RES_SUB_NONE;
		const uint8_t flora = flora_kind_by_name(rt.c_str());
		if (flora < FLORA_COUNT && flora_is_fruit(flora)) rtype = RES_FOOD;
		else if (rt == "gold" || rt == "metal") rtype = RES_GOLD;
		else if (rt == "stone" || rt == "rock") rtype = RES_STONE;
		else if (rt == "food" || rt == "fruit") rtype = RES_FOOD;
		else if (rt == "fish") {
			rtype = RES_FOOD;
			rsub = RES_SUB_FISH;
			if (!r.has("amount")) amt = FISH_FOOD_SUPPLY;
		}
		const int maxg = std::clamp((int)r.number("max_gatherers", 0), 0, 255);
		sim.spawn_resource_world((float)r.number("x", 0), (float)r.number("z", 0), rtype, amt, rsub, (uint8_t)maxg,
				flora < FLORA_COUNT ? flora : FLORA_DEFAULT);
	}
	for (const Json &f : d.array("fauna")) {
		uint8_t ft = fauna_type_by_name(f.string("type", "deer").c_str());
		if (ft >= FAUNA_COUNT) ft = FAUNA_DEER;
		const int fp = (int)f.number("player", 0);
		const uint8_t owner = fp <= 0 ? GAIA_PLAYER : (uint8_t)std::min(fp - 1, MAX_PLAYERS - 1);
		sim.spawn_fauna((float)f.number("x", 0), (float)f.number("z", 0), ft, owner);
	}
	for (const Json &t : d.array("treasures")) {
		const uint8_t tt = treasure_type_by_name(t.string("type").c_str());
		if (tt < TREASURE_TYPE_COUNT) sim.spawn_treasure((float)t.number("x", 0), (float)t.number("z", 0), tt);
	}
	for (const Json &r : d.array("ruins")) {
		const uint8_t rt = ruins_type_by_name(r.string("type").c_str());
		if (rt < RUINS_TYPE_COUNT) sim.spawn_ruins((float)r.number("x", 0), (float)r.number("z", 0), rt);
	}
	sim.begin_bulk_spawn();
	for (const Json &b : d.array("structures")) {
		const std::string st = b.string("type", "house");
		const int player = (int)b.number("player", 1);
		uint8_t team = player <= 1 ? 0 : (uint8_t)(player - 1);
		if (team >= MAX_PLAYERS) team = 1;
		const std::string tmpl = b.string("template");
		const uint8_t civ = (uint8_t)sim.player(team).civ;
		auto ends = [&](const char *s) { const size_t n = std::strlen(s); return tmpl.size() >= n && tmpl.compare(tmpl.size() - n, n, s) == 0; };
		if (ends("default_house_5") || ends("default_house_10")) {
			const int pop = civ_building_type_info(civ, BLD_HOUSE).pop_bonus;
			if (civ == CIV_HAN || (pop == 5) != ends("default_house_5")) continue;
		}
		if (civ == CIV_SPARTANS && tmpl.find("default_wall_") != std::string::npos) continue;
		if (b.has("wallset")) { spawn_map_wall(sim, b, team); continue; }
		uint8_t bt = map_structure_template(tmpl.c_str()).type;
		if (bt >= BLD_TYPE_COUNT && !tmpl.empty()) bt = map_building_template(tmpl);
		if (bt >= BLD_TYPE_COUNT) bt = map_building_type(st);
		if (bt >= BLD_TYPE_COUNT) {
			if (st.empty()) continue;
			bt = BLD_HOUSE;
		}
		sim.spawn_building_world((float)b.number("x", 0), (float)b.number("z", 0), bt, team, 1.0f);
	}
	const auto &gstr = d.array("gaia_structures");
	std::vector<uint32_t> gaia_structure_ids(gstr.size(), 0);
	for (size_t i = 0; i < gstr.size(); ++i) {
		const Json &b = gstr[i];
		if (b.has("wallset")) { gaia_structure_ids[i] = spawn_map_wall(sim, b, GAIA_PLAYER); continue; }
		const MapStructureTemplate gt = map_structure_template(b.string("template").c_str());
		const uint8_t bt = gt.type < BLD_TYPE_COUNT ? gt.type : map_building_type(b.string("type"));
		if (bt >= BLD_TYPE_COUNT) continue;
		gaia_structure_ids[i] = sim.spawn_building_world((float)b.number("x", 0), (float)b.number("z", 0), bt, GAIA_PLAYER, 1.0f);
		if (gt.type < BLD_TYPE_COUNT && gt.civ < CIV_COUNT) sim.set_building_civ(gaia_structure_ids[i], gt.civ);
	}
	for (const Json &sc : d.array("scenery")) {
		const uint8_t st = scenery_type_by_name(sc.string("type").c_str());
		if (st < SCENERY_TYPE_COUNT)
			sim.spawn_scenery((float)sc.number("x", 0), (float)sc.number("z", 0), st, (float)sc.number("angle", 0), (float)sc.number("scale", 0.25));
	}
	sim.end_bulk_spawn();
	const auto &units = d.array("units");
	std::vector<uint32_t> unit_ids(units.size(), 0);
	for (size_t i = 0; i < units.size(); ++i) {
		const Json &u = units[i];
		const int player = (int)u.number("player", 1);
		uint8_t team = player <= 1 ? 0 : (uint8_t)(player - 1);
		if (team >= MAX_PLAYERS) team = 1;
		const float ux = (float)u.number("x", 0), uz = (float)u.number("z", 0);
		const UnitType ut = map_unit_type(u.string("type", "villager"));
		if (u.has("template")) {
			const std::string t = u.string("template");
			uint16_t tmpl = CIV_UNIT_TEMPLATE_NONE;
			const int replaced = skirmish_replace(sim.team_civ(team), t.c_str(), tmpl);
			if (replaced < 0) continue;
			if (replaced) { unit_ids[i] = sim.spawn_map_unit(ux, uz, team, ut, civ_unit_template(tmpl).id); continue; }
			int rank = 0;
			std::string pick = t;
			if (Simulation::resolve_map_template(t.c_str(), &rank) == CIV_UNIT_TEMPLATE_NONE)
				for (const Json &pt : u.array("parents"))
					if (Simulation::resolve_map_template(pt.str.c_str(), &rank) != CIV_UNIT_TEMPLATE_NONE) { pick = pt.str; break; }
			unit_ids[i] = sim.spawn_map_unit(ux, uz, team, ut, pick.c_str(), rank);
			continue;
		}
		unit_ids[i] = sim.spawn_map_unit(ux, uz, team, ut, nullptr);
	}
	for (size_t i = 0; i < units.size(); ++i) {
		const int holder = (int)units[i].number("garrison_in", -1);
		if (holder < 0 || holder >= (int)unit_ids.size() || !unit_ids[i] || !unit_ids[holder]) continue;
		sim.order_board_transport(&unit_ids[i], 1, unit_ids[holder]);
	}
	const auto &gun = d.array("gaia_units");
	std::vector<uint32_t> gaia_unit_ids(gun.size(), 0);
	for (size_t i = 0; i < gun.size(); ++i) {
		const Json &u = gun[i];
		gaia_unit_ids[i] = sim.spawn_gaia_unit((float)u.number("x", 0), (float)u.number("z", 0), map_unit_type(u.string("type")),
				u.string("template").c_str(), (int)u.number("rank", 1));
	}
	for (size_t i = 0; i < gun.size(); ++i) {
		if (!gaia_unit_ids[i]) continue;
		const int ship = (int)gun[i].number("garrison_in", -1), str = (int)gun[i].number("garrison_in_structure", -1);
		if (ship >= 0 && ship < (int)gaia_unit_ids.size() && gaia_unit_ids[ship])
			sim.order_board_transport(&gaia_unit_ids[i], 1, gaia_unit_ids[ship]);
		else if (str >= 0 && str < (int)gaia_structure_ids.size() && gaia_structure_ids[str])
			sim.garrison_unit_now(gaia_unit_ids[i], gaia_structure_ids[str]);
	}
	for (const Json &tp : d.array("trigger_points")) {
		const std::string ref = tp.string("ref", "A");
		const int owner = (int)tp.number("owner", 0);
		if (ref.empty()) continue;
		sim.add_trigger_point((uint8_t)ref[0], (float)tp.number("x", 0), (float)tp.number("z", 0),
				owner <= 0 ? GAIA_PLAYER : (uint8_t)std::min(owner - 1, MAX_PLAYERS - 1));
	}
	for (const Json &cp : d.array("cinema_paths")) {
		CinemaPath path;
		std::strncpy(path.info.name, cp.string("name").c_str(), sizeof(path.info.name) - 1);
		path.info.timescale = (float)cp.number("timescale", 1.0);
		path.info.mode = cinema_mode_from_name(cp.string("mode", "ease_inout").c_str());
		path.info.style = cinema_style_from_name(cp.string("style", "default").c_str());
		path.info.look_at_target = cp.string("orientation") == "target" ? 1 : 0;
		auto nodes = [](const std::vector<Json> &arr, std::vector<CinemaNode> &out) {
			for (const Json &n : arr)
				if (n.arr.size() >= 4) out.push_back(CinemaNode{(float)n.arr[0].num, (float)n.arr[1].num, (float)n.arr[2].num, (float)n.arr[3].num});
		};
		nodes(cp.array("nodes"), path.nodes);
		nodes(cp.array("targets"), path.targets);
		if (path.info.look_at_target && path.targets.empty()) path.info.look_at_target = 0;
		sim.add_cinema_path(path);
	}
	for (const Json &s : d.array("trigger_scripts"))
		if (!sim.register_trigger_script(s.str)) g_unported_scripts.push_back(s.str);
	return true;
}

// --- the probe -----------------------------------------------------------------------------
static const char *action_name(uint16_t a) {
	static const char *const k[] = {"NONE", "FLOOD_INIT", "FLOOD_RISE_STEP", "FLOOD_WARNING", "VOLCANO_INIT", "VOLCANO_NOTIFY",
		"VOLCANO_RISE_STEP", "POLAR_SEA_INIT", "POLAR_SEA_WAVE", "TI_INIT", "TI_INTRO", "TI_COLLECTED", "TI_BATTLE", "TI_VICTORY",
		"SURVIVAL_INIT", "SURVIVAL_WAVE", "SURVIVAL_TREASURES", "SURVIVAL_OWNERSHIP", "TUTORIAL", "DEATH_CANYON_INIT",
		"ELEPHANTINE_INIT", "DANUBIUS_INIT", "DANUBIUS_CC_ATTACKERS", "DANUBIUS_SHIPS", "DANUBIUS_FILL_SHIPS",
		"DANUBIUS_UNLOAD_ORDER", "DANUBIUS_SHIP_RANGE", "DANUBIUS_OWNERSHIP", "JB_INIT", "JB_CITY_PATROL", "JB_ATTACK",
		"JB_OWNERSHIP", "GALLIC_FIELDS", "TD_RANGE", "TD_INIT", "TD_LOG", "TD_INTERVAL", "PICKUP_TEST", "UNIT_CHASING",
		"UNIT_CHASING_OWN", "UNIT_DANCING", "UNIT_DANCING_EVENT", "UNIT_PUSHING", "UNIT_MOTION", "CINEMA_DEMO", "WALL_DEMO_INIT"};
	return a < sizeof(k) / sizeof(k[0]) ? k[a] : "?";
}

static const char *notice_reason(uint8_t r) {
	static const char *const k[] = {"conquest", "units", "structures", "civic centres", "hero", "resigned", "objective",
		"last alive", "wonder", "relic", "game mode", "lost civic center", "treasure"};
	return r < sizeof(k) / sizeof(k[0]) ? k[r] : "?";
}

struct Spawned {
	uint32_t id;
	bool fauna;
	float x, z, t;
	uint8_t team;
	uint16_t tmpl;
	uint8_t utype;
	bool moved = false;
	bool checked_stuck = false;
};

int main(int argc, char **argv) {
	std::string json_map, random_map;
	double minutes = 5.0;
	uint32_t seed = 7;
	int players = 2;
	float size = 256.0f;
	uint32_t humans = 1;
	bool ai = true, quiet = false, nomad = false;
	int victory = -1, water = Simulation::WATER_LEVEL_RISING, rise = -1, difficulty = 3;
	double summary_every = 60.0;
	std::vector<uint32_t> trace;
	bool owner_log = false, death_log = false;
	for (int i = 1; i < argc; ++i) {
		auto next = [&]() { return i + 1 < argc ? argv[++i] : (char *)"0"; };
		if (!std::strcmp(argv[i], "--json")) json_map = next();
		else if (!std::strcmp(argv[i], "--random")) random_map = next();
		else if (!std::strcmp(argv[i], "--minutes")) minutes = std::atof(next());
		else if (!std::strcmp(argv[i], "--seed")) seed = (uint32_t)std::strtoul(next(), nullptr, 10);
		else if (!std::strcmp(argv[i], "--players")) players = std::atoi(next());
		else if (!std::strcmp(argv[i], "--size")) size = (float)std::atof(next());
		else if (!std::strcmp(argv[i], "--humans")) humans = (uint32_t)std::strtoul(next(), nullptr, 0);
		else if (!std::strcmp(argv[i], "--no-ai")) ai = false;
		else if (!std::strcmp(argv[i], "--victory")) victory = std::atoi(next());
		else if (!std::strcmp(argv[i], "--water")) water = std::atoi(next());
		else if (!std::strcmp(argv[i], "--rise")) rise = std::atoi(next());
		else if (!std::strcmp(argv[i], "--difficulty")) difficulty = std::atoi(next());
		else if (!std::strcmp(argv[i], "--summary")) summary_every = std::atof(next());
		else if (!std::strcmp(argv[i], "--nomad")) nomad = true;
		else if (!std::strcmp(argv[i], "--quiet")) quiet = true;
		else if (!std::strcmp(argv[i], "--trace")) trace.push_back((uint32_t)std::strtoul(next(), nullptr, 10));
		else if (!std::strcmp(argv[i], "--owners")) owner_log = true;
		else if (!std::strcmp(argv[i], "--deaths")) death_log = true;
	}
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	static Simulation s;
	MapPlayerOverrides ov;
	// 0.28 scenario settings (data/map_types.json): VictoryConditions, RevealMap, AllyView.
	uint8_t vmask = 1;
	bool reveal = false;
	if (!json_map.empty()) {
		if (!load_json_map(s, json_map, seed, ov)) return 2;
		players = std::max(1, ov.players);
		Json mt;
		if (read_json("data/map_types.json", mt)) {
			const Json *m = nullptr;
			std::vector<const Json *> stack{&mt};
			while (!stack.empty() && !m) {
				const Json *j = stack.back();
				stack.pop_back();
				for (const auto &kv : j->obj) {
					if (kv.first == json_map && kv.second.kind == Json::OBJ && kv.second.has("type")) { m = &kv.second; break; }
					if (kv.second.kind == Json::OBJ) stack.push_back(&kv.second);
				}
			}
			if (m && m->string("type") == "scenario")
				if (const Json *st = m->get("settings")) {
					if (const Json *vc = st->get("VictoryConditions"); vc && vc->kind == Json::ARR) {
						static const std::map<std::string, uint8_t> names = {{"conquest", 1}, {"conquest_units", 2},
							{"conquest_structures", 4}, {"conquest_civic_centers", 8}, {"wonder", 16}, {"capture_the_relic", 32}, {"regicide", 64}};
						vmask = 0;
						for (const Json &n : vc->arr) if (names.count(n.str)) vmask |= names.at(n.str);
					}
					reveal = st->number("RevealMap", 0) != 0;
				}
			std::printf("map type: %s, victory mask %u, reveal %d\n", m ? m->string("type").c_str() : "(none)", vmask, reveal);
		}
	} else if (!random_map.empty()) {
		int type = -1;
		for (int t = 0; t < MAP_TYPE_COUNT; ++t)
			if (random_map == map_type_info((uint8_t)t).id) { type = t; break; }
		if (type < 0) { std::fprintf(stderr, "unknown random map %s\n", random_map.c_str()); return 2; }
		s.reset();
		s.set_sim_seed(seed);
		s.set_waves_enabled(false);
		s.set_trigger_difficulty(difficulty);
		s.set_nomad(nomad);
		s.generate_random_map((MapType)type, seed, players, size * 0.5f);
	} else {
		std::fprintf(stderr, "--json NAME or --random MAPID\n");
		return 2;
	}
	s.set_trigger_difficulty(difficulty);
	s.set_victory_conditions(victory >= 0 ? (uint8_t)victory : vmask);
	s.set_waves_enabled(false);
	s.set_reveal_map(reveal);
	s.set_water_level_mode((uint8_t)water);
	if (rise >= 0) s.set_sea_level_rise_minutes(rise);
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		if (ov.civ[p] >= 0) s.set_team_civ(p, (uint8_t)ov.civ[p]);
		if (ov.has_res[p]) s.set_player_resources(p, ov.res[p][0], ov.res[p][1], ov.res[p][3], ov.res[p][2]);
		s.set_player_pop_cap(p, ov.pop[p]);
	}
	if (ov.pop_limit > 0) s.set_pop_cap(ov.pop_limit);
	if (ov.has_teams) { s.set_setup_teams(ov.team); s.apply_team_diplomacy(); }
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
		const bool seated = p < players;
		s.set_player_active(p, seated, seated && ai && !((humans >> p) & 1));
		if (seated) s.set_petra_difficulty(p, 3);
	}
	for (const std::string &u : g_unported_scripts) std::printf("UNPORTED trigger script: %s\n", u.c_str());

	const float half = s.map_half();
	auto count_units = [&](uint8_t team) {
		int n = 0;
		for (size_t i = 0; i < s.units_.count; ++i) n += s.units_.alive[i] && s.units_.team[i] == team;
		return n;
	};
	auto count_buildings = [&](uint8_t team) {
		int n = 0;
		for (const auto &b : s.buildings_) n += b.team == team && b.hp > 0.0f && !b.wrecked;
		return n;
	};
	std::printf("loaded: half %.1f, water %.2f, players %d, units", half, s.water_height(), players);
	for (uint8_t p = 0; p < players; ++p) std::printf(" p%u=%d/%db", p + 1, count_units(p), count_buildings(p));
	std::printf(" gaia=%d/%db fauna=%zu treasures=%zu points=%zu triggers=%zu\n", count_units(GAIA_PLAYER), count_buildings(GAIA_PLAYER),
			s.fauna_.count, s.treasures_.size(), s.triggers_.points.size(), s.triggers_.triggers.size());
	for (const auto &b : s.buildings_)
		if (is_civic_centre(b.type) || (b.team < MAX_PLAYERS && b.team < players && !quiet && s.buildings_.size() < 40))
			std::printf("  structure %u type %u (%s) team %u at (%.1f,%.1f)\n", b.id, b.type, building_type_info(b.type).model_id, b.team, b.x, b.z);
	{
		std::map<char, int> refs;
		for (const auto &p : s.triggers_.points) refs[(char)p.ref]++;
		std::printf("trigger points:");
		for (auto &[r, n] : refs) std::printf(" %c=%d", r, n);
		std::printf("\n");
		for (const auto &p : s.triggers_.points) {
			const float h = s.get_terrain_height(p.x, p.z);
			if (std::fabs(p.x) > half || std::fabs(p.z) > half || h < s.water_height())
				std::printf("  ODD point %c at (%.1f,%.1f) owner %u height %.2f water %.2f\n", p.ref, p.x, p.z, p.owner, h, s.water_height());
		}
	}

	std::set<uint32_t> seen_units, seen_fauna, seen_notes, seen_buildings;
	for (size_t i = 0; i < s.units_.count; ++i) seen_units.insert(s.units_.id[i]);
	for (size_t i = 0; i < s.fauna_.count; ++i) seen_fauna.insert(s.fauna_.id[i]);
	for (const auto &b : s.buildings_) seen_buildings.insert(b.id);
	std::vector<Spawned> spawned;
	std::map<uint32_t, uint8_t> building_owner;
	for (const auto &b : s.buildings_) building_owner[b.id] = b.team;
	double next_trace = 0.0;
	std::map<uint32_t, std::pair<float, float>> player_unit_pos;
	std::map<std::string, int> fired;
	const uint32_t end_ms = (uint32_t)(minutes * 60000.0);
	double next_summary = summary_every;
	uint8_t defeated_before = 0;
	int spawn_lines = 0;
	float water0 = s.water_height();
	int ticks = 0;
	uint64_t clock_ms = 0; // the match clock (the trigger clock stands still on maps without triggers)
	while (clock_ms < end_ms && !s.game_over_) {
		// Timers due this step.
		const uint32_t now = s.triggers_.time_ms;
		const uint32_t step = (uint32_t)std::lround(Simulation::FIXED_STEP * 1000.0f);
		for (const TriggerTimer &t : s.triggers_.timers)
			if (t.time_ms <= now + step) {
				fired[action_name(t.action)]++;
				if (!quiet && t.action != TRIGGER_ACTION_FLOOD_RISE_STEP && t.action != TRIGGER_ACTION_DANUBIUS_SHIP_RANGE &&
						t.action != TRIGGER_ACTION_TREASURE_ISLANDS_BATTLE && t.action != TRIGGER_ACTION_TRIGGERS_DEMO_INTERVAL)
					std::printf("[%7.1fs] timer %s arg %.2f\n", (now + step) / 1000.0, action_name(t.action), t.arg);
			}
		const bool first = ticks == 0;
		s.tick(Simulation::FIXED_STEP);
		++ticks;
		clock_ms += (uint64_t)std::lround(Simulation::FIXED_STEP * 1000.0f);
		const double ts = clock_ms / 1000.0;
		if (first) {
			std::printf("[%7.1fs] after OnInitGame: triggers %zu, timers %zu, watched %zu, invulnerable %zu, range queries %zu\n", ts,
					s.triggers_.triggers.size(), s.triggers_.timers.size(), s.triggers_.watched.size(), s.triggers_.invulnerable.size(),
					s.triggers_.range_queries.size());
			for (const TriggerTimer &t : s.triggers_.timers)
				std::printf("           queued %s at %.1fs (repeat %u ms) arg %.2f\n", action_name(t.action), t.time_ms / 1000.0, t.repeat_ms, t.arg);
		}
		for (const TimeNotification &n : s.triggers_.notifications)
			if (seen_notes.insert(n.id).second) {
				std::string text = n.text;
				for (char &c : text) if (c == '\x1f') c = '|';
				std::printf("[%7.1fs] NOTE to %02x for %.0fs: %s\n", ts, n.players, (n.end_ms - s.triggers_.time_ms) / 1000.0 + 0.0, text.c_str());
			}
		for (const auto &d : s.take_trigger_dialogs()) std::printf("[%7.1fs] DIALOG to %02x: %s\n", ts, d.players, d.caption.c_str());
		for (const auto &n : s.take_session_notices()) {
			if (n.kind == Simulation::SessionNotice::DEFEATED) std::printf("[%7.1fs] DEFEATED p%u (%s)\n", ts, n.player + 1, notice_reason(n.tech));
			else if (n.kind == Simulation::SessionNotice::WON) std::printf("[%7.1fs] WON p%u mask %02x (%s)\n", ts, n.player + 1, n.other, notice_reason(n.tech));
			else if (n.kind == Simulation::SessionNotice::LOST) std::printf("[%7.1fs] LOST p%u mask %02x (%s)\n", ts, n.player + 1, n.other, notice_reason(n.tech));
		}
		// The scans below every half second (the first tick and the match's end too).
		if (!(first || ticks % 5 == 0 || s.game_over_)) continue;
		// New entities after the first tick: script spawns, trained units, foundations.
		int tick_new_gaia = 0, tick_new_ordered = 0, tick_new_garrisoned = 0, tick_new_wet = 0, tick_new_off = 0;
		std::map<std::string, int> tick_templates;
		for (size_t i = 0; i < s.units_.count; ++i) {
			if (!s.units_.alive[i] || !seen_units.insert(s.units_.id[i]).second) continue;
			Spawned sp{s.units_.id[i], false, s.units_.pos_x[i], s.units_.pos_z[i], (float)ts, s.units_.team[i], s.units_.tmpl[i], s.units_.unit_type[i]};
			if (s.units_.team[i] == GAIA_PLAYER) spawned.push_back(sp);
			const float h = s.get_terrain_height(sp.x, sp.z);
			const bool off = std::fabs(sp.x) > half || std::fabs(sp.z) > half;
			const bool wet = !unit_is_ship(sp.utype) && h < s.water_height() - 0.25f && s.units_.action_state[i] != ACTION_GARRISONED;
			if (s.units_.team[i] == GAIA_PLAYER) {
				++tick_new_gaia;
				tick_templates[sp.tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(sp.tmpl).id : "-"]++;
				if (s.units_.order_kind[i] != ORDER_NONE) ++tick_new_ordered;
				if (s.units_.action_state[i] == ACTION_GARRISONED || s.turret_height_.count(s.units_.id[i])) ++tick_new_garrisoned;
				if (wet) ++tick_new_wet;
				if (off) ++tick_new_off;
			}
			if (s.units_.team[i] == GAIA_PLAYER && (spawn_lines < 12 || off || wet) && !(wet && s.units_.action_state[i] == ACTION_GARRISONED)) {
				++spawn_lines;
				std::printf("[%7.1fs] GAIA unit %u %s at (%.1f,%.1f) h %.2f%s%s order %d\n", ts, sp.id,
						sp.tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(sp.tmpl).id : "-", sp.x, sp.z, h,
						off ? " OFF-MAP" : "", wet ? " IN-WATER" : "", (int)s.units_.order_kind[i]);
			} else if (off || wet) {
				std::printf("[%7.1fs] p%u unit %u %s at (%.1f,%.1f)%s%s\n", ts, sp.team + 1, sp.id,
						sp.tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(sp.tmpl).id : "-", sp.x, sp.z, off ? " OFF-MAP" : "", wet ? " IN-WATER" : "");
			}
		}
		if (tick_new_gaia) {
			std::printf("[%7.1fs] +%d Gaia units (%d with orders, %d garrisoned/aboard, %d in water, %d off-map):", ts, tick_new_gaia,
					tick_new_ordered, tick_new_garrisoned, tick_new_wet, tick_new_off);
			for (auto &[k, n] : tick_templates) std::printf(" %s x%d", k.c_str(), n);
			std::printf("\n");
		}
		for (size_t f = 0; f < s.fauna_.count; ++f) {
			if (!s.fauna_.alive[f] || !seen_fauna.insert(s.fauna_.id[f]).second) continue;
			Spawned sp{s.fauna_.id[f], true, s.fauna_.pos_x[f], s.fauna_.pos_z[f], (float)ts, GAIA_PLAYER, 0, s.fauna_.fauna_type[f]};
			spawned.push_back(sp);
			const float h = s.get_terrain_height(sp.x, sp.z);
			if (spawn_lines < 60 || h < s.water_height()) {
				++spawn_lines;
				std::printf("[%7.1fs] fauna %u type %u at (%.1f,%.1f) h %.2f%s behavior %d target %u\n", ts, sp.id, sp.utype, sp.x, sp.z, h,
						h < s.water_height() ? " IN-WATER" : "", (int)s.fauna_.behavior[f], s.fauna_.target_unit_id[f]);
			}
		}
		for (const auto &b : s.buildings_)
			if (seen_buildings.insert(b.id).second && b.team == GAIA_PLAYER)
				std::printf("[%7.1fs] GAIA structure %u type %u at (%.1f,%.1f)\n", ts, b.id, b.type, b.x, b.z);
		// Moved yet?
		for (Spawned &sp : spawned) {
			if (sp.moved || sp.checked_stuck) continue;
			float x = 0, z = 0;
			bool alive = false;
			if (sp.fauna) {
				auto it = s.fauna_id_to_idx_.find(sp.id);
				if (it != s.fauna_id_to_idx_.end() && s.fauna_.alive[it->second]) { alive = true; x = s.fauna_.pos_x[it->second]; z = s.fauna_.pos_z[it->second]; }
			} else {
				const int32_t u = s.lookup_unit(sp.id);
				if (u >= 0 && s.units_.alive[u]) { alive = true; x = s.units_.pos_x[u]; z = s.units_.pos_z[u]; }
				if (u >= 0 && s.units_.action_state[u] == ACTION_GARRISONED) sp.checked_stuck = true;
			}
			if (!alive) { sp.checked_stuck = true; continue; }
			if (std::hypot(x - sp.x, z - sp.z) > 3.0f) sp.moved = true;
		}
		if (death_log) {
			std::set<uint32_t> now_alive;
			for (size_t i = 0; i < s.units_.count; ++i)
				if ((s.units_.alive[i] || s.units_.action_state[i] == ACTION_GARRISONED) && s.units_.team[i] < MAX_PLAYERS) {
					now_alive.insert(s.units_.id[i]);
					player_unit_pos[s.units_.id[i]] = {s.units_.pos_x[i], s.units_.pos_z[i]};
				}
			for (auto it = player_unit_pos.begin(); it != player_unit_pos.end();) {
				if (now_alive.count(it->first)) { ++it; continue; }
				const int32_t u = s.lookup_unit(it->first);
				const float x = it->second.first, z = it->second.second;
				float best = 1e9f; std::string who = "-";
				for (size_t i = 0; i < s.units_.count; ++i)
					if (s.units_.alive[i] && s.units_.team[i] == GAIA_PLAYER) {
						const float d = std::hypot(s.units_.pos_x[i] - x, s.units_.pos_z[i] - z);
						if (d < best) { best = d; who = s.units_.tmpl[i] < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(s.units_.tmpl[i]).id : "?"; }
					}
				float bestf = 1e9f;
				for (size_t f = 0; f < s.fauna_.count; ++f)
					if (s.fauna_.alive[f]) bestf = std::min(bestf, std::hypot(s.fauna_.pos_x[f] - x, s.fauna_.pos_z[f] - z));
				std::printf("[%7.1fs] DIED p%u unit %u %s at (%.1f,%.1f); nearest Gaia unit %s at %.1f, nearest animal %.1f\n", ts,
						u >= 0 ? s.units_.team[u] + 1 : 0, it->first, u >= 0 && s.units_.tmpl[u] < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(s.units_.tmpl[u]).id : "-",
						x, z, who.c_str(), best, bestf);
				it = player_unit_pos.erase(it);
			}
		}
		if (owner_log)
			for (const auto &b : s.buildings_) {
				auto it = building_owner.find(b.id);
				const uint8_t now_team = (b.hp > 0.0f && !b.wrecked) ? b.team : 0xFF;
				if (it == building_owner.end()) { building_owner[b.id] = now_team; continue; }
				if (it->second != now_team) {
					std::printf("[%7.1fs] structure %u type %u (%s) at (%.1f,%.1f) owner %u -> %u hp %.0f/%.0f progress %.2f\n", ts, b.id, b.type,
							building_type_info(b.type).model_id, b.x, b.z, it->second, now_team, b.hp, b.max_hp, b.build_progress);
					it->second = now_team;
				}
			}
		if (!trace.empty() && ts >= next_trace) {
			next_trace += 5.0;
			for (uint32_t id : trace) {
				const int32_t u = s.lookup_unit(id);
				if (u >= 0) {
					std::printf("[%7.1fs] trace unit %u alive %d team %u at (%.1f,%.1f) h %.2f state %d order %d has_order %d target u%u b%u (%.1f,%.1f) hp %.0f\n", ts, id,
							s.units_.alive[u], s.units_.team[u], s.units_.pos_x[u], s.units_.pos_z[u], s.get_terrain_height(s.units_.pos_x[u], s.units_.pos_z[u]),
							(int)s.units_.action_state[u], (int)s.units_.order_kind[u], (int)s.units_.has_order[u], s.units_.target_unit_id[u],
							s.units_.target_building_id[u], s.units_.target_x[u], s.units_.target_z[u], s.units_.hp[u]);
					continue;
				}
				auto it = s.fauna_id_to_idx_.find(id);
				if (it != s.fauna_id_to_idx_.end()) {
					const size_t f = it->second;
					const int32_t t = s.lookup_unit(s.fauna_.target_unit_id[f]);
					std::printf("[%7.1fs] trace fauna %u alive %d at (%.1f,%.1f) h %.2f behavior %d target %u", ts, id, s.fauna_.alive[f],
							s.fauna_.pos_x[f], s.fauna_.pos_z[f], s.get_terrain_height(s.fauna_.pos_x[f], s.fauna_.pos_z[f]), (int)s.fauna_.behavior[f],
							s.fauna_.target_unit_id[f]);
					if (t >= 0) std::printf(" (team %u at %.1f,%.1f state %d alive %d)", s.units_.team[t], s.units_.pos_x[t], s.units_.pos_z[t],
							(int)s.units_.action_state[t], s.units_.alive[t]);
					std::printf(" vel %.2f,%.2f\n", s.fauna_.vel_x[f], s.fauna_.vel_z[f]);
				}
			}
		}
		const uint8_t dm = s.defeated_mask_;
		if (dm != defeated_before) defeated_before = dm;
		if (ts >= next_summary) {
			next_summary += summary_every;
			std::printf("[%7.1fs] summary: water %.2f (+%.2f)", ts, s.water_height(), s.water_height() - water0);
			for (uint8_t p = 0; p < players; ++p)
				std::printf(" p%u=%d/%db%s", p + 1, count_units(p), count_buildings(p), s.is_defeated(p) ? "(X)" : "");
			int gaia_walkers = 0;
			for (size_t i = 0; i < s.units_.count; ++i)
				gaia_walkers += s.units_.alive[i] && s.units_.team[i] == GAIA_PLAYER && s.units_.order_kind[i] != ORDER_NONE;
			int fauna_attacking = 0;
			for (size_t f = 0; f < s.fauna_.count; ++f) fauna_attacking += s.fauna_.alive[f] && s.fauna_.behavior[f] == FAUNA_ATTACK;
			std::printf(" gaia=%d(%d ordered)/%db fauna=%zu(%d attacking) treasures=%zu timers=%zu notes=%zu vars=[",
					count_units(GAIA_PLAYER), gaia_walkers, count_buildings(GAIA_PLAYER), s.fauna_.count, fauna_attacking, s.treasures_.size(),
					s.triggers_.timers.size(), s.triggers_.notifications.size());
			for (int v = 0; v < 8; ++v) std::printf("%d%s", s.triggers_.vars[v], v < 7 ? "," : "]\n");
			for (uint8_t p = 0; p < players; ++p) {
				const Simulation::Building *cc = nullptr;
				for (const auto &b : s.buildings_) if (b.team == p && is_civic_centre(b.type) && b.hp > 0.0f && !b.wrecked) { cc = &b; break; }
				int near_gaia = 0, trained = 0;
				if (cc)
					for (size_t i = 0; i < s.units_.count; ++i)
						near_gaia += s.units_.alive[i] && s.units_.team[i] == GAIA_PLAYER && std::hypot(s.units_.pos_x[i] - cc->x, s.units_.pos_z[i] - cc->z) < 30.0f;
				(void)trained;
				std::printf("           p%u res f%d w%d s%d m%d, cc %s hp %.0f, Gaia units within 30 of it: %d\n", p + 1, s.food(p), s.wood(p), s.stone(p), s.gold(p),
						cc ? "yes" : "no", cc ? cc->hp : 0.0f, near_gaia);
			}
		}
	}
	// Stuck spawns: alive, spawned at least 60 s before the end, never 3 units from where they appeared.
	int stuck = 0, total = 0;
	const float tend = clock_ms / 1000.0f;
	for (const Spawned &sp : spawned) {
		if (sp.t + 60.0f > tend) continue;
		// Turreted on a wall or garrisoned (Jebel Barkal walls, Danubius towers): never meant to move.
		if (const int32_t k = s.lookup_unit(sp.id); k >= 0 && (s.turret_height_.count(sp.id) || s.units_.action_state[k] == ACTION_GARRISONED)) continue;
		++total;
		if (!sp.moved && !sp.checked_stuck) {
			if (stuck < 10) {
				int open = 0, here = 0;
				if (s.nav_grid_.res > 0) {
					auto [cx, cy] = s.nav_grid_.world_to_cell(sp.x, sp.z);
					here = s.nav_grid_.is_passable_for(cx, cy, PASS_INFANTRY);
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx) open += (dx || dy) && s.nav_grid_.is_passable_for(cx + dx, cy + dy, PASS_INFANTRY);
				}
				std::printf("STUCK %s %u (%s) at (%.1f,%.1f) h %.2f since %.1fs; navcell passable %d, %d of 8 neighbours\n", sp.fauna ? "fauna" : "unit", sp.id,
						!sp.fauna && sp.tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(sp.tmpl).id : "-", sp.x, sp.z,
						s.get_terrain_height(sp.x, sp.z), sp.t, here, open);
			}
			++stuck;
		}
	}
	if (std::getenv("PROBE_LEFT"))
		for (size_t u = 0; u < s.units_.count; ++u)
			if (s.units_.team[u] == (uint8_t)std::atoi(std::getenv("PROBE_LEFT")) && (s.units_.alive[u] || s.units_.action_state[u] == ACTION_GARRISONED))
				std::printf("LEFT unit %u tmpl %s alive %d state %d at (%.1f,%.1f) hp %.0f\n", s.units_.id[u],
						(s.units_.tmpl[u] < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(s.units_.tmpl[u]).id : "-"), s.units_.alive[u], s.units_.action_state[u], s.units_.pos_x[u], s.units_.pos_z[u], s.units_.hp[u]);
	if (std::getenv("PROBE_LEFT"))
		for (const auto &b : s.buildings_)
			for (uint32_t g : b.garrisoned_units)
				for (size_t u = 0; u < s.units_.count; ++u)
					if (s.units_.id[u] == g && s.units_.team[u] != b.team)
						std::printf("HELD unit %u team %u inside building %u type %u team %u hp %.0f\n", g, s.units_.team[u], b.id, b.type, b.team, b.hp);
	std::printf("END at %.1fs: game_over %d victory %d winners %02x defeated %02x; script spawns %zu (%d of %d older than 60 s never moved)\n",
			tend, s.game_over_, s.victory_, s.winner_mask(), s.defeated_mask_, spawned.size(), stuck, total);
	std::printf("timers fired:");
	for (auto &[k, n] : fired) std::printf(" %s=%d", k.c_str(), n);
	std::printf("\nevent calls:");
	for (int e = 0; e < TRIGGER_EVENT_COUNT; ++e) std::printf(" %d", (int)s.trigger_event_calls_[e]);
	std::printf("\nfinal:");
	for (uint8_t p = 0; p < players; ++p) std::printf(" p%u=%d/%db", p + 1, count_units(p), count_buildings(p));
	std::printf(" gaia=%d/%db\n", count_units(GAIA_PLAYER), count_buildings(GAIA_PLAYER));
	return 0;
}
