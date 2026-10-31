// RtsoMain's match setup (0 A.D. gui/session/session.js init and the map loader): loading a
// map or scenario, applying the Game Setup and Match Settings, and storing a networked
// setup. Split out of main_node.cpp (PLAN P4.6); the state it sets is RtsoMain's.

#include "main_node.h"
#include "l10n.h"
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/os.hpp>
#include <unordered_set>
#include <cstring>
#include <godot_cpp/classes/theme.hpp>
#include "popup_safe.h"
#include "simulation/petra_chat.h"
#include "simulation/rms_generator.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/script.hpp>
#include "user_config.h"
#include "display_colors.h"
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include "../core/diagnostics/logger.h"
#include "../core/diagnostics/perf_tracker.h"
#include "../core/job_system.h"
#include "../graphics/assets/model_library.h"
#include <sstream>
using namespace godot;
using rtso::sprintf0;

// Building names in data/maps/*.json "structures" and "gaia_structures" (tools/port_0ad_maps.py).
// BLD_TYPE_COUNT for a name it doesn't know.
static uint8_t map_building_type(const String &n) {
	if (n == "town_center" || n == "civic" || n == "civil_centre") return rtso::BLD_TOWN_CENTER;
	if (n == "house" || n == "apartment" || n == "hut" || n == "longhouse") return rtso::BLD_HOUSE;
	if (n == "barracks" || n == "hall" || n == "arsenal" || n == "elephant_stable") return rtso::BLD_BARRACKS;
	if (n == "stable") return rtso::BLD_STABLE;
	if (n == "range") return rtso::BLD_RANGE;
	if (n == "storehouse" || n == "forge") return rtso::BLD_STOREHOUSE;
	if (n == "farmstead" || n == "field") return rtso::BLD_FARMSTEAD;
	if (n == "mill" || n == "rotarymill") return rtso::BLD_MILL;
	if (n == "market") return rtso::BLD_MARKET;
	if (n == "tower" || n == "defense_tower") return rtso::BLD_TOWER;
	if (n == "sentry_tower") return rtso::BLD_SENTRY_TOWER;
	if (n == "outpost") return rtso::BLD_OUTPOST;
	if (n == "fortress" || n == "palisades_fort" || n == "keep") return rtso::BLD_FORTRESS;
	if (n == "gate" || n == "wall_gate") return rtso::BLD_GATE;
	if (n == "palisade_gate" || n == "palisades_gate") return rtso::BLD_PALISADE_GATE;
	if (n == "siege_wall" || n == "wallset_siege") return rtso::BLD_SIEGE_WALL;
	if (n == "siege_wall_gate") return rtso::BLD_SIEGE_WALL_GATE;
	if (n == "temple" || n == "tophet" || n == "shrine" || n == "pyramid") return rtso::BLD_TEMPLE;
	if (n == "wonder") return rtso::BLD_WONDER;
	if (n == "dock") return rtso::BLD_DOCK;
	if (n == "wall" || n == "wall_segment") return rtso::BLD_WALL_SEGMENT;
	if (n == "palisade") return rtso::BLD_PALISADE;
	if (n == "wall_tower") return rtso::BLD_WALL_TOWER;
	if (n == "corral") return rtso::BLD_CORRAL;
	if (n == "field_plot") return rtso::BLD_FIELD; // a Gaia field (map "field" stays a farmstead)
	if (n == "mercenary_camp") return rtso::BLD_MERCENARY_CAMP;
	return rtso::BLD_TYPE_COUNT;
}

// A map's wall, palisade or gate laid as a turned WallSet piece (port_0ad_maps.py wall_piece:
// "wallset" stone, palisade or siege; "piece" wall, tower or gate; Position "angle"; "length").
static uint32_t spawn_map_wall(rtso::Simulation &sim, const Dictionary &b, uint8_t player) {
	// structures/palisades_fort is a template_structure_defensive_tower_sentry (arrows, 3 slots,
	// territory) with <Capturable disable=""/>, though its chain names the palisades.
	if (String(b.get("template", "")).ends_with("palisades_fort")) {
		const uint32_t id = sim.spawn_building_world((float)b.get("x", 0.0), (float)b.get("z", 0.0),
				rtso::BLD_SENTRY_TOWER, player, 1.0f, (float)b.get("angle", 0.0));
		if (id) sim.set_structure_uncapturable(id);
		// <Upgrade disable=""/>, <Researcher disable=""/>, <Trainer disable=""/>.
		if (id) sim.set_structure_off(id, rtso::Simulation::STRUCT_OFF_UPGRADE | rtso::Simulation::STRUCT_OFF_RESEARCH |
				rtso::Simulation::STRUCT_OFF_TRAIN);
		return id;
	}
	const String tmpl = b.get("template", "");
	const bool low_wall = tmpl.contains("cart/s_wall_");
	const String ws = b.get("wallset", "stone"), piece = b.get("piece", "wall");
	const uint8_t kind = (ws == "palisade" || low_wall) ? rtso::WALLSET_PALISADE : ws == "siege" ? rtso::WALLSET_SIEGE : rtso::WALLSET_STONE;
	const uint8_t p = piece == "tower" ? rtso::Simulation::MAP_WALL_TOWER
			: piece == "gate" ? rtso::Simulation::MAP_WALL_GATE : rtso::Simulation::MAP_WALL_SEGMENT;
	const uint32_t id = sim.spawn_map_wall_piece(player, kind, p, (float)b.get("x", 0.0), (float)b.get("z", 0.0),
			(float)b.get("angle", 0.0), (float)b.get("length", 9.0));
	// cart/s_wall_*, the Low Wall: palisade classes (Defensive Palisade, no Wall) and its own gate upgrade price.
	if (id && low_wall) sim.set_structure_off(id, rtso::Simulation::STRUCT_LOW_WALL);
	// palisades_spike_* (<TerritoryDecay disable=""/>).
	if (id && (bool)b.get("no_decay", false)) sim.set_structure_no_decay(id);
	if (id && (bool)b.get("no_capture", false)) sim.set_structure_uncapturable(id);
	return id;
}

// The parts a map-only template switches off that its RTS0 type has (Simulation::STRUCT_OFF_*):
// structures/uffington_horse is a wonder with no GarrisonHolder, Researcher or Trainer whose
// obstruction blocks neither movement nor pathfinding.
static uint8_t map_structure_off(const String &tmpl) {
	if (tmpl.ends_with("uffington_horse"))
		return rtso::Simulation::STRUCT_OFF_GARRISON | rtso::Simulation::STRUCT_OFF_RESEARCH | rtso::Simulation::STRUCT_OFF_TRAIN |
				rtso::Simulation::STRUCT_OFF_BLOCK;
	if (tmpl.get_file().begins_with("tent_")) return rtso::Simulation::STRUCT_NOT_CRITICAL; // template_structure_special
	return 0;
}

// A player structure's 0 A.D. template ("athen/field", "spart/gerousia"; port_0ad_maps.py),
// BLD_TYPE_COUNT when RTS0 has no such building.
static uint8_t map_building_template(const String &tmpl) {
	String n = tmpl.get_slice("/", tmpl.get_slice_count("/") - 1);
	// skirmish/structures/default_X: SkirmishReplacer.js puts structures/{civ}/X in its place.
	if (n.begins_with("default_")) n = n.substr(8);
	if (n == "house_5" || n == "house_10") return rtso::BLD_HOUSE;
	static const struct { const char *name; uint8_t type; } kNames[] = {
		{"civil_centre", rtso::BLD_TOWN_CENTER}, {"house", rtso::BLD_HOUSE}, {"barracks", rtso::BLD_BARRACKS},
		{"storehouse", rtso::BLD_STOREHOUSE}, {"farmstead", rtso::BLD_FARMSTEAD}, {"field", rtso::BLD_FIELD},
		{"market", rtso::BLD_MARKET}, {"temple", rtso::BLD_TEMPLE}, {"forge", rtso::BLD_FORGE},
		{"outpost", rtso::BLD_OUTPOST}, {"sentry_tower", rtso::BLD_SENTRY_TOWER}, {"defense_tower", rtso::BLD_TOWER},
		{"arsenal", rtso::BLD_ARSENAL}, {"stable", rtso::BLD_STABLE}, {"range", rtso::BLD_RANGE},
		{"dock", rtso::BLD_DOCK}, {"fortress", rtso::BLD_FORTRESS}, {"wonder", rtso::BLD_WONDER},
		{"corral", rtso::BLD_CORRAL}, {"elephant_stable", rtso::BLD_ELEPHANT_STABLE},
		{"pillar_ashoka", rtso::BLD_PILLAR}, {"apartment", rtso::BLD_APARTMENT},
		// Generic types and civ forms whose stem is no model id (the loop below starts after the
		// generic types): the theatres, embassies, Han court, great tower, practice range and
		// grain field, the Tachara's tribute forms, the German forge and totem.
		{"theater", rtso::BLD_THEATER}, {"embassy", rtso::BLD_EMBASSY}, {"embassy_celtic", rtso::BLD_EMBASSY},
		{"civil_centre_court", rtso::BLD_IMPERIAL_COURT}, {"defense_tower_great", rtso::BLD_TOWER_GREAT},
		{"practice_range", rtso::BLD_RANGE}, {"field_grain", rtso::BLD_FIELD}, {"blacksmith", rtso::BLD_FORGE},
		{"ministry", rtso::BLD_MINISTRY}, {"totem", rtso::BLD_MONUMENT},
		{"amphitheater_pompeii", rtso::BLD_AMPHITHEATER},
		{"tachara_food", rtso::BLD_TACHARA_FOOD}, {"tachara_wood", rtso::BLD_TACHARA_WOOD},
		{"tachara_stone", rtso::BLD_TACHARA_STONE}, {"tachara_metal", rtso::BLD_TACHARA_METAL},
	};
	for (const auto &k : kNames)
		if (n == k.name) return k.type;
	// Civ structures go by their model id (civ_building_data.h: "gerousia", "syssiton").
	for (uint8_t t = rtso::BLD_GYMNASIUM; t < rtso::BLD_TYPE_COUNT; ++t)
		if (n == rtso::building_type_info(t).model_id) return t;
	return rtso::BLD_TYPE_COUNT;
}

// The civ of a 0 A.D. structure template by its folder ("structures/kush/forge": the
// Kushites), as Trainer.js resolves {native}; CIV_COUNT for a civless one.
static uint8_t map_template_civ(const String &tmpl) {
	if (tmpl.get_slice_count("/") < 3) return rtso::CIV_COUNT;
	const String code = tmpl.get_slice("/", tmpl.get_slice_count("/") - 2);
	for (uint8_t c = 0; c < rtso::CIV_COUNT; ++c)
		if (c != rtso::CIV_GAIA && code == rtso::civ_info(c).code) return c;
	return rtso::CIV_COUNT;
}

// Unit class names in data/maps/*.json "units" and "gaia_units".
static rtso::UnitType map_unit_type(const String &n) {
	static const struct { const char *name; rtso::UnitType type; } kTypes[] = {
		{"swordsman", rtso::UNIT_SWORDSMAN}, {"spearman", rtso::UNIT_SPEARMAN}, {"archer", rtso::UNIT_ARCHER},
		{"cavalry", rtso::UNIT_CAVALRY}, {"trader", rtso::UNIT_TRADER}, {"catapult", rtso::UNIT_CATAPULT},
		{"hero", rtso::UNIT_HERO}, {"trireme", rtso::UNIT_TRIREME}, {"elephant", rtso::UNIT_ELEPHANT},
		{"siege_ram", rtso::UNIT_SIEGE_RAM}, {"healer", rtso::UNIT_HEALER}, {"chariot", rtso::UNIT_CHARIOT},
		{"war_dog", rtso::UNIT_WAR_DOG}, {"bolt_shooter", rtso::UNIT_BOLT_SHOOTER}, {"fishing_boat", rtso::UNIT_FISHING_BOAT},
		{"merchant_ship", rtso::UNIT_MERCHANT_SHIP}, {"female_citizen", rtso::UNIT_FEMALE_CITIZEN}, {"carnyx", rtso::UNIT_CARNYX},
		{"worker_elephant", rtso::UNIT_WORKER_ELEPHANT}, {"archer_maiden", rtso::UNIT_ARCHER_MAIDEN}, {"camel", rtso::UNIT_CAMEL},
		{"scythed_chariot", rtso::UNIT_SCYTHED_CHARIOT}, {"minister", rtso::UNIT_MINISTER},
		{"catafalque", rtso::UNIT_CATAFALQUE}, {"wagon", rtso::UNIT_WAGON},
	};
	for (const auto &t : kTypes)
		if (n == t.name) return t.type;
	return rtso::UNIT_VILLAGER;
}

// The map structures 0.28 places by their own template that RTS0's sim folds into another type
// (structures/stall_* and celt_hut/celt_longhouse are houses) draw that template's actor
// (data/building_models.json "look_<template>").
static String map_building_look(const String &tmpl) {
	static const char *kLooks[] = {"stall_small_a", "stall_small_b", "stall_med", "stall_big", "celt_hut", "celt_longhouse"};
	const String stem = tmpl.trim_prefix("structures/");
	for (const char *l : kLooks)
		if (stem == l) return String("look_") + l;
	return String();
}

bool RtsoMain::load_map_seeded(String map_id, uint32_t seed) {
	if (!sim || !view) return false;
	clear_selection();
	view->clear_building_looks();
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
		map_civ_[p] = -1;
		map_has_res_[p] = false;
		map_player_pop_limit_[p] = 0;
		map_team_[p] = 0;
	}
	map_has_teams_ = false;
	map_pop_limit_ = -1;
	random_map_ = false;

	// 1. Procedural Random Map Generation (RMS Engine)
	if (map_id.begins_with("random_") || map_id.begins_with("procedural_") ||
		map_id == "continental" || map_id == "archipelago" || map_id == "oasis" ||
		map_id == "highlands" || map_id == "valley" || map_id == "mediterranean" ||
		map_id == "random_acropolis" || map_id == "random_spartan_valley" ||
		map_id == "danubius" || map_id == "random_danubius" ||
		map_id == "random_saharan_oases" ||
		map_id == "anatolian_plateau" || map_id == "random_anatolian_plateau" ||
		map_id == "jebel_barkal" || map_id == "random_jebel_barkal") {
		
		rtso::MapType mtype = rtso::MAP_RANDOM_CONTINENTAL;
		int num_p = std::clamp(random_map_players, 2, (int)rtso::MAX_PLAYERS);
		if (map_id.contains("archipelago")) mtype = rtso::MAP_RANDOM_ARCHIPELAGO;
		else if (map_id.contains("saharan_oases")) mtype = rtso::MAP_SAHARAN_OASES;
		else if (map_id.contains("oasis")) mtype = rtso::MAP_RANDOM_OASIS;
		else if (map_id.contains("highlands")) mtype = rtso::MAP_RANDOM_HIGHLANDS;
		else if (map_id.contains("valley") && !map_id.contains("spartan")) mtype = rtso::MAP_RANDOM_VALLEY;
		else if (map_id.contains("spartan")) mtype = rtso::MAP_SPARTAN_VALLEY;
		else if (map_id.contains("acropolis")) mtype = rtso::MAP_ACROPOLIS;
		else if (map_id.contains("danubius")) mtype = rtso::MAP_DANUBIUS;
		else if (map_id.contains("anatolian")) mtype = rtso::MAP_ANATOLIAN_PLATEAU;
		else if (map_id.contains("jebel") || map_id.contains("barkal")) mtype = rtso::MAP_JEBEL_BARKAL;
		else if (map_id.contains("mediterranean")) mtype = rtso::MAP_RANDOM_MEDITERRANEAN;
		// Newer maps resolve by id ("random_<id>" or "<id>"), see map_type_info.
		for (int t = rtso::MAP_MAINLAND; t < rtso::MAP_TYPE_COUNT; ++t) {
			String id = rtso::map_type_info((uint8_t)t).id;
			if (map_id == id || map_id == "random_" + id) { mtype = (rtso::MapType)t; break; }
		}

		random_map_ = true;
		load_progress(40, "Generating map");
		sim->generate_random_map(mtype, seed, num_p, (float)random_map_size * 0.5f, random_map_teams);
		load_progress(70, "Building terrain");
		view->rebuild_terrain(sim);

		if (cam_rig) {
			cam_rig->set_simulation(sim);
			cam_rig->set_map_bounds_half(sim->map_half());
			cam_rig->set_map_circular(sim->circular_map());
			for (const auto &b : sim->buildings()) {
				if (b.team == local_player() && rtso::is_civic_centre(b.type)) {
					cam_rig->set_target_pos(Vector3(b.x, 0, b.z));
					break;
				}
			}
		}

		UtilityFunctions::print("[RtsoMain] Generated procedural RMS map '", rtso::map_type_info(mtype).name, "' (seed=", (int64_t)seed, ", players=", num_p, ")");
		return true;
	}

	// RTS0's old "default"/"arena" start (a TC, barracks, houses and RTS0 villagers) is not a
	// 0.28 start: 0.28's default map, Mainland, gives each player its civ's StartEntities.
	if (map_id == "default" || map_id == "arena") return load_map_seeded("random_mainland", seed);

	String path = "res://data/maps/" + map_id + ".json";
	if (!FileAccess::file_exists(path)) {
		path = "res://data/maps/" + map_id;
		if (!FileAccess::file_exists(path)) {
			UtilityFunctions::printerr("[RtsoMain] Map file not found: ", map_id);
			return false;
		}
	}

	load_progress(40, "Loading map");
	Ref<FileAccess> fa = FileAccess::open(path, FileAccess::READ);
	if (!fa.is_valid()) return false;
	String content = fa->get_as_text();
	fa->close();

	Variant parsed = JSON::parse_string(content);
	if (parsed.get_type() != Variant::DICTIONARY) return false;
	Dictionary d = parsed;

	sim->set_sim_seed(seed);
	sim->reset();
	map_pop_limit_ = (int)d.get("pop_limit", -1);
	{
		// 0.28 StartingResources.onMapChange takes a map's PlayerData Resources for
		// scenarios only; a skirmish map (White Cliffs of Dover) keeps the lobby's
		// resources. Civs stay locked on every map type (PlayerCiv._set).
		bool scenario = true;
		{
			Variant mt = JSON::parse_string(FileAccess::get_file_as_string("res://data/map_types.json"));
			if (mt.get_type() == Variant::DICTIONARY) {
				Dictionary maps = Dictionary(mt).get("maps", Dictionary());
				Dictionary e = maps.get(map_id.get_file().get_basename(), Dictionary());
				if (e.has("type")) scenario = String(e["type"]) == "scenario";
			}
		}
		Array pl = d.get("players", Array());
		for (int i = 0; i < pl.size(); ++i) {
			Dictionary p = pl[i];
			const int id = (int)p.get("id", 1);
			const uint8_t team = id <= 1 ? 0 : (uint8_t)(id - 1);
			if (team >= rtso::MAX_PLAYERS) continue;
			const int c = (int)p.get("civ", -1);
			if (rtso::is_playable_civ(c)) map_civ_[team] = (int8_t)c;
			if (scenario && p.has("resources")) {
				Dictionary r = p["resources"];
				const char *keys[4] = {"food", "wood", "stone", "metal"};
				for (int k = 0; k < 4; ++k) map_res_[team][k] = (int)r.get(keys[k], 300); // Player.js: 300 of each unless set
				map_has_res_[team] = true;
			}
			// 0.28 PlayerData PopulationLimit (InitGame.js SetPerPlayerPopulationCaps).
			if (p.has("pop_limit")) map_player_pop_limit_[team] = std::max(0, (int)p["pop_limit"]);
			if (p.has("team")) {
				const int t = (int)p["team"];
				map_team_[team] = (uint8_t)(t >= 0 && t < rtso::MAX_PLAYERS ? t + 1 : 0);
				// port_0ad_maps.py gives every player "team": id - 1 by default; only a
				// map with LockTeams (another value somewhere) sets the teams itself.
				if (t != id - 1) map_has_teams_ = true;
			}
		}
		// Civs before the units: spawn_unit takes its owner's roster.
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p)
			if (map_civ_[p] >= 0) sim->set_team_civ(p, (uint8_t)map_civ_[p]);
	}

	String name = d.get("name", map_id);
	String biome = d.get("biome", "mediterranean");
	float world_size = (float)d.get("world_size", 256.0f);
	float water_h = (float)d.get("water_height", -1.0f);
	int h_res = (int)d.get("heightmap_res", 64);

	sim->set_map_half(world_size * 0.5f);
	// 0.28 ScriptSettings CircularMap ("circular", from port_0ad_maps.py); square when absent.
	sim->set_circular_map((bool)d.get("circular", false));
	sim->set_map_info(map_id.utf8().get_data(), name.utf8().get_data(), biome.utf8().get_data());

	Array h_arr = d.get("heightmap", Array());
	std::vector<float> heights;
	heights.resize(h_arr.size());
	for (int i = 0; i < h_arr.size(); ++i) {
		heights[i] = (float)h_arr[i];
	}
	sim->set_terrain_data(heights, h_res, water_h);
	// 0 A.D. per-tile terrain textures (port_0ad_maps.py extract_pmp_terrain).
	if (d.has("terrain") && d["terrain"].get_type() == Variant::DICTIONARY) {
		Dictionary t = d["terrain"];
		Array tn = t.get("names", Array());
		std::vector<std::string> names;
		for (int i = 0; i < tn.size(); ++i) names.push_back(String(tn[i]).utf8().get_data());
		sim->set_terrain_tiles(rtso::Simulation::terrain_tiles_from_rle((int)t.get("res", 0), std::move(names),
				String(t.get("rle", "")).utf8().get_data()));
	}

	// Spawn Resources
	Array res_arr = d.get("resources", Array());
	for (int i = 0; i < res_arr.size(); ++i) {
		Dictionary r = res_arr[i];
		String rtype_str = r.get("type", "tree");
		float rx = (float)r.get("x", 0.0);
		float rz = (float)r.get("z", 0.0);
		int amt = (int)r.get("amount", 600);

		uint8_t rtype = rtso::RES_WOOD;
		uint8_t rsub = rtso::RES_SUB_NONE;
		// A plant name ("pine", "berry", ...) is a tree or fruit supply of that FloraKind.
		const uint8_t flora = rtso::flora_kind_by_name(rtype_str.utf8().get_data());
		if (flora < rtso::FLORA_COUNT && rtso::flora_is_fruit(flora)) rtype = rtso::RES_FOOD;
		else if (rtype_str == "gold" || rtype_str == "metal") rtype = rtso::RES_GOLD;
		else if (rtype_str == "stone" || rtype_str == "rock") rtype = rtso::RES_STONE;
		else if (rtype_str == "food" || rtype_str == "fruit") rtype = rtso::RES_FOOD;
		else if (rtype_str == "fish") { // 0 A.D. gaia/fish/*: only fishing boats gather it
			rtype = rtso::RES_FOOD;
			rsub = rtso::RES_SUB_FISH;
			if (!r.has("amount")) amt = rtso::FISH_FOOD_SUPPLY;
		}

		// 0 A.D. ResourceSupply/MaxGatherers when the template differs from its class (banyan 12, baobab 9).
		const int maxg = std::clamp((int)r.get("max_gatherers", 0), 0, 255);
		sim->spawn_resource_world(rx, rz, rtype, amt, rsub, (uint8_t)maxg, flora < rtso::FLORA_COUNT ? flora : rtso::FLORA_DEFAULT);
	}

	// Spawn Wildlife Fauna
	Array fauna_arr = d.get("fauna", Array());
	for (int i = 0; i < fauna_arr.size(); ++i) {
		Dictionary f = fauna_arr[i];
		String ftype_str = f.get("type", "deer");
		float fx = (float)f.get("x", 0.0);
		float fz = (float)f.get("z", 0.0);
		// 0 A.D. template stem ("bear_brown" / "fauna_bear_brown"); older maps use deer/boar/sheep/wolf.
		uint8_t ftype = rtso::fauna_type_by_name(ftype_str.utf8().get_data());
		if (ftype >= rtso::FAUNA_COUNT) ftype = rtso::FAUNA_DEER;
		// A player's livestock carries its owner (map player 1 is slot 0).
		const int fp = (int)f.get("player", 0);
		const uint8_t owner = fp <= 0 ? rtso::GAIA_PLAYER : (uint8_t)std::min(fp - 1, rtso::MAX_PLAYERS - 1);
		sim->spawn_fauna(fx, fz, ftype, owner);
	}
	// 0 A.D. gaia/treasure/* and gaia/ruins/* by template stem (tools/port_0ad_maps.py).
	Array treasure_arr = d.get("treasures", Array());
	for (int i = 0; i < treasure_arr.size(); ++i) {
		Dictionary t = treasure_arr[i];
		const uint8_t tt = rtso::treasure_type_by_name(String(t.get("type", "")).utf8().get_data());
		if (tt < rtso::TREASURE_TYPE_COUNT) sim->spawn_treasure((float)t.get("x", 0.0), (float)t.get("z", 0.0), tt);
	}
	Array ruins_arr = d.get("ruins", Array());
	for (int i = 0; i < ruins_arr.size(); ++i) {
		Dictionary r = ruins_arr[i];
		const uint8_t rt = rtso::ruins_type_by_name(String(r.get("type", "")).utf8().get_data());
		if (rt < rtso::RUINS_TYPE_COUNT) sim->spawn_ruins((float)r.get("x", 0.0), (float)r.get("z", 0.0), rt);
	}

	// Spawn Structures. Each finished building would otherwise rebuild the
	// whole nav grid and territory map; do that once after the last one.
	sim->begin_bulk_spawn();
	Array str_arr = d.get("structures", Array());
	for (int i = 0; i < str_arr.size(); ++i) {
		Dictionary b = str_arr[i];
		String stype_str = b.get("type", "house");
		int player = (int)b.get("player", 1);
		float bx = (float)b.get("x", 0.0);
		float bz = (float)b.get("z", 0.0);
		uint8_t team = (player <= 1) ? 0 : (uint8_t)(player - 1);
		if (team >= rtso::MAX_PLAYERS) team = 1;

		// SkirmishReplacer.js: a civ's SkirmishReplacements keep one of default_house_5 and
		// default_house_10 (the size of its own house; Han lists neither) and the empty
		// replacement destroys the piece (the Spartans' default_wall_*). Only the Iberians'
		// SkirmishReplacements keep skirmish/structures/iber_wall_* (their Starting Walls).
		const String tmpl = b.get("template", "");
		const uint8_t civ = (uint8_t)sim->player(team).civ;
		if (tmpl.ends_with("default_house_5") || tmpl.ends_with("default_house_10")) {
			const int pop = rtso::civ_building_type_info(civ, rtso::BLD_HOUSE).pop_bonus;
			if (civ == rtso::CIV_HAN || (pop == 5) != tmpl.ends_with("default_house_5")) continue;
		}
		if (civ == rtso::CIV_SPARTANS && tmpl.contains("default_wall_")) continue;
		if (civ != rtso::CIV_IBERIANS && tmpl.contains("/iber_wall_")) continue;
		if (b.has("wallset")) {
			spawn_map_wall(*sim, b, team);
			continue;
		}
		// The structures only maps place (bolt towers, stoas, tents) by their template, then the
		// generic and civ structures by template or name.
		uint8_t btype = rtso::map_structure_template(tmpl.utf8().get_data()).type;
		if (btype >= rtso::BLD_TYPE_COUNT && !tmpl.is_empty()) btype = map_building_template(tmpl);
		if (btype >= rtso::BLD_TYPE_COUNT) btype = map_building_type(stype_str);
		if (btype >= rtso::BLD_TYPE_COUNT) {
			if (stype_str.is_empty()) continue; // a civ structure RTS0 doesn't have
			btype = rtso::BLD_HOUSE;
		}
		const uint32_t bid = sim->spawn_building_world(bx, bz, btype, team, 1.0f);
		if (bid && map_structure_off(tmpl)) sim->set_structure_off(bid, map_structure_off(tmpl));
		if (bid && tmpl.contains("mill_field_wheat")) sim->setup_mill_field_wheat(bid);
		const String look = map_building_look(tmpl);
		if (bid && !look.is_empty()) view->set_building_look(bid, btype, look);
	}
	// Gaia-owned structures (Player 0 in 0 A.D.'s map: towers, palisades, houses, docks,
	// structures/merc_camp_egyptian): hostile to everyone, captured or destroyed like 0 A.D.'s.
	Array gaia_str_arr = d.get("gaia_structures", Array());
	std::vector<uint32_t> gaia_structure_ids(gaia_str_arr.size(), 0);
	for (int i = 0; i < gaia_str_arr.size(); ++i) {
		Dictionary b = gaia_str_arr[i];
		if (b.has("wallset")) {
			gaia_structure_ids[i] = spawn_map_wall(*sim, b, rtso::GAIA_PLAYER);
			continue;
		}
		// structures/shrine_*, trading_post_*, rome/tent...: their own types, keeping the template's
		// civ; then civ structures by template (kush/temple_amun, ptol/military_colony, kush/camp_noba)
		// with their folder's civ, so a captured one trains its {native} roster; then by name.
		const String gtmpl = b.get("template", "");
		const rtso::MapStructureTemplate gt = rtso::map_structure_template(gtmpl.utf8().get_data());
		uint8_t btype = gt.type, civ = gt.type < rtso::BLD_TYPE_COUNT ? gt.civ : (uint8_t)rtso::CIV_COUNT;
		if (btype >= rtso::BLD_TYPE_COUNT && !gtmpl.is_empty()) {
			btype = map_building_template(gtmpl);
			civ = map_template_civ(gtmpl);
		}
		if (btype >= rtso::BLD_TYPE_COUNT) btype = map_building_type(b.get("type", ""));
		if (btype >= rtso::BLD_TYPE_COUNT) continue;
		gaia_structure_ids[i] = sim->spawn_building_world((float)b.get("x", 0.0), (float)b.get("z", 0.0), btype, rtso::GAIA_PLAYER, 1.0f);
		if (gaia_structure_ids[i] && map_structure_off(gtmpl)) sim->set_structure_off(gaia_structure_ids[i], map_structure_off(gtmpl));
		if (gaia_structure_ids[i] && gtmpl.contains("mill_field_wheat")) sim->setup_mill_field_wheat(gaia_structure_ids[i]);
		if (civ < rtso::CIV_COUNT && gaia_structure_ids[i]) sim->set_building_civ(gaia_structure_ids[i], civ);
		const String look = map_building_look(gtmpl);
		if (gaia_structure_ids[i] && !look.is_empty()) view->set_building_look(gaia_structure_ids[i], btype, look);
	}
	// Gaia scenery (fences, columns, obelisks): oriented obstructions.
	Array scenery_arr = d.get("scenery", Array());
	for (int i = 0; i < scenery_arr.size(); ++i) {
		Dictionary sc = scenery_arr[i];
		const uint8_t st = rtso::scenery_type_by_name(String(sc.get("type", "")).utf8().get_data());
		if (st < rtso::SCENERY_TYPE_COUNT)
			sim->spawn_scenery((float)sc.get("x", 0.0), (float)sc.get("z", 0.0), st, (float)sc.get("angle", 0.0),
					(float)sc.get("scale", 0.25));
	}
	sim->end_bulk_spawn();

	// Spawn Starting Units
	Array units_arr = d.get("units", Array());
	std::vector<uint32_t> unit_ids(units_arr.size(), 0);
	for (int i = 0; i < units_arr.size(); ++i) {
		Dictionary u = units_arr[i];
		String utype_str = u.get("type", "villager");
		int player = (int)u.get("player", 1);
		float ux = (float)u.get("x", 0.0);
		float uz = (float)u.get("z", 0.0);
		uint8_t team = (player <= 1) ? 0 : (uint8_t)(player - 1);
		if (team >= rtso::MAX_PLAYERS) team = 1;

		// The map's own roster template when RTS0 has it ("spart/infantry_javelineer_b", an _a/_e
		// one as its _b row promoted); a skirmish map's skirmish/units/* becomes the owner's civ
		// unit, or nothing.
		const rtso::UnitType utype = map_unit_type(utype_str);
		if (u.has("template")) {
			const CharString t = String(u["template"]).utf8();
			uint16_t tmpl = rtso::CIV_UNIT_TEMPLATE_NONE;
			const int replaced = rtso::skirmish_replace(sim->team_civ(team), t.get_data(), tmpl);
			if (replaced < 0) continue;
			if (replaced) {
				unit_ids[i] = sim->spawn_map_unit(ux, uz, team, utype, rtso::civ_unit_template(tmpl).id);
				continue;
			}
			// A map-only variant (rome/champion_infantry_swordsman_02) stands in as the nearest
			// template it inherits from that RTS0 has ("parents"), at the variant's own rank.
			int rank = 0;
			CharString pick = t;
			bool resolved = rtso::Simulation::resolve_map_template(t.get_data(), &rank) != rtso::CIV_UNIT_TEMPLATE_NONE;
			if (!resolved) {
				Array parents = u.get("parents", Array());
				for (int k = 0; k < parents.size() && !resolved; ++k) {
					const CharString pt = String(parents[k]).utf8();
					if (rtso::Simulation::resolve_map_template(pt.get_data(), &rank) != rtso::CIV_UNIT_TEMPLATE_NONE) { pick = pt; resolved = true; }
				}
			}
			// port_0ad_maps.py gives a unit no type when its name says nothing (units/plane): with
			// no template of its own or a parent's to stand in, it is left out.
			if (!resolved && utype_str.is_empty()) continue;
			unit_ids[i] = sim->spawn_map_unit(ux, uz, team, utype, pick.get_data(), rank,
					String::utf8(t.get_data()).ends_with("_unpacked"));
			continue;
		}
		unit_ids[i] = sim->spawn_map_unit(ux, uz, team, utype, nullptr);
	}
	// "garrison_in": index of the ship the unit starts aboard (0 A.D. <Garrison>,
	// e.g. sicilia_nomad_2p). Passengers sit on the ship, so boarding is instant.
	for (int i = 0; i < units_arr.size(); ++i) {
		Dictionary u = units_arr[i];
		int holder = (int)u.get("garrison_in", -1);
		if (holder < 0 || holder >= (int)unit_ids.size() || !unit_ids[i] || !unit_ids[holder]) continue;
		sim->order_board_transport(&unit_ids[i], 1, unit_ids[holder]);
	}
	// The map's Gaia catafalques (relics), each its own civ's ("civ"; -1 picks one from the id).
	Array relics_arr = d.get("relics", Array());
	for (int i = 0; i < relics_arr.size(); ++i) {
		Dictionary r = relics_arr[i];
		const int civ = (int)r.get("civ", -1);
		// spawn_relic takes the index playable_civ() maps to the civ.
		const int pick = rtso::is_playable_civ(civ) ? (civ < rtso::CIV_GAIA ? civ : civ - 1) : -1;
		sim->spawn_relic((float)r.get("x", 0.0), (float)r.get("z", 0.0), pick);
	}
	// Gaia-owned units: 0 A.D. template ("gaul/infantry_javelineer_a"), its rank, and the
	// Gaia ship or structure it starts in.
	Array gaia_units_arr = d.get("gaia_units", Array());
	std::vector<uint32_t> gaia_unit_ids(gaia_units_arr.size(), 0);
	for (int i = 0; i < gaia_units_arr.size(); ++i) {
		Dictionary u = gaia_units_arr[i];
		gaia_unit_ids[i] = sim->spawn_gaia_unit((float)u.get("x", 0.0), (float)u.get("z", 0.0),
				map_unit_type(u.get("type", "")), String(u.get("template", "")).utf8().get_data(), (int)u.get("rank", 1));
	}
	for (int i = 0; i < gaia_units_arr.size(); ++i) {
		Dictionary u = gaia_units_arr[i];
		if (!gaia_unit_ids[i]) continue;
		const int ship = (int)u.get("garrison_in", -1), str = (int)u.get("garrison_in_structure", -1);
		if (ship >= 0 && ship < (int)gaia_unit_ids.size() && gaia_unit_ids[ship])
			sim->order_board_transport(&gaia_unit_ids[i], 1, gaia_unit_ids[ship]);
		else if (str >= 0 && str < (int)gaia_structure_ids.size() && gaia_structure_ids[str])
			sim->garrison_unit_now(gaia_unit_ids[i], gaia_structure_ids[str]);
	}

	// 0 A.D. vision_revealers/* (Vision/Range in metres): their owner's map starts explored
	// round them (Simulation::explore_circle). Player 1 is slot 0; player 0 (Gaia) reveals nothing.
	Array rev_arr = d.get("revealers", Array());
	for (int i = 0; i < rev_arr.size(); ++i) {
		Dictionary rv = rev_arr[i];
		const int owner = (int)rv.get("player", 0);
		if (owner <= 0 || owner > rtso::MAX_PLAYERS) continue;
		const float range_m = (float)rv.get("range", 50.0);
		sim->explore_circle((uint8_t)(owner - 1), (float)rv.get("x", 0.0), (float)rv.get("z", 0.0), range_m / 4.0f);
	}

	// TriggerPoint.js markers the scripts spawn at or aim for (owner 0 is Gaia).
	Array tp_arr = d.get("trigger_points", Array());
	for (int i = 0; i < tp_arr.size(); ++i) {
		Dictionary tp = tp_arr[i];
		const String ref = tp.get("ref", "A");
		const int owner = (int)tp.get("owner", 0);
		if (ref.is_empty()) continue;
		sim->add_trigger_point((uint8_t)ref.unicode_at(0), (float)tp.get("x", 0.0), (float)tp.get("z", 0.0),
				owner <= 0 ? rtso::GAIA_PLAYER : (uint8_t)std::min(owner - 1, rtso::MAX_PLAYERS - 1));
	}
	// The map's <Paths> (MapReader ReadPaths): cinematic camera paths its scripts play.
	Array cp_arr = d.get("cinema_paths", Array());
	for (int i = 0; i < cp_arr.size(); ++i) {
		Dictionary cp = cp_arr[i];
		rtso::CinemaPath path;
		const CharString name = String(cp.get("name", "")).utf8();
		std::strncpy(path.info.name, name.get_data(), sizeof(path.info.name) - 1);
		// 0 A.D. takes names up to 128 characters; RTS0 saves 31 (CinemaPathInfo), so a
		// longer one could not be queued by its full name.
		if (name.length() >= (int)sizeof(path.info.name))
			UtilityFunctions::push_warning(vformat("[Cinema] Path name '%s' is longer than %d bytes and is cut short.", String(cp.get("name", "")), (int)sizeof(path.info.name) - 1));
		path.info.timescale = (float)(double)cp.get("timescale", 1.0);
		path.info.mode = rtso::cinema_mode_from_name(String(cp.get("mode", "ease_inout")).utf8().get_data());
		path.info.style = rtso::cinema_style_from_name(String(cp.get("style", "default")).utf8().get_data());
		path.info.look_at_target = String(cp.get("orientation", "")) == "target" ? 1 : 0;
		auto nodes = [](const Array &arr, std::vector<rtso::CinemaNode> &out) {
			for (int k = 0; k < arr.size(); ++k) {
				const Array n = arr[k];
				if (n.size() < 4) continue;
				out.push_back(rtso::CinemaNode{(float)(double)n[0], (float)(double)n[1], (float)(double)n[2], (float)(double)n[3]});
			}
		};
		nodes(cp.get("nodes", Array()), path.nodes);
		nodes(cp.get("targets", Array()), path.targets);
		if (path.info.look_at_target && path.targets.empty()) path.info.look_at_target = 0;
		sim->add_cinema_path(path);
	}
	// The map's TriggerScripts (0 A.D. ScriptSettings), ported to C++ (triggers.h).
	Array trig_arr = d.get("trigger_scripts", Array());
	for (int i = 0; i < trig_arr.size(); ++i) {
		const String script = trig_arr[i];
		if (!sim->register_trigger_script(script.utf8().get_data()))
			UtilityFunctions::printerr("[RtsoMain] Map trigger script not ported: ", script);
	}

	// Rebuild World View 3D terrain and bounds
	load_progress(70, "Building terrain");
	view->rebuild_terrain(sim);
	if (cam_rig) {
		cam_rig->set_simulation(sim);
		cam_rig->set_map_bounds_half(sim->map_half());
		// 0 A.D. opens on the player's civic centre (session.js initGUIObjects
		// jumps to it); scenarios without one fall back to Player 1's start. A map's
		// PlayerData StartingCamera (port_0ad_maps.py "camera") wins: Game.cpp sets the
		// camera there when the player has one.
		bool framed = false;
		Array players_arr = d.get("players", Array());
		for (int i = 0; i < players_arr.size() && !framed; ++i) {
			Dictionary p = players_arr[i];
			if ((int)p.get("id", 0) - 1 != (int)local_player() || !p.has("camera")) continue;
			Dictionary cam = p["camera"];
			cam_rig->set_target_pos(Vector3((float)cam.get("x", 0.0), 0, (float)cam.get("z", 0.0)));
			framed = true;
		}
		for (const auto &b : sim->buildings()) {
			if (framed) break;
			if (b.team == local_player() && rtso::is_civic_centre(b.type) && !b.wrecked) {
				cam_rig->set_target_pos(Vector3(b.x, 0, b.z));
				framed = true;
				break;
			}
		}
		if (!framed && !players_arr.is_empty()) {
			Dictionary p1 = players_arr[0];
			float start_x = (float)p1.get("start_x", 0.0);
			float start_z = (float)p1.get("start_z", 0.0);
			cam_rig->set_target_pos(Vector3(start_x, 0, start_z));
		}
	}

	apply_map_player_overrides();
	UtilityFunctions::print("[RtsoMain] Loaded 0 A.D. map '", name, "' (", world_size, "m, ", (int64_t)sim->resource_count(), " resources, ", (int64_t)sim->building_count(), " structures, ", (int64_t)sim->unit_count(), " units)");
	return true;
}

// Apply the full GameSetup staged by NMT_GAME_START (map+seed+options).
// Order mirrors _ready: map first (reset inside), options after.
void RtsoMain::apply_game_setup(const rtso::GameSetupParams &setup) {
	if (!sim) return;
	// Every peer generates the same map: the size rides in NMT_GAME_START.
	random_map_size = (setup.map_size >= 128 && setup.map_size <= 512) ? setup.map_size : 128;
	match_seed_ = setup.seed;
	turn_mgr_.set_offline_seat(0); // the lockstep seating places every client
	random_map_players = 2;
	for (uint8_t i = 0; i < setup.slot_count && i < rtso::MAX_PLAYERS; ++i)
		if (setup.slot_types[i] != 'x') random_map_players = std::max(random_map_players, (int)i + 1);
	for (uint8_t i = 0; i < rtso::MAX_PLAYERS; ++i)
		random_map_teams[i] = (i < setup.slot_count && setup.teams[i] >= 1 && setup.teams[i] <= 8) ? setup.teams[i] : 0;
	sim->set_biome_override((int)setup.biome - 1);
	sim->set_team_placement(setup.team_placement);
	sim->set_map_landscape(setup.landscape);
	sim->set_trigger_difficulty(setup.trigger_difficulty);
	sim->set_map_daytime_choice(setup.daytime);
	// Civs before the map, so starting units take their owner's roster templates.
	for (uint8_t t = 0; t < rtso::MAX_PLAYERS && t < setup.slot_count; ++t) {
		if (rtso::is_playable_civ(setup.civs[t])) sim->set_team_civ(t, setup.civs[t]);
	}
	sim->set_nomad(setup.nomad != 0);
	if (!load_map_seeded(String(setup.map_id.c_str()), setup.seed)) {
		if (lobby_status) lobby_status->set_text("GameStart failed: map regen failed");
		return;
	}
	sim->set_victory_condition(rtso::valid_victory_code(setup.victory) ? setup.victory : 0);
	sim->set_waves_enabled(setup.waves != 0);
	sim->set_last_man_standing(setup.lms != 0);
	if (setup.world_pop) sim->set_world_pop_cap(setup.world_pop);
	else if (setup.team_pop) sim->set_team_pop_cap(setup.team_pop);
	else sim->set_pop_cap(setup.pop_cap);
	sim->set_ceasefire_seconds(60.0f * (float)setup.ceasefire);
	sim->set_reveal_map(setup.reveal != 0);
	sim->set_explore_map(setup.explore != 0);
	sim->set_ally_view(setup.ally != 0);
	sim->set_wonder_duration(60.0f * (float)setup.wonder_min);
	sim->set_relic_duration(60.0f * (float)setup.relic_min);
	sim->set_relic_count(setup.relics);
	sim->set_regicide_garrison(setup.regicide_garrison != 0);
	sim->set_lock_teams(setup.lock_teams != 0);
	sim->set_cheats_enabled(setup.cheats != 0);
	sim->set_disable_spies(setup.no_spies != 0);
	sim->set_disable_treasures(setup.no_treasures != 0);
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) sim->set_player_color(p, setup.colors[p]);
	sim->set_water_level_mode(setup.water_level);
	sim->set_sea_level_rise_minutes(setup.sea_rise_min);
	{
		uint8_t teams[rtso::MAX_PLAYERS] = {};
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS && p < setup.slot_count; ++p) teams[p] = setup.teams[p];
		sim->set_setup_teams(teams);
	}
	sim->apply_team_diplomacy();
	sync_victory_selector();
	sim->set_ai_seed(setup.ai_seed);
	sim->set_petra_difficulty(setup.difficulty > 5 ? 3 : setup.difficulty);
	sim->set_petra_behavior(setup.behavior > 3 ? 2 : setup.behavior);
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
		sim->set_petra_difficulty(p, setup.diffs[p]);
		sim->set_petra_behavior(p, setup.behs[p]);
	}
	const uint8_t sr = setup.start_res < rtso::Simulation::STARTING_RESOURCE_PRESET_COUNT ? setup.start_res : 1;
	sim->set_starting_resources_preset(sr);
	for (uint8_t t = 0; t < rtso::MAX_PLAYERS && t < setup.slot_count; ++t) {
		if (rtso::is_playable_civ(setup.civs[t])) sim->set_team_civ(t, setup.civs[t]);
	}
	apply_map_player_overrides();
	// Seat every slot: Petra runs only for 'a' slots, so a second human in
	// slot 1 plays their own units instead of sharing them with the AI.
	for (uint8_t t = 0; t < rtso::MAX_PLAYERS; ++t) {
		const bool seated = t < setup.slot_count && setup.slot_types[t] != 'x';
		sim->set_player_active(t, seated, seated && setup.slot_types[t] == 'a');
	}
	clear_selection();
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (uview.team[i] == local_player()) selected_ids.push_back((int32_t)uview.id[i]);
	}
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		ms->set("map_id", String(setup.map_id.c_str()));
		ms->set("seed", (int64_t)setup.seed);
		ms->set("victory", (int)sim->victory_condition());
		ms->set("waves_enabled", sim->waves_enabled());
		ms->set("last_man_standing", sim->last_man_standing());
		ms->set("pop_cap", sim->team_pop_cap() > 0 ? sim->team_pop_cap() : sim->pop_cap_setting()); // negative: World Population
		ms->set("team_pop", sim->team_pop_cap() > 0);
		ms->set("ceasefire_min", (int)(sim->ceasefire_seconds() / 60.0f));
		ms->set("reveal_map", sim->reveal_map());
		ms->set("explore_map", sim->explore_map());
		ms->set("ally_view", sim->ally_view());
		ms->set("wonder_min", (int)(sim->wonder_duration() / 60.0f));
		ms->set("relic_min", (int)(sim->relic_duration() / 60.0f));
		ms->set("relic_count", sim->relic_count());
		ms->set("biome", (int)setup.biome);
		Array sdiffs, sbehs;
		for (uint8_t i = 0; i < setup.slot_count && i < 8; ++i) {
			sdiffs.push_back((int)setup.diffs[i]);
			sbehs.push_back((int)setup.behs[i]);
		}
		ms->set("slot_diffs", sdiffs);
		ms->set("slot_behs", sbehs);
		Array scolors;
		for (uint8_t i = 0; i < setup.slot_count && i < 8; ++i) scolors.push_back((int)setup.colors[i]);
		ms->set("slot_colors", scolors);
		ms->set("nomad", setup.nomad != 0);
		ms->set("regicide_garrison", setup.regicide_garrison != 0);
		ms->set("lock_teams", setup.lock_teams != 0);
		ms->set("cheats", setup.cheats != 0);
		ms->set("disable_spies", setup.no_spies != 0);
		ms->set("disable_treasures", setup.no_treasures != 0);
		Array snames;
		for (uint8_t i = 0; i < setup.slot_count && i < 8; ++i) snames.push_back(String::utf8(setup.names[i].c_str()));
		ms->set("slot_names", snames);
		apply_player_names(snames);
		ms->set("water_level", sim->water_level_mode());
		ms->set("team_placement", (int)setup.team_placement);
		ms->set("landscape", (int)setup.landscape);
		ms->set("trigger_difficulty", (int)setup.trigger_difficulty);
		ms->set("map_daytime", (int)setup.daytime);
		ms->set("sea_level_rise_min", sim->sea_level_rise_minutes());
		ms->set("map_size", random_map_size);
		ms->set("ai_difficulty", (int)sim->petra_difficulty());
		ms->set("ai_behavior", (int)sim->petra_behavior());
		ms->set("start_res", (int)sr);
		Array civs;
		for (uint8_t t = 0; t < 8 && t < setup.slot_count; ++t) civs.push_back((int)setup.civs[t]);
		ms->set("slot_civs", civs);
		ms->set("player_civ", (int)sim->player_civ());
		ms->set("ai_civ", (int)sim->team_civ(1));
	}
}

// Full GameSetup hand-off (0 A.D. page_gamesetup -> match launch).
// Runs at the end of _ready (view + cam_rig exist) and mirrors
// apply_game_setup (NMT_GAME_START path): map first via load_map_seeded,
// then victory / Petra / starting resources / slot civs.
void RtsoMain::apply_match_settings() {
	if (!sim) return;
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	if (!ms) {
		load_map_seeded("random_mainland", (uint32_t)Time::get_singleton()->get_ticks_msec());
		return;
	}
	String map_id = (String)ms->get("map_id");
	uint32_t seed = (uint32_t)(int64_t)(int)ms->get("seed");
	if (seed == 0 && !(bool)ms->get("seed_exact")) seed = (uint32_t)Time::get_singleton()->get_ticks_msec();
	match_seed_ = seed; // replays and saves rebuild the map from it
	if (map_id.is_empty()) map_id = "random_continental";
	int victory = (int)ms->get("victory");
	int diff = (int)ms->get("ai_difficulty");
	int behavior = (int)ms->get("ai_behavior");
	int start_res = (int)ms->get("start_res");
	// 0 A.D. map sizes; absent or out of range keeps the 128 default.
	int map_size = (int)ms->get("map_size");
	random_map_size = (map_size >= 128 && map_size <= 512) ? map_size : 128;
	Array slot_civs = ms->get("slot_civs");
	// Normalize legacy fields: if the setup screen never ran, slot_civs
	// may be empty - fall back to player_civ/ai_civ.
	int civ = (int)ms->get("player_civ");
	int ai = (int)ms->get("ai_civ");
	if (slot_civs.size() >= 2) {
		civ = (int)slot_civs[0];
		ai = (int)slot_civs[1];
	}
	// Seats from the lobby (0 human, 1 AI, 2 closed); a random map gets a base for each.
	Array slot_types = ms->get("slot_types");
	int seats = 0;
	for (int i = 0; i < slot_types.size() && i < rtso::MAX_PLAYERS; ++i)
		if ((int)slot_types[i] != 2) seats = i + 1;
	random_map_players = std::max(2, seats);
	// 0.28 PlayerAssignment: Game Setup seats you in local_slot (-1 watches); older
	// setups (-2) seat you in slot 1 and watch only when no slot is Human.
	const int local_slot = String(ms->get("net_mode")).is_empty() ? (int)ms->get("local_slot") : -2;
	const bool legacy_seat = local_slot < -1;
	local_observer_ = seats > 0;
	if (legacy_seat) {
		for (int i = 0; i < seats; ++i)
			if ((int)slot_types[i] == 0) local_observer_ = false;
	} else if (local_slot >= 0 && local_slot < seats && local_slot < slot_types.size() && (int)slot_types[local_slot] == 0) {
		local_observer_ = false;
	}
	turn_mgr_.set_offline_seat(!legacy_seat && !local_observer_ ? (uint8_t)local_slot : (uint8_t)0);
	{
		Array slot_teams_arr = ms->get("slot_teams");
		for (int i = 0; i < rtso::MAX_PLAYERS; ++i) {
			const int tm = i < slot_teams_arr.size() ? (int)slot_teams_arr[i] : 0;
			random_map_teams[i] = (uint8_t)((tm >= 1 && tm <= 8) ? tm : 0);
		}
	}
	sim->set_biome_override((int)ms->get("biome") - 1); // 0 = the map's own biome
	sim->set_team_placement((int)ms->get("team_placement"));
	sim->set_map_landscape((int)ms->get("landscape"));
	sim->set_trigger_difficulty((int)ms->get("trigger_difficulty"));
	sim->set_map_daytime_choice((int)ms->get("map_daytime"));
	// Civs before the map: spawn_unit gives each starting unit its owner's
	// roster template (Simulation::reset keeps civs), as 0 A.D. spawns
	// StartEntities from the player's own civ.
	if (rtso::is_playable_civ(civ)) sim->set_player_civ((uint8_t)civ);
	if (rtso::is_playable_civ(ai)) sim->set_team_civ(1, (uint8_t)ai);
	for (int t = 0; t < rtso::MAX_PLAYERS && t < slot_civs.size(); ++t) {
		const int sc = (int)slot_civs[t];
		if (rtso::is_playable_civ(sc)) sim->set_team_civ((uint8_t)t, (uint8_t)sc);
	}
	sim->set_nomad((bool)ms->get("nomad")); // random maps only (generate_random_map)
	if (!load_map_seeded(map_id, seed)) {
		UtilityFunctions::printerr("[RtsoMain] GameSetup map failed: ", map_id, " - Mainland.");
		if (rtso::is_playable_civ(civ)) sim->set_player_civ((uint8_t)civ);
		if (rtso::is_playable_civ(ai)) sim->set_team_civ(1, (uint8_t)ai);
		if (!load_map_seeded("random_mainland", seed)) sim->spawn_initial();
		return;
	}
	{
		// A campaign level is named by its Name (CampaignMenu.startScenario mapName).
		const String level_name = ms->get("campaign_map_name");
		if (!level_name.is_empty())
			sim->set_map_info(sim->current_map_id(), level_name.utf8().get_data(), sim->current_map_biome());
	}
	if (victory >= 0 && victory < 256 && rtso::valid_victory_code((uint8_t)victory)) sim->set_victory_condition((uint8_t)victory);
	sim->set_waves_enabled((bool)ms->get("waves_enabled"));
	sim->set_last_man_standing((bool)ms->get("last_man_standing"));
	{
		// negative: World Population (0 A.D. WorldPopulationCap); team_pop: Team Population
		const int pc = (int)ms->get("pop_cap");
		if (pc > 0 && (bool)ms->get("team_pop")) sim->set_team_pop_cap(pc);
		else sim->set_pop_cap_setting(pc);
	}
	sim->set_ceasefire_seconds(60.0f * (float)std::clamp((int)ms->get("ceasefire_min"), 0, 60));
	sim->set_reveal_map((bool)ms->get("reveal_map"));
	sim->set_explore_map((bool)ms->get("explore_map"));
	sim->set_ally_view((bool)ms->get("ally_view"));
	sim->set_wonder_duration(60.0f * (float)std::clamp((int)ms->get("wonder_min"), 0, 60));
	sim->set_relic_duration(60.0f * (float)std::clamp((int)ms->get("relic_min"), 0, 60));
	sim->set_relic_count((int)ms->get("relic_count"));
	sim->set_regicide_garrison((bool)ms->get("regicide_garrison"));
	sim->set_lock_teams((bool)ms->get("lock_teams"));
	sim->set_cheats_enabled((bool)ms->get("cheats"));
	sim->set_disable_spies((bool)ms->get("disable_spies"));
	sim->set_disable_treasures((bool)ms->get("disable_treasures"));
	{
		Array scolors = ms->get("slot_colors");
		for (int p = 0; p < rtso::MAX_PLAYERS; ++p)
			sim->set_player_color((uint8_t)p, (uint8_t)(p < scolors.size() ? std::clamp((int)scolors[p], 0, rtso::PLAYER_COLOR_COUNT - 1) : p));
	}
	sim->set_water_level_mode(std::clamp((int)ms->get("water_level"), 0, 2));
	sim->set_sea_level_rise_minutes(std::clamp((int)ms->get("sea_level_rise_min"), 0, 60));
	// Game-setup teams (0 = None, 1-4 = T1..T4) decide allied victory, shared vision and territory.
	{
		Array slot_teams = ms->get("slot_teams");
		uint8_t teams[rtso::MAX_PLAYERS] = {};
		for (int p = 0; p < rtso::MAX_PLAYERS && p < slot_teams.size(); ++p)
			teams[p] = (uint8_t)std::clamp((int)slot_teams[p], 0, 8);
		sim->set_setup_teams(teams);
	}
	sim->apply_team_diplomacy(); // stances follow the lobby's teams
	sync_victory_selector();
	sim->set_ai_seed((uint32_t)(int64_t)ms->get("ai_seed"));
	if (diff >= 0 && diff <= 5) sim->set_petra_difficulty((uint8_t)diff);
	if (behavior >= 0 && behavior <= 3) sim->set_petra_behavior((uint8_t)behavior);
	// Each AI slot's own difficulty and behaviour (slot index == player id; after the shared setters).
	{
		Array sdiffs = ms->get("slot_diffs");
		Array sbehs = ms->get("slot_behs");
		for (int p = 0; p < rtso::MAX_PLAYERS; ++p) {
			if (p < sdiffs.size()) sim->set_petra_difficulty((uint8_t)p, (uint8_t)std::clamp((int)sdiffs[p], 0, 5));
			if (p < sbehs.size()) sim->set_petra_behavior((uint8_t)p, (uint8_t)std::clamp((int)sbehs[p], 0, 3));
		}
	}
	// Game Setup speed (0 A.D. gameSpeed); the in-game speed button still cycles from here.
	{
		const float sp = (float)(double)ms->get("game_speed");
		// game_speeds.json: Turtle 0.1x up to the 20x Fast Forward (offered without humans).
		game_speed = (sp >= 0.1f && sp <= 20.0f) ? sp : 1.0f;
	}
	sim->set_starting_resources_preset(start_res); // 0 A.D. Very Low .. Deathmatch
	// Per-slot civs (slot 0 = human; extra slots honor team civs the
	// RMS/JSON spawns actually created - Petra only drives team 1).
	for (int t = 0; t < rtso::MAX_PLAYERS && t < slot_civs.size(); ++t) {
		int sc = (int)slot_civs[t];
		if (rtso::is_playable_civ(sc)) sim->set_team_civ((uint8_t)t, (uint8_t)sc);
	}
	apply_map_player_overrides();
	// Seat the lobby's slots so every AI slot runs its own Petra (as apply_game_setup does).
	if (seats > 0) {
		for (int t = 0; t < rtso::MAX_PLAYERS; ++t) {
			const int ty = t < slot_types.size() ? (int)slot_types[t] : 2;
			// Older setups: slot 1 is you unless every slot is AI (local_observer_); then
			// Petra plays it too. With player assignment any slot may be AI.
			const bool seated = t < seats && ty != 2 && (!legacy_seat || t > 0 || ty == 0 || local_observer_);
			sim->set_player_active((uint8_t)t, seated || t == 0, (!legacy_seat || t > 0 || local_observer_) && seated && ty == 1);
		}
	}
	apply_player_names(ms->get("slot_names")); // after seating: the diplomacy labels read civ and AI
	UtilityFunctions::print("[RtsoMain] GameSetup ", map_id, " seed ", (int64_t)seed,
			" civ ", rtso::civ_info(sim->player_civ(local_player() < rtso::MAX_PLAYERS ? local_player() : 0)).name,
			" vs ", rtso::civ_info(sim->team_civ(1)).name);
	// Map just (re)spawned - select the human player's units like the old
	// _ready tail did (it ran before any map existed, so it selected nothing).
	clear_selection();
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (uview.team[i] == local_player()) selected_ids.push_back((int32_t)uview.id[i]);
	}
}

// The setup the host's MatchSettings (its Game Setup) describes, as
// NMT_GAME_START / NMT_GAME_SETUP attributes; a Random civ is RANDOM_CIV_WIRE
// and seed 0 means "pick one at Start".
void RtsoMain::match_settings_setup(rtso::GameSetupParams &setup) {
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		String mid = (String)ms->get("map_id");
		setup.map_id = mid.is_empty() ? sim->current_map_id() : mid.utf8().get_data();
		setup.seed = (uint32_t)(int64_t)(int)ms->get("seed");
		setup.ai_seed = (uint32_t)(int64_t)ms->get("ai_seed");
		setup.victory = (uint8_t)((int)ms->get("victory"));
		setup.waves = (bool)ms->get("waves_enabled") ? 1 : 0;
		setup.lms = (bool)ms->get("last_man_standing") ? 1 : 0;
		setup.pop_cap = (uint16_t)std::clamp((int)ms->get("pop_cap"), 0, 1000);
		// A negative pop_cap is Game Setup's World Population.
		setup.world_pop = (uint16_t)std::clamp(-(int)ms->get("pop_cap"), 0, rtso::Simulation::MAX_WORLD_POP);
		// team_pop: the positive pop_cap is 0 A.D. 0.28's Team Population.
		if (setup.pop_cap > 0 && (bool)ms->get("team_pop")) {
			setup.team_pop = (uint16_t)std::min((int)ms->get("pop_cap"), rtso::Simulation::MAX_TEAM_POP);
			setup.pop_cap = 0;
		}
		setup.ceasefire = (uint8_t)std::clamp((int)ms->get("ceasefire_min"), 0, 60);
		setup.reveal = (bool)ms->get("reveal_map") ? 1 : 0;
		setup.explore = (bool)ms->get("explore_map") ? 1 : 0;
		setup.ally = (bool)ms->get("ally_view") ? 1 : 0;
		setup.wonder_min = (uint8_t)std::clamp((int)ms->get("wonder_min"), 0, 60);
		setup.relic_min = (uint8_t)std::clamp((int)ms->get("relic_min"), 0, 60);
		setup.relics = (uint8_t)std::clamp((int)ms->get("relic_count"), 1, 16);
		setup.biome = (uint8_t)std::clamp((int)ms->get("biome"), 0, (int)rtso::BIOME_COUNT);
		Array sdiffs = ms->get("slot_diffs");
		Array sbehs = ms->get("slot_behs");
		for (int i = 0; i < 8; ++i) {
			if (i < sdiffs.size()) setup.diffs[i] = (uint8_t)std::clamp((int)sdiffs[i], 0, 5);
			if (i < sbehs.size()) setup.behs[i] = (uint8_t)std::clamp((int)sbehs[i], 0, 3);
		}
		Array scolors = ms->get("slot_colors");
		for (int i = 0; i < 8 && i < scolors.size(); ++i)
			setup.colors[i] = (uint8_t)std::clamp((int)scolors[i], 0, rtso::PLAYER_COLOR_COUNT - 1);
		setup.speed = (uint16_t)std::clamp((int)std::lround(100.0 * (double)ms->get("game_speed")), 10, 2000);
		setup.lock_players = (bool)ms->get("map_locks_players") ? 1 : 0;
		setup.rated = (bool)ms->get("rated") ? 1 : 0;
		setup.nomad = (bool)ms->get("nomad") ? 1 : 0;
		setup.regicide_garrison = (bool)ms->get("regicide_garrison") ? 1 : 0;
		setup.lock_teams = (bool)ms->get("lock_teams") ? 1 : 0;
		setup.cheats = (bool)ms->get("cheats") ? 1 : 0;
		setup.no_spies = (bool)ms->get("disable_spies") ? 1 : 0;
		setup.no_treasures = (bool)ms->get("disable_treasures") ? 1 : 0;
		Array snames = ms->get("slot_names");
		for (int i = 0; i < 8 && i < snames.size(); ++i) setup.names[i] = String(snames[i]).utf8().get_data();
		setup.water_level = (uint8_t)std::clamp((int)ms->get("water_level"), 0, 2);
		setup.team_placement = (uint8_t)std::clamp((int)ms->get("team_placement"), 0, (int)rtso::TEAM_PLACEMENT_COUNT - 1);
		setup.landscape = (uint8_t)std::clamp((int)ms->get("landscape"), 0, 15);
		setup.map_filter = String(ms->get("map_filter")).utf8().get_data();
		setup.trigger_difficulty = (uint8_t)std::clamp((int)ms->get("trigger_difficulty"), 1, 5);
		setup.daytime = (uint8_t)std::clamp((int)ms->get("map_daytime"), 0, 2);
		setup.sea_rise_min = (uint8_t)std::clamp((int)ms->get("sea_level_rise_min"), 0, 60);
		int msz = (int)ms->get("map_size");
		setup.map_size = (uint16_t)((msz >= 128 && msz <= 512) ? msz : 128);
		setup.difficulty = (uint8_t)((int)ms->get("ai_difficulty"));
		setup.behavior = (uint8_t)((int)ms->get("ai_behavior"));
		setup.start_res = (uint8_t)((int)ms->get("start_res"));
		Array civs = ms->get("slot_civs");
		Array types = ms->get("slot_types");
		Array teams = ms->get("slot_teams");
		setup.slot_count = 0;
		for (int i = 0; i < 8 && i < civs.size(); ++i) {
			int c = (int)civs[i];
			setup.civs[i] = rtso::is_playable_civ(c) ? (uint8_t)c : rtso::RANDOM_CIV_WIRE;
			int ty = (i < types.size()) ? (int)types[i] : 2;
			setup.slot_types[i] = (ty == 0) ? 'h' : ((ty == 1) ? 'a' : 'x');
			int tm = (i < teams.size()) ? (int)teams[i] : 0;
			setup.teams[i] = (tm >= 0 && tm < 10) ? (uint8_t)tm : 0;
			setup.slot_count++;
		}
		if (setup.slot_count == 0) {
			setup.slot_count = 2;
			setup.civs[0] = sim->player_civ();
			setup.civs[1] = sim->team_civ(1);
		}
	} else {
		setup.map_id = sim->current_map_id();
		setup.civs[0] = sim->player_civ();
		setup.civs[1] = sim->team_civ(1);
	}
	if (setup.map_id.empty()) setup.map_id = "random_continental";
	if (!rtso::valid_victory_code(setup.victory)) setup.victory = sim->victory_condition();
	if (setup.difficulty > 5) setup.difficulty = sim->petra_difficulty();
	if (setup.behavior > 3) setup.behavior = sim->petra_behavior();
	if (setup.start_res >= rtso::Simulation::STARTING_RESOURCE_PRESET_COUNT) setup.start_res = 1;
}

// Client: the host's setup into our MatchSettings, so Game Setup shows it.
void RtsoMain::store_net_setup_in_match_settings(const rtso::GameSetupParams &setup) {
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	if (!ms) return;
	ms->set("map_id", String(setup.map_id.c_str()));
	ms->set("seed", (int64_t)setup.seed);
	ms->set("victory", (int)setup.victory);
	ms->set("waves_enabled", setup.waves != 0);
	ms->set("last_man_standing", setup.lms != 0);
	ms->set("pop_cap", setup.world_pop ? -(int)setup.world_pop : setup.team_pop ? (int)setup.team_pop : (int)setup.pop_cap);
	ms->set("team_pop", setup.team_pop != 0 && !setup.world_pop);
	ms->set("ceasefire_min", (int)setup.ceasefire);
	ms->set("reveal_map", setup.reveal != 0);
	ms->set("explore_map", setup.explore != 0);
	ms->set("ally_view", setup.ally != 0);
	ms->set("wonder_min", (int)setup.wonder_min);
	ms->set("relic_min", (int)setup.relic_min);
	ms->set("relic_count", (int)setup.relics);
	ms->set("biome", (int)setup.biome);
	ms->set("nomad", setup.nomad != 0);
	ms->set("regicide_garrison", setup.regicide_garrison != 0);
	ms->set("lock_teams", setup.lock_teams != 0);
	ms->set("cheats", setup.cheats != 0);
	ms->set("disable_spies", setup.no_spies != 0);
	ms->set("disable_treasures", setup.no_treasures != 0);
	ms->set("water_level", (int)setup.water_level);
	ms->set("team_placement", (int)setup.team_placement);
	ms->set("landscape", (int)setup.landscape);
	ms->set("trigger_difficulty", (int)setup.trigger_difficulty);
	ms->set("map_daytime", (int)setup.daytime);
	ms->set("sea_level_rise_min", (int)setup.sea_rise_min);
	ms->set("map_filter", String::utf8(setup.map_filter.c_str()));
	ms->set("map_size", (int)setup.map_size);
	ms->set("ai_difficulty", (int)setup.difficulty);
	ms->set("ai_behavior", (int)setup.behavior);
	ms->set("start_res", (int)setup.start_res);
	ms->set("game_speed", setup.speed / 100.0);
	ms->set("map_locks_players", setup.lock_players != 0);
	ms->set("rated", setup.rated != 0);
	Array types, civs, teams, diffs, behs, colors, names;
	for (uint8_t i = 0; i < setup.slot_count && i < 8; ++i) {
		types.push_back(setup.slot_types[i] == 'h' ? 0 : setup.slot_types[i] == 'a' ? 1 : 2);
		civs.push_back(setup.civs[i] == rtso::RANDOM_CIV_WIRE ? -1 : (int)setup.civs[i]);
		teams.push_back((int)setup.teams[i]);
		diffs.push_back((int)setup.diffs[i]);
		behs.push_back((int)setup.behs[i]);
		colors.push_back((int)setup.colors[i]);
		names.push_back(String::utf8(setup.names[i].c_str()));
	}
	ms->set("slot_types", types);
	ms->set("slot_civs", civs);
	ms->set("slot_teams", teams);
	ms->set("slot_diffs", diffs);
	ms->set("slot_behs", behs);
	ms->set("slot_colors", colors);
	ms->set("slot_names", names);
	ms->set("player_count", (int)setup.slot_count);
}

// --- Additional save, load, and session setup methods (split from main_node.cpp) ---

bool RtsoMain::save_game(String path, String description) {
	if (!sim) return false;
	String global_path = ProjectSettings::get_singleton()->globalize_path(path);
	Ref<DirAccess> dir = DirAccess::open("user://");
	if (dir.is_valid()) {
		dir->make_dir_recursive("saves");
	}
	bool res = sim->save_state(global_path.utf8().get_data());
	if (res) {
		// The save and load dialogs list saves by this metadata (0 A.D. gui/savedgames).
		write_save_metadata(path, description);
		write_save_thumbnail(path);
		print_line("[RTS0] Game saved successfully to " + path);
	}
	return res;
}

bool RtsoMain::load_game(String path) {
	return load_world(path, saved_gui(path));
}

// session.js restoreSavedGameData gets the save's GUI data: a saved game's camera and
// control groups, or a quicksave's groups alone.
bool RtsoMain::load_world(const String &path, const Dictionary &gui) {
	if (!sim) return false;
	String global_path = ProjectSettings::get_singleton()->globalize_path(path);
	load_progress(50, "Loading saved game");
	// The replay so far stays valid up to here; what follows starts from the save. Loading
	// leaves this match (0.28 endGame), so its replay keeps its summary.
	stop_replay_recording(true);
	bool success = sim->load_state(global_path.utf8().get_data());
	if (success) {
		selected_ids.clear();
		selected_building_id = 0;
		restore_saved_gui(gui);
		// The models the saved world draws load here, behind the loading page (0.28's
		// LDR_ProgressiveLoad), not on the first frame of play.
		if (view) {
			const uint64_t t0 = Time::get_singleton()->get_ticks_msec();
			view->warm_models(sim, [this](int done, int total) {
				load_progress(60 + 35 * done / std::max(1, total), "Loading models");
			});
			print_line(String("[RTS0] Loaded the saved world's models in ") + String::num_int64(Time::get_singleton()->get_ticks_msec() - t0) + " ms");
		}
		print_line("[RTS0] Game loaded successfully from " + path);
		// The game goes on from the save as a new replay that starts from it.
		if (!loading_replay_start_ && !net_lockstep()) begin_recording_from_save(path);
	}
	return success;
}

PackedByteArray RtsoMain::get_fow_grid() const {
	if (!sim) return PackedByteArray();
	const uint8_t* grid = sim->fow_grid(view_player());
	int sz = sim->fow_res() * sim->fow_res();
	PackedByteArray arr;
	arr.resize(sz);
	for (int i = 0; i < sz; ++i) arr.set(i, grid[i]);
	return arr;
}

void RtsoMain::_on_map_selected(int index) {
	PackedStringArray maps = get_available_maps();
	if (index >= 0 && index < maps.size()) {
		load_map(maps[index]);
	}
}

PackedStringArray RtsoMain::get_available_maps() const {
	PackedStringArray maps;
	Ref<DirAccess> dir = DirAccess::open("res://data/maps");
	if (dir.is_valid()) {
		dir->list_dir_begin();
		String fname = dir->get_next();
		PackedStringArray json_maps;
		while (!fname.is_empty()) {
			if (!dir->current_is_dir() && fname.ends_with(".json")) {
				json_maps.push_back(fname.get_basename());
			}
			fname = dir->get_next();
		}
		json_maps.sort();
		for (int i = 0; i < json_maps.size(); ++i) {
			maps.push_back(json_maps[i]);
		}
	}
	if (maps.is_empty()) {
		maps.push_back("acropolis_bay");
		maps.push_back("corinthian_isthmus");
		maps.push_back("atlas_valleys");
		maps.push_back("bactria");
		maps.push_back("battle_for_the_tiber");
	}
	maps.push_back("default");
	maps.push_back("random_continental");
	maps.push_back("random_archipelago");
	maps.push_back("random_oasis");
	maps.push_back("random_highlands");
	maps.push_back("random_valley");
	maps.push_back("random_mediterranean");
	maps.push_back("random_acropolis");
	maps.push_back("random_spartan_valley");
	maps.push_back("random_danubius");
	maps.push_back("random_saharan_oases");
	maps.push_back("random_anatolian_plateau");
	maps.push_back("random_jebel_barkal");
	for (int t = rtso::MAP_MAINLAND; t < rtso::MAP_TYPE_COUNT; ++t)
		maps.push_back(String("random_") + rtso::map_type_info((uint8_t)t).id);
	return maps;
}

bool RtsoMain::load_map(String map_id) {
	uint32_t seed = (uint32_t)Time::get_singleton()->get_ticks_msec();
	if (sim) {
		sim->set_biome_override(-1); // a map picked in game keeps its own biome
		sim->set_nomad(false);
	}
	local_observer_ = false; // a map picked in game is played from slot 1
	return load_map_seeded(map_id, seed);
}

void RtsoMain::apply_map_player_overrides() {
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
		if (map_civ_[p] >= 0) sim->set_team_civ(p, (uint8_t)map_civ_[p]);
		if (map_has_res_[p]) sim->set_player_resources(p, map_res_[p][0], map_res_[p][1], map_res_[p][3], map_res_[p][2]);
		sim->set_player_pop_cap(p, map_player_pop_limit_[p]);
	}
	if (map_pop_limit_ > 0) sim->set_pop_cap(map_pop_limit_);
	if (map_has_teams_) {
		// A map with LockTeams sets the teams itself (0 A.D. gamesetup locks them).
		sim->set_setup_teams(map_team_);
		sim->apply_team_diplomacy();
	}
}

void RtsoMain::_on_civ_selected(int index) {
	set_civ(index);
}

void RtsoMain::_on_ai_diff_selected(int index) {
	if (!sim || !ai_diff_selector) return;
	sim->set_petra_difficulty((uint8_t)ai_diff_selector->get_item_id(index));
}

void RtsoMain::_on_ai_behavior_selected(int index) {
	if (!sim || !ai_behavior_selector) return;
	sim->set_petra_behavior((uint8_t)ai_behavior_selector->get_item_id(index));
}

String RtsoMain::player_display_name(uint8_t pid) const {
	if (pid >= rtso::MAX_PLAYERS) return pid == rtso::GAIA_PLAYER ? String("Gaia") : String("an observer");
	// 0 A.D. onFinalizeAttributes: a seated client's own name replaces the slot's.
	if (net_lockstep())
		for (const auto &e : net_host_.playerAssignment())
			if (e.player == pid && !e.name.empty()) return String::utf8(e.name.c_str());
	if (!player_names_[pid].is_empty()) return player_names_[pid];
	return "Player " + String::num_int64(pid + 1);
}

// messages.js onClientLeave / onClientJoin: the seat's client left the match
// and nobody (a rejoin under a new id) has taken the seat again.
bool RtsoMain::player_offline(uint8_t pid) const {
	if (!net_lockstep() || !net_game_started_ || pid >= rtso::MAX_PLAYERS || pid >= net_setup_.setup.slot_count) return false;
	const int c = net_setup_.slot_client[pid];
	if (c <= 0 || !turn_mgr_.client_left((uint32_t)c)) return false;
	for (const auto &e : net_host_.playerAssignment())
		if (e.player == pid && !turn_mgr_.client_left(e.client)) return false;
	return true;
}

void RtsoMain::apply_player_names(const Array &names) {
	for (int p = 0; p < rtso::MAX_PLAYERS; ++p)
		player_names_[p] = p < names.size() ? String(names[p]).strip_edges().left(48) : String();
}

void RtsoMain::sync_victory_selector() {
	if (!sim || !victory_selector) return;
	for (int i = 0; i < victory_selector->get_item_count(); ++i) {
		if (victory_selector->get_item_id(i) == (int)sim->victory_condition()) victory_selector->select(i);
	}
}

void RtsoMain::_on_victory_selected(int index) {
	if (!sim || !victory_selector) return;
	sim->set_victory_condition((uint8_t)victory_selector->get_item_id(index));
}

void RtsoMain::_on_voice_button_pressed() {
	use_english_voice_orders_ = !use_english_voice_orders_;
	if (voice_btn)
		voice_btn->set_text(use_english_voice_orders_ ? "Voice: EN" : "Voice: Civ");
}

