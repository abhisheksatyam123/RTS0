#include "main_node.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include "../core/diagnostics/logger.h"
#include "../core/diagnostics/perf_tracker.h"
#include "../core/job_system.h"
#include "../graphics/assets/model_library.h"

using namespace godot;

void RtsoMain::_bind_methods() {
	ClassDB::bind_method(D_METHOD("enter_build_mode", "enable", "btype"), &RtsoMain::enter_build_mode, DEFVAL(rtso::BLD_HOUSE));
	ClassDB::bind_method(D_METHOD("clear_selection"), &RtsoMain::clear_selection);
	ClassDB::bind_method(D_METHOD("_on_minimap_clicked", "world_pos"), &RtsoMain::_on_minimap_clicked);

	const StringName cls = get_class_static();
	auto bind_enum = [&](const char *enum_name, const char *name, int64_t value) {
		ClassDB::bind_integer_constant(cls, enum_name, name, value);
	};
	bind_enum("UnitType", "VILLAGER", rtso::UNIT_VILLAGER);
	bind_enum("UnitType", "SWORDSMAN", rtso::UNIT_SWORDSMAN);
	bind_enum("UnitType", "ARCHER", rtso::UNIT_ARCHER);
	bind_enum("UnitType", "SPEARMAN", rtso::UNIT_SPEARMAN);
	bind_enum("UnitType", "CAVALRY", rtso::UNIT_CAVALRY);
	bind_enum("UnitType", "NONE", rtso::UNIT_TYPE_NONE);

	bind_enum("BuildingType", "TOWN_CENTER", rtso::BLD_TOWN_CENTER);
	bind_enum("BuildingType", "HOUSE", rtso::BLD_HOUSE);
	bind_enum("BuildingType", "BARRACKS", rtso::BLD_BARRACKS);
	bind_enum("BuildingType", "STOREHOUSE", rtso::BLD_STOREHOUSE);
	bind_enum("BuildingType", "TOWER", rtso::BLD_TOWER);
	bind_enum("BuildingType", "FARMSTEAD", rtso::BLD_FARMSTEAD);

	bind_enum("UnitAction", "IDLE", rtso::ACTION_IDLE);
	bind_enum("UnitAction", "MOVE", rtso::ACTION_MOVE);
	bind_enum("UnitAction", "ATTACK", rtso::ACTION_ATTACK);
	bind_enum("UnitAction", "GATHER", rtso::ACTION_GATHER);
	bind_enum("UnitAction", "RETURN_DROPSITE", rtso::ACTION_RETURN_DROPSITE);
	bind_enum("UnitAction", "BUILD", rtso::ACTION_BUILD);
	bind_enum("UnitAction", "DYING", rtso::ACTION_DYING);

	bind_enum("UnitStance", "AGGRESSIVE", rtso::STANCE_AGGRESSIVE);
	bind_enum("UnitStance", "DEFENSIVE", rtso::STANCE_DEFENSIVE);
	bind_enum("UnitStance", "STAND_GROUND", rtso::STANCE_STAND_GROUND);
}

RtsoMain::RtsoMain() {
	sim = new rtso::Simulation();
}

RtsoMain::~RtsoMain() {
	delete sim;
}

void RtsoMain::_ready() {
	rtso::Logger::info("Main", "RTS0 0 A.D. Reborn Ready — Pure C++ GDExtension");

	anim_ctrl.load("res://assets/animations");
	sim->spawn_initial();

	cam_rig = memnew(RtsoCameraRig);
	add_child(cam_rig);

	view = memnew(RtsoWorldView);
	add_child(view);

	CanvasLayer *ui_layer = memnew(CanvasLayer);
	add_child(ui_layer);

	sel_rect = memnew(RtsoSelectionRect);
	ui_layer->add_child(sel_rect);

	debug_overlay = memnew(RtsoDebugOverlay);
	ui_layer->add_child(debug_overlay);

	minimap_view = memnew(RtsoMiniMapView);
	minimap_view->connect("minimap_clicked", Callable(this, "_on_minimap_clicked"));
	ui_layer->add_child(minimap_view);

	// 1. Top HUD Bar Panel (Food, Wood, Gold, Stone, Pop, Waves)
	top_hud_panel = memnew(Panel);
	top_hud_panel->set_position(Vector2(20, 12));
	top_hud_panel->set_size(Vector2(880, 42));

	Ref<StyleBoxFlat> top_style;
	top_style.instantiate();
	top_style->set_bg_color(Color(0.08f, 0.09f, 0.13f, 0.88f));
	top_style->set_corner_radius_all(6);
	top_style->set_border_width_all(1);
	top_style->set_border_color(Color(0.35f, 0.35f, 0.42f, 0.8f));
	top_hud_panel->add_theme_stylebox_override("panel", top_style);
	ui_layer->add_child(top_hud_panel);

	top_hud_label = memnew(Label);
	top_hud_label->set_position(Vector2(14, 8));
	top_hud_label->set_size(Vector2(850, 26));
	top_hud_label->add_theme_font_size_override("font_size", 14);
	top_hud_label->add_theme_color_override("font_color", Color(0.95f, 0.95f, 0.98f));
	top_hud_panel->add_child(top_hud_label);

	// 2. Bottom Inspector Panel (Selected Unit / Building Card)
	inspector_panel = memnew(Panel);
	inspector_panel->set_position(Vector2(220, 520));
	inspector_panel->set_size(Vector2(640, 150));

	Ref<StyleBoxFlat> insp_style;
	insp_style.instantiate();
	insp_style->set_bg_color(Color(0.09f, 0.10f, 0.14f, 0.90f));
	insp_style->set_corner_radius_all(8);
	insp_style->set_border_width_all(1);
	insp_style->set_border_color(Color(0.40f, 0.40f, 0.50f, 0.85f));
	inspector_panel->add_theme_stylebox_override("panel", insp_style);
	ui_layer->add_child(inspector_panel);

	inspector_title = memnew(Label);
	inspector_title->set_position(Vector2(16, 12));
	inspector_title->set_size(Vector2(600, 24));
	inspector_title->add_theme_font_size_override("font_size", 16);
	inspector_title->add_theme_color_override("font_color", Color(1.0f, 0.88f, 0.4f));
	inspector_panel->add_child(inspector_title);

	inspector_info = memnew(Label);
	inspector_info->set_position(Vector2(16, 40));
	inspector_info->set_size(Vector2(600, 48));
	inspector_info->add_theme_font_size_override("font_size", 13);
	inspector_info->add_theme_color_override("font_color", Color(0.85f, 0.85f, 0.92f));
	inspector_panel->add_child(inspector_info);

	inspector_actions = memnew(Label);
	inspector_actions->set_position(Vector2(16, 92));
	inspector_actions->set_size(Vector2(600, 44));
	inspector_actions->add_theme_font_size_override("font_size", 13);
	inspector_actions->add_theme_color_override("font_color", Color(0.4f, 0.95f, 0.6f));
	inspector_panel->add_child(inspector_actions);

	// 3. Victory / Defeat Overlay Banner
	banner_panel = memnew(Panel);
	banner_panel->set_position(Vector2(320, 240));
	banner_panel->set_size(Vector2(500, 160));
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
	banner_label->set_position(Vector2(20, 30));
	banner_label->set_size(Vector2(460, 100));
	banner_label->add_theme_font_size_override("font_size", 28);
	banner_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	banner_panel->add_child(banner_label);

	// 4. Background Music (0 A.D. Theme)
	if (ResourceLoader::get_singleton()->exists("res://assets/music/theme.ogg")) {
		Ref<AudioStream> stream = ResourceLoader::get_singleton()->load("res://assets/music/theme.ogg");
		if (stream.is_valid()) {
			music_player = memnew(AudioStreamPlayer);
			music_player->set_stream(stream);
			music_player->set_autoplay(true);
			music_player->set_volume_db(-6.0f);
			add_child(music_player);
			music_player->play();
		}
	}

	// 5. Ghost Building Preview Mesh
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

	// Select initial player units
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (uview.team[i] == 0) selected_ids.push_back((int32_t)uview.id[i]);
	}
}

void RtsoMain::_on_minimap_clicked(Vector2 world_pos) {
	if (cam_rig) {
		cam_rig->set_target_pos(Vector3(world_pos.x, 0.0f, world_pos.y));
	}
}

void RtsoMain::update_ghost_mesh() {
	if (!ghost_mesh) return;
	const char *mid = "house";
	if (current_build_type == rtso::BLD_BARRACKS) mid = "barracks";
	else if (current_build_type == rtso::BLD_TOWER) mid = "wall_tower";
	else if (current_build_type == rtso::BLD_STOREHOUSE) mid = "storehouse";
	else if (current_build_type == rtso::BLD_FARMSTEAD) mid = "farmstead";
	else if (current_build_type == rtso::BLD_TOWN_CENTER) mid = "town_center";

	const auto &bentry = rtso::ModelLibrary::get("buildings", mid);
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
	rtso::PerfTracker::begin_frame();
	uint64_t start_us = Time::get_singleton()->get_ticks_usec();

	static int auto_shot_frames = 0;
	if (++auto_shot_frames == 60) {
		Ref<Image> img = get_viewport()->get_texture()->get_image();
		if (img.is_valid()) {
			img->save_png("/home/abhi/project/RTS0/screen_game.png");
			print_line("[RTS0] Auto-screenshot frame 60 saved to /home/abhi/project/RTS0/screen_game.png");
		}
	}

	accumulator += delta;
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

	// Update Building Placement Ghost
	if (build_mode && ghost_mesh) {
		Vector2 mouse_pos = get_viewport()->get_mouse_position();
		auto [cell_x, cell_y] = mouse_cell(mouse_pos);
		int footprint = (current_build_type == rtso::BLD_TOWN_CENTER || current_build_type == rtso::BLD_BARRACKS) ? 2 : 1;

		float gx = (cell_x + footprint * 0.5f) * rtso::Simulation::GRID;
		float gz = (cell_y + footprint * 0.5f) * rtso::Simulation::GRID;
		ghost_mesh->set_global_position(Vector3(gx, 0.05f, gz));

		bool can_build = sim->can_place(cell_x, cell_y, footprint);
		ghost_mesh->set_material_override(can_build ? ghost_mat_ok : ghost_mat_bad);
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
	if (!top_hud_label) return;

	// Top Resource Bar
	String hud_txt = "🍞 Food: " + String::num_int64(sim->food()) +
		"  |  🌲 Wood: " + String::num_int64(sim->wood()) +
		"  |  🪙 Gold: " + String::num_int64(sim->gold()) +
		"  |  🪨 Stone: " + String::num_int64(sim->stone()) +
		"  |  👥 Pop: " + String::num_int64(sim->pop_current()) + "/" + String::num_int64(sim->pop_max()) +
		"  |  ⚔️ Wave " + String::num_int64(sim->wave_number()) +
		" (Next in " + String::num((double)sim->wave_time_remaining(), 0) + "s)";

	top_hud_label->set_text(hud_txt);

	// Bottom Inspector Card
	if (selected_building_id != 0) {
		const auto &bldgs = sim->buildings();
		const rtso::Simulation::Building *found_b = nullptr;
		for (const auto &b : bldgs) {
			if (b.id == selected_building_id) { found_b = &b; break; }
		}

		if (found_b) {
			String bname = "Town Center (Athenian)";
			if (found_b->type == rtso::BLD_HOUSE) bname = "House (+5 Population)";
			else if (found_b->type == rtso::BLD_BARRACKS) bname = "Barracks (Infantry / Ranged / Cavalry)";
			else if (found_b->type == rtso::BLD_TOWER) bname = "Defense Tower (Ballistic Arrows)";
			else if (found_b->type == rtso::BLD_STOREHOUSE) bname = "Storehouse (Wood/Gold/Stone Dropsite)";
			else if (found_b->type == rtso::BLD_FARMSTEAD) bname = "Farmstead (Food Dropsite)";

			inspector_title->set_text("🏛️ " + bname);

			String status = "HP: " + String::num((double)found_b->hp, 0) + " / " + String::num((double)found_b->max_hp, 0);
			if (found_b->build_progress < 1.0f) {
				status += " | 🔨 Under Construction (" + String::num((double)(found_b->build_progress * 100.0f), 0) + "%)";
			} else if (found_b->production_unit_type != rtso::UNIT_TYPE_NONE) {
				status += " | ⏳ Training " + String(rtso::unit_type_name(found_b->production_unit_type)) +
					" (" + String::num((double)found_b->production_timer, 1) + "s)";
			}
			inspector_info->set_text(status);

			if (found_b->type == rtso::BLD_TOWN_CENTER) {
				inspector_actions->set_text("Actions: [V] Train Villager (50 Food) | [B] Build Structures");
			} else if (found_b->type == rtso::BLD_BARRACKS) {
				inspector_actions->set_text("Actions: [S] Swordsman (60g,30w) | [A] Archer (40g,50w) | [C] Cavalry (80g,50f)");
			} else {
				inspector_actions->set_text("Passive structure | Right-click to repair if damaged.");
			}
			inspector_panel->set_visible(true);
			return;
		}
	}

	if (selected_ids.size() > 0) {
		auto uview = sim->units();
		int villager_cnt = 0, sword_cnt = 0, archer_cnt = 0, spear_cnt = 0, cav_cnt = 0;
		float total_hp = 0, total_max_hp = 0;

		for (int i = 0; i < selected_ids.size(); ++i) {
			uint32_t uid = (uint32_t)selected_ids[i];
			for (size_t u = 0; u < uview.count; ++u) {
				if (uview.id[u] == uid && uview.alive[u]) {
					total_hp += uview.hp[u];
					total_max_hp += uview.max_hp[u];
					if (uview.unit_type[u] == rtso::UNIT_VILLAGER) villager_cnt++;
					else if (uview.unit_type[u] == rtso::UNIT_SWORDSMAN) sword_cnt++;
					else if (uview.unit_type[u] == rtso::UNIT_ARCHER) archer_cnt++;
					else if (uview.unit_type[u] == rtso::UNIT_SPEARMAN) spear_cnt++;
					else if (uview.unit_type[u] == rtso::UNIT_CAVALRY) cav_cnt++;
					break;
				}
			}
		}

		String unit_title = "⚔️ Selected Army (" + String::num_int64(selected_ids.size()) + " Units)";
		if (selected_ids.size() == 1) {
			if (villager_cnt == 1) unit_title = "🧑 Villager (Worker / Builder)";
			else if (sword_cnt == 1) unit_title = "🗡️ Hoplite Swordsman (Melee)";
			else if (archer_cnt == 1) unit_title = "🏹 Cretan Archer (Ranged Ballistics)";
			else if (spear_cnt == 1) unit_title = "🛡️ Spearman (Anti-Cavalry)";
			else if (cav_cnt == 1) unit_title = "🐎 Cavalry Raider (Fast Flank)";
		}

		inspector_title->set_text(unit_title);
		inspector_info->set_text("Total HP: " + String::num((double)total_hp, 0) + " / " + String::num((double)total_max_hp, 0) +
			" | Composition: " + String::num_int64(villager_cnt) + " Villagers, " +
			String::num_int64(sword_cnt) + " Swords, " + String::num_int64(archer_cnt) + " Archers, " +
			String::num_int64(cav_cnt) + " Cavalry");

		if (villager_cnt > 0) {
			inspector_actions->set_text("Orders: [RMB] Move/Gather/Build | [B] Build Menu ([H] House, [R] Barracks, [T] Tower, [S] Storehouse)");
		} else {
			inspector_actions->set_text("Stances: [F1] Aggressive | [F2] Defensive | [F3] Stand Ground | [RMB] Attack / Move");
		}
		inspector_panel->set_visible(true);
	} else {
		inspector_panel->set_visible(false);
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
		sim->cmd_train(selected_building_id, utype);
		return;
	}
	// Fallback: search player's Town Center or Barracks
	for (const auto &b : sim->buildings()) {
		if (b.team == 0 && b.build_progress >= 1.0f) {
			if (utype == rtso::UNIT_VILLAGER && b.type == rtso::BLD_TOWN_CENTER && b.production_unit_type == rtso::UNIT_TYPE_NONE) {
				sim->cmd_train(b.id, utype);
				break;
			} else if (utype != rtso::UNIT_VILLAGER && b.type == rtso::BLD_BARRACKS && b.production_unit_type == rtso::UNIT_TYPE_NONE) {
				sim->cmd_train(b.id, utype);
				break;
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

		// F12 Screenshot
		if (key->get_keycode() == KEY_F12) {
			Ref<Image> img = get_viewport()->get_texture()->get_image();
			if (img.is_valid()) {
				img->save_png("/home/abhi/project/RTS0/screen_game.png");
				print_line("[RTS0] Screenshot saved to /home/abhi/project/RTS0/screen_game.png");
			}
			return;
		}

		// Build Hotkeys
		if (key->get_keycode() == KEY_B) {
			enter_build_mode(!build_mode, rtso::BLD_HOUSE);
			return;
		}
		if (build_mode) {
			if (key->get_keycode() == KEY_H) { enter_build_mode(true, rtso::BLD_HOUSE); return; }
			if (key->get_keycode() == KEY_R) { enter_build_mode(true, rtso::BLD_BARRACKS); return; }
			if (key->get_keycode() == KEY_T) { enter_build_mode(true, rtso::BLD_TOWER); return; }
			if (key->get_keycode() == KEY_S) { enter_build_mode(true, rtso::BLD_STOREHOUSE); return; }
			if (key->get_keycode() == KEY_F) { enter_build_mode(true, rtso::BLD_FARMSTEAD); return; }
		}

		// Unit Training Hotkeys
		if (key->get_keycode() == KEY_V) { try_train_unit(rtso::UNIT_VILLAGER); return; }
		if (key->get_keycode() == KEY_S && !build_mode) { try_train_unit(rtso::UNIT_SWORDSMAN); return; }
		if (key->get_keycode() == KEY_A) { try_train_unit(rtso::UNIT_ARCHER); return; }
		if (key->get_keycode() == KEY_C) { try_train_unit(rtso::UNIT_CAVALRY); return; }

		// Stances Hotkeys
		if (key->get_keycode() == KEY_F1) { sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), rtso::STANCE_AGGRESSIVE); return; }
		if (key->get_keycode() == KEY_F2) { sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), rtso::STANCE_DEFENSIVE); return; }
		if (key->get_keycode() == KEY_F3) { sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), rtso::STANCE_STAND_GROUND); return; }

		if (key->get_keycode() == KEY_F9) {
			sim->spawn_war(2000);
			print_line("[RTS0] Spawned 1000 vs 1000 war. Press F9 again to add another wave.");
			return;
		}

		if (key->get_keycode() == KEY_F10) {
			get_tree()->change_scene_to_file("res://scenes/menu/options.tscn");
			return;
		}
	}

	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid()) {
		Vector2 mouse_pos = mb->get_position();

		if (mb->get_button_index() == MOUSE_BUTTON_LEFT) {
			if (mb->is_pressed()) {
				if (build_mode) {
					auto [cell_x, cell_y] = mouse_cell(mouse_pos);
					if (sim->cmd_build(cell_x, cell_y, current_build_type)) {
						enter_build_mode(false);
					}
					return;
				}
				dragging = true;
				drag_start = mouse_pos;
				sel_rect->set_rect(drag_start, mouse_pos, true);
			} else {
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

						// 1. Check unit hit
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
						} else {
							// 2. Check building hit
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
						// Box selection (units only)
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
						}
					}
				}
			}
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && mb->is_pressed()) {
			if (selected_ids.size() == 0) return;
			Vector3 click_pos = get_ground_click_pos(mouse_pos);

			// 1. Check if right-clicked on Enemy Unit (Attack Order)
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
				return;
			}

			// 2. Check if right-clicked on Resource Node (Gather Order)
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
				return;
			}

			// 3. Check if right-clicked on Unfinished Building (Build / Repair Order)
			for (const auto &b : sim->buildings()) {
				if (b.team != 0) continue;
				float dx = b.x - click_pos.x;
				float dz = b.z - click_pos.z;
				float hit_r = b.half_size + 1.0f;
				if (dx * dx + dz * dz <= hit_r * hit_r) {
					sim->order_build_or_repair((const uint32_t *)selected_ids.ptr(), selected_ids.size(), b.id);
					return;
				}
			}

			// 4. Default: Move Order to clicked position
			sim->cmd_move((const uint32_t *)selected_ids.ptr(), selected_ids.size(), click_pos.x, click_pos.z);
		}
	}

	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && dragging) {
		sel_rect->set_rect(drag_start, mm->get_position(), true);
	}
}

