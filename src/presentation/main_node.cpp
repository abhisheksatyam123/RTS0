#include "main_node.h"
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

using namespace godot;

void RtsoMain::_bind_methods() {
	ClassDB::bind_method(D_METHOD("enter_build_mode", "enable", "btype"), &RtsoMain::enter_build_mode, DEFVAL(rtso::BLD_HOUSE));
	ClassDB::bind_method(D_METHOD("clear_selection"), &RtsoMain::clear_selection);
	ClassDB::bind_method(D_METHOD("_on_minimap_clicked", "world_pos"), &RtsoMain::_on_minimap_clicked);
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
	ClassDB::bind_method(D_METHOD("set_civ", "civ_idx"), &RtsoMain::set_civ);
	ClassDB::bind_method(D_METHOD("get_civ_info", "civ_idx"), &RtsoMain::get_civ_info);

	const StringName cls = get_class_static();
	auto bind_enum = [&](const char *enum_name, const char *name, int64_t value) {
		ClassDB::bind_integer_constant(cls, enum_name, name, value);
	};
	bind_enum("UnitType", "VILLAGER", rtso::UNIT_VILLAGER);
	bind_enum("UnitType", "SWORDSMAN", rtso::UNIT_SWORDSMAN);
	bind_enum("UnitType", "ARCHER", rtso::UNIT_ARCHER);
	bind_enum("UnitType", "SPEARMAN", rtso::UNIT_SPEARMAN);
	bind_enum("UnitType", "CAVALRY", rtso::UNIT_CAVALRY);
	bind_enum("UnitType", "CATAPULT", rtso::UNIT_CATAPULT);
	bind_enum("UnitType", "HERO", rtso::UNIT_HERO);
	bind_enum("UnitType", "TRIREME", rtso::UNIT_TRIREME);
	bind_enum("UnitType", "ELEPHANT", rtso::UNIT_ELEPHANT);
	bind_enum("UnitType", "SIEGE_RAM", rtso::UNIT_SIEGE_RAM);
	bind_enum("UnitType", "NONE", rtso::UNIT_TYPE_NONE);

	bind_enum("BuildingType", "TOWN_CENTER", rtso::BLD_TOWN_CENTER);
	bind_enum("BuildingType", "HOUSE", rtso::BLD_HOUSE);
	bind_enum("BuildingType", "BARRACKS", rtso::BLD_BARRACKS);
	bind_enum("BuildingType", "STOREHOUSE", rtso::BLD_STOREHOUSE);
	bind_enum("BuildingType", "TOWER", rtso::BLD_TOWER);
	bind_enum("BuildingType", "FARMSTEAD", rtso::BLD_FARMSTEAD);
	bind_enum("BuildingType", "MARKET", rtso::BLD_MARKET);
	bind_enum("BuildingType", "FORTRESS", rtso::BLD_FORTRESS);
}

RtsoMain::RtsoMain() {
	sim = new rtso::Simulation();
}

RtsoMain::~RtsoMain() {
	delete sim;
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

	anim_ctrl.load("res://assets/animations");
	sim->spawn_initial();

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
	ui_layer->add_child(minimap_view);

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
	civ_emblem->set_texture(load_tex("res://assets/textures/ui/emblems/athenians.png"));
	top_hbox->add_child(civ_emblem);

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

	map_selector = memnew(OptionButton);
	map_selector->set_custom_minimum_size(Vector2(145, 28));
	map_selector->add_item("Acropolis Bay (2P)", 0);
	map_selector->add_item("Corinthian Isthmus (4P)", 1);
	map_selector->add_item("Atlas Valleys (8P)", 2);
	map_selector->add_item("Bactria (2P)", 3);
	map_selector->add_item("Battle for Tiber (3P)", 4);
	map_selector->add_item("Standard Arena (128m)", 5);
	map_selector->connect("item_selected", Callable(this, "_on_map_selected"));
	top_hbox->add_child(map_selector);

	civ_selector = memnew(OptionButton);
	civ_selector->set_custom_minimum_size(Vector2(115, 28));
	civ_selector->add_item("Athenians", 0);
	civ_selector->add_item("Spartans", 1);
	civ_selector->add_item("Persians", 2);
	civ_selector->add_item("Romans", 3);
	civ_selector->add_item("Carthaginians", 4);
	civ_selector->add_item("Macedonians", 5);
	civ_selector->connect("item_selected", Callable(this, "_on_civ_selected"));
	top_hbox->add_child(civ_selector);

	// =========================================================================
	// 2. BOTTOM INSPECTOR PANEL (Portrait, segmented HP bar, Armor, Combat stats)
	// =========================================================================
	inspector_panel = memnew(Panel);
	inspector_panel->set_anchor(SIDE_LEFT, 0.0f);
	inspector_panel->set_anchor(SIDE_RIGHT, 0.0f);
	inspector_panel->set_anchor(SIDE_TOP, 1.0f);
	inspector_panel->set_anchor(SIDE_BOTTOM, 1.0f);
	inspector_panel->set_offset(SIDE_LEFT, 214.0f);
	inspector_panel->set_offset(SIDE_RIGHT, 694.0f);
	inspector_panel->set_offset(SIDE_TOP, -170.0f);
	inspector_panel->set_offset(SIDE_BOTTOM, -12.0f);

	Ref<StyleBoxFlat> insp_style;
	insp_style.instantiate();
	insp_style->set_bg_color(Color(0.08f, 0.09f, 0.13f, 0.93f));
	insp_style->set_corner_radius_all(8);
	insp_style->set_border_width_all(1);
	insp_style->set_border_color(Color(0.42f, 0.45f, 0.56f, 0.85f));
	inspector_panel->add_theme_stylebox_override("panel", insp_style);
	ui_layer->add_child(inspector_panel);

	inspector_portrait = memnew(TextureRect);
	inspector_portrait->set_position(Vector2(12, 12));
	inspector_portrait->set_size(Vector2(100, 100));
	inspector_portrait->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	inspector_portrait->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_COVERED);
	inspector_panel->add_child(inspector_portrait);

	inspector_title = memnew(Label);
	inspector_title->set_position(Vector2(120, 10));
	inspector_title->set_size(Vector2(340, 22));
	inspector_title->add_theme_font_size_override("font_size", 15);
	inspector_title->add_theme_color_override("font_color", Color(1.0f, 0.88f, 0.4f));
	inspector_panel->add_child(inspector_title);

	inspector_class = memnew(Label);
	inspector_class->set_position(Vector2(120, 32));
	inspector_class->set_size(Vector2(340, 18));
	inspector_class->add_theme_font_size_override("font_size", 11);
	inspector_class->add_theme_color_override("font_color", Color(0.7f, 0.75f, 0.85f));
	inspector_panel->add_child(inspector_class);

	inspector_hp_bar = memnew(ProgressBar);
	inspector_hp_bar->set_position(Vector2(120, 52));
	inspector_hp_bar->set_size(Vector2(335, 14));
	inspector_hp_bar->set_show_percentage(false);
	inspector_panel->add_child(inspector_hp_bar);

	inspector_hp_label = memnew(Label);
	inspector_hp_label->set_position(Vector2(120, 50));
	inspector_hp_label->set_size(Vector2(335, 18));
	inspector_hp_label->add_theme_font_size_override("font_size", 10);
	inspector_hp_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	inspector_panel->add_child(inspector_hp_label);

	inspector_stats = memnew(Label);
	inspector_stats->set_position(Vector2(120, 70));
	inspector_stats->set_size(Vector2(340, 36));
	inspector_stats->add_theme_font_size_override("font_size", 11);
	inspector_stats->add_theme_color_override("font_color", Color(0.85f, 0.88f, 0.95f));
	inspector_panel->add_child(inspector_stats);

	inspector_extra = memnew(Label);
	inspector_extra->set_position(Vector2(12, 118));
	inspector_extra->set_size(Vector2(340, 30));
	inspector_extra->add_theme_font_size_override("font_size", 11);
	inspector_extra->add_theme_color_override("font_color", Color(0.4f, 0.95f, 0.6f));
	inspector_panel->add_child(inspector_extra);

	ungarrison_btn = memnew(Button);
	ungarrison_btn->set_position(Vector2(356, 115));
	ungarrison_btn->set_size(Vector2(100, 28));
	ungarrison_btn->set_text("Ungarrison (Z)");
	ungarrison_btn->add_theme_font_size_override("font_size", 11);
	ungarrison_btn->connect("pressed", Callable(this, "_on_ungarrison_pressed"));
	ungarrison_btn->set_visible(false);
	inspector_panel->add_child(ungarrison_btn);

	// =========================================================================
	// 3. COMMAND & ACTION PANEL (Stances, Formations, Training, Build Grid)
	// =========================================================================
	command_panel = memnew(Panel);
	command_panel->set_anchor(SIDE_LEFT, 1.0f);
	command_panel->set_anchor(SIDE_RIGHT, 1.0f);
	command_panel->set_anchor(SIDE_TOP, 1.0f);
	command_panel->set_anchor(SIDE_BOTTOM, 1.0f);
	command_panel->set_offset(SIDE_LEFT, -574.0f);
	command_panel->set_offset(SIDE_RIGHT, -12.0f);
	command_panel->set_offset(SIDE_TOP, -170.0f);
	command_panel->set_offset(SIDE_BOTTOM, -12.0f);

	Ref<StyleBoxFlat> cmd_style;
	cmd_style.instantiate();
	cmd_style->set_bg_color(Color(0.08f, 0.09f, 0.13f, 0.93f));
	cmd_style->set_corner_radius_all(8);
	cmd_style->set_border_width_all(1);
	cmd_style->set_border_color(Color(0.42f, 0.45f, 0.56f, 0.85f));
	command_panel->add_theme_stylebox_override("panel", cmd_style);
	ui_layer->add_child(command_panel);

	command_title = memnew(Label);
	command_title->set_position(Vector2(10, 6));
	command_title->set_size(Vector2(540, 20));
	command_title->add_theme_font_size_override("font_size", 13);
	command_title->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.4f));
	command_title->set_text("Commands & Actions");
	command_panel->add_child(command_title);

	// Stances Bar (Aggressive, Defensive, Stand Ground, Hold, Passive)
	stances_bar = memnew(HBoxContainer);
	stances_bar->set_position(Vector2(10, 30));
	stances_bar->set_size(Vector2(540, 32));
	stances_bar->add_theme_constant_override("separation", 6);
	command_panel->add_child(stances_bar);

	const char *stance_names[5] = {"Aggressive", "Defensive", "Stand Ground", "Hold", "Passive"};
	for (int s = 0; s < 5; ++s) {
		Button *sb = memnew(Button);
		sb->set_custom_minimum_size(Vector2(78, 28));
		sb->set_text(stance_names[s]);
		sb->add_theme_font_size_override("font_size", 10);
		sb->connect("pressed", Callable(this, "_on_stance_selected").bind(s));
		stances_bar->add_child(sb);
	}

	// Actions & Formations Bar
	actions_bar = memnew(HBoxContainer);
	actions_bar->set_position(Vector2(10, 66));
	actions_bar->set_size(Vector2(540, 32));
	actions_bar->add_theme_constant_override("separation", 6);
	command_panel->add_child(actions_bar);

	// Production Bar (Training & Tech)
	production_bar = memnew(HBoxContainer);
	production_bar->set_position(Vector2(10, 102));
	production_bar->set_size(Vector2(540, 44));
	production_bar->add_theme_constant_override("separation", 6);
	command_panel->add_child(production_bar);

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

	// Select initial player units
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (uview.team[i] == 0) selected_ids.push_back((int32_t)uview.id[i]);
	}
}

void RtsoMain::play_voice(const char *path) {
	if (!voice_player) return;
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(path);
		if (s.is_valid()) {
			voice_player->set_stream(s);
			voice_player->play();
		}
	}
}

void RtsoMain::play_spatial_sfx(const char *path, Vector3 pos) {
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
			play_voice("res://assets/sounds/voice/select_1.ogg");
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
		sim->order_ungarrison(selected_building_id);
	}
}

void RtsoMain::_on_stance_selected(int stance) {
	if (!selected_ids.is_empty()) {
		sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), (uint8_t)stance);
	}
}

void RtsoMain::_on_formation_selected(int formation) {
	if (!selected_ids.is_empty()) {
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
		sim->cmd_research(selected_building_id, (uint8_t)tech_id);
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
	const char *mid = rtso::building_type_model(current_build_type);
	String mid_str = mid;
	String cat = "buildings";
	auto bentry = rtso::ModelLibrary::get(cat, mid_str);
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
	if (paused_via_menu) return;
	double scaled_delta = delta * game_speed;
	match_elapsed_time += scaled_delta;

	rtso::PerfTracker::begin_frame();
	uint64_t start_us = Time::get_singleton()->get_ticks_usec();

	accumulator += scaled_delta;
	int ticks = 0;
	while (accumulator >= rtso::Simulation::FIXED_STEP) {
		sim->tick(rtso::Simulation::FIXED_STEP);
		accumulator -= rtso::Simulation::FIXED_STEP;
		ticks++;
		if (ticks > 8) break;
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

	// Update Building Placement Ghost
	if (build_mode && ghost_mesh) {
		Vector2 mouse_pos = get_viewport()->get_mouse_position();
		auto [cell_x, cell_y] = mouse_cell(mouse_pos);
		int footprint = rtso::building_type_info(current_build_type).footprint;

		float gx = (cell_x + footprint * 0.5f) * rtso::Simulation::GRID;
		float gz = (cell_y + footprint * 0.5f) * rtso::Simulation::GRID;
		ghost_mesh->set_global_position(Vector3(gx, 0.05f, gz));

		bool can_build = sim->can_place(cell_x, cell_y, footprint);
		ghost_mesh->set_material_override(can_build ? ghost_mat_ok : ghost_mat_bad);
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
			inspector_class->set_text("Athenian Architecture • Garrison Cap: " + String::num_int64(found_b->garrison_cap));

			inspector_hp_bar->set_max(found_b->max_hp);
			inspector_hp_bar->set_value(found_b->hp);
			inspector_hp_label->set_text("HP: " + String::num((double)found_b->hp, 0) + " / " + String::num((double)found_b->max_hp, 0));

			String status = "State: Operational";
			if (found_b->build_progress < 1.0f) {
				status = "Under Construction (" + String::num((double)(found_b->build_progress * 100.0f), 0) + "%)";
			} else if (found_b->research_tech != rtso::TECH_NONE) {
				status = "Researching " + String(rtso::tech_info(found_b->research_tech).name) +
					" (" + String::num((double)(found_b->research_duration - found_b->research_timer), 1) + "s)";
			} else if (!found_b->queue.empty()) {
				String q_str = "Queue (" + String::num_int64(found_b->queue.size()) + "/8): ";
				for (size_t qi = 0; qi < found_b->queue.size(); ++qi) {
					if (qi == 0) {
						q_str += "> " + String(rtso::unit_type_name(found_b->queue[qi].unit_type)) +
							" (" + String::num((double)(found_b->queue[qi].duration - found_b->queue[qi].timer), 1) + "s) ";
					} else {
						q_str += "[" + String::num_int64(qi + 1) + "] " + String(rtso::unit_type_name(found_b->queue[qi].unit_type)) + " ";
					}
				}
				status = q_str;
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
		int v_cnt = 0, s_cnt = 0, a_cnt = 0, sp_cnt = 0, c_cnt = 0;
		float hp_sum = 0, hp_max_sum = 0;
		uint8_t single_utype = rtso::UNIT_VILLAGER;

		for (int i = 0; i < selected_ids.size(); ++i) {
			uint32_t uid = (uint32_t)selected_ids[i];
			for (size_t u = 0; u < uview.count; ++u) {
				if (uview.id[u] == uid && uview.alive[u]) {
					hp_sum += uview.hp[u];
					hp_max_sum += uview.max_hp[u];
					single_utype = uview.unit_type[u];
					if (uview.unit_type[u] == rtso::UNIT_VILLAGER) v_cnt++;
					else if (uview.unit_type[u] == rtso::UNIT_SWORDSMAN) s_cnt++;
					else if (uview.unit_type[u] == rtso::UNIT_ARCHER) a_cnt++;
					else if (uview.unit_type[u] == rtso::UNIT_SPEARMAN) sp_cnt++;
					else if (uview.unit_type[u] == rtso::UNIT_CAVALRY) c_cnt++;
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
			inspector_class->set_text("Kills: " + String::num_int64(u_kills) + " • Speed: " + String::num(uinfo.speed, 1) + " m/s");

			inspector_hp_bar->set_max(hp_max_sum);
			inspector_hp_bar->set_value(hp_sum);
			inspector_hp_label->set_text("HP: " + String::num((double)hp_sum, 0) + " / " + String::num((double)hp_max_sum, 0));

			inspector_stats->set_text("Attack: " + String::num(uinfo.attack_damage, 0) +
				" | Range: " + String::num(uinfo.attack_range, 1) +
				" | Armor: " + String::num(uinfo.hack_armor, 0) + "H/" + String::num(uinfo.pierce_armor, 0) + "P");

			inspector_extra->set_text("Orders: [RMB] Move/Attack/Gather | Stances: [F1-F5]");
		} else {
			inspector_portrait->set_texture(load_tex("res://assets/textures/ui/portraits/swordsman.png"));
			inspector_title->set_text("Selected Company (" + String::num_int64(selected_ids.size()) + " Units)");
			inspector_class->set_text("Athenian Taskforce • Combined Arms");

			inspector_hp_bar->set_max(hp_max_sum);
			inspector_hp_bar->set_value(hp_sum);
			inspector_hp_label->set_text("HP: " + String::num((double)hp_sum, 0) + " / " + String::num((double)hp_max_sum, 0));

			inspector_stats->set_text("Comp: " + String::num_int64(v_cnt) + " Villagers, " +
				String::num_int64(s_cnt) + " Swords, " + String::num_int64(a_cnt) + " Archers, " +
				String::num_int64(sp_cnt) + " Spears, " + String::num_int64(c_cnt) + " Cav");

			inspector_extra->set_text("Stances: [F1-F5] | Formations: [Z,X,C,V]");
		}

		ungarrison_btn->set_visible(false);
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
		if (found_b && found_b->build_progress >= 1.0f) {
			if (found_b->type == rtso::BLD_TOWN_CENTER) {
				Button *btn_v = memnew(Button);
				btn_v->set_text("Villager (50f)");
				btn_v->connect("pressed", Callable(this, "_on_train_pressed").bind((int)rtso::UNIT_VILLAGER));
				production_bar->add_child(btn_v);

				if (sim->player_phase() == 1) {
					Button *btn_phase = memnew(Button);
					btn_phase->set_text("Town Phase (200f,200w,100g)");
					btn_phase->connect("pressed", Callable(this, "_on_tech_pressed").bind((int)rtso::TECH_PHASE_TOWN));
					production_bar->add_child(btn_phase);
				} else if (sim->player_phase() == 2) {
					Button *btn_phase = memnew(Button);
					btn_phase->set_text("City Phase (400f,400w,300g,200s)");
					btn_phase->connect("pressed", Callable(this, "_on_tech_pressed").bind((int)rtso::TECH_PHASE_CITY));
					production_bar->add_child(btn_phase);
				}
			} else if (found_b->type == rtso::BLD_BARRACKS) {
				Button *btn_s = memnew(Button);
				btn_s->set_text("Swordsman (60g,30w)");
				btn_s->connect("pressed", Callable(this, "_on_train_pressed").bind((int)rtso::UNIT_SWORDSMAN));
				production_bar->add_child(btn_s);

				Button *btn_a = memnew(Button);
				btn_a->set_text("Archer (40g,50w)");
				btn_a->connect("pressed", Callable(this, "_on_train_pressed").bind((int)rtso::UNIT_ARCHER));
				production_bar->add_child(btn_a);

				Button *btn_sp = memnew(Button);
				btn_sp->set_text("Spearman (30f,40w,20g)");
				btn_sp->connect("pressed", Callable(this, "_on_train_pressed").bind((int)rtso::UNIT_SPEARMAN));
				production_bar->add_child(btn_sp);

				Button *btn_c = memnew(Button);
				btn_c->set_text("Cavalry (80g,50f)");
				btn_c->connect("pressed", Callable(this, "_on_train_pressed").bind((int)rtso::UNIT_CAVALRY));
				production_bar->add_child(btn_c);

				if (sim->player_phase() >= 2) {
					Button *btn_hero = memnew(Button);
					btn_hero->set_text("Champion Hero (200f,200g)");
					btn_hero->connect("pressed", Callable(this, "_on_train_pressed").bind((int)rtso::UNIT_HERO));
					production_bar->add_child(btn_hero);
				}

				if (sim->player_phase() >= 3) {
					Button *btn_cat = memnew(Button);
					btn_cat->set_text("Catapult Siege (150w,100g,50s)");
					btn_cat->connect("pressed", Callable(this, "_on_train_pressed").bind((int)rtso::UNIT_CATAPULT));
					production_bar->add_child(btn_cat);
				}

				if (sim->player_civ() == rtso::CIV_CARTHAGINIANS || sim->player_phase() >= 3) {
					Button *btn_ele = memnew(Button);
					btn_ele->set_text("War Elephant (250f,200g)");
					btn_ele->connect("pressed", Callable(this, "_on_train_pressed").bind((int)rtso::UNIT_ELEPHANT));
					production_bar->add_child(btn_ele);
				}

				if (sim->player_phase() >= 3) {
					Button *btn_ram = memnew(Button);
					btn_ram->set_text("Siege Ram (200w,100g)");
					btn_ram->connect("pressed", Callable(this, "_on_train_pressed").bind((int)rtso::UNIT_SIEGE_RAM));
					production_bar->add_child(btn_ram);
				}
			} else if (found_b->type == rtso::BLD_DOCK) {
				Button *btn_tri = memnew(Button);
				btn_tri->set_text("War Trireme (200w,150g)");
				btn_tri->connect("pressed", Callable(this, "_on_train_pressed").bind((int)rtso::UNIT_TRIREME));
				production_bar->add_child(btn_tri);
			} else if (found_b->type == rtso::BLD_MARKET) {
				Button *btn_t = memnew(Button);
				btn_t->set_text("Merchant Trader (60f,60w)");
				btn_t->connect("pressed", Callable(this, "_on_train_pressed").bind((int)rtso::UNIT_TRADER));
				production_bar->add_child(btn_t);
			}
		}
	} else if (!selected_ids.is_empty()) {
		// Villager Build Menu
		bool has_villager = false;
		auto uview = sim->units();
		for (int i = 0; i < selected_ids.size(); ++i) {
			for (size_t u = 0; u < uview.count; ++u) {
				if (uview.id[u] == (uint32_t)selected_ids[i] && uview.unit_type[u] == rtso::UNIT_VILLAGER) {
					has_villager = true;
					break;
				}
			}
		}

		if (has_villager) {
			const char *b_names[10] = {
				"House (80w)", "Barracks (150w,50g)", "Tower (100w,100s)", "Storehouse (100w)",
				"Farmstead (100w)", "Market (200w,100g)", "Temple (150w,100g,100s)",
				"Wonder (500f,500w,500g,500s)", "Dock (200w,100s)", "Wall (40s)"
			};
			int b_types[10] = {
				rtso::BLD_HOUSE, rtso::BLD_BARRACKS, rtso::BLD_TOWER, rtso::BLD_STOREHOUSE,
				rtso::BLD_FARMSTEAD, rtso::BLD_MARKET, rtso::BLD_TEMPLE,
				rtso::BLD_WONDER, rtso::BLD_DOCK, rtso::BLD_WALL_SEGMENT
			};
			for (int b = 0; b < 10; ++b) {
				Button *bb = memnew(Button);
				bb->set_text(b_names[b]);
				bb->add_theme_font_size_override("font_size", 10);
				bb->connect("pressed", Callable(this, "_on_build_btn_pressed").bind(b_types[b]));
				actions_bar->add_child(bb);
			}
		}
	}
}

void RtsoMain::enter_build_mode(bool enable, uint8_t btype) {
	build_mode = enable;
	current_build_type = btype;
	update_ghost_mesh();
	if (ghost_mesh) ghost_mesh->set_visible(build_mode);
}

void RtsoMain::try_train_unit(rtso::UnitType utype) {
	if (selected_building_id != 0) {
		if (sim->cmd_train(selected_building_id, utype)) {
			play_voice("res://assets/sounds/voice/select_2.ogg");
		}
		return;
	}
	for (const auto &b : sim->buildings()) {
		if (b.team == 0 && b.build_progress >= 1.0f) {
			if (utype == rtso::UNIT_VILLAGER && b.type == rtso::BLD_TOWN_CENTER) {
				if (sim->cmd_train(b.id, utype)) {
					play_voice("res://assets/sounds/voice/select_2.ogg");
					break;
				}
			else if (utype == rtso::UNIT_CATAPULT && (b.type == rtso::BLD_BARRACKS || b.type == rtso::BLD_FORTRESS)) {
				if (sim->cmd_train(b.id, utype)) {
					play_voice("res://assets/sounds/voice/select_2.ogg");
					break;
				}
			}
			else if (utype == rtso::UNIT_TRADER && b.type == rtso::BLD_MARKET) {
				if (sim->cmd_train(b.id, utype)) {
					play_voice("res://assets/sounds/voice/select_2.ogg");
					break;
				}
			}
			} else if (utype != rtso::UNIT_VILLAGER && b.type == rtso::BLD_BARRACKS) {
				if (sim->cmd_train(b.id, utype)) {
					play_voice("res://assets/sounds/voice/select_2.ogg");
					break;
				}
			}
		}
	}
}

void RtsoMain::clear_selection() {
	selected_ids.clear();
	selected_building_id = 0;
}

void RtsoMain::_unhandled_input(const Ref<InputEvent> &event) {
	Ref<InputEventKey> key = event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if (key->get_keycode() == KEY_ESCAPE) {
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

		if (key->get_keycode() == KEY_PERIOD || key->get_keycode() == KEY_TAB) {
			cycle_idle_worker();
			return;
		}

		if (key->get_keycode() == KEY_D) {
			Node *diplo = get_node_or_null(NodePath("DiplomacyDialog"));
			if (diplo) diplo->call("toggle");
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
				sim->cmd_stop((const uint32_t *)selected_ids.ptr(), selected_ids.size());
				return;
			}
		}

		// Hotkeys: Stances
		if (key->get_keycode() == KEY_F1) { sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), rtso::STANCE_AGGRESSIVE); return; }
		if (key->get_keycode() == KEY_F2) { sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), rtso::STANCE_DEFENSIVE); return; }
		if (key->get_keycode() == KEY_F3) { sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), rtso::STANCE_STAND_GROUND); return; }
		if (key->get_keycode() == KEY_F4) { sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), rtso::STANCE_HOLD_GROUND); return; }
		// F5 stance passive (when no mod key) \u2014 if Ctrl held, it is QuickSave instead
		if (key->get_keycode() == KEY_F5 && !key->is_ctrl_pressed()) {
			sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), rtso::STANCE_PASSIVE);
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
			play_voice("res://assets/sounds/voice/select_2.ogg");
			return;
		}
		if (key->get_keycode() == KEY_P && !selected_ids.is_empty()) {
			patrol_mode = true;
			attack_move_mode = false;
			guard_mode = false;
			play_voice("res://assets/sounds/voice/select_2.ogg");
			return;
		}
		// G key — guard mode: next RMB click picks target to guard
		if (key->get_keycode() == KEY_G && !selected_ids.is_empty()) {
			guard_mode = !guard_mode;
			attack_move_mode = false;
			patrol_mode = false;
			play_voice("res://assets/sounds/voice/select_2.ogg");
			return;
		}

		// Hotkeys: Training
		if (key->get_keycode() == KEY_V) { try_train_unit(rtso::UNIT_VILLAGER); return; }
		if (key->get_keycode() == KEY_A) { try_train_unit(rtso::UNIT_ARCHER); return; }
		if (key->get_keycode() == KEY_C) { try_train_unit(rtso::UNIT_CAVALRY); return; }

		// Hotkeys: Build Mode
		if (key->get_keycode() == KEY_B) { enter_build_mode(!build_mode, rtso::BLD_HOUSE); return; }
		if (build_mode) {
			if (key->get_keycode() == KEY_H) { enter_build_mode(true, rtso::BLD_HOUSE); return; }
			if (key->get_keycode() == KEY_R) { enter_build_mode(true, rtso::BLD_BARRACKS); return; }
			if (key->get_keycode() == KEY_T) { enter_build_mode(true, rtso::BLD_TOWER); return; }
		}

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
			get_tree()->change_scene_to_file("res://scenes/menu/options.tscn");
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
					if (sim->cmd_build(cell_x, cell_y, current_build_type)) {
						play_voice("res://assets/sounds/voice/build_1.ogg");
						enter_build_mode(false);
					}
					return;
				}
				dragging = true;
				drag_start = mouse_pos;
				sel_rect->set_rect(drag_start, mouse_pos, true);
			} else {
				if (wall_drag_mode) {
					wall_drag_mode = false;
					Vector3 drag_end_pos = get_ground_click_pos(mouse_pos);
					float dist = wall_drag_start_pos.distance_to(drag_end_pos);
					if (dist > 1.5f) {
						int seg_count = std::max(1, (int)(dist / 6.0f));
						Vector3 step_vec = (drag_end_pos - wall_drag_start_pos) / (float)seg_count;

						// Start node tower
						sim->spawn_building_world(wall_drag_start_pos.x, wall_drag_start_pos.z, rtso::BLD_TOWER, 0, 0.1f);

						for (int s = 0; s < seg_count; ++s) {
							Vector3 mid_p = wall_drag_start_pos + step_vec * ((float)s + 0.5f);
							sim->spawn_building_world(mid_p.x, mid_p.z, rtso::BLD_WALL_SEGMENT, 0, 0.1f);

							Vector3 end_p = wall_drag_start_pos + step_vec * ((float)s + 1.0f);
							if (s == seg_count - 1) {
								sim->spawn_building_world(end_p.x, end_p.z, rtso::BLD_TOWER, 0, 0.1f);
							}
						}
						play_voice("res://assets/sounds/voice/build_1.ogg");
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
							selected_ids.push_back((int32_t)uview.id[best_uidx]);
							play_voice("res://assets/sounds/voice/select_1.ogg");
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
								play_voice("res://assets/sounds/voice/select_3.ogg");
							}
						}
					}
				}
			}
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && mb->is_pressed()) {
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
				sim->cmd_set_rally_point(selected_building_id, click_pos.x, click_pos.z, target_res, target_bld);
				play_voice("res://assets/sounds/voice/select_2.ogg");
				return;
			}

			if (selected_ids.is_empty()) return;

			if (attack_move_mode) {
				sim->order_attack_move((const uint32_t *)selected_ids.ptr(), selected_ids.size(), click_pos.x, click_pos.z);
				play_voice("res://assets/sounds/voice/attack_1.ogg");
				attack_move_mode = false;
				return;
			}
			if (patrol_mode) {
				sim->order_patrol((const uint32_t *)selected_ids.ptr(), selected_ids.size(), click_pos.x, click_pos.z);
				play_voice("res://assets/sounds/voice/select_2.ogg");
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
					sim->order_guard((const uint32_t *)selected_ids.ptr(), selected_ids.size(), target_ally_id);
					play_voice("res://assets/sounds/voice/select_2.ogg");
				}
				guard_mode = false;
				return;
			}

			// 1. Right click on Enemy Unit (Attack)
			auto uview = sim->units();
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
				sim->order_attack((const uint32_t *)selected_ids.ptr(), selected_ids.size(), clicked_enemy_id);
				play_voice("res://assets/sounds/voice/attack_1.ogg");
				return;
			}

			// 2. Right click on Wildlife Fauna (Hunting)
			auto fview = sim->fauna();
			uint32_t clicked_fauna_id = 0;
			float best_fd2 = 2.5f * 2.5f;
			for (size_t f = 0; f < fview.count; ++f) {
				if (!fview.alive[f]) continue;
				float dx = fview.pos_x[f] - click_pos.x;
				float dz = fview.pos_z[f] - click_pos.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best_fd2) {
					best_fd2 = d2;
					clicked_fauna_id = fview.id[f];
				}
			}
			if (clicked_fauna_id != 0) {
				sim->order_hunt((const uint32_t *)selected_ids.ptr(), selected_ids.size(), clicked_fauna_id);
				play_voice("res://assets/sounds/voice/attack_1.ogg");
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
				sim->order_gather((const uint32_t *)selected_ids.ptr(), selected_ids.size(), clicked_res_id);
				play_voice("res://assets/sounds/voice/gather_1.ogg");
				return;
			}

			// 4. Right click on Building (Build / Repair / Garrison / Gate Upgrade)
			for (const auto &b : sim->buildings()) {
				if (b.team != 0) continue;
				float dx = b.x - click_pos.x;
				float dz = b.z - click_pos.z;
				float hit_r = b.half_size + 1.0f;
				if (dx * dx + dz * dz <= hit_r * hit_r) {
					if (b.build_progress < 1.0f || b.hp < b.max_hp) {
						sim->order_build_or_repair((const uint32_t *)selected_ids.ptr(), selected_ids.size(), b.id);
						play_voice("res://assets/sounds/voice/build_1.ogg");
					} else if (b.type == rtso::BLD_WALL_SEGMENT && Input::get_singleton()->is_key_pressed(KEY_CTRL)) {
						// Upgrade Wall to Gate
						sim->spawn_building_world(b.x, b.z, rtso::BLD_GATE, 0, 1.0f);
						play_voice("res://assets/sounds/voice/build_1.ogg");
					} else {
						// Garrison units inside building
						sim->order_garrison((const uint32_t *)selected_ids.ptr(), selected_ids.size(), b.id);
						clear_selection();
					}
					return;
				}
			}

			// 5. Default: Move Order
			sim->cmd_move((const uint32_t *)selected_ids.ptr(), selected_ids.size(), click_pos.x, click_pos.z);
			play_voice("res://assets/sounds/voice/move_1.ogg");
		}
	}

	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && dragging) {
		sel_rect->set_rect(drag_start, mm->get_position(), true);
	}
}

void RtsoMain::_on_map_selected(int index) {
	static const char *map_ids[] = {
		"acropolis_bay",
		"corinthian_isthmus",
		"atlas_valleys",
		"bactria",
		"battle_for_the_tiber",
		"default"
	};
	if (index >= 0 && index < 6) {
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
	return maps;
}

bool RtsoMain::load_map(String map_id) {
	if (!sim || !view) return false;
	clear_selection();

	if (map_id == "default" || map_id == "arena") {
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

void RtsoMain::set_civ(int civ_idx) {
	if (!sim) return;
	if (civ_idx < 0 || civ_idx >= rtso::CIV_COUNT) civ_idx = rtso::CIV_ATHENIANS;
	sim->set_player_civ((uint8_t)civ_idx);

	const auto &info = rtso::civ_info(civ_idx);
	if (civ_emblem) {
		civ_emblem->set_texture(load_tex(info.emblem_path));
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
