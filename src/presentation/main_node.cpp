#include "main_node.h"
#include "popup_safe.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
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

void RtsoMain::_bind_methods() {
	ClassDB::bind_method(D_METHOD("enter_build_mode", "enable", "btype"), &RtsoMain::enter_build_mode, DEFVAL(rtso::BLD_HOUSE));
	ClassDB::bind_method(D_METHOD("clear_selection"), &RtsoMain::clear_selection);
	ClassDB::bind_method(D_METHOD("_on_minimap_clicked", "world_pos"), &RtsoMain::_on_minimap_clicked);
	ClassDB::bind_method(D_METHOD("_on_flare_requested", "world_pos"), &RtsoMain::_on_flare_requested);
	ClassDB::bind_method(D_METHOD("_on_idle_worker_clicked"), &RtsoMain::_on_idle_worker_clicked);
	ClassDB::bind_method(D_METHOD("_on_speed_button_pressed"), &RtsoMain::_on_speed_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_menu_button_pressed"), &RtsoMain::_on_menu_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_ungarrison_pressed"), &RtsoMain::_on_ungarrison_pressed);
	ClassDB::bind_method(D_METHOD("_on_stance_selected", "stance"), &RtsoMain::_on_stance_selected);
	ClassDB::bind_method(D_METHOD("_on_formation_selected", "formation"), &RtsoMain::_on_formation_selected);
	ClassDB::bind_method(D_METHOD("_on_train_pressed", "utype"), &RtsoMain::_on_train_pressed);
	ClassDB::bind_method(D_METHOD("_on_build_btn_pressed", "btype"), &RtsoMain::_on_build_btn_pressed);
	ClassDB::bind_method(D_METHOD("_on_tech_pressed", "tech_id"), &RtsoMain::_on_tech_pressed);
	ClassDB::bind_method(D_METHOD("save_game", "path"), &RtsoMain::save_game, DEFVAL("user://saves/quicksave.rts0sav"));
	ClassDB::bind_method(D_METHOD("load_game", "path"), &RtsoMain::load_game, DEFVAL("user://saves/quicksave.rts0sav"));
	ClassDB::bind_method(D_METHOD("get_fow_grid"), &RtsoMain::get_fow_grid);
	ClassDB::bind_method(D_METHOD("order_guard_units", "target_id"), &RtsoMain::order_guard_units);
	ClassDB::bind_method(D_METHOD("spawn_large_battle", "total_units"), &RtsoMain::spawn_large_battle, DEFVAL(1000));
	ClassDB::bind_method(D_METHOD("_on_map_selected", "index"), &RtsoMain::_on_map_selected);
	ClassDB::bind_method(D_METHOD("load_map", "map_id"), &RtsoMain::load_map);
	ClassDB::bind_method(D_METHOD("get_available_maps"), &RtsoMain::get_available_maps);
	ClassDB::bind_method(D_METHOD("_on_civ_selected", "index"), &RtsoMain::_on_civ_selected);
	ClassDB::bind_method(D_METHOD("_on_ai_diff_selected", "index"), &RtsoMain::_on_ai_diff_selected);
	ClassDB::bind_method(D_METHOD("_on_ai_behavior_selected", "index"), &RtsoMain::_on_ai_behavior_selected);
	ClassDB::bind_method(D_METHOD("_on_victory_selected", "index"), &RtsoMain::_on_victory_selected);
	ClassDB::bind_method(D_METHOD("_on_barter_requested", "sell_type", "buy_type", "amount"), &RtsoMain::_on_barter_requested);
	ClassDB::bind_method(D_METHOD("set_civ", "civ_idx"), &RtsoMain::set_civ);
	ClassDB::bind_method(D_METHOD("get_civ_info", "civ_idx"), &RtsoMain::get_civ_info);
	ClassDB::bind_method(D_METHOD("get_match_summary"), &RtsoMain::get_match_summary);

	ClassDB::bind_method(D_METHOD("_on_queue_slot_gui_input", "event", "slot_idx"), &RtsoMain::_on_queue_slot_gui_input);
	ClassDB::bind_method(D_METHOD("_on_queue_slot_cancel", "slot_idx"), &RtsoMain::_on_queue_slot_cancel);
	ClassDB::bind_method(D_METHOD("toggle_chat"), &RtsoMain::toggle_chat);
	ClassDB::bind_method(D_METHOD("_on_chat_send_pressed"), &RtsoMain::_on_chat_send_pressed);
	ClassDB::bind_method(D_METHOD("_on_chat_text_submitted", "text"), &RtsoMain::_on_chat_text_submitted);
	ClassDB::bind_method(D_METHOD("_on_chat_channel_btn_pressed", "channel_idx"), &RtsoMain::_on_chat_channel_btn_pressed);
	ClassDB::bind_method(D_METHOD("toggle_diplomacy"), &RtsoMain::toggle_diplomacy);
	ClassDB::bind_method(D_METHOD("_on_diplo_stance_changed", "stance", "player_idx"), &RtsoMain::_on_diplo_stance_changed);
	ClassDB::bind_method(D_METHOD("_on_diplo_tribute_pressed", "player_idx", "res_type"), &RtsoMain::_on_diplo_tribute_pressed);
	ClassDB::bind_method(D_METHOD("set_use_english_voice_orders", "enable"), &RtsoMain::set_use_english_voice_orders);
	ClassDB::bind_method(D_METHOD("is_use_english_voice_orders"), &RtsoMain::is_use_english_voice_orders);
	ClassDB::bind_method(D_METHOD("_on_lobby_button_pressed"), &RtsoMain::_on_lobby_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_voice_button_pressed"), &RtsoMain::_on_voice_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_lobby_host_pressed"), &RtsoMain::_on_lobby_host_pressed);
	ClassDB::bind_method(D_METHOD("_on_lobby_join_pressed"), &RtsoMain::_on_lobby_join_pressed);
	ClassDB::bind_method(D_METHOD("_on_lobby_start_pressed"), &RtsoMain::_on_lobby_start_pressed);
	ClassDB::bind_method(D_METHOD("apply_net_game_start"), &RtsoMain::apply_net_game_start);
	ClassDB::bind_method(D_METHOD("load_map_seeded", "map_id", "seed"), &RtsoMain::load_map_seeded);
	ClassDB::bind_method(D_METHOD("_on_lobby_refresh_pressed"), &RtsoMain::_on_lobby_refresh_pressed);

	const StringName cls = get_class_static();
	auto bind_enum = [&](const char *enum_name, const char *name, int64_t value) {
		ClassDB::bind_integer_constant(cls, enum_name, name, value);
	};
	bind_enum("UnitType", "VILLAGER", rtso::UNIT_VILLAGER);
	bind_enum("UnitType", "SWORDSMAN", rtso::UNIT_SWORDSMAN);
	bind_enum("UnitType", "ARCHER", rtso::UNIT_ARCHER);
	bind_enum("UnitType", "SPEARMAN", rtso::UNIT_SPEARMAN);
	bind_enum("UnitType", "CAVALRY", rtso::UNIT_CAVALRY);
	bind_enum("UnitType", "TRADER", rtso::UNIT_TRADER);
	bind_enum("UnitType", "CATAPULT", rtso::UNIT_CATAPULT);
	bind_enum("UnitType", "HERO", rtso::UNIT_HERO);
	bind_enum("UnitType", "TRIREME", rtso::UNIT_TRIREME);
	bind_enum("UnitType", "ELEPHANT", rtso::UNIT_ELEPHANT);
	bind_enum("UnitType", "SIEGE_RAM", rtso::UNIT_SIEGE_RAM);
	bind_enum("UnitType", "HEALER", rtso::UNIT_HEALER);
	bind_enum("UnitType", "CHARIOT", rtso::UNIT_CHARIOT);
	bind_enum("UnitType", "WAR_DOG", rtso::UNIT_WAR_DOG);
	bind_enum("UnitType", "BOLT_SHOOTER", rtso::UNIT_BOLT_SHOOTER);
	bind_enum("UnitType", "FISHING_BOAT", rtso::UNIT_FISHING_BOAT);
	bind_enum("UnitType", "MERCHANT_SHIP", rtso::UNIT_MERCHANT_SHIP);
	bind_enum("UnitType", "FEMALE_CITIZEN", rtso::UNIT_FEMALE_CITIZEN);
	bind_enum("UnitType", "CARNYX", rtso::UNIT_CARNYX);
	bind_enum("UnitType", "WORKER_ELEPHANT", rtso::UNIT_WORKER_ELEPHANT);
	bind_enum("UnitType", "ARCHER_MAIDEN", rtso::UNIT_ARCHER_MAIDEN);
	bind_enum("UnitType", "CAMEL", rtso::UNIT_CAMEL);
	bind_enum("UnitType", "SCYTHED_CHARIOT", rtso::UNIT_SCYTHED_CHARIOT);
	bind_enum("UnitType", "NONE", rtso::UNIT_TYPE_NONE);

	bind_enum("BuildingType", "TOWN_CENTER", rtso::BLD_TOWN_CENTER);
	bind_enum("BuildingType", "HOUSE", rtso::BLD_HOUSE);
	bind_enum("BuildingType", "BARRACKS", rtso::BLD_BARRACKS);
	bind_enum("BuildingType", "STOREHOUSE", rtso::BLD_STOREHOUSE);
	bind_enum("BuildingType", "TOWER", rtso::BLD_TOWER);
	bind_enum("BuildingType", "FARMSTEAD", rtso::BLD_FARMSTEAD);
	bind_enum("BuildingType", "MARKET", rtso::BLD_MARKET);
	bind_enum("BuildingType", "FORTRESS", rtso::BLD_FORTRESS);
	bind_enum("BuildingType", "GATE", rtso::BLD_GATE);
	bind_enum("BuildingType", "TEMPLE", rtso::BLD_TEMPLE);
	bind_enum("BuildingType", "WONDER", rtso::BLD_WONDER);
	bind_enum("BuildingType", "DOCK", rtso::BLD_DOCK);
	bind_enum("BuildingType", "WALL_SEGMENT", rtso::BLD_WALL_SEGMENT);
	bind_enum("BuildingType", "CORRAL", rtso::BLD_CORRAL);

	bind_enum("Civilization", "ATHENIANS", rtso::CIV_ATHENIANS);
	bind_enum("Civilization", "SPARTANS", rtso::CIV_SPARTANS);
	bind_enum("Civilization", "PERSIANS", rtso::CIV_PERSIANS);
	bind_enum("Civilization", "ROMANS", rtso::CIV_ROMANS);
	bind_enum("Civilization", "CARTHAGINIANS", rtso::CIV_CARTHAGINIANS);
	bind_enum("Civilization", "MACEDONIANS", rtso::CIV_MACEDONIANS);
	bind_enum("Civilization", "BRITONS", rtso::CIV_BRITONS);
	bind_enum("Civilization", "GAULS", rtso::CIV_GAULS);
	bind_enum("Civilization", "IBERIANS", rtso::CIV_IBERIANS);
	bind_enum("Civilization", "MAURYANS", rtso::CIV_MAURYANS);
	bind_enum("Civilization", "KUSHITES", rtso::CIV_KUSHITES);
	bind_enum("Civilization", "PTOLEMIES", rtso::CIV_PTOLEMIES);
	bind_enum("Civilization", "SELEUCIDS", rtso::CIV_SELEUCIDS);
}

RtsoMain::RtsoMain() {
	sim = new rtso::Simulation();
}

RtsoMain::~RtsoMain() {
	net_host_.stop();
	delete sim;
}

void RtsoMain::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE) {
		if (SceneTree *t = get_tree()) {
			rtso::hide_node_popups(t->get_root());
		} else {
			rtso::hide_node_popups(this);
		}
	}
}

static Ref<Texture2D> load_tex(const String &path) {
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<Texture2D> tex = ResourceLoader::get_singleton()->load(path);
		if (tex.is_valid()) return tex;
	}
	return Ref<Texture2D>();
}

void RtsoMain::_ready() {
	rtso::Logger::info("Main", "RTS0 0 A.D. Reborn Ready — Full 0 A.D. UI & Simulation");

	// Incoming chat: 0 A.D. NetMessages.h NMT_CHAT parity. Fires on the main
	// thread from net_host_.poll() (_process), so appending to chat_log is safe.
	// net_host.cpp:540 dispatch -> onChat (:395) -> display here.
	net_host_.onChat([this](const rtso::ChatMsg &cm) {
		String msg = String::utf8(cm.message.c_str());
		// Tactical flare piggybacks NMT_CHAT (approximation of 0 A.D. minimap
		// flare): "[Flare] x,z" -> local map ping, not chat text.
		if (msg.begins_with("[Flare]")) {
			String coords = msg.substr(7).strip_edges();
			int32_t comma = coords.find(",");
			if (comma > 0) {
				float fx = coords.substr(0, comma).to_float();
				float fz = coords.substr(comma + 1).to_float();
				if (minimap_view) minimap_view->add_ping(Vector2(fx, fz), Color(1.0f, 0.45f, 0.1f));
			}
			return;
		}
		if (!chat_log) return;
		String tag_col = "#55bbff"; // All (default channel color, 0 A.D. ChatBounce.js:15-38 parity)
		String chan_prefix = "[All]";
		if (msg.begins_with("[Allies]")) {
			tag_col = "#55ff55";
			chan_prefix = "[Allies]";
			msg = msg.substr(8).strip_edges();
		} else if (msg.begins_with("[Team]")) {
			tag_col = "#ffff55";
			chan_prefix = "[Team]";
			msg = msg.substr(6).strip_edges();
		} else if (msg.begins_with("[Enemies]")) {
			tag_col = "#ff5555";
			chan_prefix = "[Enemies]";
			msg = msg.substr(9).strip_edges();
		} else if (msg.begins_with("[Player-")) {
			int close_b = msg.find("]");
			if (close_b > 0) {
				chan_prefix = msg.substr(0, close_b + 1);
				tag_col = "#eebb22";
				msg = msg.substr(close_b + 1).strip_edges();
			}
		}
		chat_log->append_text("[color=" + tag_col + "]" + chan_prefix + " " +
				String::utf8(cm.sender_guid.c_str()) + "[/color]: " + msg + "\n");
	});

	// Stash the GameSetup first: load_map_seeded needs view + cam_rig, which
	// are created below. apply_match_settings() runs after they exist.
	// (Map choice was previously dropped — map_index stored but never
	// consumed. Victory / Petra difficulty+behavior / starting resources /
	// per-slot civs apply after the map load because reset()/generate()
	// overwrite stocks.)
	anim_ctrl.load("res://assets/animations");

	bool has_light = false;
	for (int i = 0; i < get_child_count(); ++i) {
		if (Object::cast_to<DirectionalLight3D>(get_child(i))) {
			has_light = true;
			break;
		}
	}
	if (!has_light) {
		DirectionalLight3D *sun = memnew(DirectionalLight3D);
		sun->set_position(Vector3(0, 50, 0));
		sun->set_rotation_degrees(Vector3(-55, 35, 0));
		sun->set_color(Color(1.0f, 0.96f, 0.90f));
		sun->set_param(Light3D::PARAM_ENERGY, 1.4f);
		add_child(sun);
	}

	cam_rig = memnew(RtsoCameraRig);
	add_child(cam_rig);

	view = memnew(RtsoWorldView);
	add_child(view);

	ui_layer = memnew(CanvasLayer);
	add_child(ui_layer);

	sel_rect = memnew(RtsoSelectionRect);
	ui_layer->add_child(sel_rect);

	debug_overlay = memnew(RtsoDebugOverlay);
	ui_layer->add_child(debug_overlay);

	minimap_view = memnew(RtsoMiniMapView);
	minimap_view->connect("minimap_clicked", Callable(this, "_on_minimap_clicked"));
	minimap_view->connect("flare_requested", Callable(this, "_on_flare_requested"));
	ui_layer->add_child(minimap_view);

	// Minimap layer toggles (methods were ClassDB-bound but had no UI driver).
	// Row sits directly above the 190px minimap (see minimap ctor offsets).
	{
		HBoxContainer *mm_btns = memnew(HBoxContainer);
		mm_btns->set_anchor(SIDE_LEFT, Control::ANCHOR_BEGIN);
		mm_btns->set_anchor(SIDE_RIGHT, Control::ANCHOR_BEGIN);
		mm_btns->set_anchor(SIDE_TOP, Control::ANCHOR_END);
		mm_btns->set_anchor(SIDE_BOTTOM, Control::ANCHOR_END);
		mm_btns->set_offset(SIDE_LEFT, 14.0f);
		mm_btns->set_offset(SIDE_RIGHT, 204.0f);
		mm_btns->set_offset(SIDE_TOP, -230.0f);
		mm_btns->set_offset(SIDE_BOTTOM, -206.0f);
		mm_btns->add_theme_constant_override("separation", 4);
		struct MmBtn { const char *label; const char *method; };
		const MmBtn mm_btn_defs[4] = {
			// 3-state shroud cycle (RtsoMiniMapView::cycle_shroud, minimap_view.h ShroudMode):
			// On -> terrain-only -> off -> On. 0AD MiniMapPanel.js:8-11 panel buttons.
			{"Shroud", "cycle_shroud"},
			{"Terr", "toggle_territory"},
			{"Res", "toggle_resources"},
			{"Diplo", "toggle_diplomacy"},
		};
		for (const MmBtn &d : mm_btn_defs) {
			Button *b = memnew(Button);
			b->set_text(d.label);
			b->set_custom_minimum_size(Vector2(44, 22));
			b->set_focus_mode(Control::FOCUS_NONE);
			b->add_theme_font_size_override("font_size", 9);
			b->connect("pressed", Callable(minimap_view, d.method));
			mm_btns->add_child(b);
		}
		ui_layer->add_child(mm_btns);
	}

	// =========================================================================
	// 1. TOP HUD BAR PANEL (Civ Crest, 4 Resources, Pop, Idle, Clock, Speed, Menu)
	// =========================================================================
	top_hud_panel = memnew(Panel);
	top_hud_panel->set_anchor(SIDE_LEFT, 0.0f);
	top_hud_panel->set_anchor(SIDE_RIGHT, 1.0f);
	top_hud_panel->set_anchor(SIDE_TOP, 0.0f);
	top_hud_panel->set_anchor(SIDE_BOTTOM, 0.0f);
	top_hud_panel->set_offset(SIDE_LEFT, 12.0f);
	top_hud_panel->set_offset(SIDE_RIGHT, -12.0f);
	top_hud_panel->set_offset(SIDE_TOP, 8.0f);
	top_hud_panel->set_offset(SIDE_BOTTOM, 54.0f);

	Ref<StyleBoxFlat> top_style;
	top_style.instantiate();
	top_style->set_bg_color(Color(0.07f, 0.08f, 0.12f, 0.92f));
	top_style->set_corner_radius_all(6);
	top_style->set_border_width_all(1);
	top_style->set_border_color(Color(0.35f, 0.38f, 0.48f, 0.85f));
	top_hud_panel->add_theme_stylebox_override("panel", top_style);
	ui_layer->add_child(top_hud_panel);

	HBoxContainer *top_hbox = memnew(HBoxContainer);
	top_hbox->set_anchor(SIDE_LEFT, 0.0f);
	top_hbox->set_anchor(SIDE_RIGHT, 1.0f);
	top_hbox->set_anchor(SIDE_TOP, 0.0f);
	top_hbox->set_anchor(SIDE_BOTTOM, 1.0f);
	top_hbox->set_offset(SIDE_LEFT, 8.0f);
	top_hbox->set_offset(SIDE_RIGHT, -8.0f);
	top_hbox->set_offset(SIDE_TOP, 4.0f);
	top_hbox->set_offset(SIDE_BOTTOM, -4.0f);
	top_hbox->add_theme_constant_override("separation", 10);
	top_hud_panel->add_child(top_hbox);

	// Civ Emblem
	civ_emblem = memnew(TextureRect);
	civ_emblem->set_custom_minimum_size(Vector2(38, 38));
	civ_emblem->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	civ_emblem->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	civ_emblem->set_texture(load_tex(rtso::civ_info(sim->player_civ()).emblem_path));
	top_hbox->add_child(civ_emblem);

	// Phase Emblem Badge (Village, Town, City)
	phase_emblem = memnew(TextureRect);
	phase_emblem->set_custom_minimum_size(Vector2(38, 38));
	phase_emblem->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	phase_emblem->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	phase_emblem->set_texture(load_tex("res://assets/textures/ui/phases/village.png"));
	top_hbox->add_child(phase_emblem);

	// Resource Items (Food, Wood, Gold, Stone)
	const char *res_paths[4] = {
		"res://assets/textures/ui/icons/food.png",
		"res://assets/textures/ui/icons/wood.png",
		"res://assets/textures/ui/icons/metal.png",
		"res://assets/textures/ui/icons/stone.png"
	};
	for (int i = 0; i < 4; ++i) {
		HBoxContainer *rc = memnew(HBoxContainer);
		rc->add_theme_constant_override("separation", 4);

		res_icons[i] = memnew(TextureRect);
		res_icons[i]->set_custom_minimum_size(Vector2(26, 26));
		res_icons[i]->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		res_icons[i]->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		res_icons[i]->set_texture(load_tex(res_paths[i]));
		rc->add_child(res_icons[i]);

		res_labels[i] = memnew(Label);
		res_labels[i]->set_custom_minimum_size(Vector2(95, 26));
		res_labels[i]->add_theme_font_size_override("font_size", 13);
		res_labels[i]->add_theme_color_override("font_color", Color(0.95f, 0.95f, 0.98f));
		rc->add_child(res_labels[i]);

		top_hbox->add_child(rc);
	}

	// Population Item
	HBoxContainer *pop_box = memnew(HBoxContainer);
	pop_box->add_theme_constant_override("separation", 4);
	pop_icon = memnew(TextureRect);
	pop_icon->set_custom_minimum_size(Vector2(26, 26));
	pop_icon->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	pop_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	pop_icon->set_texture(load_tex("res://assets/textures/ui/icons/population.png"));
	pop_box->add_child(pop_icon);

	pop_label = memnew(Label);
	pop_label->set_custom_minimum_size(Vector2(75, 26));
	pop_label->add_theme_font_size_override("font_size", 13);
	pop_box->add_child(pop_label);
	top_hbox->add_child(pop_box);

	// Idle Worker Button
	idle_worker_btn = memnew(Button);
	idle_worker_btn->set_custom_minimum_size(Vector2(90, 30));
	idle_worker_btn->set_text("Idle: 0");
	idle_worker_btn->add_theme_font_size_override("font_size", 12);
	idle_worker_btn->connect("pressed", Callable(this, "_on_idle_worker_clicked"));
	top_hbox->add_child(idle_worker_btn);

	// Clock & Wave
	clock_label = memnew(Label);
	clock_label->set_custom_minimum_size(Vector2(65, 26));
	clock_label->add_theme_font_size_override("font_size", 13);
	clock_label->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.4f));
	clock_label->set_text("00:00");
	top_hbox->add_child(clock_label);

	wave_label = memnew(Label);
	wave_label->set_custom_minimum_size(Vector2(140, 26));
	wave_label->add_theme_font_size_override("font_size", 12);
	wave_label->add_theme_color_override("font_color", Color(0.95f, 0.6f, 0.5f));
	top_hbox->add_child(wave_label);

	// Speed & Menu
	speed_btn = memnew(Button);
	speed_btn->set_custom_minimum_size(Vector2(45, 28));
	speed_btn->set_text("1x");
	speed_btn->add_theme_font_size_override("font_size", 12);
	speed_btn->connect("pressed", Callable(this, "_on_speed_button_pressed"));
	top_hbox->add_child(speed_btn);

	menu_btn = memnew(Button);
	menu_btn->set_custom_minimum_size(Vector2(60, 28));
	menu_btn->set_text("Menu");
	menu_btn->add_theme_font_size_override("font_size", 12);
	menu_btn->connect("pressed", Callable(this, "_on_menu_button_pressed"));
	top_hbox->add_child(menu_btn);

	map_selector = memnew(RtsoSafeSelect);
	map_selector->set_custom_minimum_size(Vector2(165, 28));
	map_selector->add_item("Acropolis Bay (2P)", 0);
	map_selector->add_item("Corinthian Isthmus (4P)", 1);
	map_selector->add_item("Atlas Valleys (8P)", 2);
	map_selector->add_item("Bactria (2P)", 3);
	map_selector->add_item("Battle for Tiber (3P)", 4);
	map_selector->add_item("Standard Arena (128m)", 5);
	map_selector->add_item("Procedural Continental (RMS)", 6);
	map_selector->add_item("Procedural Archipelago (RMS)", 7);
	map_selector->add_item("Procedural Oasis (RMS)", 8);
	map_selector->add_item("Procedural Highlands (RMS)", 9);
	map_selector->add_item("Procedural Valley (RMS)", 10);
	map_selector->add_item("Procedural Mediterranean (RMS)", 11);
	map_selector->add_item("Procedural Acropolis (RMS)", 12);
	map_selector->add_item("Procedural Spartan Valley (RMS)", 13);
	map_selector->connect("item_selected", Callable(this, "_on_map_selected"));
	top_hbox->add_child(map_selector);

	civ_selector = memnew(RtsoSafeSelect);
	civ_selector->set_custom_minimum_size(Vector2(125, 28));
	civ_selector->add_item("Athenians", rtso::CIV_ATHENIANS);
	civ_selector->add_item("Spartans", rtso::CIV_SPARTANS);
	civ_selector->add_item("Persians", rtso::CIV_PERSIANS);
	civ_selector->add_item("Romans", rtso::CIV_ROMANS);
	civ_selector->add_item("Carthaginians", rtso::CIV_CARTHAGINIANS);
	civ_selector->add_item("Macedonians", rtso::CIV_MACEDONIANS);
	civ_selector->add_item("Britons", rtso::CIV_BRITONS);
	civ_selector->add_item("Gauls", rtso::CIV_GAULS);
	civ_selector->add_item("Iberians", rtso::CIV_IBERIANS);
	civ_selector->add_item("Mauryans", rtso::CIV_MAURYANS);
	civ_selector->add_item("Kushites", rtso::CIV_KUSHITES);
	civ_selector->add_item("Ptolemies", rtso::CIV_PTOLEMIES);
	civ_selector->add_item("Seleucids", rtso::CIV_SELEUCIDS);
	civ_selector->connect("item_selected", Callable(this, "_on_civ_selected"));
	civ_selector->select((int)sim->player_civ());
	top_hbox->add_child(civ_selector);

	ai_diff_selector = memnew(RtsoSafeSelect);
	ai_diff_selector->set_custom_minimum_size(Vector2(100, 28));
	ai_diff_selector->add_item("AI Sandbox", rtso::PETRA_SANDBOX);
	ai_diff_selector->add_item("AI Very Easy", rtso::PETRA_VERY_EASY);
	ai_diff_selector->add_item("AI Easy", rtso::PETRA_EASY);
	ai_diff_selector->add_item("AI Medium", rtso::PETRA_MEDIUM);
	ai_diff_selector->add_item("AI Hard", rtso::PETRA_HARD);
	ai_diff_selector->add_item("AI Very Hard", rtso::PETRA_VERY_HARD);
	ai_diff_selector->select(rtso::PETRA_MEDIUM);
	ai_diff_selector->connect("item_selected", Callable(this, "_on_ai_diff_selected"));
	top_hbox->add_child(ai_diff_selector);

	ai_behavior_selector = memnew(RtsoSafeSelect);
	ai_behavior_selector->set_custom_minimum_size(Vector2(110, 28));
	ai_behavior_selector->add_item("AI Balanced", rtso::PETRA_BEHAVIOR_BALANCED);
	ai_behavior_selector->add_item("AI Defensive", rtso::PETRA_BEHAVIOR_DEFENSIVE);
	ai_behavior_selector->add_item("AI Aggressive", rtso::PETRA_BEHAVIOR_AGGRESSIVE);
	ai_behavior_selector->add_item("AI Random", rtso::PETRA_BEHAVIOR_RANDOM);
	ai_behavior_selector->connect("item_selected", Callable(this, "_on_ai_behavior_selected"));
	top_hbox->add_child(ai_behavior_selector);

	victory_selector = memnew(RtsoSafeSelect);
	victory_selector->set_custom_minimum_size(Vector2(110, 28));
	victory_selector->add_item("Conquest", rtso::VICTORY_CONQUEST);
	victory_selector->add_item("Wonder", rtso::VICTORY_WONDER);
	victory_selector->add_item("Relics", rtso::VICTORY_RELIC);
	victory_selector->add_item("Endless", rtso::VICTORY_ENDLESS);
	victory_selector->connect("item_selected", Callable(this, "_on_victory_selected"));
	top_hbox->add_child(victory_selector);

	voice_btn = memnew(Button);
	voice_btn->set_custom_minimum_size(Vector2(72, 28));
	voice_btn->set_text("Voice: EN");
	voice_btn->add_theme_font_size_override("font_size", 12);
	voice_btn->connect("pressed", Callable(this, "_on_voice_button_pressed"));
	top_hbox->add_child(voice_btn);

	lobby_btn = memnew(Button);
	lobby_btn->set_custom_minimum_size(Vector2(64, 28));
	lobby_btn->set_text("Lobby");
	lobby_btn->add_theme_font_size_override("font_size", 12);
	lobby_btn->connect("pressed", Callable(this, "_on_lobby_button_pressed"));
	top_hbox->add_child(lobby_btn);

	lobby_panel = memnew(Panel);
	lobby_panel->set_visible(false);
	lobby_panel->set_anchor(SIDE_LEFT, 0.0f);
	lobby_panel->set_anchor(SIDE_RIGHT, 0.0f);
	lobby_panel->set_anchor(SIDE_TOP, 0.0f);
	lobby_panel->set_anchor(SIDE_BOTTOM, 0.0f);
	lobby_panel->set_offset(SIDE_LEFT, 16.0f);
	lobby_panel->set_offset(SIDE_RIGHT, 420.0f);
	lobby_panel->set_offset(SIDE_TOP, 52.0f);
	lobby_panel->set_offset(SIDE_BOTTOM, 320.0f);
	ui_layer->add_child(lobby_panel);

	VBoxContainer *lobby_box = memnew(VBoxContainer);
	lobby_box->set_anchors_preset(Control::PRESET_FULL_RECT);
	lobby_box->set_offset(SIDE_LEFT, 8);
	lobby_box->set_offset(SIDE_RIGHT, -8);
	lobby_box->set_offset(SIDE_TOP, 8);
	lobby_box->set_offset(SIDE_BOTTOM, -8);
	lobby_panel->add_child(lobby_box);

	Label *lobby_title = memnew(Label);
	lobby_title->set_text("Multiplayer lobby");
	lobby_box->add_child(lobby_title);

	HBoxContainer *name_row = memnew(HBoxContainer);
	lobby_name = memnew(LineEdit);
	lobby_name->set_placeholder("Player name");
	lobby_name->set_text("Player");
	lobby_name->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	rtso::parent_line_edit(name_row, lobby_name);
	lobby_host = memnew(LineEdit);
	lobby_host->set_placeholder("127.0.0.1");
	lobby_host->set_text("127.0.0.1");
	lobby_host->set_custom_minimum_size(Vector2(110, 0));
	rtso::parent_line_edit(name_row, lobby_host);
	lobby_port = memnew(LineEdit);
	lobby_port->set_placeholder("20595");
	lobby_port->set_text("20595");
	lobby_port->set_custom_minimum_size(Vector2(70, 0));
	rtso::parent_line_edit(name_row, lobby_port);
	lobby_box->add_child(name_row);

	lobby_rooms = memnew(ItemList);
	lobby_rooms->set_custom_minimum_size(Vector2(0, 140));
	lobby_box->add_child(lobby_rooms);

	HBoxContainer *lobby_actions = memnew(HBoxContainer);
	Button *host_b = memnew(Button);
	host_b->set_text("Host");
	host_b->connect("pressed", Callable(this, "_on_lobby_host_pressed"));
	lobby_actions->add_child(host_b);
	Button *join_b = memnew(Button);
	join_b->set_text("Join");
	join_b->connect("pressed", Callable(this, "_on_lobby_join_pressed"));
	lobby_actions->add_child(join_b);
	Button *ref_b = memnew(Button);
	ref_b->set_text("Refresh");
	ref_b->connect("pressed", Callable(this, "_on_lobby_refresh_pressed"));
	lobby_actions->add_child(ref_b);
	Button *start_b = memnew(Button);
	start_b->set_text("Start");
	start_b->set_tooltip_text("Host: regenerate map with a fresh seed and broadcast map+seed (NMT_GAME_START)");
	start_b->connect("pressed", Callable(this, "_on_lobby_start_pressed"));
	lobby_actions->add_child(start_b);
	lobby_box->add_child(lobby_actions);

	lobby_status = memnew(Label);
	lobby_status->set_text("Lobby HTTP 127.0.0.1:8088 — run tools/lobby_server.py");
	lobby_box->add_child(lobby_status);

	// =========================================================================
	// 2. BOTTOM INSPECTOR PANEL (5-Bar Stack: Health, Capture, XP, Stamina, Aura)
	// =========================================================================
	inspector_panel = memnew(Panel);
	inspector_panel->set_anchor(SIDE_LEFT, 0.0f);
	inspector_panel->set_anchor(SIDE_RIGHT, 0.0f);
	inspector_panel->set_anchor(SIDE_TOP, 1.0f);
	inspector_panel->set_anchor(SIDE_BOTTOM, 1.0f);
	inspector_panel->set_offset(SIDE_LEFT, 214.0f);
	inspector_panel->set_offset(SIDE_RIGHT, 730.0f);
	inspector_panel->set_offset(SIDE_TOP, -198.0f);
	inspector_panel->set_offset(SIDE_BOTTOM, -10.0f);

	Ref<StyleBoxFlat> insp_style;
	insp_style.instantiate();
	insp_style->set_bg_color(Color(0.08f, 0.09f, 0.13f, 0.94f));
	insp_style->set_corner_radius_all(8);
	insp_style->set_border_width_all(1);
	insp_style->set_border_color(Color(0.42f, 0.45f, 0.56f, 0.85f));
	inspector_panel->add_theme_stylebox_override("panel", insp_style);
	ui_layer->add_child(inspector_panel);

	inspector_portrait = memnew(TextureRect);
	inspector_portrait->set_position(Vector2(10, 10));
	inspector_portrait->set_size(Vector2(84, 84));
	inspector_portrait->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	inspector_portrait->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_COVERED);
	inspector_panel->add_child(inspector_portrait);

	inspector_title = memnew(Label);
	inspector_title->set_position(Vector2(102, 6));
	inspector_title->set_size(Vector2(404, 18));
	inspector_title->add_theme_font_size_override("font_size", 13);
	inspector_title->add_theme_color_override("font_color", Color(1.0f, 0.88f, 0.4f));
	inspector_panel->add_child(inspector_title);

	inspector_class = memnew(Label);
	inspector_class->set_position(Vector2(102, 24));
	inspector_class->set_size(Vector2(404, 15));
	inspector_class->add_theme_font_size_override("font_size", 10);
	inspector_class->add_theme_color_override("font_color", Color(0.7f, 0.75f, 0.85f));
	inspector_panel->add_child(inspector_class);

	// Helper lambda to setup 5 inspector bars with PNG icons or fallback text-based glyph icon labels
	// (0 A.D. gui/session/selection_details.js icon parity: HP/CP/XP/STA/AURA)
	auto make_insp_bar = [&](ProgressBar *&pbar, Label *&plbl, int y, Color fill_col, const char *glyph_text, const char *icon_path = nullptr) {
		Label *glyph_lbl = memnew(Label);
		glyph_lbl->set_position(Vector2(102, y - 2));
		glyph_lbl->set_size(Vector2(42, 14));
		glyph_lbl->set_text(glyph_text);
		glyph_lbl->add_theme_font_size_override("font_size", 9);
		glyph_lbl->add_theme_color_override("font_color", fill_col);
		inspector_panel->add_child(glyph_lbl);

		if (icon_path && *icon_path) {
			Ref<Texture2D> icon_tex = load_tex(icon_path);
			if (icon_tex.is_valid()) {
				TextureRect *icon_rect = memnew(TextureRect);
				icon_rect->set_position(Vector2(102, y - 2));
				icon_rect->set_size(Vector2(14, 14));
				icon_rect->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
				icon_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
				icon_rect->set_texture(icon_tex);
				inspector_panel->add_child(icon_rect);
				glyph_lbl->set_visible(false);
			}
		}

		pbar = memnew(ProgressBar);
		pbar->set_position(Vector2(148, y));
		pbar->set_size(Vector2(358, 11));
		pbar->set_show_percentage(false);

		Ref<StyleBoxFlat> bg_sb;
		bg_sb.instantiate();
		bg_sb->set_bg_color(Color(0.10f, 0.12f, 0.16f, 0.85f));
		bg_sb->set_corner_radius_all(2);
		pbar->add_theme_stylebox_override("background", bg_sb);

		Ref<StyleBoxFlat> fg_sb;
		fg_sb.instantiate();
		fg_sb->set_bg_color(fill_col);
		fg_sb->set_corner_radius_all(2);
		pbar->add_theme_stylebox_override("fill", fg_sb);
		inspector_panel->add_child(pbar);

		plbl = memnew(Label);
		plbl->set_position(Vector2(148, y - 1));
		plbl->set_size(Vector2(358, 13));
		plbl->add_theme_font_size_override("font_size", 9);
		plbl->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		plbl->add_theme_color_override("font_color", Color(0.95f, 0.95f, 0.98f));
		inspector_panel->add_child(plbl);
	};

	// 1. Health Bar (Green)
	make_insp_bar(inspector_hp_bar, inspector_hp_label, 41, Color(0.25f, 0.75f, 0.35f), "HP", "res://assets/textures/ui/icons/insp_health_fg.png");
	// 2. Capture Points Bar (Cyan / Blue)
	make_insp_bar(inspector_capture_bar, inspector_capture_label, 54, Color(0.35f, 0.55f, 0.95f), "CP", "res://assets/textures/ui/icons/insp_capture_bar.png");
	// 3. Veterancy / Experience XP Bar (Gold / Amber)
	make_insp_bar(inspector_xp_bar, inspector_xp_label, 67, Color(0.95f, 0.75f, 0.20f), "XP", "res://assets/textures/ui/icons/insp_experience_fg.png");
	// 4. Stamina / Energy Bar (Teal / Mint)
	make_insp_bar(inspector_stamina_bar, inspector_stamina_label, 80, Color(0.20f, 0.85f, 0.75f), "STA", "res://assets/textures/ui/icons/insp_stamina_fg.png");
	// 5. Aura Influence Bar (Magenta / Violet) — no public.zip session icon, fallback to text glyph badge
	make_insp_bar(inspector_aura_bar, inspector_aura_label, 93, Color(0.85f, 0.35f, 0.85f), "AURA", nullptr);

	inspector_stats = memnew(Label);
	inspector_stats->set_position(Vector2(102, 108));
	inspector_stats->set_size(Vector2(404, 38));
	inspector_stats->add_theme_font_size_override("font_size", 10);
	inspector_stats->add_theme_color_override("font_color", Color(0.85f, 0.88f, 0.95f));
	inspector_panel->add_child(inspector_stats);

	inspector_extra = memnew(Label);
	inspector_extra->set_position(Vector2(10, 100));
	inspector_extra->set_size(Vector2(88, 48));
	inspector_extra->add_theme_font_size_override("font_size", 9);
	inspector_extra->add_theme_color_override("font_color", Color(0.4f, 0.95f, 0.6f));
	inspector_panel->add_child(inspector_extra);

	ungarrison_btn = memnew(Button);
	ungarrison_btn->set_position(Vector2(10, 150));
	ungarrison_btn->set_size(Vector2(88, 26));
	ungarrison_btn->set_text("Ungarrison (Z)");
	ungarrison_btn->add_theme_font_size_override("font_size", 10);
	ungarrison_btn->connect("pressed", Callable(this, "_on_ungarrison_pressed"));
	ungarrison_btn->set_visible(false);
	inspector_panel->add_child(ungarrison_btn);

	// =========================================================================
	// 3. COMMAND & ACTION PANEL (Stances, Formations, Training, 8-Slot Queue)
	// =========================================================================
	command_panel = memnew(Panel);
	command_panel->set_anchor(SIDE_LEFT, 1.0f);
	command_panel->set_anchor(SIDE_RIGHT, 1.0f);
	command_panel->set_anchor(SIDE_TOP, 1.0f);
	command_panel->set_anchor(SIDE_BOTTOM, 1.0f);
	command_panel->set_offset(SIDE_LEFT, -584.0f);
	command_panel->set_offset(SIDE_RIGHT, -12.0f);
	command_panel->set_offset(SIDE_TOP, -198.0f);
	command_panel->set_offset(SIDE_BOTTOM, -10.0f);

	Ref<StyleBoxFlat> cmd_style;
	cmd_style.instantiate();
	cmd_style->set_bg_color(Color(0.08f, 0.09f, 0.13f, 0.94f));
	cmd_style->set_corner_radius_all(8);
	cmd_style->set_border_width_all(1);
	cmd_style->set_border_color(Color(0.42f, 0.45f, 0.56f, 0.85f));
	command_panel->add_theme_stylebox_override("panel", cmd_style);
	ui_layer->add_child(command_panel);

	command_title = memnew(Label);
	command_title->set_position(Vector2(10, 6));
	command_title->set_size(Vector2(552, 16));
	command_title->add_theme_font_size_override("font_size", 12);
	command_title->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.4f));
	command_title->set_text("Commands & Actions");
	command_panel->add_child(command_title);

	// Stances Bar (Aggressive, Defensive, Stand Ground, Hold, Passive)
	stances_bar = memnew(HBoxContainer);
	stances_bar->set_position(Vector2(10, 24));
	stances_bar->set_size(Vector2(552, 26));
	stances_bar->add_theme_constant_override("separation", 6);
	command_panel->add_child(stances_bar);

	const char *stance_names[5] = {"Aggressive", "Defensive", "Stand Ground", "Hold Ground", "Passive"};
	// Icons mapped by BEHAVIOR parity to 0 A.D. g_Stances (UnitAI.js:80):
	// RTS0 Aggressive leashes 1000m = 0AD "violent" (respondChaseBeyondVision, UnitAI.js:80);
	// RTS0 Defensive 18m/20m leash = 0AD "aggressive" (respondChase, UnitAI.js:92);
	// RTS0 Hold Ground 8m leash = 0AD "defensive" (respondHoldGround, UnitAI.js:104).
	// All 5 icons distinct (0AD stance_panel.xml repeat=5; icon set violent/aggressive/defensive/standground/passive).
	const char *stance_icons[5] = {
		"res://assets/textures/ui/stances/violent.png",
		"res://assets/textures/ui/stances/aggressive.png",
		"res://assets/textures/ui/stances/standground.png",
		"res://assets/textures/ui/stances/defensive.png",
		"res://assets/textures/ui/stances/passive.png"
	};
	for (int s = 0; s < 5; ++s) {
		Button *sb = memnew(Button);
		sb->set_custom_minimum_size(Vector2(95, 24));
		sb->set_text(stance_names[s]);
		Ref<Texture2D> s_ico = load_tex(stance_icons[s]);
		if (s_ico.is_valid()) {
			sb->set_button_icon(s_ico);
			sb->set_expand_icon(true);
		}
		sb->add_theme_font_size_override("font_size", 9);
		sb->connect("pressed", Callable(this, "_on_stance_selected").bind(s));
		stances_bar->add_child(sb);
	}

	// Actions & Formations Bar
	actions_bar = memnew(HBoxContainer);
	actions_bar->set_position(Vector2(10, 52));
	actions_bar->set_size(Vector2(552, 26));
	actions_bar->add_theme_constant_override("separation", 2);
	command_panel->add_child(actions_bar);

	// Production Bar (Training & Tech)
	production_bar = memnew(HBoxContainer);
	production_bar->set_position(Vector2(10, 80));
	production_bar->set_size(Vector2(552, 38));
	production_bar->add_theme_constant_override("separation", 6);
	command_panel->add_child(production_bar);

	// Visual 8-Slot Production Queue Bar (ProductionQueue.js parity)
	queue_bar = memnew(HBoxContainer);
	queue_bar->set_position(Vector2(10, 122));
	queue_bar->set_size(Vector2(552, 54));
	queue_bar->add_theme_constant_override("separation", 6);
	command_panel->add_child(queue_bar);

	for (int q = 0; q < 8; ++q) {
		Panel *sp = memnew(Panel);
		sp->set_custom_minimum_size(Vector2(46, 48));

		Ref<StyleBoxFlat> slot_sb;
		slot_sb.instantiate();
		slot_sb->set_bg_color(Color(0.09f, 0.11f, 0.15f, 0.90f));
		slot_sb->set_border_width_all(1);
		slot_sb->set_border_color(Color(0.35f, 0.38f, 0.48f, 0.70f));
		slot_sb->set_corner_radius_all(4);
		sp->add_theme_stylebox_override("panel", slot_sb);

		TextureRect *ico = memnew(TextureRect);
		ico->set_position(Vector2(3, 2));
		ico->set_size(Vector2(40, 36));
		ico->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		ico->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		ico->set_visible(false);
		sp->add_child(ico);

		ProgressBar *pb = memnew(ProgressBar);
		pb->set_position(Vector2(3, 39));
		pb->set_size(Vector2(40, 4));
		pb->set_show_percentage(false);

		Ref<StyleBoxFlat> pb_fg;
		pb_fg.instantiate();
		pb_fg->set_bg_color(Color(0.95f, 0.75f, 0.20f));
		pb_fg->set_corner_radius_all(1);
		pb->add_theme_stylebox_override("fill", pb_fg);
		pb->set_visible(false);
		sp->add_child(pb);

		Label *chip = memnew(Label);
		chip->set_position(Vector2(20, 1));
		chip->set_size(Vector2(22, 12));
		chip->add_theme_font_size_override("font_size", 9);
		chip->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
		chip->add_theme_color_override("font_color", Color(1.0f, 0.9f, 0.3f));
		chip->set_visible(false);
		sp->add_child(chip);

		sp->connect("gui_input", Callable(this, "_on_queue_slot_gui_input").bind(q));

		queue_slots[q].panel = sp;
		queue_slots[q].icon = ico;
		queue_slots[q].progress = pb;
		queue_slots[q].chip = chip;
		queue_bar->add_child(sp);
	}

	// =========================================================================
	// 4. BANNER OVERLAY (Victory / Defeat)
	// =========================================================================
	banner_panel = memnew(Panel);
	banner_panel->set_anchor(SIDE_LEFT, 0.5f);
	banner_panel->set_anchor(SIDE_RIGHT, 0.5f);
	banner_panel->set_anchor(SIDE_TOP, 0.5f);
	banner_panel->set_anchor(SIDE_BOTTOM, 0.5f);
	banner_panel->set_offset(SIDE_LEFT, -220.0f);
	banner_panel->set_offset(SIDE_RIGHT, 220.0f);
	banner_panel->set_offset(SIDE_TOP, -80.0f);
	banner_panel->set_offset(SIDE_BOTTOM, 80.0f);
	banner_panel->set_visible(false);

	Ref<StyleBoxFlat> banner_style;
	banner_style.instantiate();
	banner_style->set_bg_color(Color(0.05f, 0.06f, 0.09f, 0.95f));
	banner_style->set_corner_radius_all(10);
	banner_style->set_border_width_all(2);
	banner_style->set_border_color(Color(0.8f, 0.7f, 0.2f, 1.0f));
	banner_panel->add_theme_stylebox_override("panel", banner_style);
	ui_layer->add_child(banner_panel);

	banner_label = memnew(Label);
	banner_label->set_position(Vector2(20, 25));
	banner_label->set_size(Vector2(400, 110));
	banner_label->add_theme_font_size_override("font_size", 22);
	banner_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	banner_panel->add_child(banner_label);

	// =========================================================================
	// 5. IN-GAME CHAT OVERLAY (LineEdit + All/Allies/Team channels)
	// =========================================================================
	chat_panel = memnew(Panel);
	chat_panel->set_anchor(SIDE_LEFT, 0.0f);
	chat_panel->set_anchor(SIDE_TOP, 0.0f);
	chat_panel->set_offset(SIDE_LEFT, 16.0f);
	chat_panel->set_offset(SIDE_RIGHT, 400.0f);
	chat_panel->set_offset(SIDE_TOP, 60.0f);
	chat_panel->set_offset(SIDE_BOTTOM, 260.0f);

	Ref<StyleBoxFlat> chat_style;
	chat_style.instantiate();
	chat_style->set_bg_color(Color(0.06f, 0.07f, 0.10f, 0.94f));
	chat_style->set_corner_radius_all(6);
	chat_style->set_border_width_all(1);
	chat_style->set_border_color(Color(0.35f, 0.38f, 0.48f, 0.85f));
	chat_panel->add_theme_stylebox_override("panel", chat_style);

	VBoxContainer *chat_vbox = memnew(VBoxContainer);
	chat_vbox->set_anchor(SIDE_LEFT, 0.0f);
	chat_vbox->set_anchor(SIDE_RIGHT, 1.0f);
	chat_vbox->set_anchor(SIDE_TOP, 0.0f);
	chat_vbox->set_anchor(SIDE_BOTTOM, 1.0f);
	chat_vbox->set_offset(SIDE_LEFT, 8.0f);
	chat_vbox->set_offset(SIDE_RIGHT, -8.0f);
	chat_vbox->set_offset(SIDE_TOP, 6.0f);
	chat_vbox->set_offset(SIDE_BOTTOM, -6.0f);
	chat_vbox->add_theme_constant_override("separation", 4);
	chat_panel->add_child(chat_vbox);

	HBoxContainer *chat_top_bar = memnew(HBoxContainer);
	chat_top_bar->set_custom_minimum_size(Vector2(0, 24));
	Label *chat_title = memnew(Label);
	chat_title->set_text("💬 In-Game Chat");
	chat_title->add_theme_font_size_override("font_size", 12);
	chat_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_top_bar->add_child(chat_title);

	Button *chat_close = memnew(Button);
	chat_close->set_custom_minimum_size(Vector2(22, 22));
	chat_close->set_text("✕");
	chat_close->add_theme_font_size_override("font_size", 10);
	chat_close->connect("pressed", Callable(this, "toggle_chat"));
	chat_top_bar->add_child(chat_close);
	chat_vbox->add_child(chat_top_bar);

	chat_log = memnew(RichTextLabel);
	chat_log->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_log->set_use_bbcode(true);
	chat_log->set_scroll_follow(true);
	chat_log->add_theme_font_size_override("normal_font_size", 11);
	chat_log->set_text("[color=#888888]Hellenic League Session Chat. Press Enter to send messages.[/color]\n");
	chat_vbox->add_child(chat_log);

	// Chat addressee selector row above chat input (0 A.D. gui/session/chat/ChatWindow.js:35-58 parity)
	HBoxContainer *chat_addressee_bar = memnew(HBoxContainer);
	chat_addressee_bar->set_custom_minimum_size(Vector2(0, 24));

	Label *to_lbl = memnew(Label);
	to_lbl->set_text("To:");
	to_lbl->add_theme_font_size_override("font_size", 10);
	to_lbl->add_theme_color_override("font_color", Color(0.75f, 0.78f, 0.88f));
	chat_addressee_bar->add_child(to_lbl);

	chat_channel = memnew(RtsoSafeSelect);
	chat_channel->set_custom_minimum_size(Vector2(110, 22));
	chat_channel->add_item("All", 0);
	chat_channel->add_item("Allies", 1);
	chat_channel->add_item("Enemies", 2);
	for (int p = 1; p <= 8; ++p) {
		chat_channel->add_item("Player-" + String::num_int64(p), 2 + p);
	}
	chat_channel->add_theme_font_size_override("font_size", 10);
	chat_addressee_bar->add_child(chat_channel);

	auto add_quick_btn = [&](const String &label, int ch_idx) {
		Button *b = memnew(Button);
		b->set_custom_minimum_size(Vector2(42, 20));
		b->set_text(label);
		b->add_theme_font_size_override("font_size", 9);
		b->connect("pressed", Callable(this, "_on_chat_channel_btn_pressed").bind(ch_idx));
		chat_addressee_bar->add_child(b);
	};
	add_quick_btn("All", 0);
	add_quick_btn("Ally", 1);
	add_quick_btn("Enemy", 2);

	chat_vbox->add_child(chat_addressee_bar);

	HBoxContainer *chat_input_bar = memnew(HBoxContainer);
	chat_input_bar->set_custom_minimum_size(Vector2(0, 26));
	chat_input = memnew(LineEdit);
	chat_input->set_placeholder("Type message...");
	chat_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_input->add_theme_font_size_override("font_size", 11);
	chat_input->connect("text_submitted", Callable(this, "_on_chat_text_submitted"));
	rtso::parent_line_edit(chat_input_bar, chat_input);

	chat_send_btn = memnew(Button);
	chat_send_btn->set_custom_minimum_size(Vector2(55, 24));
	chat_send_btn->set_text("Send");
	chat_send_btn->add_theme_font_size_override("font_size", 11);
	chat_send_btn->connect("pressed", Callable(this, "_on_chat_send_pressed"));
	chat_input_bar->add_child(chat_send_btn);
	chat_vbox->add_child(chat_input_bar);

	chat_panel->set_visible(false);
	ui_layer->add_child(chat_panel);

	// =========================================================================
	// 6. DIPLOMACY & TRIBUTE DIALOG (8-Player Matrix)
	// =========================================================================
	diplomacy_panel = memnew(Panel);
	diplomacy_panel->set_anchor(SIDE_LEFT, 0.5f);
	diplomacy_panel->set_anchor(SIDE_RIGHT, 0.5f);
	diplomacy_panel->set_anchor(SIDE_TOP, 0.5f);
	diplomacy_panel->set_anchor(SIDE_BOTTOM, 0.5f);
	diplomacy_panel->set_offset(SIDE_LEFT, -320.0f);
	diplomacy_panel->set_offset(SIDE_RIGHT, 320.0f);
	diplomacy_panel->set_offset(SIDE_TOP, -210.0f);
	diplomacy_panel->set_offset(SIDE_BOTTOM, 210.0f);

	Ref<StyleBoxFlat> diplo_style;
	diplo_style.instantiate();
	diplo_style->set_bg_color(Color(0.06f, 0.07f, 0.10f, 0.97f));
	diplo_style->set_corner_radius_all(8);
	diplo_style->set_border_width_all(2);
	diplo_style->set_border_color(Color(0.85f, 0.72f, 0.25f, 0.95f));
	diplomacy_panel->add_theme_stylebox_override("panel", diplo_style);

	VBoxContainer *diplo_vbox = memnew(VBoxContainer);
	diplo_vbox->set_anchor(SIDE_LEFT, 0.0f);
	diplo_vbox->set_anchor(SIDE_RIGHT, 1.0f);
	diplo_vbox->set_anchor(SIDE_TOP, 0.0f);
	diplo_vbox->set_anchor(SIDE_BOTTOM, 1.0f);
	diplo_vbox->set_offset(SIDE_LEFT, 16.0f);
	diplo_vbox->set_offset(SIDE_RIGHT, -16.0f);
	diplo_vbox->set_offset(SIDE_TOP, 12.0f);
	diplo_vbox->set_offset(SIDE_BOTTOM, -12.0f);
	diplo_vbox->add_theme_constant_override("separation", 6);
	diplomacy_panel->add_child(diplo_vbox);

	Label *diplo_title = memnew(Label);
	diplo_title->set_text("🏛️ Hellenic League — Diplomacy & Tribute Matrix");
	diplo_title->add_theme_font_size_override("font_size", 15);
	diplo_title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	diplo_title->add_theme_color_override("font_color", Color(1.0f, 0.88f, 0.4f));
	diplo_vbox->add_child(diplo_title);

	HSeparator *diplo_sep1 = memnew(HSeparator);
	diplo_vbox->add_child(diplo_sep1);

	const char *player_names[8] = {
		"🔵 Athens (You)",
		"🔴 Sparta (AI)",
		"🟡 Persia (AI)",
		"🟣 Rome (AI)",
		"⚪ Carthage (AI)",
		"🟢 Macedon (AI)",
		"🟠 Gaul (AI)",
		"🟤 Maurya (AI)"
	};

	for (int p = 0; p < 8; ++p) {
		HBoxContainer *prow = memnew(HBoxContainer);
		prow->set_custom_minimum_size(Vector2(0, 26));
		prow->add_theme_constant_override("separation", 8);

		diplo_player_names[p] = memnew(Label);
		diplo_player_names[p]->set_custom_minimum_size(Vector2(165, 24));
		diplo_player_names[p]->set_text(player_names[p]);
		diplo_player_names[p]->add_theme_font_size_override("font_size", 11);
		prow->add_child(diplo_player_names[p]);

		diplo_stances[p] = memnew(RtsoSafeSelect);
		diplo_stances[p]->set_custom_minimum_size(Vector2(95, 24));
		diplo_stances[p]->add_theme_font_size_override("font_size", 10);
		if (p == 0) {
			diplo_stances[p]->add_item("Self", 0);
			diplo_stances[p]->set_disabled(true);
		} else {
			diplo_stances[p]->add_item("Ally", 0);
			diplo_stances[p]->add_item("Neutral", 1);
			diplo_stances[p]->add_item("Enemy", 2);
			diplo_stances[p]->select(2); // Enemy
			diplo_stances[p]->connect("item_selected", Callable(this, "_on_diplo_stance_changed").bind(p));
		}
		prow->add_child(diplo_stances[p]);

		if (p == 0) {
			Label *self_lbl = memnew(Label);
			self_lbl->set_text("— Player Base Command —");
			self_lbl->add_theme_font_size_override("font_size", 10);
			self_lbl->add_theme_color_override("font_color", Color(0.6f, 0.6f, 0.7f));
			prow->add_child(self_lbl);
		} else {
			const char *t_icons[4] = {"🍞 +100", "🌲 +100", "🪙 +100", "🪨 +100"};
			int t_res[4] = {rtso::RES_FOOD, rtso::RES_WOOD, rtso::RES_GOLD, rtso::RES_STONE};
			for (int r = 0; r < 4; ++r) {
				Button *tb = memnew(Button);
				tb->set_custom_minimum_size(Vector2(68, 22));
				tb->set_text(t_icons[r]);
				tb->add_theme_font_size_override("font_size", 9);
				tb->connect("pressed", Callable(this, "_on_diplo_tribute_pressed").bind(p, t_res[r]));
				prow->add_child(tb);
			}
		}

		diplo_vbox->add_child(prow);
	}

	HSeparator *diplo_sep2 = memnew(HSeparator);
	diplo_vbox->add_child(diplo_sep2);

	diplo_close_btn = memnew(Button);
	diplo_close_btn->set_custom_minimum_size(Vector2(130, 28));
	diplo_close_btn->set_text("Close (D / Esc)");
	diplo_close_btn->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
	diplo_close_btn->add_theme_font_size_override("font_size", 11);
	diplo_close_btn->connect("pressed", Callable(this, "toggle_diplomacy"));
	diplo_vbox->add_child(diplo_close_btn);

	diplomacy_panel->set_visible(false);
	ui_layer->add_child(diplomacy_panel);
	rtso::strip_engine_popups(ui_layer);

	// =========================================================================
	// 5. AUDIO PLAYERS (Voice, SFX, Alarm, Music)
	// =========================================================================
	voice_player = memnew(AudioStreamPlayer);
	voice_player->set_volume_db(-2.0f);
	add_child(voice_player);

	alarm_player = memnew(AudioStreamPlayer);
	alarm_player->set_volume_db(-4.0f);
	sfx_player = memnew(AudioStreamPlayer);
	sfx_player->set_volume_db(-3.0f);
	add_child(sfx_player);
	add_child(alarm_player);

	if (ResourceLoader::get_singleton()->exists("res://assets/music/peace_1.ogg")) {
		Ref<AudioStream> stream = ResourceLoader::get_singleton()->load("res://assets/music/peace_1.ogg");
		if (stream.is_valid()) {
			music_player = memnew(AudioStreamPlayer);
			music_player->set_stream(stream);
			music_player->set_autoplay(true);
			music_player->set_volume_db(-8.0f);
			add_child(music_player);
			music_player->play();
		}
	}

	if (ResourceLoader::get_singleton()->exists("res://assets/music/battle_1.ogg")) {
		Ref<AudioStream> bstream = ResourceLoader::get_singleton()->load("res://assets/music/battle_1.ogg");
		if (bstream.is_valid()) {
			battle_music_player = memnew(AudioStreamPlayer);
			battle_music_player->set_stream(bstream);
			battle_music_player->set_autoplay(true);
			battle_music_player->set_volume_db(-80.0f);
			add_child(battle_music_player);
			battle_music_player->play();
		}
	}

	if (ResourceLoader::get_singleton()->exists("res://assets/sounds/ambient/nature_ambient.ogg")) {
		Ref<AudioStream> amb_stream = ResourceLoader::get_singleton()->load("res://assets/sounds/ambient/nature_ambient.ogg");
		if (amb_stream.is_valid()) {
			ambient_player = memnew(AudioStreamPlayer);
			ambient_player->set_stream(amb_stream);
			ambient_player->set_autoplay(true);
			ambient_player->set_volume_db(-12.0f);
			add_child(ambient_player);
			ambient_player->play();
		}
	}

	if (ResourceLoader::get_singleton()->exists("res://assets/sounds/ambient/wind_ambient.ogg")) {
		Ref<AudioStream> wind_stream = ResourceLoader::get_singleton()->load("res://assets/sounds/ambient/wind_ambient.ogg");
		if (wind_stream.is_valid()) {
			ambient_wind_player = memnew(AudioStreamPlayer);
			ambient_wind_player->set_stream(wind_stream);
			ambient_wind_player->set_autoplay(true);
			ambient_wind_player->set_volume_db(-25.0f);
			add_child(ambient_wind_player);
			ambient_wind_player->play();
		}
	}

	if (ResourceLoader::get_singleton()->exists("res://assets/sounds/ambient/water_coast.ogg")) {
		Ref<AudioStream> water_stream = ResourceLoader::get_singleton()->load("res://assets/sounds/ambient/water_coast.ogg");
		if (water_stream.is_valid()) {
			ambient_water_player = memnew(AudioStreamPlayer);
			ambient_water_player->set_stream(water_stream);
			ambient_water_player->set_autoplay(true);
			ambient_water_player->set_volume_db(-80.0f);
			add_child(ambient_water_player);
			ambient_water_player->play();
		}
	}

	spatial_sfx_player = memnew(AudioStreamPlayer3D);
	spatial_sfx_player->set_max_distance(75.0f);
	spatial_sfx_player->set_unit_size(10.0f);
	spatial_sfx_player->set_volume_db(0.0f);
	add_child(spatial_sfx_player);

	// =========================================================================
	// 6. GHOST BUILDING & RALLY FLAG PREVIEW
	// =========================================================================
	ghost_mat_ok.instantiate();
	ghost_mat_ok->set_albedo(Color(0.2f, 0.9f, 0.2f, 0.55f));
	ghost_mat_ok->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	ghost_mat_ok->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	ghost_mat_bad.instantiate();
	ghost_mat_bad->set_albedo(Color(0.9f, 0.2f, 0.2f, 0.55f));
	ghost_mat_bad->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	ghost_mat_bad->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	ghost_mesh = memnew(MeshInstance3D);
	ghost_mesh->set_visible(false);
	add_child(ghost_mesh);
	update_ghost_mesh();

	// 3D Rally Flag Marker
	rally_flag = memnew(MeshInstance3D);
	Ref<CylinderMesh> flag_c;
	flag_c.instantiate();
	flag_c->set_top_radius(0.08f);
	flag_c->set_bottom_radius(0.08f);
	flag_c->set_height(1.6f);
	rally_flag->set_mesh(flag_c);
	Ref<StandardMaterial3D> flag_mat;
	flag_mat.instantiate();
	flag_mat->set_albedo(Color(0.2f, 0.8f, 1.0f));
	rally_flag->set_material_override(flag_mat);
	rally_flag->set_visible(false);
	add_child(rally_flag);

	// (Unit selection now happens in apply_match_settings, after the map
	// exists — the old block here ran before any map loaded and selected
	// nothing. apply_net_game_start re-selects on GameStart via
	// load_map_seeded -> clear_selection; selection follows in _process.)
	// G3 Barter dialog wiring: connect BarterDialog.barter_requested -> _on_barter_requested
	Node *barter = get_node_or_null(NodePath("BarterDialog"));
	if (barter) {
		barter->connect("barter_requested", Callable(this, "_on_barter_requested"));
	}

	apply_match_settings();
}

void RtsoMain::play_voice(const char *path) {
	if (!voice_player || !path) return;
	String p = path;
	String final_path = p;
	if (!final_path.begins_with("res://")) {
		if (final_path.begins_with("audio/")) {
			final_path = "res://assets/sounds/" + final_path.substr(6);
		} else if (final_path.begins_with("assets/sounds/")) {
			final_path = "res://" + final_path;
		} else {
			final_path = "res://" + final_path;
		}
	}
	if (!ResourceLoader::get_singleton()->exists(final_path)) {
		if (ResourceLoader::get_singleton()->exists(p)) {
			final_path = p;
		} else if (ResourceLoader::get_singleton()->exists("res://" + p)) {
			final_path = "res://" + p;
		}
	}
	if (ResourceLoader::get_singleton()->exists(final_path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(final_path);
		if (s.is_valid()) {
			voice_player->set_stream(s);
			voice_player->play();
		}
	}
}

void RtsoMain::play_civ_voice(const char *action) {
	if (!voice_player) return;
	static uint32_t voice_rand_idx = 0;
	++voice_rand_idx;

	std::string act(action);

	if (use_english_voice_orders_) {
		// Pure English orders voice lines
		if (act == "select") {
			const char *opts[] = {
				"res://assets/sounds/voice/select_1.ogg",
				"res://assets/sounds/voice/select_2.ogg",
				"res://assets/sounds/voice/select_3.ogg"
			};
			play_voice(opts[voice_rand_idx % 3]);
		} else if (act == "move") {
			const char *opts[] = {
				"res://assets/sounds/voice/move_1.ogg",
				"res://assets/sounds/voice/move_2.ogg"
			};
			play_voice(opts[voice_rand_idx % 2]);
		} else if (act == "attack") {
			const char *opts[] = {
				"res://assets/sounds/voice/attack_1.ogg",
				"res://assets/sounds/voice/attack_2.ogg"
			};
			play_voice(opts[voice_rand_idx % 2]);
		} else if (act == "build") {
			play_voice("res://assets/sounds/voice/build_1.ogg");
		} else if (act == "gather") {
			play_voice("res://assets/sounds/voice/gather_1.ogg");
		} else if (act == "repair") {
			play_voice("res://assets/sounds/voice/repair_1.ogg");
		}
		return;
	}

	uint8_t current_civ = sim ? sim->player_civ() : (uint8_t)rtso::CIV_ATHENIANS;
	const char *chosen_file = nullptr;

	if (current_civ == rtso::CIV_ATHENIANS || current_civ == rtso::CIV_SPARTANS ||
		current_civ == rtso::CIV_MACEDONIANS || current_civ == rtso::CIV_PTOLEMIES ||
		current_civ == rtso::CIV_SELEUCIDS) {
		// Ancient Hellenic voice lines pool (audio/voice/hellenes/)
		if (act == "select") {
			// Selection: "Khaire", "Ti estin", "Hetoimos"
			const char *opts[][2] = {
				{ "res://assets/sounds/voice/hellenes/khaire.ogg", "res://assets/sounds/voice/greek/civ_female_what_is_it_1.ogg" },
				{ "res://assets/sounds/voice/hellenes/ti_estin.ogg", "res://assets/sounds/voice/greek/civ_female_what_is_it_2.ogg" },
				{ "res://assets/sounds/voice/hellenes/hetoimos.ogg", "res://assets/sounds/voice/greek/civ_female_what_is_it_3.ogg" }
			};
			int idx = voice_rand_idx % 3;
			chosen_file = ResourceLoader::get_singleton()->exists(opts[idx][0]) ? opts[idx][0] : opts[idx][1];
		} else if (act == "move") {
			// Move orders: "Poreuomai", "Baino"
			const char *opts[][2] = {
				{ "res://assets/sounds/voice/hellenes/poreuomai.ogg", "res://assets/sounds/voice/greek/civ_female_walk_1.ogg" },
				{ "res://assets/sounds/voice/hellenes/baino.ogg", "res://assets/sounds/voice/greek/civ_female_march_1.ogg" }
			};
			int idx = voice_rand_idx % 2;
			chosen_file = ResourceLoader::get_singleton()->exists(opts[idx][0]) ? opts[idx][0] : opts[idx][1];
		} else if (act == "attack") {
			// Attack orders: "Epi tous polemious!", "Eis makhen!"
			const char *opts[][2] = {
				{ "res://assets/sounds/voice/hellenes/epi_tous_polemious.ogg", "res://assets/sounds/voice/greek/civ_female_attack_1.ogg" },
				{ "res://assets/sounds/voice/hellenes/eis_makhen.ogg", "res://assets/sounds/voice/greek/civ_female_go_out_against_1.ogg" }
			};
			int idx = voice_rand_idx % 2;
			chosen_file = ResourceLoader::get_singleton()->exists(opts[idx][0]) ? opts[idx][0] : opts[idx][1];
		} else if (act == "build") {
			// Build orders: "Poieso"
			chosen_file = ResourceLoader::get_singleton()->exists("res://assets/sounds/voice/hellenes/poieso.ogg")
				? "res://assets/sounds/voice/hellenes/poieso.ogg"
				: "res://assets/sounds/voice/greek/civ_female_build_1.ogg";
		} else if (act == "gather") {
			// Gather orders: "Ergon"
			chosen_file = ResourceLoader::get_singleton()->exists("res://assets/sounds/voice/hellenes/ergon.ogg")
				? "res://assets/sounds/voice/hellenes/ergon.ogg"
				: "res://assets/sounds/voice/greek/civ_female_work_land_1.ogg";
		}
	} else if (current_civ == rtso::CIV_ROMANS || current_civ == rtso::CIV_BRITONS ||
	           current_civ == rtso::CIV_GAULS) {
		// Latin / Roman voice pool
		if (act == "select") {
			const char *opts[] = {
				"res://assets/sounds/voice/latin/civ_male_hello_1.ogg",
				"res://assets/sounds/voice/latin/civ_male_what_is_it_1.ogg",
				"res://assets/sounds/voice/latin/civ_female_hello_1.ogg"
			};
			chosen_file = opts[voice_rand_idx % 3];
		} else if (act == "move") {
			const char *opts[] = {
				"res://assets/sounds/voice/latin/civ_male_march_1.ogg",
				"res://assets/sounds/voice/latin/civ_male_walk_1.ogg"
			};
			chosen_file = opts[voice_rand_idx % 2];
		} else if (act == "attack") {
			const char *opts[] = {
				"res://assets/sounds/voice/latin/civ_male_attack_1.ogg",
				"res://assets/sounds/voice/latin/civ_male_fight_1.ogg"
			};
			chosen_file = opts[voice_rand_idx % 2];
		} else if (act == "build") {
			chosen_file = "res://assets/sounds/voice/latin/civ_male_build_1.ogg";
		} else if (act == "gather") {
			chosen_file = "res://assets/sounds/voice/latin/civ_male_work_land_1.ogg";
		}
	} else if (current_civ == rtso::CIV_PERSIANS || current_civ == rtso::CIV_CARTHAGINIANS ||
	           current_civ == rtso::CIV_IBERIANS || current_civ == rtso::CIV_MAURYANS) {
		// Persian voice pool
		if (act == "select") {
			const char *opts[] = {
				"res://assets/sounds/voice/persian/civ_male_hello_1.ogg",
				"res://assets/sounds/voice/persian/civ_female_hello_1.ogg"
			};
			chosen_file = opts[voice_rand_idx % 2];
		} else if (act == "move") {
			const char *opts[] = {
				"res://assets/sounds/voice/persian/civ_male_forward_1.ogg",
				"res://assets/sounds/voice/persian/civ_male_forward_2.ogg"
			};
			chosen_file = opts[voice_rand_idx % 2];
		} else if (act == "attack") {
			const char *opts[] = {
				"res://assets/sounds/voice/persian/civ_male_attack_1.ogg",
				"res://assets/sounds/voice/persian/civ_female_to_war_1.ogg"
			};
			chosen_file = opts[voice_rand_idx % 2];
		} else if (act == "build") {
			chosen_file = "res://assets/sounds/voice/persian/civ_male_build_1.ogg";
		} else if (act == "gather") {
			chosen_file = "res://assets/sounds/voice/persian/civ_male_gather_1.ogg";
		}
	} else if (current_civ == rtso::CIV_KUSHITES) {
		// Napatan / Kushite voice pool
		if (act == "select") {
			const char *opts[] = {
				"res://assets/sounds/voice/napatan/civ_male_what_is_it_01.ogg",
				"res://assets/sounds/voice/napatan/civ_female_hello_01.ogg"
			};
			chosen_file = opts[voice_rand_idx % 2];
		} else if (act == "move") {
			const char *opts[] = {
				"res://assets/sounds/voice/napatan/civ_male_walk_01.ogg",
				"res://assets/sounds/voice/napatan/civ_female_march_01.ogg"
			};
			chosen_file = opts[voice_rand_idx % 2];
		} else if (act == "attack") {
			const char *opts[] = {
				"res://assets/sounds/voice/napatan/civ_male_attack_01.ogg",
				"res://assets/sounds/voice/napatan/civ_male_go_out_against_01.ogg"
			};
			chosen_file = opts[voice_rand_idx % 2];
		} else if (act == "build") {
			chosen_file = "res://assets/sounds/voice/napatan/civ_male_build_01.ogg";
		} else if (act == "gather") {
			chosen_file = "res://assets/sounds/voice/napatan/civ_male_gather_01.ogg";
		}
	}

	// Fallback to default voices if specific civ file doesn't exist
	if (chosen_file && ResourceLoader::get_singleton()->exists(chosen_file)) {
		play_voice(chosen_file);
		return;
	}

	if (act == "select") play_voice("res://assets/sounds/voice/select_1.ogg");
	else if (act == "move") play_voice("res://assets/sounds/voice/move_1.ogg");
	else if (act == "attack") play_voice("res://assets/sounds/voice/attack_1.ogg");
	else if (act == "build") play_voice("res://assets/sounds/voice/build_1.ogg");
	else if (act == "gather") play_voice("res://assets/sounds/voice/gather_1.ogg");
}

void RtsoMain::play_spatial_sfx(const char *path, Vector3 pos) {
	if (view) {
		view->play_spatial_sound_path(path, pos);
		return;
	}
	if (!spatial_sfx_player) return;
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(path);
		if (s.is_valid()) {
			spatial_sfx_player->set_position(pos);
			spatial_sfx_player->set_stream(s);
			spatial_sfx_player->play();
		}
	}
}

void RtsoMain::play_sfx(const char *path) {
	if (!sfx_player) return;
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(path);
		if (s.is_valid()) {
			sfx_player->set_stream(s);
			sfx_player->play();
		}
	}
}

void RtsoMain::play_alarm(const char *path) {
	if (!alarm_player) return;
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(path);
		if (s.is_valid()) {
			alarm_player->set_stream(s);
			alarm_player->play();
		}
	}
}

// Tactical flare: broadcast to session peers over NMT_CHAT (approximation of
// 0 A.D.'s dedicated flare message; receive side parses "[Flare] x,z" in the
// onChat callback registered in _ready()). Local ping already added by caller.
void RtsoMain::_on_flare_requested(Vector2 world_pos) {
	if (!net_lockstep()) return;
	std::string wire = "[Flare] " + std::to_string(world_pos.x) + "," + std::to_string(world_pos.y);
	net_host_.sendChat(wire);
}

void RtsoMain::_on_minimap_clicked(Vector2 world_pos) {
	if (cam_rig) {
		cam_rig->set_target_pos(Vector3(world_pos.x, 0.0f, world_pos.y));
	}
}

void RtsoMain::_on_idle_worker_clicked() {
	cycle_idle_worker();
}

void RtsoMain::cycle_idle_worker() {
	auto idle_ids = sim->get_idle_workers(0);
	if (idle_ids.empty()) return;

	last_idle_index = (last_idle_index + 1) % idle_ids.size();
	uint32_t target_uid = idle_ids[last_idle_index];

	selected_ids.clear();
	selected_building_id = 0;
	selected_ids.push_back((int32_t)target_uid);

	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (uview.id[i] == target_uid && cam_rig) {
			cam_rig->set_target_pos(Vector3(uview.pos_x[i], 0, uview.pos_z[i]));
			play_civ_voice("select");
			break;
		}
	}
}

void RtsoMain::_on_speed_button_pressed() {
	if (game_speed == 1.0f) game_speed = 1.5f;
	else if (game_speed == 1.5f) game_speed = 2.0f;
	else game_speed = 1.0f;

	if (speed_btn) speed_btn->set_text(String::num(game_speed, 1) + "x");
}

void RtsoMain::_on_menu_button_pressed() {
	Node *pause = get_node_or_null(NodePath("PauseMenu"));
	if (pause) {
		pause->call("toggle");
		paused_via_menu = !paused_via_menu;
	}
}

void RtsoMain::_on_ungarrison_pressed() {
	if (selected_building_id != 0) {
		if (net_lockstep())
			post_lockstep(13, nullptr, 0, 0, 0, selected_building_id, 0);
		else
			sim->order_ungarrison(selected_building_id);
	} else if (!selected_ids.is_empty()) {
		for (int s = 0; s < selected_ids.size(); ++s) {
			if (sim->is_transport((uint32_t)selected_ids[s])) {
				uint32_t tid = (uint32_t)selected_ids[s];
				if (net_lockstep())
					post_lockstep(13, &tid, 1, 0, 0, 0, 1);
				else
					sim->order_disembark_all(tid);
			}
		}
	}
}

void RtsoMain::_on_stance_selected(int stance) {
	if (!selected_ids.is_empty()) {
		if (net_lockstep())
			post_lockstep(7, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, 0, (uint8_t)stance);
		else
			sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), (uint8_t)stance);
	}
}

void RtsoMain::_on_formation_selected(int formation) {
	if (!selected_ids.is_empty()) {
		if (net_lockstep())
			post_lockstep(8, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, 0, (uint8_t)formation);
		else
			sim->order_set_formation((const uint32_t *)selected_ids.ptr(), selected_ids.size(), (uint8_t)formation);
	}
}

void RtsoMain::_on_train_pressed(int utype) {
	try_train_unit((rtso::UnitType)utype);
}

void RtsoMain::_on_build_btn_pressed(int btype) {
	enter_build_mode(true, (uint8_t)btype);
}

void RtsoMain::_on_tech_pressed(int tech_id) {
	if (selected_building_id != 0) {
		if (net_lockstep())
			post_lockstep(9, nullptr, 0, 0, 0, selected_building_id, (uint8_t)tech_id);
		else
			sim->cmd_research(selected_building_id, (uint8_t)tech_id);
	}
}

void RtsoMain::_on_barter_requested(int sell_type, int buy_type, int amount) {
	if (!sim) return;
	if (sell_type < 0 || sell_type >= 4 || buy_type < 0 || buy_type >= 4) return;
	// Lockstep: barter mutates resources + market prices (both hashed) -> cmd_type 23
	// (turn_manager.cpp) applies on every peer at the turn boundary. 0 A.D. Barter.js.
	bool ok = false;
	if (net_lockstep()) {
		post_lockstep(23, nullptr, 0, (float)sell_type, (float)buy_type, (uint32_t)amount, 0);
		ok = true; // validated deterministically on every peer at the boundary
	} else {
		ok = sim->cmd_barter((rtso::ResourceType)sell_type, (rtso::ResourceType)buy_type, amount);
	}
	if (ok) {
		play_sfx("res://assets/sounds/ui/bonus.ogg");
		// Price labels will be refreshed on next update_hud / barter dialog visibility
	}
}

bool RtsoMain::save_game(String path) {
	if (!sim) return false;
	String global_path = ProjectSettings::get_singleton()->globalize_path(path);
	Ref<DirAccess> dir = DirAccess::open("user://");
	if (dir.is_valid()) {
		dir->make_dir_recursive("saves");
	}
	bool res = sim->save_state(global_path.utf8().get_data());
	if (res) {
		print_line("[RTS0] Game saved successfully to " + path);
	}
	return res;
}

bool RtsoMain::load_game(String path) {
	if (!sim) return false;
	String global_path = ProjectSettings::get_singleton()->globalize_path(path);
	bool success = sim->load_state(global_path.utf8().get_data());
	if (success) {
		selected_ids.clear();
		selected_building_id = 0;
		print_line("[RTS0] Game loaded successfully from " + path);
	}
	return success;
}

PackedByteArray RtsoMain::get_fow_grid() const {
	if (!sim) return PackedByteArray();
	const uint8_t* grid = sim->fow_grid();
	int sz = rtso::Simulation::FOW_GRID * rtso::Simulation::FOW_GRID;
	PackedByteArray arr;
	arr.resize(sz);
	for (int i = 0; i < sz; ++i) arr.set(i, grid[i]);
	return arr;
}

void RtsoMain::order_guard_units(uint32_t target_id) {
	if (!sim || selected_ids.is_empty()) return;
	std::vector<uint32_t> ids;
	for (int i = 0; i < selected_ids.size(); ++i) ids.push_back((uint32_t)selected_ids[i]);
	sim->order_guard(ids.data(), ids.size(), target_id);
}

void RtsoMain::spawn_large_battle(int total_units) {
	if (!sim) return;
	clear_selection();
	if (banner_panel) banner_panel->set_visible(false);
	sim->spawn_war(total_units);
	if (cam_rig) {
		cam_rig->set_target_pos(Vector3(0, 0, 0));
	}
	print_line(String("[RTS0] Spawned massive war simulation: ") + String::num_int64(total_units) + " units across 128m map.");
}

void RtsoMain::update_ghost_mesh() {
	if (!ghost_mesh) return;
	String cat = "buildings";
	const char *fid = rtso::civ_building_foundation(current_build_type);
	auto bentry = rtso::ModelLibrary::get(cat, String(fid));
	if (!bentry.valid) {
		uint8_t pciv = sim ? sim->player_civ() : (uint8_t)rtso::CIV_ATHENIANS;
		bentry = rtso::ModelLibrary::get(cat, String(rtso::civ_building_model(pciv, current_build_type)));
	}
	if (bentry.valid) {
		ghost_mesh->set_mesh(bentry.mesh);
		ghost_mesh->set_scale(Vector3(bentry.scale, bentry.scale, bentry.scale));
	} else {
		Ref<BoxMesh> box;
		box.instantiate();
		box->set_size(Vector3(1.8f, 1.8f, 1.8f));
		ghost_mesh->set_mesh(box);
	}
}

void RtsoMain::_process(double delta) {
	if (net_host_.getRole() != rtso::NetRole::None) {
		net_host_.poll(net_arena_);
		net_arena_.reset();
		// Consume NMT_GAME_START seed-sync queued by TurnManager::onMessage.
		apply_net_game_start();
	}
	if (paused_via_menu) return;
	double scaled_delta = delta * game_speed;
	match_elapsed_time += scaled_delta;

	rtso::PerfTracker::begin_frame();
	uint64_t start_us = Time::get_singleton()->get_ticks_usec();

	accumulator += scaled_delta;
	int ticks = 0;
	if (net_lockstep()) {
		if (net_game_started_) {
			// NetClientTurnManager: one 200ms turn batch, sim steps inside TurnManager::tick
			if (turn_mgr_.tick((float)scaled_delta, *sim))
				ticks = rtso::TurnManager::TICKS_PER_TURN;
			if (turn_mgr_.has_out_of_sync() && lobby_status)
				lobby_status->set_text("OOS turn " + String::num_int64(turn_mgr_.oos_turn()));
		}
		// else pre-Start lobby phase: poll + GAME_START consume only (above) —
		// no lockstep turns and no hash exchange across divergent boot worlds.
	} else {
		while (accumulator >= rtso::Simulation::FIXED_STEP) {
			sim->tick(rtso::Simulation::FIXED_STEP);
			accumulator -= rtso::Simulation::FIXED_STEP;
			ticks++;
			if (ticks > 8) break;
		}
	}

	// Shift-queued waypoints: issue the next staged point once the unit reaches
	// the active one — 0 A.D. gui/session/input.js:1345 ("session.queue ... add
	// the order to the unit's order queue"). RTS0's UnitView has one target_x/z,
	// so pending points are staged presentation-side and flushed here.
	if (!waypoint_queue.empty()) {
		auto uview = sim->units();
		for (auto it = waypoint_queue.begin(); it != waypoint_queue.end();) {
			QueuedWaypoint &qw = it->second;
			if (qw.active >= (int)qw.pts.size()) {
				it = waypoint_queue.erase(it);
				continue;
			}
			size_t idx = (size_t)-1;
			for (size_t i = 0; i < uview.count; ++i) {
				if (uview.alive[i] && uview.id[i] == it->first) { idx = i; break; }
			}
			if (idx == (size_t)-1) { // unit died — queue dies with it
				it = waypoint_queue.erase(it);
				continue;
			}
			// Reached the unit's own destination (formation offset aware) or
			// stopped (sim sets ACTION_IDLE on arrival, simulation.cpp cmd_move).
			const float dx = uview.pos_x[idx] - uview.target_x[idx];
			const float dz = uview.pos_z[idx] - uview.target_z[idx];
			const bool reached = dx * dx + dz * dz <= 1.5f * 1.5f ||
				uview.action_state[idx] == (uint8_t)rtso::ACTION_IDLE;
			if (!reached) { ++it; continue; }
			if (qw.active + 1 >= (int)qw.pts.size()) {
				it = waypoint_queue.erase(it); // last waypoint reached
				continue;
			}
			++qw.active;
			const Vector3 &next = qw.pts[qw.active];
			if (net_lockstep())
				post_lockstep(1, &it->first, 1, next.x, next.z);
			else
				sim->cmd_move(&it->first, 1, next.x, next.z);
			++it;
		}
	}

	uint64_t after_sim_us = Time::get_singleton()->get_ticks_usec();
	rtso::PerfTracker::record_tick((after_sim_us - start_us) / 1000);
	rtso::PerfTracker::record_draw((int)sim->unit_count(), (int)sim->building_count());

	view->sync(sim, (float)delta, selected_ids);
	if (cam_rig) {
		minimap_view->sync(sim, cam_rig->get_target_pos());
	}
	uint64_t after_sync_us = Time::get_singleton()->get_ticks_usec();
	rtso::PerfTracker::record_sync((after_sync_us - after_sim_us) / 1000);
	rtso::PerfTracker::sample_memory();
	rtso::PerfTracker::maybe_log();

	String perf_txt = rtso::PerfTracker::get_report() +
		" | Tick US: " + String::num_int64(sim->stats().tick_us) +
		" | Jobs: " + String::num_int64((int64_t)rtso::JobSystem::instance().parallel_width()) +
		" | FPS: " + String::num_int64(Engine::get_singleton()->get_frames_per_second());
	debug_overlay->update_info(perf_txt);

	update_hud();
	if (sim && chat_log) {
		for (const auto &line : sim->pending_petra_chat()) {
			chat_log->append_text("[color=#88ccff]Petra:[/color] " + String(line.text) + "\n");
		}
		if (!sim->pending_petra_chat().empty()) sim->clear_petra_chat();
	}
	update_inspector();
	update_command_panel();

	// Adaptive Dynamic Music Crossfader (Music.js)
	int active_combatants = 0;
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (uview.alive[i] && uview.action_state[i] == rtso::ACTION_ATTACK) {
			active_combatants++;
		}
	}
	int active_projectiles = (int)sim->projectiles().size();
	float target_intensity = (active_combatants >= 3 || active_projectiles >= 2) ? 1.0f : 0.0f;

	if (target_intensity > combat_intensity) {
		combat_intensity = std::min(1.0f, combat_intensity + (float)delta * 0.75f); // Fast cross-fade to battle
	} else {
		combat_intensity = std::max(0.0f, combat_intensity - (float)delta * 0.20f); // Slow return to peace
	}

	if (music_player && battle_music_player) {
		float peace_db = -8.0f - (combat_intensity * 60.0f);
		float battle_db = -80.0f + (combat_intensity * 74.0f); // up to -6.0dB
		music_player->set_volume_db(peace_db);
		battle_music_player->set_volume_db(battle_db);
	}

	// Environmental Ambience: Viewport Elevation & Coastal Water Proximity
	Vector3 cam_world_pos(0, 30, 0);
	if (cam_rig && cam_rig->get_camera()) {
		cam_world_pos = cam_rig->get_camera()->get_global_position();
	} else if (cam_rig) {
		cam_world_pos = cam_rig->get_target_pos() + Vector3(0, 30, 0);
	}

	// 1. Wind & Nature elevation crossfade
	float elev_factor = Math::clamp((cam_world_pos.y - 12.0f) / 38.0f, 0.0f, 1.0f);
	float target_wind_db = Math::lerp(-32.0f, -8.0f, elev_factor);
	float target_nature_db = Math::lerp(-12.0f, -28.0f, elev_factor);

	if (ambient_wind_player) {
		if (!ambient_wind_player->is_playing()) ambient_wind_player->play();
		float cur_db = ambient_wind_player->get_volume_db();
		ambient_wind_player->set_volume_db(Math::lerp(cur_db, target_wind_db, (float)delta * 3.5f));
	}
	if (ambient_player) {
		if (!ambient_player->is_playing()) ambient_player->play();
		float cur_db = ambient_player->get_volume_db();
		ambient_player->set_volume_db(Math::lerp(cur_db, target_nature_db, (float)delta * 3.5f));
	}

	// 2. Coastal water proximity modulation
	float target_water_db = -80.0f;
	if (sim && sim->water_height() > 0.0f) {
		float water_h = sim->water_height();
		Vector3 target_pos = cam_rig ? cam_rig->get_target_pos() : cam_world_pos;
		float terrain_h = sim->get_terrain_height(target_pos.x, target_pos.z);
		float d_water = terrain_h - water_h;
		if (d_water <= 3.5f) {
			float coast_factor = Math::clamp(1.0f - (d_water / 3.5f), 0.0f, 1.0f);
			target_water_db = Math::lerp(-40.0f, -10.0f, coast_factor);
		}
	}
	if (ambient_water_player) {
		if (!ambient_water_player->is_playing()) ambient_water_player->play();
		float cur_db = ambient_water_player->get_volume_db();
		ambient_water_player->set_volume_db(Math::lerp(cur_db, target_water_db, (float)delta * 3.0f));
	}

	// Update Building Placement Ghost (0 A.D. gui/session/building_placement.js:45-92 BuildPreview parity)
	if (build_mode) {
		Vector2 mouse_pos = get_viewport()->get_mouse_position();
		if (wall_drag_mode) {
			if (ghost_mesh && ghost_mesh->is_visible()) ghost_mesh->set_visible(false);
			Vector3 drag_curr_pos = get_ground_click_pos(mouse_pos);
			float dist = wall_drag_start_pos.distance_to(drag_curr_pos);
			int seg_count = std::max(1, (int)(dist / 6.0f));
			Vector3 step_vec = (drag_curr_pos - wall_drag_start_pos) / (float)seg_count;
			float wall_rot_y = std::atan2(step_vec.x, step_vec.z);

			size_t total_ghosts = (size_t)seg_count + 2;
			while (wall_ghosts.size() < total_ghosts) {
				MeshInstance3D *g = memnew(MeshInstance3D);
				g->set_visible(false);
				add_child(g);
				wall_ghosts.push_back(g);
			}

			uint8_t pciv = sim ? sim->player_civ() : (uint8_t)rtso::CIV_ATHENIANS;
			auto tower_entry = rtso::ModelLibrary::get("buildings", String(rtso::civ_building_foundation(rtso::BLD_TOWER)));
			if (!tower_entry.valid) {
				tower_entry = rtso::ModelLibrary::get("buildings", String(rtso::civ_building_model(pciv, rtso::BLD_TOWER)));
			}
			auto wall_entry = rtso::ModelLibrary::get("buildings", String(rtso::civ_building_foundation(rtso::BLD_WALL_SEGMENT)));
			if (!wall_entry.valid) {
				wall_entry = rtso::ModelLibrary::get("buildings", String(rtso::civ_building_model(pciv, rtso::BLD_WALL_SEGMENT)));
			}

			auto configure_ghost = [&](size_t idx, Vector3 pos, bool is_tower, float yaw) {
				MeshInstance3D *g = wall_ghosts[idx];
				auto &entry = is_tower ? tower_entry : wall_entry;
				if (entry.valid) {
					g->set_mesh(entry.mesh);
					g->set_scale(Vector3(entry.scale, entry.scale, entry.scale));
				} else {
					Ref<BoxMesh> box;
					box.instantiate();
					box->set_size(is_tower ? Vector3(2.2f, 4.5f, 2.2f) : Vector3(1.2f, 2.5f, 5.5f));
					g->set_mesh(box);
				}
				g->set_global_position(Vector3(pos.x, 0.05f, pos.z));
				g->set_rotation(Vector3(0.0f, yaw, 0.0f));

				int cx = (int)std::floor(pos.x / rtso::Simulation::GRID);
				int cy = (int)std::floor(pos.z / rtso::Simulation::GRID);
				int footprint = is_tower ? 2 : 1;
				bool can_place = sim ? sim->can_place(cx, cy, footprint) : true;
				g->set_material_override(can_place ? ghost_mat_ok : ghost_mat_bad);
				g->set_visible(true);
			};

			// Start node tower
			configure_ghost(0, wall_drag_start_pos, true, 0.0f);

			// Intermediate wall segments
			for (int s = 0; s < seg_count; ++s) {
				Vector3 mid_p = wall_drag_start_pos + step_vec * ((float)s + 0.5f);
				configure_ghost(1 + s, mid_p, false, wall_rot_y);
			}

			// End node tower
			Vector3 end_p = wall_drag_start_pos + step_vec * (float)seg_count;
			configure_ghost(seg_count + 1, end_p, true, 0.0f);

			for (size_t i = total_ghosts; i < wall_ghosts.size(); ++i) {
				if (wall_ghosts[i] && wall_ghosts[i]->is_visible()) {
					wall_ghosts[i]->set_visible(false);
				}
			}
		} else {
			for (auto *wg : wall_ghosts) {
				if (wg && wg->is_visible()) wg->set_visible(false);
			}
			if (ghost_mesh) {
				ghost_mesh->set_visible(true);
				auto [cell_x, cell_y] = mouse_cell(mouse_pos);
				int footprint = rtso::building_type_info(current_build_type).footprint;

				float gx = (cell_x + footprint * 0.5f) * rtso::Simulation::GRID;
				float gz = (cell_y + footprint * 0.5f) * rtso::Simulation::GRID;
				ghost_mesh->set_global_position(Vector3(gx, 0.05f, gz));

				bool can_build = sim->can_place(cell_x, cell_y, footprint);
				ghost_mesh->set_material_override(can_build ? ghost_mat_ok : ghost_mat_bad);
			}
		}
	} else {
		if (ghost_mesh && ghost_mesh->is_visible()) ghost_mesh->set_visible(false);
		for (auto *wg : wall_ghosts) {
			if (wg && wg->is_visible()) wg->set_visible(false);
		}
	}

	// Update 3D Rally Point Marker
	if (rally_flag) {
		bool show_rally = false;
		if (selected_building_id != 0) {
			for (const auto &b : sim->buildings()) {
				if (b.id == selected_building_id && b.team == 0 && b.has_rally_point) {
					rally_flag->set_global_position(Vector3(b.rally_x, 0.8f, b.rally_z));
					show_rally = true;
					break;
				}
			}
		}
		rally_flag->set_visible(show_rally);
	}

	static bool summary_shown = false;
	if ((sim->is_victory() || sim->is_game_over()) && !summary_shown) {
		summary_shown = true;
		Node *summary = get_node_or_null(NodePath("SummaryScreen"));
		if (summary) {
			Dictionary rec;
			const auto &r = sim->record();
			rec["units_trained"] = r.units_trained;
			rec["units_lost"] = r.units_lost;
			rec["enemies_killed"] = r.enemies_killed;
			rec["buildings_constructed"] = r.buildings_constructed;
			rec["buildings_destroyed"] = r.buildings_destroyed;
			rec["total_food"] = r.total_food_gathered;
			rec["total_wood"] = r.total_wood_gathered;
			rec["total_gold"] = r.total_gold_gathered;
			rec["total_stone"] = r.total_stone_gathered;
			summary->call("show_summary", sim->is_victory(), rec, (float)match_elapsed_time);
		}
	}

	// Check Victory / Defeat Condition
	if (sim->is_victory()) {
		banner_panel->set_visible(true);
		banner_label->set_text("VICTORY!\nYou successfully defended Athens against all enemy invasions!");
		banner_label->add_theme_color_override("font_color", Color(0.3f, 1.0f, 0.5f));
	} else if (sim->is_game_over()) {
		banner_panel->set_visible(true);
		banner_label->set_text("DEFEAT!\nYour Athenian Town Center has fallen!");
		banner_label->add_theme_color_override("font_color", Color(1.0f, 0.3f, 0.3f));
	}
}

Vector3 RtsoMain::get_ground_click_pos(Vector2 mouse_pos) const {
	if (!cam_rig || !cam_rig->get_camera()) return Vector3();
	Camera3D *cam = cam_rig->get_camera();
	Vector3 ray_origin = cam->project_ray_origin(mouse_pos);
	Vector3 ray_dir = cam->project_ray_normal(mouse_pos);
	if (std::abs(ray_dir.y) < 0.0001f) return Vector3();
	float t = -ray_origin.y / ray_dir.y;
	return ray_origin + ray_dir * t;
}

std::pair<int, int> RtsoMain::mouse_cell(Vector2 mouse_pos) const {
	Vector3 pos = get_ground_click_pos(mouse_pos);
	int cx = (int)std::floor(pos.x / rtso::Simulation::GRID);
	int cy = (int)std::floor(pos.z / rtso::Simulation::GRID);
	return {cx, cy};
}

void RtsoMain::update_hud() {
	if (!res_labels[0]) return;

	// Dynamic Civ Crest & Phase Badge Emblems
	const auto &cinfo = rtso::civ_info(sim->player_civ());
	if (civ_emblem) {
		civ_emblem->set_texture(load_tex(cinfo.emblem_path));
	}
	if (phase_emblem) {
		uint8_t pphase = sim->player_phase();
		if (pphase == 1) phase_emblem->set_texture(load_tex("res://assets/textures/ui/phases/village.png"));
		else if (pphase == 2) phase_emblem->set_texture(load_tex("res://assets/textures/ui/phases/town.png"));
		else phase_emblem->set_texture(load_tex("res://assets/textures/ui/phases/city.png"));
	}

	// Resource counts & rates
	res_labels[0]->set_text(String::num_int64(sim->food()) + " (+" + String::num(sim->food_rate(), 0) + "/m)");
	res_labels[1]->set_text(String::num_int64(sim->wood()) + " (+" + String::num(sim->wood_rate(), 0) + "/m)");
	res_labels[2]->set_text(String::num_int64(sim->gold()) + " (+" + String::num(sim->gold_rate(), 0) + "/m)");
	res_labels[3]->set_text(String::num_int64(sim->stone()) + " (+" + String::num(sim->stone_rate(), 0) + "/m)");

	// Pop & Idle
	int cur_pop = sim->pop_current();
	int max_pop = sim->pop_max();
	pop_label->set_text(String::num_int64(cur_pop) + "/" + String::num_int64(max_pop));
	if (cur_pop >= max_pop) pop_label->add_theme_color_override("font_color", Color(1.0f, 0.3f, 0.3f));
	else pop_label->add_theme_color_override("font_color", Color(0.95f, 0.95f, 0.98f));

	auto idle_workers = sim->get_idle_workers(0);
	idle_worker_btn->set_text("Idle: " + String::num_int64(idle_workers.size()));
	if (!idle_workers.empty()) idle_worker_btn->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.2f));
	else idle_worker_btn->add_theme_color_override("font_color", Color(0.85f, 0.85f, 0.88f));

	// Clock & Wave
	int mins = (int)match_elapsed_time / 60;
	int secs = (int)match_elapsed_time % 60;
	String min_str = (mins < 10 ? "0" : "") + String::num_int64(mins);
	String sec_str = (secs < 10 ? "0" : "") + String::num_int64(secs);
	clock_label->set_text(min_str + ":" + sec_str);

	String phase_name = (sim->player_phase() == 1) ? "Village Phase" : ((sim->player_phase() == 2) ? "Town Phase" : "City Phase");
	if (sim->is_wonder_active()) {
		int wmins = (int)sim->wonder_time_remaining() / 60;
		int wsecs = (int)sim->wonder_time_remaining() % 60;
		String wmin_str = (wmins < 10 ? "0" : "") + String::num_int64(wmins);
		String wsec_str = (wsecs < 10 ? "0" : "") + String::num_int64(wsecs);
		wave_label->set_text("Wonder Victory in " + wmin_str + ":" + wsec_str + " | " + phase_name);
	} else {
		wave_label->set_text("Wave " + String::num_int64(sim->wave_number()) +
			" (" + String::num((double)sim->wave_time_remaining(), 0) + "s) | " + phase_name);
	}
}

void RtsoMain::update_inspector() {
	if (!inspector_panel) return;

	if (selected_building_id != 0) {
		const auto &bldgs = sim->buildings();
		const rtso::Simulation::Building *found_b = nullptr;
		for (const auto &b : bldgs) {
			if (b.id == selected_building_id) { found_b = &b; break; }
		}

		if (found_b) {
			const auto &binfo = rtso::building_type_info(found_b->type);
			inspector_portrait->set_texture(load_tex("res://assets/textures/ui/portraits/" + String(binfo.portrait_id) + ".png"));
			inspector_title->set_text(String(binfo.name));
			String civ_name = rtso::civ_info(sim->player_civ()).name;
			inspector_class->set_text(civ_name + " Architecture • Garrison Cap: " + String::num_int64(found_b->garrison_cap));

			// 1. Health Bar
			inspector_hp_bar->set_max(found_b->max_hp);
			inspector_hp_bar->set_value(found_b->hp);
			inspector_hp_label->set_text("HP: " + String::num((double)found_b->hp, 0) + " / " + String::num((double)found_b->max_hp, 0));

			// 2. Capture Points Bar
			float max_cp = (found_b->max_capture_points > 0.0f) ? found_b->max_capture_points : 1000.0f;
			float cur_cp = (found_b->max_capture_points > 0.0f) ? found_b->capture_points : (found_b->hp / found_b->max_hp * max_cp);
			inspector_capture_bar->set_max(max_cp);
			inspector_capture_bar->set_value(cur_cp);
			inspector_capture_label->set_text("Capture: " + String::num((double)cur_cp, 0) + " / " + String::num((double)max_cp, 0));

			// 3. Veterancy / Tech Research XP Bar
			if (found_b->research_tech != rtso::TECH_NONE && found_b->research_duration > 0.0f) {
				inspector_xp_bar->set_max(found_b->research_duration);
				inspector_xp_bar->set_value(found_b->research_timer);
				inspector_xp_label->set_text("Research: " + String(rtso::tech_info(found_b->research_tech).name) + " (" + String::num(found_b->research_timer, 1) + "s / " + String::num(found_b->research_duration, 1) + "s)");
			} else {
				inspector_xp_bar->set_max(100.0f);
				inspector_xp_bar->set_value(100.0f);
				inspector_xp_label->set_text("Technology Research: Ready");
			}

			// 4. Stamina / Structure Gauge
			float struct_prog = std::clamp(found_b->build_progress * 100.0f, 0.0f, 100.0f);
			inspector_stamina_bar->set_max(100.0f);
			inspector_stamina_bar->set_value(struct_prog);
			inspector_stamina_label->set_text(struct_prog >= 100.0f ? "Structure: 100% Integrity" : ("Construction: " + String::num(struct_prog, 0) + "%"));

			// 5. Aura Influence Gauge
			float aura_r = (found_b->type == rtso::BLD_TOWN_CENTER) ? 32.0f : ((found_b->type == rtso::BLD_FORTRESS) ? 28.0f : ((found_b->type == rtso::BLD_TOWER) ? 18.0f : ((found_b->type == rtso::BLD_TEMPLE) ? 18.0f : 0.0f)));
			if (aura_r > 0.0f) {
				inspector_aura_bar->set_max(32.0f);
				inspector_aura_bar->set_value(aura_r);
				inspector_aura_label->set_text("Aura: " + String::num(aura_r, 0) + "m Territory Influence (Active)");
			} else if (found_b->garrison_cap > 0) {
				inspector_aura_bar->set_max((float)found_b->garrison_cap);
				inspector_aura_bar->set_value((float)found_b->garrisoned_units.size());
				inspector_aura_label->set_text("Garrison: " + String::num_int64(found_b->garrisoned_units.size()) + " / " + String::num_int64(found_b->garrison_cap));
			} else {
				inspector_aura_bar->set_max(100.0f);
				inspector_aura_bar->set_value(100.0f);
				inspector_aura_label->set_text("Aura: Standard Influence");
			}

			String status = "State: Operational";
			if (found_b->build_progress < 1.0f) {
				status = "Under Construction (" + String::num((double)(found_b->build_progress * 100.0f), 0) + "%)";
			} else if (found_b->research_tech != rtso::TECH_NONE) {
				status = "Researching " + String(rtso::tech_info(found_b->research_tech).name) +
					" (" + String::num((double)(found_b->research_duration - found_b->research_timer), 1) + "s)";
			} else if (!found_b->queue.empty()) {
				status = "Training Active (" + String::num_int64(found_b->queue.size()) + "/8 queued) — Right-click slot to cancel";
			}
			inspector_stats->set_text(status);

			String extra = "👥 Garrison: " + String::num_int64(found_b->garrisoned_units.size()) + " / " + String::num_int64(found_b->garrison_cap);
			if (found_b->has_rally_point) extra += " | 🚩 Rally Point Set";
			inspector_extra->set_text(extra);

			ungarrison_btn->set_visible(!found_b->garrisoned_units.empty());
			inspector_panel->set_visible(true);
			return;
		}
	}

	if (!selected_ids.is_empty()) {
		auto uview = sim->units();
		int v_cnt = 0, f_cnt = 0, s_cnt = 0, a_cnt = 0, sp_cnt = 0, c_cnt = 0;
		int h_cnt = 0, ch_cnt = 0, ele_cnt = 0, siege_cnt = 0, naval_cnt = 0;
		float hp_sum = 0, hp_max_sum = 0;
		uint8_t single_utype = rtso::UNIT_VILLAGER;

		for (int i = 0; i < selected_ids.size(); ++i) {
			uint32_t uid = (uint32_t)selected_ids[i];
			for (size_t u = 0; u < uview.count; ++u) {
				if (uview.id[u] == uid && uview.alive[u]) {
					hp_sum += uview.hp[u];
					hp_max_sum += uview.max_hp[u];
					uint8_t ut = uview.unit_type[u];
					single_utype = ut;
					if (ut == rtso::UNIT_VILLAGER) v_cnt++;
					else if (ut == rtso::UNIT_FEMALE_CITIZEN) f_cnt++;
					else if (ut == rtso::UNIT_SWORDSMAN) s_cnt++;
					else if (ut == rtso::UNIT_ARCHER || ut == rtso::UNIT_ARCHER_MAIDEN) a_cnt++;
					else if (ut == rtso::UNIT_SPEARMAN || ut == rtso::UNIT_CARNYX) sp_cnt++;
					else if (ut == rtso::UNIT_CAVALRY || ut == rtso::UNIT_CAMEL || ut == rtso::UNIT_HERO) c_cnt++;
					else if (ut == rtso::UNIT_HEALER) h_cnt++;
					else if (ut == rtso::UNIT_CHARIOT || ut == rtso::UNIT_SCYTHED_CHARIOT) ch_cnt++;
					else if (ut == rtso::UNIT_ELEPHANT || ut == rtso::UNIT_WORKER_ELEPHANT) ele_cnt++;
					else if (ut == rtso::UNIT_CATAPULT || ut == rtso::UNIT_SIEGE_RAM || ut == rtso::UNIT_BOLT_SHOOTER) siege_cnt++;
					else if (ut == rtso::UNIT_TRIREME || ut == rtso::UNIT_FISHING_BOAT || ut == rtso::UNIT_MERCHANT_SHIP) naval_cnt++;
					else v_cnt++;
					break;
				}
			}
		}

		if (selected_ids.size() == 1) {
			const auto &uinfo = rtso::unit_type_info(single_utype);
			inspector_portrait->set_texture(load_tex("res://assets/textures/ui/portraits/" + String(uinfo.portrait_id) + ".png"));
			
			uint32_t uid = (uint32_t)selected_ids[0];
			int u_kills = 0;
			int u_rank = 1;
			for (size_t u = 0; u < uview.count; ++u) {
				if (uview.id[u] == uid) {
					u_kills = (int)uview.kills[u];
					u_rank = (int)uview.rank[u];
					break;
				}
			}

			String rank_badge = (u_rank == 3) ? " [Elite]" : ((u_rank == 2) ? " [Advanced]" : " [Citizen]");
			inspector_title->set_text(String(uinfo.name) + rank_badge);

			const char *role_name = "Infantry";
			if (single_utype == rtso::UNIT_VILLAGER || single_utype == rtso::UNIT_FEMALE_CITIZEN) role_name = "Economic Citizen";
			else if (single_utype == rtso::UNIT_ARCHER || single_utype == rtso::UNIT_ARCHER_MAIDEN) role_name = "Ranged Skirmisher";
			else if (single_utype == rtso::UNIT_CAVALRY || single_utype == rtso::UNIT_CAMEL) role_name = "Shock Cavalry";
			else if (single_utype == rtso::UNIT_CHARIOT || single_utype == rtso::UNIT_SCYTHED_CHARIOT) role_name = "War Chariot";
			else if (single_utype == rtso::UNIT_ELEPHANT || single_utype == rtso::UNIT_WORKER_ELEPHANT) role_name = "War Elephant";
			else if (single_utype == rtso::UNIT_HEALER) role_name = "Temple Healer";
			else if (single_utype == rtso::UNIT_CATAPULT || single_utype == rtso::UNIT_BOLT_SHOOTER || single_utype == rtso::UNIT_SIEGE_RAM) role_name = "Siege Artillery";
			else if (single_utype == rtso::UNIT_TRIREME || single_utype == rtso::UNIT_FISHING_BOAT || single_utype == rtso::UNIT_MERCHANT_SHIP) role_name = "Naval Vessel";

			inspector_class->set_text(String("Role: ") + role_name + " • Kills: " + String::num_int64(u_kills) + " • Speed: " + String::num(uinfo.speed, 1) + " m/s");

			// 1. Health Bar
			inspector_hp_bar->set_max(hp_max_sum);
			inspector_hp_bar->set_value(hp_sum);
			inspector_hp_label->set_text("HP: " + String::num((double)hp_sum, 0) + " / " + String::num((double)hp_max_sum, 0));

			// 2. Capture Resistance Bar
			inspector_capture_bar->set_max(100.0f);
			inspector_capture_bar->set_value(100.0f);
			inspector_capture_label->set_text("Capture Resistance: 100 / 100");

			// 3. Veterancy / Experience XP Bar
			float req_xp = (float)(u_rank * 100);
			float cur_xp = (float)(u_kills * 15 + (u_rank - 1) * 50);
			if (cur_xp > req_xp && u_rank >= 3) cur_xp = req_xp;
			inspector_xp_bar->set_max(req_xp);
			inspector_xp_bar->set_value(cur_xp);
			inspector_xp_label->set_text("Veterancy XP: " + String::num(cur_xp, 0) + " / " + String::num(req_xp, 0) + " (" + String::num_int64(u_kills) + " Kills)");

			// 4. Stamina / Energy Gauge
			inspector_stamina_bar->set_max(100.0f);
			inspector_stamina_bar->set_value(100.0f);
			inspector_stamina_label->set_text("Stamina: 100 / 100 (Energy)");

			// 5. Aura Influence Gauge
			bool has_hero_aura = (single_utype == rtso::UNIT_HERO || single_utype == rtso::UNIT_HEALER || single_utype == rtso::UNIT_CARNYX || single_utype == rtso::UNIT_FEMALE_CITIZEN);
			inspector_aura_bar->set_max(100.0f);
			inspector_aura_bar->set_value(has_hero_aura ? 100.0f : 50.0f);
			if (single_utype == rtso::UNIT_HERO) inspector_aura_label->set_text("Aura: 16m Heroic Morale Aura (Active)");
			else if (single_utype == rtso::UNIT_HEALER) inspector_aura_label->set_text("Aura: 12m Divine Healing Aura (Active)");
			else if (single_utype == rtso::UNIT_CARNYX) inspector_aura_label->set_text("Aura: 14m Carnyx Battle Cry Aura (Active)");
			else if (single_utype == rtso::UNIT_FEMALE_CITIZEN) inspector_aura_label->set_text("Aura: 10m Civic Inspiration Aura (+15% Gather)");
			else inspector_aura_label->set_text("Aura Influence: Active Standard");

			String stats_line = "⚔️ Attack: " + String::num(uinfo.attack_damage, 0);
			if (uinfo.attack_range > 2.0f) stats_line += " (" + String::num(uinfo.attack_range, 1) + "m)";
			stats_line += " | 🛡️ Armor: " + String::num(uinfo.hack_armor, 0) + "H / " + String::num(uinfo.pierce_armor, 0) + "P / " + String::num(uinfo.crush_armor, 0) + "C";
			if (uinfo.carry_capacity > 0) stats_line += " | 🎒 Cap: " + String::num_int64(uinfo.carry_capacity);
			inspector_stats->set_text(stats_line);

			if (single_utype == rtso::UNIT_TRIREME) {
				size_t p_count = sim->get_transport_passenger_count(uid);
				if (p_count > 0) {
					inspector_extra->set_text("🚢 Transport: " + String::num_int64(p_count) + " / " + String::num_int64(rtso::Simulation::TRANSPORT_CAPACITY) + " Passengers | [Z] Disembark All");
					ungarrison_btn->set_text("Disembark (Z)");
					ungarrison_btn->set_visible(true);
				} else {
					inspector_extra->set_text("🚢 Arrow warship (ranged pierce, 3× vs ships) • Capacity: " + String::num_int64(rtso::Simulation::TRANSPORT_CAPACITY) + " troops");
					ungarrison_btn->set_text("Ungarrison (Z)");
					ungarrison_btn->set_visible(false);
				}
			} else {
				inspector_extra->set_text("Orders: [RMB] Move/Attack/Gather | Stances: [F1-F5] | Formations: [Z,X,C,V]");
				ungarrison_btn->set_text("Ungarrison (Z)");
				ungarrison_btn->set_visible(false);
			}
		} else {
			inspector_portrait->set_texture(load_tex("res://assets/textures/ui/portraits/swordsman.png"));
			inspector_title->set_text("Selected Company (" + String::num_int64(selected_ids.size()) + " Units)");
			String civ_name = rtso::civ_info(sim->player_civ()).name;
			inspector_class->set_text(civ_name + " Taskforce • Combined Arms");

			// 1. Health Bar
			inspector_hp_bar->set_max(hp_max_sum);
			inspector_hp_bar->set_value(hp_sum);
			inspector_hp_label->set_text("HP: " + String::num((double)hp_sum, 0) + " / " + String::num((double)hp_max_sum, 0));

			// 2. Capture Resistance Bar
			inspector_capture_bar->set_max(100.0f);
			inspector_capture_bar->set_value(100.0f);
			inspector_capture_label->set_text("Capture Resistance: 100%");

			// 3. Veterancy / Company XP Bar
			inspector_xp_bar->set_max((float)(selected_ids.size() * 100));
			inspector_xp_bar->set_value((float)(selected_ids.size() * 50));
			inspector_xp_label->set_text("Company Experience: " + String::num_int64(selected_ids.size()) + " Battlegroup");

			// 4. Stamina Gauge
			inspector_stamina_bar->set_max(100.0f);
			inspector_stamina_bar->set_value(100.0f);
			inspector_stamina_label->set_text("Company Stamina: 100 / 100");

			// 5. Aura Influence Gauge
			inspector_aura_bar->set_max(100.0f);
			inspector_aura_bar->set_value(100.0f);
			inspector_aura_label->set_text("Aura: Combined Arms Influence");

			String comp_str = "Comp: ";
			if (v_cnt > 0) comp_str += String::num_int64(v_cnt) + " Citizens, ";
			if (f_cnt > 0) comp_str += String::num_int64(f_cnt) + " Females, ";
			if (s_cnt > 0) comp_str += String::num_int64(s_cnt) + " Swords, ";
			if (a_cnt > 0) comp_str += String::num_int64(a_cnt) + " Archers, ";
			if (sp_cnt > 0) comp_str += String::num_int64(sp_cnt) + " Spears, ";
			if (c_cnt > 0) comp_str += String::num_int64(c_cnt) + " Cav, ";
			if (ch_cnt > 0) comp_str += String::num_int64(ch_cnt) + " Chariots, ";
			if (ele_cnt > 0) comp_str += String::num_int64(ele_cnt) + " Elephants, ";
			if (h_cnt > 0) comp_str += String::num_int64(h_cnt) + " Healers, ";
			if (siege_cnt > 0) comp_str += String::num_int64(siege_cnt) + " Siege, ";
			if (naval_cnt > 0) comp_str += String::num_int64(naval_cnt) + " Ships, ";
			if (comp_str.ends_with(", ")) comp_str = comp_str.substr(0, comp_str.length() - 2);
			inspector_stats->set_text(comp_str);

			inspector_extra->set_text("Stances: [F1-F5] | Formations: [Z,X,C,V]");
			ungarrison_btn->set_visible(false);
		}

		inspector_panel->set_visible(true);
	} else {
		inspector_panel->set_visible(false);
	}
}

void RtsoMain::update_command_panel() {
	if (!command_panel) return;

	// Clear existing dynamic buttons in actions_bar and production_bar
	TypedArray<Node> act_children = actions_bar->get_children();
	for (int i = 0; i < act_children.size(); ++i) {
		Node *n = Object::cast_to<Node>(act_children[i]);
		if (n) { actions_bar->remove_child(n); memdelete(n); }
	}
	TypedArray<Node> prod_children = production_bar->get_children();
	for (int i = 0; i < prod_children.size(); ++i) {
		Node *n = Object::cast_to<Node>(prod_children[i]);
		if (n) { production_bar->remove_child(n); memdelete(n); }
	}

	if (selected_building_id != 0) {
		const auto &bldgs = sim->buildings();
		const rtso::Simulation::Building *found_b = nullptr;
		for (const auto &b : bldgs) {
			if (b.id == selected_building_id) { found_b = &b; break; }
		}

		// Update 8-slot Visual Production Queue (ProductionQueue.js parity)
		if (found_b) {
			for (int q = 0; q < 8; ++q) {
				if (q < (int)found_b->queue.size()) {
					uint8_t utype = found_b->queue[q].unit_type;
					const auto &uinfo = rtso::unit_type_info(utype);
					queue_slots[q].icon->set_texture(load_tex("res://assets/textures/ui/portraits/" + String(uinfo.portrait_id) + ".png"));
					queue_slots[q].icon->set_visible(true);
					queue_slots[q].chip->set_text(String::num_int64(q + 1));
					queue_slots[q].chip->set_visible(true);

					if (q == 0) {
						queue_slots[q].progress->set_visible(true);
						queue_slots[q].progress->set_max(found_b->queue[0].duration);
						queue_slots[q].progress->set_value(found_b->queue[0].timer);
					} else {
						queue_slots[q].progress->set_visible(false);
					}
				} else {
					queue_slots[q].icon->set_visible(false);
					queue_slots[q].progress->set_visible(false);
					queue_slots[q].chip->set_visible(false);
				}
			}
		}

		if (found_b && found_b->build_progress >= 1.0f) {
			uint8_t pciv = sim->player_civ();
			uint8_t pphase = sim->player_phase();
			uint32_t roster = rtso::civ_train_mask(pciv);
			auto add_train = [&](rtso::UnitType t, const char *label) {
				if ((roster & rtso::unit_bit((uint8_t)t)) == 0) return;
				Button *btn = memnew(Button);
				btn->set_text(label);
				btn->connect("pressed", Callable(this, "_on_train_pressed").bind((int)t));
				production_bar->add_child(btn);
			};

			if (found_b->type == rtso::BLD_TOWN_CENTER) {
				add_train(rtso::UNIT_VILLAGER, "Villager (50f)");
				add_train(rtso::UNIT_FEMALE_CITIZEN, "Female Citizen (40f)");
				add_train(rtso::UNIT_HEALER, "Healer (80f,60g)");

				if (pphase == 1) {
					Button *btn_phase = memnew(Button);
					btn_phase->set_text("Town Phase (200f,200w,100g)");
					// Tech icon parity: 0 A.D. phase buttons use portraits/technologies
					// town_phase icon (extracted -> textures/ui/phases/town.png). Cost text kept.
					btn_phase->set_button_icon(load_tex("res://assets/textures/ui/phases/town.png"));
					btn_phase->set_icon_alignment(HORIZONTAL_ALIGNMENT_CENTER);
					btn_phase->add_theme_constant_override("icon_max_width", 20);
					btn_phase->connect("pressed", Callable(this, "_on_tech_pressed").bind((int)rtso::TECH_PHASE_TOWN));
					production_bar->add_child(btn_phase);
				} else if (pphase == 2) {
					Button *btn_phase = memnew(Button);
					btn_phase->set_text("City Phase (400f,400w,300g,200s)");
					btn_phase->set_button_icon(load_tex("res://assets/textures/ui/phases/city.png"));
					btn_phase->set_icon_alignment(HORIZONTAL_ALIGNMENT_CENTER);
					btn_phase->add_theme_constant_override("icon_max_width", 20);
					btn_phase->connect("pressed", Callable(this, "_on_tech_pressed").bind((int)rtso::TECH_PHASE_CITY));
					production_bar->add_child(btn_phase);
				}
			} else if (found_b->type == rtso::BLD_BARRACKS || found_b->type == rtso::BLD_FORTRESS) {
				add_train(rtso::UNIT_SWORDSMAN, "Swordsman (60g,30w)");
				add_train(rtso::UNIT_ARCHER, "Archer (40g,50w)");
				add_train(rtso::UNIT_SPEARMAN, "Spearman (30f,40w,20g)");
				add_train(rtso::UNIT_CAVALRY, "Cavalry (80g,50f)");
				add_train(rtso::UNIT_CAMEL, "Camel Rider (60f,70g)");
				add_train(rtso::UNIT_CHARIOT, "Celtic Chariot (80f,100w,60g)");
				add_train(rtso::UNIT_WAR_DOG, "War Dog (60f)");
				add_train(rtso::UNIT_CARNYX, "Carnyx Herald (70f,40w,30g)");
				add_train(rtso::UNIT_ARCHER_MAIDEN, "Archer Maiden (40f,60w,40g)");
				add_train(rtso::UNIT_WORKER_ELEPHANT, "Worker Elephant (180f,80w)");
				if (pciv == rtso::CIV_MAURYANS && pphase >= 2)
					add_train(rtso::UNIT_ELEPHANT, "Elephant Archer (175f,75w)");

				if (pphase >= 2) {
					add_train(rtso::UNIT_HERO, "Champion Hero (200f,200g)");
					add_train(rtso::UNIT_BOLT_SHOOTER, "Bolt Shooter (120w,80g)");
				}
				if (pphase >= 3) {
					add_train(rtso::UNIT_CATAPULT, "Catapult Artillery (150w,100g,50s)");
					add_train(rtso::UNIT_SIEGE_RAM, "Siege Ram (200w,100g)");
					if (pciv != rtso::CIV_MAURYANS)
						add_train(rtso::UNIT_ELEPHANT, "War Elephant (250f,200g)");
					add_train(rtso::UNIT_SCYTHED_CHARIOT, "Scythed Chariot (100f,80w,120g)");
				}
			} else if (found_b->type == rtso::BLD_DOCK) {
				add_train(rtso::UNIT_FISHING_BOAT, "Fishing Boat (75w)");
				add_train(rtso::UNIT_MERCHANT_SHIP, "Merchant Ship (120w,40g)");
				add_train(rtso::UNIT_TRIREME, "War Trireme (200w,150g)");
			} else if (found_b->type == rtso::BLD_MARKET) {
				add_train(rtso::UNIT_TRADER, "Merchant Trader (60f,60w)");
			} else if (found_b->type == rtso::BLD_TEMPLE) {
				add_train(rtso::UNIT_HEALER, "Temple Healer (80f,60g)");
			} else if (found_b->type == rtso::BLD_FARMSTEAD || found_b->type == rtso::BLD_CORRAL) {
				add_train(rtso::UNIT_WAR_DOG, "Guard Dog (60f)");
			}
		}
	} else {
		// Non-building selected: clear visual queue slots
		for (int q = 0; q < 8; ++q) {
			queue_slots[q].icon->set_visible(false);
			queue_slots[q].progress->set_visible(false);
			queue_slots[q].chip->set_visible(false);
		}

		if (!selected_ids.is_empty()) {
			auto uview = sim->units();

			// 0 A.D. Parity: gui/session/selection_panels.js:265-330 (g_SelectionPanels.Formation)
			// and gui/session/selection_panels_left/formation_panel.xml:1-12 (unitFormationButton repeat=15).
			// Reference: 0 A.D. template_player.xml:84-96 (12 actionable formations, skipping FORMATION_NONE).
			// Active formation highlighted with gold font color, matching 0 A.D. guiSelection overlay.
			static const struct FormationButtonDef {
				const char *name;
				uint8_t id;
				const char *icon;
			} kFormationButtons[12] = {
				{"Line",       rtso::FORMATION_LINE,         "res://assets/textures/ui/formations/line.png"},
				{"Box",        rtso::FORMATION_BOX,          "res://assets/textures/ui/formations/box.png"},
				{"Col",        rtso::FORMATION_COLUMN,       "res://assets/textures/ui/formations/column.png"},
				{"Phalanx",    rtso::FORMATION_PHALANX,      "res://assets/textures/ui/formations/phalanx.png"},
				{"Wedge",      rtso::FORMATION_WEDGE,        "res://assets/textures/ui/formations/wedge.png"},
				{"Skirmish",   rtso::FORMATION_SCATTER,      "res://assets/textures/ui/formations/scatter.png"},
				{"LineOpen",   rtso::FORMATION_LINE_OPEN,    "res://assets/textures/ui/formations/line_open.png"},
				{"ColOpen",    rtso::FORMATION_COLUMN_OPEN,  "res://assets/textures/ui/formations/column_open.png"},
				{"Flank",      rtso::FORMATION_FLANK,        "res://assets/textures/ui/formations/flank.png"},
				{"BattleLine", rtso::FORMATION_BATTLE_LINE,  "res://assets/textures/ui/formations/battle_line.png"},
				{"ShipLine",   rtso::FORMATION_SHIPS_LINE,   "res://assets/textures/ui/formations/ship_line.png"},
				{"ShipCol",    rtso::FORMATION_SHIPS_COLUMN, "res://assets/textures/ui/formations/ship_column.png"}
			};

			// Query active formation across selected units to highlight matching button
			uint8_t active_formation = rtso::FORMATION_NONE;
			for (int i = 0; i < selected_ids.size(); ++i) {
				for (size_t u = 0; u < uview.count; ++u) {
					if (uview.id[u] == (uint32_t)selected_ids[i]) {
						if (uview.formation[u] != rtso::FORMATION_NONE) {
							active_formation = uview.formation[u];
							break;
						}
					}
				}
				if (active_formation != rtso::FORMATION_NONE) break;
			}

			for (int f = 0; f < 12; ++f) {
				Button *fb = memnew(Button);
				fb->set_custom_minimum_size(Vector2(44, 24));
				fb->set_clip_text(true);
				fb->set_tooltip_text(kFormationButtons[f].name);
				fb->set_text(kFormationButtons[f].name);
				Ref<Texture2D> f_ico = load_tex(kFormationButtons[f].icon);
				if (f_ico.is_valid()) {
					fb->set_button_icon(f_ico);
					fb->set_expand_icon(true);
				}
				fb->add_theme_font_size_override("font_size", 8);
				if (active_formation == kFormationButtons[f].id) {
					fb->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.2f));
				}
				fb->connect("pressed", Callable(this, "_on_formation_selected").bind((int)kFormationButtons[f].id));
				actions_bar->add_child(fb);
			}

			// Villager Build Menu
			bool has_worker = false;
			for (int i = 0; i < selected_ids.size(); ++i) {
				for (size_t u = 0; u < uview.count; ++u) {
					if (uview.id[u] == (uint32_t)selected_ids[i]) {
						uint8_t ut = uview.unit_type[u];
						if (ut == rtso::UNIT_VILLAGER || ut == rtso::UNIT_FEMALE_CITIZEN || ut == rtso::UNIT_WORKER_ELEPHANT) {
							has_worker = true;
							break;
						}
					}
				}
			}

			if (has_worker) {
				const char *b_names[13] = {
					"House (80w)", "Barracks (150w,50g)", "Tower (100w,100s)", "Storehouse (100w)",
					"Farmstead (100w)", "Corral (100w)", "Market (200w,100g)", "Temple (150w,100g,100s)",
					"Fortress (300w,200g,300s)", "Gate (100w,100s)", "Dock (200w,100s)", "Wall (40s)",
					"Wonder (500f,500w,500g,500s)"
				};
				int b_types[13] = {
					rtso::BLD_HOUSE, rtso::BLD_BARRACKS, rtso::BLD_TOWER, rtso::BLD_STOREHOUSE,
					rtso::BLD_FARMSTEAD, rtso::BLD_CORRAL, rtso::BLD_MARKET, rtso::BLD_TEMPLE,
					rtso::BLD_FORTRESS, rtso::BLD_GATE, rtso::BLD_DOCK, rtso::BLD_WALL_SEGMENT,
					rtso::BLD_WONDER
				};
				for (int b = 0; b < 13; ++b) {
					Button *bb = memnew(Button);
					bb->set_text(b_names[b]);
					bb->add_theme_font_size_override("font_size", 9);
					bb->connect("pressed", Callable(this, "_on_build_btn_pressed").bind(b_types[b]));
					production_bar->add_child(bb);
				}
			}
		}
	}
}

void RtsoMain::enter_build_mode(bool enable, uint8_t btype) {
	build_mode = enable;
	current_build_type = btype;
	if (!enable) {
		wall_drag_mode = false;
		for (auto *wg : wall_ghosts) {
			if (wg) wg->set_visible(false);
		}
	}
	update_ghost_mesh();
	if (ghost_mesh) ghost_mesh->set_visible(build_mode && !wall_drag_mode);
}

void RtsoMain::try_train_unit(rtso::UnitType utype) {
	if (!sim || !rtso::civ_can_train(sim->player_civ(), (uint8_t)utype)) return;
	uint32_t bid = selected_building_id;
	if (bid == 0) {
		for (const auto &b : sim->buildings()) {
			if (b.team != 0 || b.build_progress < 1.0f) continue;
			bool match = false;
			if ((utype == rtso::UNIT_VILLAGER || utype == rtso::UNIT_FEMALE_CITIZEN) && b.type == rtso::BLD_TOWN_CENTER)
				match = true;
			else if (utype == rtso::UNIT_TRADER && b.type == rtso::BLD_MARKET)
				match = true;
			else if ((utype == rtso::UNIT_TRIREME || utype == rtso::UNIT_FISHING_BOAT || utype == rtso::UNIT_MERCHANT_SHIP) && b.type == rtso::BLD_DOCK)
				match = true;
			else if (utype == rtso::UNIT_HEALER && (b.type == rtso::BLD_TEMPLE || b.type == rtso::BLD_TOWN_CENTER))
				match = true;
			else if (b.type == rtso::BLD_BARRACKS || b.type == rtso::BLD_FORTRESS)
				match = true;
			if (match) {
				bid = b.id;
				break;
			}
		}
	}
	if (bid == 0) return;
	if (net_lockstep()) {
		post_lockstep(6, nullptr, 0, 0, 0, bid, (uint8_t)utype);
		play_civ_voice("select");
		return;
	}
	if (sim->cmd_train(bid, utype))
		play_civ_voice("select");
}

void RtsoMain::clear_selection() {
	selected_ids.clear();
	selected_building_id = 0;
}

// Drop staged Shift waypoints of the current selection — a stop (or any other
// non-queued order) replaces the unit's queue; only input.js:1345 session.queue
// (Shift) appends. 0 A.D. gui/session/input.js:1345.
void RtsoMain::drop_staged_waypoints() {
	for (int s = 0; s < selected_ids.size(); ++s)
		waypoint_queue.erase((uint32_t)selected_ids[s]);
}

void RtsoMain::_unhandled_input(const Ref<InputEvent> &event) {
	Ref<InputEventKey> key = event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if (key->get_keycode() == KEY_ESCAPE) {
			if (chat_panel && chat_panel->is_visible()) {
				chat_panel->set_visible(false);
				return;
			}
			if (diplomacy_panel && diplomacy_panel->is_visible()) {
				diplomacy_panel->set_visible(false);
				return;
			}
			Node *pause = get_node_or_null(NodePath("PauseMenu"));
			if (pause) {
				pause->call("toggle");
				paused_via_menu = !paused_via_menu;
				return;
			}
			if (build_mode) enter_build_mode(false);
			else clear_selection();
			return;
		}

		if (key->get_keycode() == KEY_ENTER || key->get_keycode() == KEY_KP_ENTER) {
			toggle_chat();
			return;
		}

		// Control groups: Ctrl+0-9 stores the current selection, bare 0-9 recalls
		// it, Shift+0-9 appends to selection (alive player units only) — 0 A.D.
		// gui/session/input.js:1684 performGroup("save"/"select"); dispatch at input.js:906.
		if (key->get_keycode() >= KEY_0 && key->get_keycode() <= KEY_9) {
			const int grp = (int)(key->get_keycode() - KEY_0);
			if (key->is_ctrl_pressed()) {
				selected_groups[grp] = selected_ids;
				return;
			}
			PackedInt32Array recall;
			if (sim) {
				auto gview = sim->units();
				for (int g = 0; g < selected_groups[grp].size(); ++g) {
					const int32_t gid = selected_groups[grp][g];
					for (size_t i = 0; i < gview.count; ++i) {
						if (gview.alive[i] && gview.id[i] == (uint32_t)gid && gview.team[i] == 0) {
							recall.push_back(gid);
							break;
						}
					}
				}
			}
			if (!recall.is_empty()) {
				if (key->is_shift_pressed()) {
					// Shift+0-9 appends group to selection (0 A.D. input.js:1684)
					for (int r = 0; r < recall.size(); ++r) {
						if (!selected_ids.has(recall[r]))
							selected_ids.push_back(recall[r]);
					}
				} else {
					selected_ids = recall;
				}
				selected_building_id = 0;
				play_civ_voice("select");
			}
			return;
		}

		if (key->get_keycode() == KEY_PERIOD || key->get_keycode() == KEY_TAB) {
			cycle_idle_worker();
			return;
		}

		if (key->get_keycode() == KEY_D) {
			toggle_diplomacy();
			return;
		}

		if (key->get_keycode() == KEY_M) {
			Node *barter = get_node_or_null(NodePath("BarterDialog"));
			if (barter) barter->call("toggle");
			return;
		}

		if (key->get_keycode() == KEY_Z) {
			_on_ungarrison_pressed();
			return;
		}

		if (key->get_keycode() == KEY_S && !build_mode) {
			if (selected_ids.size() > 0) {
				drop_staged_waypoints(); // stop replaces the unit order queue (0 A.D. input.js:1345)
				if (net_lockstep())
					post_lockstep(2, (const uint32_t *)selected_ids.ptr(), selected_ids.size());
				else
					sim->cmd_stop((const uint32_t *)selected_ids.ptr(), selected_ids.size());
				return;
			}
		}

		// Hotkeys: Stances
		auto apply_stance = [&](uint8_t st) {
			if (selected_ids.is_empty()) return;
			if (net_lockstep())
				post_lockstep(7, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, 0, st);
			else
				sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), st);
		};
		if (key->get_keycode() == KEY_F1) { apply_stance(rtso::STANCE_AGGRESSIVE); return; }
		if (key->get_keycode() == KEY_F2) { apply_stance(rtso::STANCE_DEFENSIVE); return; }
		if (key->get_keycode() == KEY_F3) { apply_stance(rtso::STANCE_STAND_GROUND); return; }
		if (key->get_keycode() == KEY_F4) { apply_stance(rtso::STANCE_HOLD_GROUND); return; }
		// F5 stance passive (when no mod key) \u2014 if Ctrl held, it is QuickSave instead
		if (key->get_keycode() == KEY_F5 && !key->is_ctrl_pressed()) {
			apply_stance(rtso::STANCE_PASSIVE);
			return;
		}
		// F5 = QuickSave (Ctrl+F5 / standalone F5 overrides stance when Ctrl held)
		if (key->get_keycode() == KEY_F5 && key->is_ctrl_pressed()) {
			save_game("user://saves/quicksave.rts0sav");
			return;
		}
		if (key->get_keycode() == KEY_F6 && key->is_ctrl_pressed()) {
			load_game("user://saves/quicksave.rts0sav");
			return;
		}
		// Standalone F5/F6 quicksave-quickload (0 A.D. style)
		if (key->get_keycode() == KEY_F5 && selected_ids.is_empty()) {
			save_game("user://saves/quicksave.rts0sav");
			return;
		}
		if (key->get_keycode() == KEY_F6) {
			load_game("user://saves/quicksave.rts0sav");
			return;
		}

		// Hotkeys: Tactical Commands
		if (key->get_keycode() == KEY_Q && !selected_ids.is_empty()) {
			attack_move_mode = true;
			patrol_mode = false;
			guard_mode = false;
			play_civ_voice("select");
			return;
		}
		if (key->get_keycode() == KEY_P && !selected_ids.is_empty()) {
			patrol_mode = true;
			attack_move_mode = false;
			guard_mode = false;
			play_civ_voice("select");
			return;
		}
		// G key — guard mode: next RMB click picks target to guard
		if (key->get_keycode() == KEY_G && !selected_ids.is_empty()) {
			guard_mode = !guard_mode;
			attack_move_mode = false;
			patrol_mode = false;
			play_civ_voice("select");
			return;
		}

		// Hotkeys: Build Mode (covers all 14 RTS0 building types; 0 A.D. input.js lacks
		// direct letter mappings so we use first-letter mnemonics with non-conflicting fallbacks)
		if (key->get_keycode() == KEY_B) { enter_build_mode(!build_mode, rtso::BLD_HOUSE); return; }
		if (build_mode) {
			if (key->get_keycode() == KEY_C) { enter_build_mode(true, rtso::BLD_TOWN_CENTER); return; } // Civic Centre
			if (key->get_keycode() == KEY_H) { enter_build_mode(true, rtso::BLD_HOUSE); return; }       // House
			if (key->get_keycode() == KEY_R) { enter_build_mode(true, rtso::BLD_BARRACKS); return; }    // baRracks
			if (key->get_keycode() == KEY_S) { enter_build_mode(true, rtso::BLD_STOREHOUSE); return; }  // Storehouse
			if (key->get_keycode() == KEY_T) { enter_build_mode(true, rtso::BLD_TOWER); return; }       // Tower
			if (key->get_keycode() == KEY_F) { enter_build_mode(true, rtso::BLD_FARMSTEAD); return; }   // Farmstead
			if (key->get_keycode() == KEY_M) { enter_build_mode(true, rtso::BLD_MARKET); return; }      // Market
			if (key->get_keycode() == KEY_K) { enter_build_mode(true, rtso::BLD_FORTRESS); return; }    // Keep / Fortress
			if (key->get_keycode() == KEY_G) { enter_build_mode(true, rtso::BLD_GATE); return; }        // Gate
			if (key->get_keycode() == KEY_E || key->get_keycode() == KEY_P) { enter_build_mode(true, rtso::BLD_TEMPLE); return; } // tEmple / temPle
			if (key->get_keycode() == KEY_W) { enter_build_mode(true, rtso::BLD_WONDER); return; }      // Wonder
			if (key->get_keycode() == KEY_D) { enter_build_mode(true, rtso::BLD_DOCK); return; }        // Dock
			if (key->get_keycode() == KEY_L) { enter_build_mode(true, rtso::BLD_WALL_SEGMENT); return; } // waLl
			if (key->get_keycode() == KEY_O || key->get_keycode() == KEY_A) { enter_build_mode(true, rtso::BLD_CORRAL); return; } // cOrral / Animal
		}

		// Hotkeys: Training
		if (key->get_keycode() == KEY_V) { try_train_unit(rtso::UNIT_VILLAGER); return; }
		if (key->get_keycode() == KEY_A) { try_train_unit(rtso::UNIT_ARCHER); return; }
		if (key->get_keycode() == KEY_C) { try_train_unit(rtso::UNIT_CAVALRY); return; }

		if (key->get_keycode() == KEY_F7) {
			spawn_large_battle(500);
			return;
		}

		if (key->get_keycode() == KEY_F8) {
			spawn_large_battle(1000);
			return;
		}

		if (key->get_keycode() == KEY_F9) {
			spawn_large_battle(2000);
			return;
		}

		if (key->get_keycode() == KEY_F10) {
			if (SceneTree *t = get_tree()) {
				rtso::hide_node_popups(t->get_root());
				t->change_scene_to_file("res://scenes/menu/options.tscn");
			}
			return;
		}

		if (key->get_keycode() == KEY_F11) {
			if (ui_layer) ui_layer->set_visible(!ui_layer->is_visible());
			return;
		}

		if (key->get_keycode() == KEY_F12) {
			Viewport *vp = get_viewport();
			if (vp) {
				Ref<ViewportTexture> tex = vp->get_texture();
				if (tex.is_valid()) {
					Ref<Image> img = tex->get_image();
					if (img.is_valid()) {
						img->save_png("screen_battle.png");
						print_line("[RTS0] Saved screenshot to screen_battle.png");
					}
				}
			}
			return;
		}
	}

	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid()) {
		Vector2 mouse_pos = mb->get_position();

		if (mb->get_button_index() == MOUSE_BUTTON_LEFT) {
			if (mb->is_pressed()) {
				if (mb->is_alt_pressed()) {
					Vector3 click_pos = get_ground_click_pos(mouse_pos);
					if (minimap_view) {
						minimap_view->add_ping(Vector2(click_pos.x, click_pos.z), Color(1.0f, 0.4f, 0.2f));
						_on_flare_requested(Vector2(click_pos.x, click_pos.z));
						play_alarm("res://assets/sounds/ui/alarm.ogg");
					}
					return;
				}

				if (build_mode) {
					if (current_build_type == rtso::BLD_WALL_SEGMENT) {
						wall_drag_mode = true;
						wall_drag_start_pos = get_ground_click_pos(mouse_pos);
						return;
					}
					auto [cell_x, cell_y] = mouse_cell(mouse_pos);
					if (net_lockstep()) {
						post_lockstep(5, (const uint32_t *)selected_ids.ptr(), selected_ids.size(),
								(float)cell_x, (float)cell_y, 0, current_build_type);
						play_civ_voice("build");
						enter_build_mode(false);
					} else if (uint32_t bid = sim->cmd_build(cell_x, cell_y, current_build_type)) {
						if (!selected_ids.is_empty())
							sim->order_build_or_repair((const uint32_t *)selected_ids.ptr(), selected_ids.size(), bid);
						play_civ_voice("build");
						enter_build_mode(false);
					}
					return;
				}
				// Godot flags the second press of a double-click (0 A.D. ev.clicks == 2,
				// input.js:893); consumed on release below.
				double_click_pending = mb->is_double_click();
				dragging = true;
				drag_start = mouse_pos;
				sel_rect->set_rect(drag_start, mouse_pos, true);
			} else {
				const bool dbl_click = double_click_pending;
				double_click_pending = false;
				if (wall_drag_mode) {
					wall_drag_mode = false;
					for (auto *wg : wall_ghosts) {
						if (wg) wg->set_visible(false);
					}
					Vector3 drag_end_pos = get_ground_click_pos(mouse_pos);
					float dist = wall_drag_start_pos.distance_to(drag_end_pos);
					if (dist > 1.5f) {
						int seg_count = std::max(1, (int)(dist / 6.0f));
						Vector3 step_vec = (drag_end_pos - wall_drag_start_pos) / (float)seg_count;

						// Commit wall nodes through existing lockstep/build command paths
						// (0 A.D. gui/session/building_placement.js:45-92 / placement.js)
						auto place_wall_node = [&](const Vector3 &p, uint8_t btype) {
							int cx = (int)std::floor(p.x / rtso::Simulation::GRID);
							int cz = (int)std::floor(p.z / rtso::Simulation::GRID);
							if (net_lockstep()) {
								post_lockstep(5, (const uint32_t *)selected_ids.ptr(), selected_ids.size(),
								              (float)cx, (float)cz, 0, btype);
							} else if (sim) {
								uint32_t bid = sim->cmd_build(cx, cz, btype);
								if (!bid) {
									// Sandbox / cost-fallback placement
									bid = sim->spawn_building_world(p.x, p.z, btype, 0, 0.0f);
								}
								if (bid && !selected_ids.is_empty()) {
									sim->order_build_or_repair((const uint32_t *)selected_ids.ptr(), selected_ids.size(), bid);
								}
							}
						};

						// Start node tower
						place_wall_node(wall_drag_start_pos, rtso::BLD_TOWER);

						for (int s = 0; s < seg_count; ++s) {
							Vector3 mid_p = wall_drag_start_pos + step_vec * ((float)s + 0.5f);
							place_wall_node(mid_p, rtso::BLD_WALL_SEGMENT);

							if (s == seg_count - 1) {
								Vector3 end_p = wall_drag_start_pos + step_vec * ((float)s + 1.0f);
								place_wall_node(end_p, rtso::BLD_TOWER);
							}
						}
						play_civ_voice("build");
					}
					enter_build_mode(false);
					return;
				}
				if (dragging) {
					dragging = false;
					sel_rect->set_rect(Vector2(), Vector2(), false);
					Vector2 drag_end = mouse_pos;

					if (drag_start.distance_to(drag_end) < 8.0f) {
						// Single click selection
						Vector3 click_pos = get_ground_click_pos(mouse_pos);
						auto uview = sim->units();
						selected_ids.clear();
						selected_building_id = 0;

						// Check unit hit
						size_t best_uidx = (size_t)-1;
						float best_ud2 = 2.0f * 2.0f;
						for (size_t i = 0; i < uview.count; ++i) {
							if (!uview.alive[i] || uview.team[i] != 0) continue;
							float dx = uview.pos_x[i] - click_pos.x;
							float dz = uview.pos_z[i] - click_pos.z;
							float d2 = dx * dx + dz * dz;
							if (d2 < best_ud2) {
								best_ud2 = d2;
								best_uidx = i;
							}
						}

						if (best_uidx != (size_t)-1) {
							if (dbl_click) {
								// Double-click selects every same-type player unit on screen —
								// 0 A.D. gui/session/input.js:1022-1042 (ev.clicks == 2 →
								// selectionGroupName/template match via
								// Engine.PickSimilarPlayerEntities(showOffscreen=false)).
								// RTS0 matches on unit_type (no selectionGroupName data yet).
								const uint8_t same_type = uview.unit_type[best_uidx];
								Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
								if (cam) {
									const Vector2 vp_size = get_viewport()->get_visible_rect().size;
									for (size_t i = 0; i < uview.count; ++i) {
										if (!uview.alive[i] || uview.team[i] != 0 ||
											uview.unit_type[i] != same_type)
											continue;
										Vector3 world_pos(uview.pos_x[i], 0, uview.pos_z[i]);
										if (cam->is_position_behind(world_pos)) continue;
										Vector2 screen_pos = cam->unproject_position(world_pos);
										if (screen_pos.x >= 0.0f && screen_pos.x <= vp_size.x &&
											screen_pos.y >= 0.0f && screen_pos.y <= vp_size.y)
											selected_ids.push_back((int32_t)uview.id[i]);
									}
								}
								if (selected_ids.is_empty()) // camera-less fallback: keep the hit unit
									selected_ids.push_back((int32_t)uview.id[best_uidx]);
							} else {
								selected_ids.push_back((int32_t)uview.id[best_uidx]);
							}
							play_civ_voice("select");
						} else {
							// Check building hit
							for (const auto &b : sim->buildings()) {
								if (b.team != 0) continue;
								float dx = b.x - click_pos.x;
								float dz = b.z - click_pos.z;
								float hit_r = b.half_size + 0.6f;
								if (dx * dx + dz * dz <= hit_r * hit_r) {
									selected_building_id = b.id;
									break;
								}
							}
						}
					} else {
						// Box selection
						Vector2 min_p(std::min(drag_start.x, drag_end.x), std::min(drag_start.y, drag_end.y));
						Vector2 max_p(std::max(drag_start.x, drag_end.x), std::max(drag_start.y, drag_end.y));

						auto uview = sim->units();
						selected_ids.clear();
						selected_building_id = 0;
						Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
						if (cam) {
							for (size_t i = 0; i < uview.count; ++i) {
								if (!uview.alive[i] || uview.team[i] != 0) continue;
								Vector3 world_pos(uview.pos_x[i], 0, uview.pos_z[i]);
								Vector2 screen_pos = cam->unproject_position(world_pos);
								if (screen_pos.x >= min_p.x && screen_pos.x <= max_p.x &&
									screen_pos.y >= min_p.y && screen_pos.y <= max_p.y) {
									selected_ids.push_back((int32_t)uview.id[i]);
								}
							}
							if (selected_ids.size() > 0) {
								play_civ_voice("select");
							}
						}
					}
				}
			}
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && mb->is_pressed()) {
			if (build_mode) {
				enter_build_mode(false);
				return;
			}
			Vector3 click_pos = get_ground_click_pos(mouse_pos);

			// If building selected, right click sets Rally Point!
			if (selected_building_id != 0) {
				uint32_t target_res = 0, target_bld = 0;
				auto rview = sim->resources();
				for (size_t r = 0; r < rview.count; ++r) {
					if (!rview.alive[r]) continue;
					float dx = rview.x[r] - click_pos.x;
					float dz = rview.z[r] - click_pos.z;
					if (dx * dx + dz * dz < 2.0f * 2.0f) { target_res = rview.id[r]; break; }
				}
				for (const auto &b : sim->buildings()) {
					if (b.id == selected_building_id) continue;
					float dx = b.x - click_pos.x;
					float dz = b.z - click_pos.z;
					if (dx * dx + dz * dz < 3.0f * 3.0f) { target_bld = b.id; break; }
				}
				if (net_lockstep()) {
					uint32_t extra[2] = {target_res, target_bld};
					post_lockstep(14, extra, 2, click_pos.x, click_pos.z, selected_building_id);
				} else {
					sim->cmd_set_rally_point(selected_building_id, click_pos.x, click_pos.z, target_res, target_bld);
				}
				play_civ_voice("select");
				return;
			}

			if (selected_ids.is_empty()) return;

			// Staged Shift waypoints survive only if this click resolves to the
			// default move order below — every other right-click order replaces the
			// unit's queue (0 A.D. gui/session/input.js:1345 session.queue appends;
			// RTS0 queues plain move waypoints only). Stash here, restore below.
			const bool queue_shift = mb->is_shift_pressed();
			std::vector<std::pair<uint32_t, QueuedWaypoint>> stashed_wp;
			for (int s = 0; s < selected_ids.size(); ++s) {
				const uint32_t sid = (uint32_t)selected_ids[s];
				auto wit = waypoint_queue.find(sid);
				if (wit != waypoint_queue.end()) {
					stashed_wp.push_back(*wit);
					waypoint_queue.erase(wit);
				}
			}

			if (attack_move_mode) {
				if (net_lockstep())
					post_lockstep(10, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), click_pos.x, click_pos.z);
				else
					sim->order_attack_move((const uint32_t *)selected_ids.ptr(), selected_ids.size(), click_pos.x, click_pos.z);
				play_civ_voice("attack");
				attack_move_mode = false;
				return;
			}
			if (patrol_mode) {
				if (net_lockstep())
					post_lockstep(11, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), click_pos.x, click_pos.z);
				else
					sim->order_patrol((const uint32_t *)selected_ids.ptr(), selected_ids.size(), click_pos.x, click_pos.z);
				play_civ_voice("select");
				patrol_mode = false;
				return;
			}
			if (guard_mode) {
				auto uview = sim->units();
				uint32_t target_ally_id = 0;
				float best_ad2 = 3.0f * 3.0f;
				for (size_t i = 0; i < uview.count; ++i) {
					if (!uview.alive[i] || uview.team[i] != 0) continue;
					float dx = uview.pos_x[i] - click_pos.x;
					float dz = uview.pos_z[i] - click_pos.z;
					float d2 = dx * dx + dz * dz;
					if (d2 < best_ad2) {
						best_ad2 = d2;
						target_ally_id = uview.id[i];
					}
				}
				if (target_ally_id != 0) {
					if (net_lockstep())
						post_lockstep(21, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, target_ally_id);
					else
						sim->order_guard((const uint32_t *)selected_ids.ptr(), selected_ids.size(), target_ally_id);
					play_civ_voice("select");
				}
				guard_mode = false;
				return;
			}

			auto uview = sim->units();

			// 0. Right click with Loaded Transport on Shore/Land (Disembark)
			bool has_loaded_transport = false;
			uint32_t transport_sel_id = 0;
			for (int s = 0; s < selected_ids.size(); ++s) {
				if (sim->is_transport((uint32_t)selected_ids[s]) && sim->get_transport_passenger_count((uint32_t)selected_ids[s]) > 0) {
					has_loaded_transport = true;
					transport_sel_id = (uint32_t)selected_ids[s];
					break;
				}
			}
			if (has_loaded_transport && sim->get_terrain_height(click_pos.x, click_pos.z) >= sim->water_height() + 0.1f) {
				if (net_lockstep())
					post_lockstep(19, &transport_sel_id, 1, click_pos.x, click_pos.z);
				else
					sim->order_disembark(transport_sel_id, click_pos.x, click_pos.z);
				play_civ_voice("move");
				return;
			}

			// 1. Right click on Allied War Trireme / Transport (Board Naval Transport)
			uint32_t clicked_transport_id = 0;
			for (size_t i = 0; i < uview.count; ++i) {
				if (!uview.alive[i] || uview.team[i] != 0 || uview.unit_type[i] != rtso::UNIT_TRIREME) continue;
				float dx = uview.pos_x[i] - click_pos.x;
				float dz = uview.pos_z[i] - click_pos.z;
				if (dx * dx + dz * dz <= 3.2f * 3.2f) {
					clicked_transport_id = uview.id[i];
					break;
				}
			}
			if (clicked_transport_id != 0) {
				if (net_lockstep())
					post_lockstep(18, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, clicked_transport_id);
				else
					sim->order_board_transport((const uint32_t *)selected_ids.ptr(), selected_ids.size(), clicked_transport_id);
				play_civ_voice("move");
				return;
			}

			// 2. Right click on Enemy Unit (Attack)
			uint32_t clicked_enemy_id = 0;
			float best_ed2 = 2.2f * 2.2f;
			for (size_t i = 0; i < uview.count; ++i) {
				if (!uview.alive[i] || uview.team[i] == 0) continue;
				float dx = uview.pos_x[i] - click_pos.x;
				float dz = uview.pos_z[i] - click_pos.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best_ed2) {
					best_ed2 = d2;
					clicked_enemy_id = uview.id[i];
				}
			}
			if (clicked_enemy_id != 0) {
				if (net_lockstep())
					post_lockstep(3, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, clicked_enemy_id);
				else
					sim->order_attack((const uint32_t *)selected_ids.ptr(), selected_ids.size(), clicked_enemy_id);
				play_civ_voice("attack");
				return;
			}

			// 3. Right click on Wildlife Fauna (Hunting vs Herding into Corral)
			auto fview = sim->fauna();
			uint32_t clicked_fauna_id = 0;
			uint8_t clicked_fauna_type = 0;
			float best_fd2 = 2.5f * 2.5f;
			for (size_t f = 0; f < fview.count; ++f) {
				if (!fview.alive[f]) continue;
				float dx = fview.pos_x[f] - click_pos.x;
				float dz = fview.pos_z[f] - click_pos.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best_fd2) {
					best_fd2 = d2;
					clicked_fauna_id = fview.id[f];
					clicked_fauna_type = fview.fauna_type[f];
				}
			}
			if (clicked_fauna_id != 0) {
				const auto &finfo = rtso::fauna_type_info(clicked_fauna_type);
				uint8_t herd = finfo.is_domestic ? 1 : 0;
				if (net_lockstep())
					post_lockstep(17, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, clicked_fauna_id, herd);
				else if (herd)
					sim->order_herd((const uint32_t *)selected_ids.ptr(), selected_ids.size(), clicked_fauna_id);
				else
					sim->order_hunt((const uint32_t *)selected_ids.ptr(), selected_ids.size(), clicked_fauna_id);
				play_civ_voice(herd ? "gather" : "attack");
				return;
			}

			// 3. Right click on Resource Node (Gather)
			auto rview = sim->resources();
			uint32_t clicked_res_id = 0;
			float best_rd2 = 2.2f * 2.2f;
			for (size_t r = 0; r < rview.count; ++r) {
				if (!rview.alive[r] || rview.amount[r] <= 0) continue;
				float dx = rview.x[r] - click_pos.x;
				float dz = rview.z[r] - click_pos.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best_rd2) {
					best_rd2 = d2;
					clicked_res_id = rview.id[r];
				}
			}
			if (clicked_res_id != 0) {
				if (net_lockstep())
					post_lockstep(4, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, clicked_res_id);
				else
					sim->order_gather((const uint32_t *)selected_ids.ptr(), selected_ids.size(), clicked_res_id);
				play_civ_voice("gather");
				return;
			}

			// 4. Right click on Building (Attack Enemy vs Build/Repair/Garrison Allied)
			for (const auto &b : sim->buildings()) {
				float dx = b.x - click_pos.x;
				float dz = b.z - click_pos.z;
				float hit_r = b.half_size + 1.2f;
				if (dx * dx + dz * dz <= hit_r * hit_r) {
					if (b.team != 0) {
						if (net_lockstep())
							post_lockstep(15, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, b.id);
						else
							sim->order_attack_building((const uint32_t *)selected_ids.ptr(), selected_ids.size(), b.id);
						play_civ_voice("attack");
					} else {
						if (b.build_progress < 1.0f || b.hp < b.max_hp) {
							if (net_lockstep())
								post_lockstep(16, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, b.id);
							else
								sim->order_build_or_repair((const uint32_t *)selected_ids.ptr(), selected_ids.size(), b.id);
							play_civ_voice("build");
						} else if (b.type == rtso::BLD_WALL_SEGMENT && Input::get_singleton()->is_key_pressed(KEY_CTRL)) {
							sim->spawn_building_world(b.x, b.z, rtso::BLD_GATE, 0, 1.0f);
							play_civ_voice("build");
						} else {
							if (net_lockstep())
								post_lockstep(12, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, b.id);
							else
								sim->order_garrison((const uint32_t *)selected_ids.ptr(), selected_ids.size(), b.id);
							clear_selection();
						}
					}
					return;
				}
			}

			// 5. Default: Move Order
			if (queue_shift) {
				// Shift+right-click queues the waypoint instead of replacing the
				// order — 0 A.D. gui/session/input.js:1345 (session.queue hotkey
				// appends to the unit's order queue). Restore stashed points, then
				// append this click for units already walking a queue; units without
				// one start a fresh queue with a single batched cmd_move so
				// formation offsets still apply (simulation.cpp cmd_move).
				for (auto &entry : stashed_wp)
					waypoint_queue.insert(std::move(entry));
				stashed_wp.clear();
				PackedInt32Array fresh;
				for (int s = 0; s < selected_ids.size(); ++s) {
					const uint32_t sid = (uint32_t)selected_ids[s];
					auto wit = waypoint_queue.find(sid);
					if (wit == waypoint_queue.end())
						fresh.push_back((int32_t)sid);
					else
						wit->second.pts.push_back(click_pos);
				}
				if (!fresh.is_empty()) {
					if (net_lockstep())
						post_lockstep(1, (const uint32_t *)fresh.ptr(), fresh.size(), click_pos.x, click_pos.z);
					else
						sim->cmd_move((const uint32_t *)fresh.ptr(), fresh.size(), click_pos.x, click_pos.z);
					QueuedWaypoint nq;
					nq.pts.push_back(click_pos);
					for (int s = 0; s < fresh.size(); ++s)
						waypoint_queue[(uint32_t)fresh[s]] = nq;
				}
				play_civ_voice("move");
				return;
			}
			// Plain right-click: stashed waypoints dropped (order replaced).
			if (net_lockstep())
				post_lockstep(1, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), click_pos.x, click_pos.z);
			else
				sim->cmd_move((const uint32_t *)selected_ids.ptr(), selected_ids.size(), click_pos.x, click_pos.z);
			play_civ_voice("move");
		}
	}

	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && dragging) {
		sel_rect->set_rect(drag_start, mm->get_position(), true);
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
		sim->spawn_initial();
		return;
	}
	String map_id = (String)ms->get("map_id");
	uint32_t seed = (uint32_t)(int64_t)(int)ms->get("seed");
	if (seed == 0) seed = (uint32_t)Time::get_singleton()->get_ticks_msec();
	if (map_id.is_empty()) map_id = "random_continental";
	int victory = (int)ms->get("victory");
	int diff = (int)ms->get("ai_difficulty");
	int behavior = (int)ms->get("ai_behavior");
	int start_res = (int)ms->get("start_res");
	Array slot_civs = ms->get("slot_civs");
	// Normalize legacy fields: if the setup screen never ran, slot_civs
	// may be empty — fall back to player_civ/ai_civ.
	int civ = (int)ms->get("player_civ");
	int ai = (int)ms->get("ai_civ");
	if (slot_civs.size() >= 2) {
		civ = (int)slot_civs[0];
		ai = (int)slot_civs[1];
	}
	if (!load_map_seeded(map_id, seed)) {
		UtilityFunctions::printerr("[RtsoMain] GameSetup map failed: ", map_id, " — default arena.");
		if (civ >= 0 && civ < rtso::CIV_COUNT) sim->set_player_civ((uint8_t)civ);
		if (ai >= 0 && ai < rtso::CIV_COUNT) sim->set_team_civ(1, (uint8_t)ai);
		sim->spawn_initial();
		return;
	}
	if (victory >= 0 && victory <= 3) sim->set_victory_condition((uint8_t)victory);
	if (diff >= 0 && diff <= 5) sim->set_petra_difficulty((uint8_t)diff);
	if (behavior >= 0 && behavior <= 3) sim->set_petra_behavior((uint8_t)behavior);
	// Starting-resources presets (0 sparse / 1 normal / 2 generous / 3 abundant).
	static const int kStartRes[4][4] = {
		{150, 200, 150, 100}, {300, 400, 300, 200},
		{500, 700, 500, 350}, {900, 1200, 900, 600},
	};
	if (start_res < 0 || start_res > 3) start_res = 1;
	sim->set_starting_resources(kStartRes[start_res][0], kStartRes[start_res][1],
			kStartRes[start_res][2], kStartRes[start_res][3]);
	// Per-slot civs (slot 0 = human; extra slots honor team civs the
	// RMS/JSON spawns actually created — Petra only drives team 1).
	for (int t = 0; t < 4 && t < slot_civs.size(); ++t) {
		int sc = (int)slot_civs[t];
		if (sc >= 0 && sc < rtso::CIV_COUNT) sim->set_team_civ((uint8_t)t, (uint8_t)sc);
	}
	UtilityFunctions::print("[RtsoMain] GameSetup ", map_id, " seed ", (int64_t)seed,
			" civ ", rtso::civ_info(sim->player_civ()).name,
			" vs ", rtso::civ_info(sim->team_civ(1)).name);
	// Map just (re)spawned — select the human player's units like the old
	// _ready tail did (it ran before any map existed, so it selected nothing).
	clear_selection();
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (uview.team[i] == 0) selected_ids.push_back((int32_t)uview.id[i]);
	}
}

// Match summary for the setup E2E + future summary screen: what the sim
// actually holds after apply_match_settings / apply_game_setup.
Dictionary RtsoMain::get_match_summary() const {
	Dictionary d;
	if (!sim) return d;
	d["map_id"] = String(sim->current_map_id().c_str());
	d["map_name"] = String(sim->current_map_name().c_str());
	d["seed"] = (int64_t)sim->sim_seed();
	d["victory"] = (int)sim->victory_condition();
	d["petra_diff"] = (int)sim->petra_difficulty();
	d["petra_behavior"] = (int)sim->petra_behavior();
	d["food"] = sim->food();
	d["wood"] = sim->wood();
	d["gold"] = sim->gold();
	d["stone"] = sim->stone();
	d["player_civ"] = (int)sim->player_civ();
	d["ai_civ"] = (int)sim->team_civ(1);
	d["units"] = (int64_t)sim->unit_count();
	d["buildings"] = (int64_t)sim->building_count();
	return d;
}

void RtsoMain::_on_map_selected(int index) {
	static const char *map_ids[] = {
		"acropolis_bay",
		"corinthian_isthmus",
		"atlas_valleys",
		"bactria",
		"battle_for_the_tiber",
		"default",
		"random_continental",
		"random_archipelago",
		"random_oasis",
		"random_highlands",
		"random_valley",
		"random_mediterranean",
		"random_acropolis",
		"random_spartan_valley",
		"random_danubius",
		"random_saharan_oases",
		"random_anatolian_plateau",
		"random_jebel_barkal"
	};
	if (index >= 0 && index < 18) {
		load_map(map_ids[index]);
	}
}

PackedStringArray RtsoMain::get_available_maps() const {
	PackedStringArray maps;
	maps.push_back("acropolis_bay");
	maps.push_back("corinthian_isthmus");
	maps.push_back("atlas_valleys");
	maps.push_back("bactria");
	maps.push_back("battle_for_the_tiber");
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
	return maps;
}

bool RtsoMain::load_map(String map_id) {
	uint32_t seed = (uint32_t)Time::get_singleton()->get_ticks_msec();
	return load_map_seeded(map_id, seed);
}

bool RtsoMain::load_map_seeded(String map_id, uint32_t seed) {
	if (!sim || !view) return false;
	clear_selection();

	// 1. Procedural Random Map Generation (RMS Engine)
	if (map_id.begins_with("random_") || map_id.begins_with("procedural_") ||
		map_id == "continental" || map_id == "archipelago" || map_id == "oasis" ||
		map_id == "highlands" || map_id == "valley" || map_id == "mediterranean" ||
		map_id == "random_acropolis" || map_id == "random_spartan_valley" ||
		map_id == "danubius" || map_id == "random_danubius" ||
		map_id == "saharan_oases" || map_id == "random_saharan_oases" ||
		map_id == "anatolian_plateau" || map_id == "random_anatolian_plateau" ||
		map_id == "jebel_barkal" || map_id == "random_jebel_barkal") {
		
		rtso::MapType mtype = rtso::MAP_RANDOM_CONTINENTAL;
		int num_p = 2;
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
		else if (map_id.contains("mediterranean")) { mtype = rtso::MAP_RANDOM_MEDITERRANEAN; num_p = 4; }

		sim->generate_random_map(mtype, seed, num_p, 64.0f);
		view->rebuild_terrain(sim);

		if (cam_rig) {
			cam_rig->set_map_bounds_half(sim->map_half());
			for (const auto &b : sim->buildings()) {
				if (b.team == 0 && b.type == rtso::BLD_TOWN_CENTER) {
					cam_rig->set_target_pos(Vector3(b.x, 0, b.z));
					break;
				}
			}
		}

		UtilityFunctions::print("[RtsoMain] Generated procedural RMS map '", rtso::map_type_info(mtype).name, "' (seed=", (int64_t)seed, ", players=", num_p, ")");
		return true;
	}

	if (map_id == "default" || map_id == "arena") {
		sim->set_sim_seed(seed);
		sim->spawn_initial();
		view->rebuild_terrain(sim);
		if (cam_rig) {
			cam_rig->set_map_bounds_half(sim->map_half());
			cam_rig->set_target_pos(Vector3(0, 0, 0));
		}
		UtilityFunctions::print("[RtsoMain] Loaded default arena map.");
		return true;
	}

	String path = "res://data/maps/" + map_id + ".json";
	if (!FileAccess::file_exists(path)) {
		path = "res://data/maps/" + map_id;
		if (!FileAccess::file_exists(path)) {
			UtilityFunctions::printerr("[RtsoMain] Map file not found: ", map_id);
			return false;
		}
	}

	Ref<FileAccess> fa = FileAccess::open(path, FileAccess::READ);
	if (!fa.is_valid()) return false;
	String content = fa->get_as_text();
	fa->close();

	Variant parsed = JSON::parse_string(content);
	if (parsed.get_type() != Variant::DICTIONARY) return false;
	Dictionary d = parsed;

	sim->set_sim_seed(seed);
	sim->reset();

	String name = d.get("name", map_id);
	String biome = d.get("biome", "mediterranean");
	float world_size = (float)d.get("world_size", 256.0f);
	float water_h = (float)d.get("water_height", -1.0f);
	int h_res = (int)d.get("heightmap_res", 64);

	sim->set_map_half(world_size * 0.5f);
	sim->set_map_info(map_id.utf8().get_data(), name.utf8().get_data(), biome.utf8().get_data());

	Array h_arr = d.get("heightmap", Array());
	std::vector<float> heights;
	heights.resize(h_arr.size());
	for (int i = 0; i < h_arr.size(); ++i) {
		heights[i] = (float)h_arr[i];
	}
	sim->set_terrain_data(heights, h_res, water_h);

	// Spawn Resources
	Array res_arr = d.get("resources", Array());
	for (int i = 0; i < res_arr.size(); ++i) {
		Dictionary r = res_arr[i];
		String rtype_str = r.get("type", "tree");
		float rx = (float)r.get("x", 0.0);
		float rz = (float)r.get("z", 0.0);
		int amt = (int)r.get("amount", 600);

		uint8_t rtype = rtso::RES_WOOD;
		if (rtype_str == "gold" || rtype_str == "metal") rtype = rtso::RES_GOLD;
		else if (rtype_str == "stone" || rtype_str == "rock") rtype = rtso::RES_STONE;
		else if (rtype_str == "food" || rtype_str == "fruit") rtype = rtso::RES_FOOD;

		sim->spawn_resource_world(rx, rz, rtype, amt);
	}

	// Spawn Structures
	Array str_arr = d.get("structures", Array());
	for (int i = 0; i < str_arr.size(); ++i) {
		Dictionary b = str_arr[i];
		String stype_str = b.get("type", "house");
		int player = (int)b.get("player", 1);
		float bx = (float)b.get("x", 0.0);
		float bz = (float)b.get("z", 0.0);
		uint8_t team = (player == 1) ? 0 : 1;

		uint8_t btype = rtso::BLD_HOUSE;
		if (stype_str == "town_center" || stype_str == "civic") btype = rtso::BLD_TOWN_CENTER;
		else if (stype_str == "barracks") btype = rtso::BLD_BARRACKS;
		else if (stype_str == "house") btype = rtso::BLD_HOUSE;
		else if (stype_str == "storehouse") btype = rtso::BLD_STOREHOUSE;
		else if (stype_str == "tower") btype = rtso::BLD_TOWER;
		else if (stype_str == "fortress") btype = rtso::BLD_FORTRESS;

		sim->spawn_building_world(bx, bz, btype, team, 1.0f);
	}

	// Spawn Starting Units
	Array units_arr = d.get("units", Array());
	for (int i = 0; i < units_arr.size(); ++i) {
		Dictionary u = units_arr[i];
		String utype_str = u.get("type", "villager");
		int player = (int)u.get("player", 1);
		float ux = (float)u.get("x", 0.0);
		float uz = (float)u.get("z", 0.0);
		uint8_t team = (player == 1) ? 0 : 1;

		rtso::UnitType utype = rtso::UNIT_VILLAGER;
		if (utype_str == "swordsman") utype = rtso::UNIT_SWORDSMAN;
		else if (utype_str == "spearman") utype = rtso::UNIT_SPEARMAN;
		else if (utype_str == "archer") utype = rtso::UNIT_ARCHER;
		else if (utype_str == "cavalry") utype = rtso::UNIT_CAVALRY;
		else if (utype_str == "catapult") utype = rtso::UNIT_CATAPULT;

		const auto &uinfo = rtso::unit_type_info(utype);
		sim->spawn_unit(ux, uz, team, utype, uinfo.hp);
	}

	// Rebuild World View 3D terrain and bounds
	view->rebuild_terrain(sim);
	if (cam_rig) {
		cam_rig->set_map_bounds_half(sim->map_half());
		// Find Player 1 start position
		Array players_arr = d.get("players", Array());
		if (!players_arr.is_empty()) {
			Dictionary p1 = players_arr[0];
			float start_x = (float)p1.get("start_x", 0.0);
			float start_z = (float)p1.get("start_z", 0.0);
			cam_rig->set_target_pos(Vector3(start_x, 0, start_z));
		}
	}

	UtilityFunctions::print("[RtsoMain] Loaded 0 A.D. map '", name, "' (", world_size, "m, ", sim->resource_count(), " resources, ", sim->building_count(), " structures, ", sim->unit_count(), " units)");
	return true;
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

void RtsoMain::_on_victory_selected(int index) {
	if (!sim || !victory_selector) return;
	sim->set_victory_condition((uint8_t)victory_selector->get_item_id(index));
}

void RtsoMain::_on_voice_button_pressed() {
	use_english_voice_orders_ = !use_english_voice_orders_;
	if (voice_btn)
		voice_btn->set_text(use_english_voice_orders_ ? "Voice: EN" : "Voice: Civ");
}

void RtsoMain::_on_lobby_button_pressed() {
	if (!lobby_panel) return;
	lobby_panel->set_visible(!lobby_panel->is_visible());
	if (lobby_panel->is_visible())
		refresh_lobby_rooms();
}

void RtsoMain::refresh_lobby_rooms() {
	if (!lobby_rooms) return;
	lobby_rooms->clear();
	lobby_room_cache_.clear();
	std::string body;
	if (!rtso::lobby_http_get(lobby_http_host_, lobby_http_port_, "/rooms", body)) {
		if (lobby_status)
			lobby_status->set_text("Lobby unreachable — start: python3 tools/lobby_server.py");
		return;
	}
	lobby_room_cache_ = rtso::parse_lobby_rooms(body);
	for (const auto &r : lobby_room_cache_) {
		String line = String(r.id.c_str()) + "  " + String(r.host_ip.c_str()) + ":" +
		              String::num_int64(r.host_port) + "  " + String::num_int64(r.players) +
		              "/" + String::num_int64(r.max_players) + "  " + String(r.map_id.c_str());
		lobby_rooms->add_item(line);
	}
	if (lobby_status)
		lobby_status->set_text(String::num_int64((int64_t)lobby_room_cache_.size()) + " rooms");
}

void RtsoMain::_on_lobby_refresh_pressed() {
	refresh_lobby_rooms();
}

void RtsoMain::post_lockstep(uint8_t cmd_type, const uint32_t *ids, size_t n,
                             float x, float z, uint32_t tid, uint8_t param) {
	rtso::NetCommand c;
	c.cmd_type = cmd_type;
	if (ids && n) c.entity_ids.assign(ids, ids + n);
	c.target_x = x;
	c.target_z = z;
	c.target_id = tid;
	c.param = param;
	turn_mgr_.post_local_command(c);
}

void RtsoMain::_on_lobby_host_pressed() {
	rtso::NetConfig cfg;
	cfg.port = (uint16_t)lobby_port->get_text().to_int();
	if (cfg.port == 0) cfg.port = rtso::PS_DEFAULT_PORT;
	net_host_.setLocalPlayerName(lobby_name->get_text().utf8().get_data());
	if (!net_host_.startHost(cfg)) {
		if (lobby_status) lobby_status->set_text("Host bind failed");
		return;
	}
	turn_mgr_.reset();
	turn_mgr_.set_net_host(&net_host_, 0);
	turn_mgr_.clear_game_started();
	net_game_started_ = false; // wait for Start (NMT_GAME_START) before any lockstep turn
	std::ostringstream js;
	js << "{\"id\":\"room-" << cfg.port << "\",\"host_ip\":\"127.0.0.1\",\"host_port\":"
	   << cfg.port << ",\"players\":1,\"max_players\":" << (int)cfg.maxPlayers
	   << ",\"map_id\":\"default\"}";
	std::string body;
	rtso::lobby_http_post_json(lobby_http_host_, lobby_http_port_, "/rooms", js.str(), body);
	if (lobby_status) lobby_status->set_text("Hosting on " + String::num_int64(cfg.port));
	refresh_lobby_rooms();
}

void RtsoMain::_on_lobby_start_pressed() {
	if (!sim) return;
	if (!net_host_.isHost()) {
		if (lobby_status) lobby_status->set_text("Start is host-only — Join waits for host Start");
		return;
	}
	// Seed-sync + full setup broadcast: the host's CURRENT match settings
	// (map+seed+victory+petra+slots from the GameSetup screen) are packed
	// into NMT_GAME_START so the join path applies the identical setup via
	// apply_game_setup. generate_random_map funnels the seed into
	// set_sim_seed, so both RNG streams (RMS + sim ticks) match.
	rtso::GameSetupParams setup;
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		String mid = (String)ms->get("map_id");
		setup.map_id = mid.is_empty() ? sim->current_map_id() : mid.utf8().get_data();
		setup.seed = (uint32_t)(int64_t)(int)ms->get("seed");
		setup.victory = (uint8_t)((int)ms->get("victory"));
		setup.difficulty = (uint8_t)((int)ms->get("ai_difficulty"));
		setup.behavior = (uint8_t)((int)ms->get("ai_behavior"));
		setup.start_res = (uint8_t)((int)ms->get("start_res"));
		Array civs = ms->get("slot_civs");
		Array types = ms->get("slot_types");
		Array teams = ms->get("slot_teams");
		setup.slot_count = 0;
		for (int i = 0; i < 8 && i < civs.size(); ++i) {
			int c = (int)civs[i];
			setup.civs[i] = (c >= 0 && c < rtso::CIV_COUNT) ? (uint8_t)c : 0;
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
	if (setup.seed == 0) setup.seed = (uint32_t)Time::get_singleton()->get_ticks_msec();
	if (setup.victory > 3) setup.victory = sim->victory_condition();
	if (setup.difficulty > 5) setup.difficulty = sim->petra_difficulty();
	if (setup.behavior > 3) setup.behavior = sim->petra_behavior();
	if (setup.start_res > 3) setup.start_res = 1;
	apply_game_setup(setup);
	turn_mgr_.reset();
	turn_mgr_.set_net_host(&net_host_, 0);
	turn_mgr_.set_game_start_applied_sim(sim);
	turn_mgr_.mark_game_started(sim);
	net_host_.note_game_start_sent();
	net_host_.sendGameStart(rtso::format_game_start_full(setup));
	net_game_started_ = true;
	print_line("[RTS0][GameStart] HOST broadcast setup: map=" + String(setup.map_id.c_str()) +
	           " seed=" + String::num_int64((int64_t)setup.seed) +
	           " slots=" + String::num_int64(setup.slot_count) +
	           " civ0=" + String::num_int64((int64_t)setup.civs[0]) +
	           " civ1=" + String::num_int64((int64_t)setup.civs[1]) +
	           " types=" + String::num_int64((int64_t)setup.slot_types[0]) +
	           "/" + String::num_int64((int64_t)setup.slot_types[1]) +
	           " res=" + String::num_int64((int64_t)setup.start_res));
	if (lobby_status)
		lobby_status->set_text(String("Started ") + String(setup.map_id.c_str()) +
		                         " seed " + String::num_int64((int64_t)setup.seed));
}

// Apply the full GameSetup staged by NMT_GAME_START (map+seed+options).
// Order mirrors _ready: map first (reset inside), options after.
void RtsoMain::apply_game_setup(const rtso::GameSetupParams &setup) {
	if (!sim) return;
	if (!load_map_seeded(String(setup.map_id.c_str()), setup.seed)) {
		if (lobby_status) lobby_status->set_text("GameStart failed: map regen failed");
		return;
	}
	sim->set_victory_condition(setup.victory > 3 ? 0 : setup.victory);
	sim->set_petra_difficulty(setup.difficulty > 5 ? 3 : setup.difficulty);
	sim->set_petra_behavior(setup.behavior > 3 ? 2 : setup.behavior);
	static const int kStartRes[4][4] = {
		{150, 200, 150, 100}, {300, 400, 300, 200},
		{500, 700, 500, 350}, {900, 1200, 900, 600},
	};
	uint8_t sr = setup.start_res > 3 ? 1 : setup.start_res;
	sim->set_starting_resources(kStartRes[sr][0], kStartRes[sr][1],
			kStartRes[sr][2], kStartRes[sr][3]);
	for (uint8_t t = 0; t < 4 && t < setup.slot_count; ++t) {
		if (setup.civs[t] < rtso::CIV_COUNT) sim->set_team_civ(t, setup.civs[t]);
	}
	clear_selection();
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (uview.team[i] == 0) selected_ids.push_back((int32_t)uview.id[i]);
	}
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		ms->set("map_id", String(setup.map_id.c_str()));
		ms->set("seed", (int64_t)setup.seed);
		ms->set("victory", (int)sim->victory_condition());
		ms->set("ai_difficulty", (int)sim->petra_difficulty());
		ms->set("ai_behavior", (int)sim->petra_behavior());
		ms->set("start_res", (int)sr);
		Array civs;
		for (uint8_t t = 0; t < 4 && t < setup.slot_count; ++t) civs.push_back((int)setup.civs[t]);
		ms->set("slot_civs", civs);
		ms->set("player_civ", (int)sim->player_civ());
		ms->set("ai_civ", (int)sim->team_civ(1));
	}
}
void RtsoMain::apply_net_game_start() {
	if (!sim) return;
	turn_mgr_.set_game_start_applied_sim(sim);
	rtso::GameSetupParams setup;
	if (!turn_mgr_.take_pending_game_start_full(setup)) return;
	apply_game_setup(setup);
	turn_mgr_.reset();
	turn_mgr_.set_net_host(&net_host_, net_host_.isHost() ? 0 : 1);
	turn_mgr_.set_game_start_applied_sim(sim);
	turn_mgr_.note_game_start_applied(sim);
	net_game_started_ = true;
	print_line("[RTS0][GameStart] JOINER consumed NMT_GAME_START: map=" + String(setup.map_id.c_str()) +
	           " seed=" + String::num_int64((int64_t)setup.seed) +
	           " slots=" + String::num_int64(setup.slot_count) +
	           " civ0=" + String::num_int64((int64_t)setup.civs[0]) +
	           " civ1=" + String::num_int64((int64_t)setup.civs[1]) +
	           " types=" + String::num_int64((int64_t)setup.slot_types[0]) +
	           "/" + String::num_int64((int64_t)setup.slot_types[1]) +
	           " res=" + String::num_int64((int64_t)setup.start_res));
	if (lobby_status)
		lobby_status->set_text(String("GameStart ") + String(setup.map_id.c_str()) +
		                         " seed " + String::num_int64((int64_t)setup.seed));
}

void RtsoMain::_on_lobby_join_pressed() {
	std::string ip = lobby_host->get_text().utf8().get_data();
	uint16_t port = (uint16_t)lobby_port->get_text().to_int();
	PackedInt32Array sel = lobby_rooms->get_selected_items();
	if (sel.size() > 0) {
		int idx = sel[0];
		if (idx >= 0 && (size_t)idx < lobby_room_cache_.size()) {
			ip = lobby_room_cache_[(size_t)idx].host_ip;
			port = lobby_room_cache_[(size_t)idx].host_port;
		}
	}
	if (port == 0) port = rtso::PS_DEFAULT_PORT;
	if (ip.empty()) ip = "127.0.0.1";
	net_host_.setLocalPlayerName(lobby_name->get_text().utf8().get_data());
	if (!net_host_.startClient(ip, port)) {
		if (lobby_status) lobby_status->set_text("Join failed");
		return;
	}
	turn_mgr_.reset();
	turn_mgr_.set_net_host(&net_host_, 1);
	turn_mgr_.clear_game_started();
	net_game_started_ = false; // wait for host's NMT_GAME_START before lockstep turns
	if (lobby_status)
		lobby_status->set_text("Connecting " + String(ip.c_str()) + ":" + String::num_int64(port));
}

void RtsoMain::set_civ(int civ_idx) {
	if (!sim) return;
	if (civ_idx < 0 || civ_idx >= rtso::CIV_COUNT) civ_idx = rtso::CIV_ATHENIANS;
	sim->set_player_civ((uint8_t)civ_idx);

	const auto &info = rtso::civ_info(civ_idx);
	if (civ_emblem) {
		civ_emblem->set_texture(load_tex(info.emblem_path));
	}
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		ms->set("player_civ", civ_idx);
	}
	UtilityFunctions::print("[RtsoMain] Switched Civilization to: ", info.name, " (", info.description, ")");
}

Dictionary RtsoMain::get_civ_info(int civ_idx) const {
	Dictionary d;
	const auto &info = rtso::civ_info(civ_idx);
	d["civ"] = (int)info.civ;
	d["code"] = String(info.code);
	d["name"] = String(info.name);
	d["emblem"] = String(info.emblem_path);
	d["desc"] = String(info.description);
	d["metal_mult"] = info.metal_gather_mult;
	d["melee_dmg_mult"] = info.melee_damage_mult;
	d["hp_mult"] = info.hp_mult;
	d["build_speed_mult"] = info.build_speed_mult;
	d["trade_gain_mult"] = info.trade_gain_mult;
	return d;
}

void RtsoMain::_on_queue_slot_gui_input(const Ref<InputEvent> &event, int slot_idx) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid() && mb->is_pressed()) {
		if (mb->get_button_index() == MOUSE_BUTTON_RIGHT || mb->get_button_index() == MOUSE_BUTTON_LEFT) {
			_on_queue_slot_cancel(slot_idx);
			get_viewport()->set_input_as_handled();
		}
	}
}

void RtsoMain::_on_queue_slot_cancel(int slot_idx) {
	if (selected_building_id != 0 && sim) {
		if (net_lockstep()) {
			post_lockstep(20, nullptr, 0, 0, 0, selected_building_id, (uint8_t)slot_idx);
			play_sfx("res://assets/sounds/ui/cancel.ogg");
			return;
		}
		if (sim->cmd_cancel_train(selected_building_id, (size_t)slot_idx)) {
			play_sfx("res://assets/sounds/ui/cancel.ogg");
			update_command_panel();
			update_inspector();
		}
	}
}

void RtsoMain::toggle_chat() {
	if (!chat_panel) return;
	bool vis = !chat_panel->is_visible();
	chat_panel->set_visible(vis);
	if (vis && chat_input) {
		chat_input->grab_focus();
	}
}

void RtsoMain::_on_chat_channel_btn_pressed(int channel_idx) {
	if (chat_channel) {
		chat_channel->select(channel_idx);
	}
}

void RtsoMain::_on_chat_send_pressed() {
	if (!chat_input || !chat_log) return;
	String text = chat_input->get_text().strip_edges();
	if (!text.is_empty()) {
		int ch = chat_channel ? chat_channel->get_selected() : 0;
		String ch_prefix = "[color=#55bbff][All][/color]";
		String wire_tag = "";
		if (ch == 1) {
			ch_prefix = "[color=#55ff55][Allies][/color]";
			wire_tag = "[Allies] ";
		} else if (ch == 2) {
			ch_prefix = "[color=#ff5555][Enemies][/color]";
			wire_tag = "[Enemies] ";
		} else if (ch >= 3) {
			int p_num = ch - 2;
			ch_prefix = "[color=#eebb22][Player-" + String::num_int64(p_num) + "][/color]";
			wire_tag = "[Player-" + String::num_int64(p_num) + "] ";
		}
		String civ_name = rtso::civ_info(sim->player_civ()).name;
		chat_log->append_text(ch_prefix + " [color=#ffffff]" + civ_name + "[/color]: " + text + "\n");
		chat_input->clear();
		play_sfx("res://assets/sounds/ui/chat.ogg");
		// Network: 0 A.D. NMT_CHAT broadcast (NetHost::sendChat net_host.cpp:327).
		// Channel travels as wire tag (0 A.D. gui/session/chat/ChatBounce.js:15-38 parity).
		// Parsed client-side in onChat display without breaking protocol compatibility.
		if (net_lockstep()) {
			std::string wire = (wire_tag + text).utf8().get_data();
			net_host_.sendChat(wire);
		}
	}
	if (chat_panel) chat_panel->set_visible(false);
}

void RtsoMain::_on_chat_text_submitted(const String &text) {
	_on_chat_send_pressed();
}

void RtsoMain::toggle_diplomacy() {
	if (!diplomacy_panel) return;
	diplomacy_panel->set_visible(!diplomacy_panel->is_visible());
	if (diplomacy_panel->is_visible()) {
		play_sfx("res://assets/sounds/ui/diplomacy.ogg");
	}
}

void RtsoMain::_on_diplo_stance_changed(int stance, int player_idx) {
	if (!sim || player_idx < 0 || player_idx >= 8) return;
	// Lockstep: stance MUST be a turn command (cmd_type 22, turn_manager.cpp dispatch)
	// — diplomacy_state() is hashed (turn_manager.cpp:213), so a local-only change
	// desyncs peers. Offline applies immediately via the validated sim cmd.
	// 0 A.D. Diplomacy.js / DiplomacyDialog.js OnStanceChange parity.
	if (net_lockstep())
		post_lockstep(22, nullptr, 0, 0, 0, (uint32_t)player_idx, (uint8_t)stance);
	else
		sim->cmd_set_diplomacy((uint8_t)player_idx, (uint8_t)stance);
	String st_name = (stance == 0) ? "Ally" : ((stance == 1) ? "Neutral" : "Enemy");
	UtilityFunctions::print("[RTS0] Set diplomacy stance with Player ", player_idx + 1, " to ", st_name);
	if (chat_log) {
		chat_log->append_text("[color=#eebb22]🏛️ Diplomacy with Player " + String::num_int64(player_idx + 1) + " set to " + st_name + "[/color]\n");
	}
}

void RtsoMain::_on_diplo_tribute_pressed(int player_idx, int res_type) {
	if (!sim || player_idx <= 0 || player_idx >= 8) return;
	bool ok = sim->send_tribute((uint8_t)player_idx, (uint8_t)res_type, 100);
	if (ok) {
		const char *r_names[4] = {"Wood", "Gold", "Stone", "Food"};
		String r_name = (res_type >= 0 && res_type < 4) ? r_names[res_type] : "Resource";
		UtilityFunctions::print("[RTS0] Sent 100 ", r_name, " tribute to Player ", player_idx + 1);
		if (chat_log) {
			chat_log->append_text("[color=#55ffaa]🎁 Sent 100 " + r_name + " tribute to Player " + String::num_int64(player_idx + 1) + "[/color]\n");
		}
		play_sfx("res://assets/sounds/ui/bonus.ogg");
		update_hud();
	} else {
		play_alarm("res://assets/sounds/ui/alarm.ogg");
	}
}
