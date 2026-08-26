#include "main_node.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/engine.hpp>
#include "../core/diagnostics/logger.h"
#include "../core/diagnostics/perf_tracker.h"

using namespace godot;

void RtsoMain::_bind_methods() {
	ClassDB::bind_method(D_METHOD("enter_build_mode", "enable"), &RtsoMain::enter_build_mode);
	ClassDB::bind_method(D_METHOD("clear_selection"), &RtsoMain::clear_selection);
}

RtsoMain::RtsoMain() {
	sim = new rtso::Simulation();
}

RtsoMain::~RtsoMain() {
	delete sim;
}

void RtsoMain::_ready() {
	rtso::Logger::info("Main", "RTS0 C++ Native Main Ready — menus: main_menu/options/lobby/pause, anims: 1220 PSA 80 skeletons");

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
	ui_layer->add_child(minimap_view);

	hud_label = memnew(Label);
	hud_label->set_position(Vector2(200, 10));
	ui_layer->add_child(hud_label);

	// Ghost building mesh
	Ref<BoxMesh> box;
	box.instantiate();
	box->set_size(Vector3(1.8f, 1.8f, 1.8f));

	ghost_mat_ok.instantiate();
	ghost_mat_ok->set_albedo(Color(0.2f, 0.9f, 0.2f, 0.5f));
	ghost_mat_ok->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);

	ghost_mat_bad.instantiate();
	ghost_mat_bad->set_albedo(Color(0.9f, 0.2f, 0.2f, 0.5f));
	ghost_mat_bad->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);

	ghost_mesh = memnew(MeshInstance3D);
	ghost_mesh->set_mesh(box);
	ghost_mesh->set_visible(false);
	add_child(ghost_mesh);

	// Select initial villagers
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (uview.team[i] == 0) selected_ids.push_back((int32_t)uview.id[i]);
	}
}

void RtsoMain::_process(double delta) {
	if (paused_via_menu) return;
	rtso::PerfTracker::begin_frame();
	uint64_t start_us = Time::get_singleton()->get_ticks_usec();

	accumulator += delta;
	int ticks = 0;
	while (accumulator >= rtso::Simulation::FIXED_STEP) {
		sim->tick(rtso::Simulation::FIXED_STEP);
		accumulator -= rtso::Simulation::FIXED_STEP;
		ticks++;
		if (ticks > 8) break;
	}

	uint64_t end_us = Time::get_singleton()->get_ticks_usec();
	rtso::PerfTracker::record_tick((end_us - start_us) / 1000);
	rtso::PerfTracker::record_draw((int)sim->unit_count(), (int)sim->building_count());

	// animation state machine — mirrors 0ad UnitAnimation (idle/walk/attack/gather)
	{
		auto uview = sim->units();
		bool any_moving = false;
		for (size_t i=0;i<uview.count;++i) if (uview.vel_x[i]!=0 || uview.vel_z[i]!=0) { any_moving = true; break; }
		anim_ctrl.play(any_moving ? rtso::AnimState::Walk : rtso::AnimState::Idle);
		anim_ctrl.update((float)delta * 1000.0f);
	}

	view->sync(sim, (float)(accumulator / rtso::Simulation::FIXED_STEP), selected_ids);
	// TODO: view->sync_animations(anim_ctrl) — skeletal PSA driven via Skeleton3D
	minimap_view->sync(sim);

	String perf_txt = rtso::PerfTracker::get_report() +
		" | Tick US: " + String::num_int64(sim->stats().tick_us) +
		" | FPS: " + String::num_int64(Engine::get_singleton()->get_frames_per_second());
	debug_overlay->update_info(perf_txt);

	update_hud();

	if (build_mode && ghost_mesh) {
		Vector2 mouse_pos = get_viewport()->get_mouse_position();
		auto [cell_x, cell_y] = mouse_cell(mouse_pos);
		Vector3 center((cell_x + 0.5f) * rtso::Simulation::GRID, 0.9f, (cell_y + 0.5f) * rtso::Simulation::GRID);
		ghost_mesh->set_global_position(center);

		bool valid = sim->gold() >= rtso::Simulation::HOUSE_COST && sim->can_place(cell_x, cell_y, false);
		ghost_mesh->set_material_override(valid ? ghost_mat_ok : ghost_mat_bad);
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
	if (!hud_label) return;
	String txt = "Gold: " + String::num_int64(sim->gold()) +
		" | Units: " + String::num_int64(sim->unit_count()) +
		" | Wave: " + String::num_int64(sim->wave_number()) +
		" | [B] Build House (50g) | [RMB] Move/Gather";
	hud_label->set_text(txt);
}

void RtsoMain::enter_build_mode(bool enable) {
	build_mode = enable;
	if (ghost_mesh) ghost_mesh->set_visible(build_mode);
}

void RtsoMain::clear_selection() {
	selected_ids.clear();
}

void RtsoMain::_unhandled_input(const Ref<InputEvent> &event) {
	Ref<InputEventKey> key = event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if (key->get_keycode() == KEY_ESCAPE) {
			// ESC mirrors 0ad gui/session/Menu.xml — pause overlay
			Node *pause = get_node_or_null(NodePath("PauseMenu"));
			if (pause) {
				// call GDScript toggle()
				pause->call("toggle");
				paused_via_menu = !paused_via_menu;
				return;
			}
			if (build_mode) enter_build_mode(false);
			else clear_selection();
			return;
		}
		if (key->get_keycode() == KEY_B) {
			enter_build_mode(!build_mode);
			return;
		}
		if (key->get_keycode() == KEY_F10) {
			// quick options like 0ad splashscreen options
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
					if (sim->cmd_build_house(cell_x, cell_y)) {
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
						size_t best_idx = (size_t)-1;
						float best_d2 = 2.0f * 2.0f;
						for (size_t i = 0; i < uview.count; ++i) {
							if (!uview.alive[i] || uview.team[i] != 0) continue;
							float dx = uview.pos_x[i] - click_pos.x;
							float dz = uview.pos_z[i] - click_pos.z;
							float d2 = dx * dx + dz * dz;
							if (d2 < best_d2) {
								best_d2 = d2;
								best_idx = i;
							}
						}
						if (best_idx != (size_t)-1) {
							selected_ids.push_back((int32_t)uview.id[best_idx]);
						}
					} else {
						// Box selection
						Vector2 min_p(std::min(drag_start.x, drag_end.x), std::min(drag_start.y, drag_end.y));
						Vector2 max_p(std::max(drag_start.x, drag_end.x), std::max(drag_start.y, drag_end.y));

						auto uview = sim->units();
						selected_ids.clear();
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

			// Check if right clicked on resource
			auto rview = sim->resources();
			uint32_t clicked_res_id = 0;
			float best_rd2 = 2.0f * 2.0f;
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
			} else {
				sim->cmd_move((const uint32_t *)selected_ids.ptr(), selected_ids.size(), click_pos.x, click_pos.z);
			}
		}
	}

	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && dragging) {
		sel_rect->set_rect(drag_start, mm->get_position(), true);
	}
}
