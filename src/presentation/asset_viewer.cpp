#include "asset_viewer.h"
#include "popup_safe.h"
#include "../graphics/assets/model_library.h"
#include "../graphics/assets/unit_template_models.h"
#include "../simulation/sim_types.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/animation_library.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/plane_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <algorithm>
#include <cstdio>

using namespace godot;

// Preview models in player 1's colour (0 A.D. player_trans surfaces).
static void preview_player_color(Node *n) {
	const uint32_t rgb = rtso::PLAYER_COLORS[1];
	rtso::ModelLibrary::set_player_color(n, Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f));
}

void RtsoAssetViewer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_cat_selected", "index"), &RtsoAssetViewer::_on_cat_selected);
	ClassDB::bind_method(D_METHOD("_on_civ_selected", "index"), &RtsoAssetViewer::_on_civ_selected);
	ClassDB::bind_method(D_METHOD("_on_item_selected", "index"), &RtsoAssetViewer::_on_item_selected);
	ClassDB::bind_method(D_METHOD("_on_clip_selected", "index"), &RtsoAssetViewer::_on_clip_selected);
	ClassDB::bind_method(D_METHOD("_on_clip_clicked", "index", "at_position", "mouse_button_index"), &RtsoAssetViewer::_on_clip_clicked);
	ClassDB::bind_method(D_METHOD("_on_clip_prev"), &RtsoAssetViewer::_on_clip_prev);
	ClassDB::bind_method(D_METHOD("_on_clip_next"), &RtsoAssetViewer::_on_clip_next);
	ClassDB::bind_method(D_METHOD("_on_play"), &RtsoAssetViewer::_on_play);
	ClassDB::bind_method(D_METHOD("_on_pause"), &RtsoAssetViewer::_on_pause);
	ClassDB::bind_method(D_METHOD("_on_rest"), &RtsoAssetViewer::_on_rest);
	ClassDB::bind_method(D_METHOD("_on_back"), &RtsoAssetViewer::_on_back);
	ClassDB::bind_method(D_METHOD("_on_toggle_view", "pressed"), &RtsoAssetViewer::_on_toggle_view);
	ClassDB::bind_method(D_METHOD("_on_speed_changed", "v"), &RtsoAssetViewer::_on_speed_changed);
	ClassDB::bind_method(D_METHOD("_on_seek_started"), &RtsoAssetViewer::_on_seek_started);
	ClassDB::bind_method(D_METHOD("_on_seek_changed", "v"), &RtsoAssetViewer::_on_seek_changed);
}

RtsoAssetViewer::RtsoAssetViewer() {}
RtsoAssetViewer::~RtsoAssetViewer() {}

void RtsoAssetViewer::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE) {
		if (SceneTree *t = get_tree()) {
			rtso::hide_node_popups(t->get_root());
		} else {
			rtso::hide_node_popups(this);
		}
	}
}

AnimationPlayer *RtsoAssetViewer::find_anim(Node *n) {
	if (!n) return nullptr;
	if (AnimationPlayer *ap = Object::cast_to<AnimationPlayer>(n)) return ap;
	for (int i = 0; i < n->get_child_count(); ++i) {
		if (AnimationPlayer *ap = find_anim(n->get_child(i))) return ap;
	}
	return nullptr;
}

int RtsoAssetViewer::count_meshes(Node *n) {
	if (!n) return 0;
	int c = 0;
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) {
		if (mi->get_mesh().is_valid()) c = 1;
	}
	for (int i = 0; i < n->get_child_count(); ++i)
		c += count_meshes(n->get_child(i));
	return c;
}

void RtsoAssetViewer::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) return;
	build_stage();
	build_ui();
	rebuild_item_list();
	if (item_list->get_item_count() > 0) {
		item_list->select(0);
		_on_item_selected(0);
	}
}

void RtsoAssetViewer::build_stage() {
	Ref<Environment> env;
	env.instantiate();
	env->set_background(Environment::BG_COLOR);
	env->set_bg_color(Color(0.12f, 0.14f, 0.17f));
	env->set_ambient_light_color(Color(0.55f, 0.58f, 0.62f));
	env->set_ambient_light_energy(0.45f);
	WorldEnvironment *we = memnew(WorldEnvironment);
	we->set_environment(env);
	add_child(we);

	DirectionalLight3D *sun = memnew(DirectionalLight3D);
	sun->set_rotation_degrees(Vector3(-50, 35, 0));
	sun->set_shadow(true);
	add_child(sun);

	Ref<PlaneMesh> ground;
	ground.instantiate();
	ground->set_size(Vector2(40, 40));
	Ref<StandardMaterial3D> gmat;
	gmat.instantiate();
	gmat->set_albedo(Color(0.22f, 0.24f, 0.22f));
	MeshInstance3D *gmi = memnew(MeshInstance3D);
	gmi->set_mesh(ground);
	gmi->set_material_override(gmat);
	add_child(gmi);

	Ref<CylinderMesh> gizmo;
	gizmo.instantiate();
	gizmo->set_top_radius(0.04f);
	gizmo->set_bottom_radius(0.04f);
	gizmo->set_height(1.8f);
	Ref<StandardMaterial3D> gizmo_mat;
	gizmo_mat.instantiate();
	gizmo_mat->set_albedo(Color(0.2f, 0.85f, 0.4f, 0.55f));
	gizmo_mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	gizmo_mi = memnew(MeshInstance3D);
	gizmo_mi->set_mesh(gizmo);
	gizmo_mi->set_material_override(gizmo_mat);
	gizmo_mi->set_position(Vector3(-2.2f, 0.9f, 0));
	add_child(gizmo_mi);

	preview_root = memnew(Node3D);
	add_child(preview_root);

	cam = memnew(Camera3D);
	add_child(cam);
	cam->make_current();
	update_camera();
}

void RtsoAssetViewer::build_ui() {
	CanvasLayer *layer = memnew(CanvasLayer);
	add_child(layer);

	auto make_panel = []() -> Panel * {
		Panel *p = memnew(Panel);
		Ref<StyleBoxFlat> st;
		st.instantiate();
		st->set_bg_color(Color(0.07f, 0.08f, 0.11f, 0.92f));
		st->set_corner_radius_all(6);
		p->add_theme_stylebox_override("panel", st);
		return p;
	};

	Panel *left = make_panel();
	left->set_anchor(SIDE_LEFT, 0);
	left->set_anchor(SIDE_RIGHT, 0);
	left->set_anchor(SIDE_TOP, 0);
	left->set_anchor(SIDE_BOTTOM, 1);
	left->set_offset(SIDE_LEFT, 8);
	left->set_offset(SIDE_RIGHT, 280);
	left->set_offset(SIDE_TOP, 8);
	left->set_offset(SIDE_BOTTOM, -8);
	layer->add_child(left);

	VBoxContainer *lv = memnew(VBoxContainer);
	lv->set_anchors_preset(Control::PRESET_FULL_RECT);
	lv->set_offset(SIDE_LEFT, 8);
	lv->set_offset(SIDE_RIGHT, -8);
	lv->set_offset(SIDE_TOP, 8);
	lv->set_offset(SIDE_BOTTOM, -8);
	left->add_child(lv);

	Label *title = memnew(Label);
	title->set_text("Asset viewer");
	title->add_theme_font_size_override("font_size", 18);
	lv->add_child(title);

	cat_box = memnew(RtsoSafeSelect);
	cat_box->add_item("Units");
	cat_box->add_item("Fauna");
	cat_box->add_item("Buildings");
	cat_box->add_item("Vegetation");
	cat_box->connect("item_selected", Callable(this, "_on_cat_selected"));
	lv->add_child(cat_box);

	Label *civ_title = memnew(Label);
	civ_title->set_text("Civilization");
	civ_title->add_theme_font_size_override("font_size", 13);
	lv->add_child(civ_title);

	civ_box = memnew(RtsoSafeSelect);
	for (int c = 0; c < rtso::CIV_COUNT; ++c)
		civ_box->add_item(rtso::civ_info((uint8_t)c).name, c);
	civ_box->select(rtso::CIV_ATHENIANS);
	civ_box->connect("item_selected", Callable(this, "_on_civ_selected"));
	lv->add_child(civ_box);

	item_list = memnew(ItemList);
	item_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	item_list->connect("item_selected", Callable(this, "_on_item_selected"));
	lv->add_child(item_list);

	Button *back = memnew(Button);
	back->set_text("Back to menu");
	back->connect("pressed", Callable(this, "_on_back"));
	lv->add_child(back);

	Panel *mid = make_panel();
	mid->set_anchor(SIDE_LEFT, 0);
	mid->set_anchor(SIDE_RIGHT, 1);
	mid->set_anchor(SIDE_TOP, 1);
	mid->set_anchor(SIDE_BOTTOM, 1);
	mid->set_offset(SIDE_LEFT, 292);
	mid->set_offset(SIDE_RIGHT, -348);
	mid->set_offset(SIDE_TOP, -118);
	mid->set_offset(SIDE_BOTTOM, -8);
	layer->add_child(mid);

	VBoxContainer *mv = memnew(VBoxContainer);
	mv->set_anchors_preset(Control::PRESET_FULL_RECT);
	mv->set_offset(SIDE_LEFT, 8);
	mv->set_offset(SIDE_RIGHT, -8);
	mv->set_offset(SIDE_TOP, 6);
	mv->set_offset(SIDE_BOTTOM, -6);
	mid->add_child(mv);

	now_playing = memnew(Label);
	now_playing->set_text("No animation");
	now_playing->add_theme_font_size_override("font_size", 14);
	mv->add_child(now_playing);

	HBoxContainer *row = memnew(HBoxContainer);
	mv->add_child(row);
	Button *prev = memnew(Button);
	prev->set_text("< Prev");
	prev->connect("pressed", Callable(this, "_on_clip_prev"));
	row->add_child(prev);
	Button *play = memnew(Button);
	play->set_text("Play");
	play->connect("pressed", Callable(this, "_on_play"));
	row->add_child(play);
	Button *pause = memnew(Button);
	pause->set_text("Pause");
	pause->connect("pressed", Callable(this, "_on_pause"));
	row->add_child(pause);
	Button *rest = memnew(Button);
	rest->set_text("Rest");
	rest->connect("pressed", Callable(this, "_on_rest"));
	row->add_child(rest);
	Button *next = memnew(Button);
	next->set_text("Next >");
	next->connect("pressed", Callable(this, "_on_clip_next"));
	row->add_child(next);
	chk_loop = memnew(CheckBox);
	chk_loop->set_text("Loop");
	chk_loop->set_pressed(true);
	row->add_child(chk_loop);
	chk_full = memnew(CheckBox);
	chk_full->set_text("Full scene");
	chk_full->set_pressed(true);
	chk_full->connect("toggled", Callable(this, "_on_toggle_view"));
	row->add_child(chk_full);
	chk_crowd = memnew(CheckBox);
	chk_crowd->set_text("Crowd mesh");
	chk_crowd->set_pressed(false);
	chk_crowd->connect("toggled", Callable(this, "_on_toggle_view"));
	row->add_child(chk_crowd);

	HBoxContainer *row2 = memnew(HBoxContainer);
	mv->add_child(row2);
	Label *sp = memnew(Label);
	sp->set_text("Speed");
	sp->set_custom_minimum_size(Vector2(48, 0));
	row2->add_child(sp);
	speed_sl = memnew(HSlider);
	speed_sl->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	speed_sl->set_min(0.25);
	speed_sl->set_max(2.0);
	speed_sl->set_step(0.05);
	speed_sl->set_value(1.0);
	speed_sl->connect("value_changed", Callable(this, "_on_speed_changed"));
	row2->add_child(speed_sl);
	Label *sk = memnew(Label);
	sk->set_text("Seek");
	sk->set_custom_minimum_size(Vector2(40, 0));
	row2->add_child(sk);
	seek_sl = memnew(HSlider);
	seek_sl->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	seek_sl->set_min(0);
	seek_sl->set_max(1);
	seek_sl->set_step(0.01);
	seek_sl->connect("drag_started", Callable(this, "_on_seek_started"));
	seek_sl->connect("value_changed", Callable(this, "_on_seek_changed"));
	row2->add_child(seek_sl);

	Panel *right = make_panel();
	right->set_anchor(SIDE_LEFT, 1);
	right->set_anchor(SIDE_RIGHT, 1);
	right->set_anchor(SIDE_TOP, 0);
	right->set_anchor(SIDE_BOTTOM, 1);
	right->set_offset(SIDE_LEFT, -340);
	right->set_offset(SIDE_RIGHT, -8);
	right->set_offset(SIDE_TOP, 8);
	right->set_offset(SIDE_BOTTOM, -8);
	layer->add_child(right);

	VBoxContainer *rv = memnew(VBoxContainer);
	rv->set_anchors_preset(Control::PRESET_FULL_RECT);
	rv->set_offset(SIDE_LEFT, 10);
	rv->set_offset(SIDE_RIGHT, -10);
	rv->set_offset(SIDE_TOP, 8);
	rv->set_offset(SIDE_BOTTOM, -8);
	right->add_child(rv);

	Label *anim_title = memnew(Label);
	anim_title->set_text("Animations");
	anim_title->add_theme_font_size_override("font_size", 16);
	rv->add_child(anim_title);

	clip_list = memnew(ItemList);
	clip_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	clip_list->set_custom_minimum_size(Vector2(0, 160));
	clip_list->set_allow_reselect(true);
	clip_list->connect("item_selected", Callable(this, "_on_clip_selected"));
	clip_list->connect("item_clicked", Callable(this, "_on_clip_clicked"));
	rv->add_child(clip_list);

	diag = memnew(Label);
	diag->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	diag->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	diag->add_theme_font_size_override("font_size", 12);
	rv->add_child(diag);

	status = memnew(Label);
	status->set_anchor(SIDE_LEFT, 0);
	status->set_anchor(SIDE_RIGHT, 1);
	status->set_anchor(SIDE_TOP, 0);
	status->set_anchor(SIDE_BOTTOM, 0);
	status->set_offset(SIDE_LEFT, 292);
	status->set_offset(SIDE_RIGHT, -350);
	status->set_offset(SIDE_TOP, 8);
	status->set_offset(SIDE_BOTTOM, 36);
	status->set_text("LMB orbit  ·  wheel zoom  ·  [ ] prev/next clip  ·  Space play/pause  ·  1-9 jump  ·  F frame  ·  Esc");
	status->add_theme_font_size_override("font_size", 12);
	layer->add_child(status);
}

void RtsoAssetViewer::rebuild_item_list() {
	items.clear();
	item_list->clear();
	int cat = cat_box->get_selected();
	uint8_t civ = rtso::CIV_ATHENIANS;
	if (civ_box) {
		int id = civ_box->get_item_id(civ_box->get_selected());
		if (id >= 0 && id < rtso::CIV_COUNT)
			civ = (uint8_t)id;
		civ_box->set_disabled(cat != 0 && cat != 2);
	}
	const auto &cinfo = rtso::civ_info(civ);
	if (cat == 0) {
		for (int t = 0; t < rtso::UNIT_TYPE_COUNT; ++t) {
			if (!rtso::civ_can_train(civ, (uint8_t)t))
				continue;
			const auto &info = rtso::unit_type_info((uint8_t)t);
			CatalogItem it;
			it.cat = "units";
			it.id = rtso::civ_unit_model(civ, (uint8_t)t);
			it.label = String(info.name) + "  (" + String(cinfo.code) + "_" + String(info.model_id) + ")";
			items.push_back(it);
		}
		// The civ's roster templates drawn with their own 0 A.D. actor.
		const String prefix = String(cinfo.code) + "_";
		for (int k = 0; k < rtso::UNIT_TEMPLATE_DRAW_MODEL_COUNT; ++k) {
			const String id = rtso::unit_template_draw_model(k);
			if (!id.begins_with(prefix)) continue;
			CatalogItem it;
			it.cat = "units";
			it.id = id;
			it.label = id;
			items.push_back(it);
		}
	} else if (cat == 1) {
		for (int t = 0; t < rtso::FAUNA_COUNT; ++t) {
			const auto &info = rtso::fauna_type_info((uint8_t)t);
			CatalogItem it;
			it.cat = "units";
			it.id = info.model_id;
			it.label = String(info.name) + "  (" + String(info.model_id) + ")";
			items.push_back(it);
		}
	} else if (cat == 2) {
		for (int t = 0; t < rtso::BLD_TYPE_COUNT; ++t) {
			const auto &info = rtso::building_type_info((uint8_t)t);
			CatalogItem it;
			it.cat = "buildings";
			it.id = rtso::civ_building_model(civ, (uint8_t)t);
			it.label = String(info.name) + "  (" + String(cinfo.code) + "_" + String(info.model_id) + ")";
			items.push_back(it);
		}
		static const char *site[][2] = {
			{"foundation_3x3", "Foundation 3x3 (house)"},
			{"foundation_5x5", "Foundation 5x5 (farmstead)"},
			{"foundation_6x6", "Foundation 6x6 (barracks)"},
			{"foundation_8x8", "Foundation 8x8 (civic centre)"},
			{"foundation_4x6", "Foundation 4x6 (temple)"},
			{"foundation_3x3_tower", "Foundation tower"},
			{"foundation_wall", "Foundation wall"},
			{"foundation_dock", "Foundation dock"},
			{"scaffold_small", "Scaffold (small)"},
			{"scaffold_large", "Scaffold (large)"},
			{"rubble_small", "Rubble 3x3"},
			{"rubble_large", "Rubble 6x6"},
		};
		for (auto &row : site) {
			CatalogItem it;
			it.cat = "buildings";
			it.id = row[0];
			it.label = String(row[1]) + "  (" + String(row[0]) + ")";
			items.push_back(it);
		}
		{
			CatalogItem it;
			it.cat = "buildings";
			it.id = String(cinfo.code) + "_town_center_rubble";
			it.label = String(cinfo.name) + " civic-centre rubble  (" + it.id + ")";
			items.push_back(it);
		}
	} else {
		static const char *veg[][2] = {
			{"tree", "Broadleaf tree"},
			{"pine", "Pine"},
			{"palm", "Palm"},
			{"gold_vein", "Gold vein"},
			{"stone_mine", "Stone mine"},
		};
		for (auto &v : veg) {
			CatalogItem it;
			it.cat = "vegetation";
			it.id = v[0];
			it.label = String(v[1]) + "  (" + String(v[0]) + ")";
			items.push_back(it);
		}
	}
	for (const auto &it : items)
		item_list->add_item(it.label);
}

bool RtsoAssetViewer::anim_ok() const {
	return anim && UtilityFunctions::is_instance_valid(anim);
}

void RtsoAssetViewer::release_preview() {
	if (anim_ok())
		anim->stop();
	anim = nullptr;
	cur_clip = String();
	clip_names.clear();
	clip_index = -1;
	if (preview_scene) {
		if (preview_root && preview_scene->get_parent() == preview_root)
			preview_root->remove_child(preview_scene);
		preview_scene->queue_free();
		preview_scene = nullptr;
	}
	if (crowd_mi) {
		if (preview_root && crowd_mi->get_parent() == preview_root)
			preview_root->remove_child(crowd_mi);
		crowd_mi->queue_free();
		crowd_mi = nullptr;
	}
	if (site_foundation) {
		if (preview_root && site_foundation->get_parent() == preview_root)
			preview_root->remove_child(site_foundation);
		site_foundation->queue_free();
		site_foundation = nullptr;
	}
	if (site_scaffold) {
		if (preview_root && site_scaffold->get_parent() == preview_root)
			preview_root->remove_child(site_scaffold);
		site_scaffold->queue_free();
		site_scaffold = nullptr;
	}
	if (site_rubble) {
		if (preview_root && site_rubble->get_parent() == preview_root)
			preview_root->remove_child(site_rubble);
		site_rubble->queue_free();
		site_rubble = nullptr;
	}
}

void RtsoAssetViewer::load_preview() {
	release_preview();

	if (cur_id.is_empty()) return;
	const rtso::ModelLibrary::Entry &e = rtso::ModelLibrary::get(cur_cat, cur_id);
	float sc = e.scale > 0.05f ? e.scale : 1.0f;

	if (e.scene.is_valid()) {
		preview_scene = e.scene->instantiate();
		if (preview_scene) {
			rtso::ModelLibrary::apply_imported_material_fixups(preview_scene);
			preview_player_color(preview_scene);
			preview_root->add_child(preview_scene);
			Node3D *n3 = Object::cast_to<Node3D>(preview_scene);
			if (n3) {
				n3->set_scale(Vector3(sc, sc, sc));
				n3->set_position(Vector3(-1.1f, e.y_offset, 0));
			}
			anim = find_anim(preview_scene);
			if (cur_cat == "buildings" && n3) {
				bool site = cur_id.begins_with("foundation_") || cur_id.begins_with("scaffold_") ||
					cur_id.begins_with("rubble_") || cur_id.ends_with("_rubble");
				if (!site) {
					if (!anim) {
						anim = memnew(AnimationPlayer);
						n3->add_child(anim);
					}
					Ref<AnimationLibrary> lib;
					if (anim->has_animation_library(""))
						lib = anim->get_animation_library("");
					else {
						lib.instantiate();
						anim->add_animation_library("", lib);
					}
					if (lib.is_valid() && !lib->has_animation("Prebuild")) {
						float rise = std::max(2.0f, e.aabb.size.y * sc * 0.85f);
						Ref<Animation> idle;
						idle.instantiate();
						idle->set_length(1.0);
						idle->set_loop_mode(Animation::LOOP_LINEAR);
						int ti = idle->add_track(Animation::TYPE_SCALE_3D);
						idle->track_set_path(ti, NodePath("."));
						idle->track_insert_key(ti, 0.0, Vector3(sc, sc, sc));
						idle->track_insert_key(ti, 1.0, Vector3(sc, sc, sc));
						lib->add_animation("Idle", idle);

						Ref<Animation> pre;
						pre.instantiate();
						pre->set_length(1.0);
						pre->set_loop_mode(Animation::LOOP_LINEAR);
						int tpr = pre->add_track(Animation::TYPE_SCALE_3D);
						pre->track_set_path(tpr, NodePath("."));
						pre->track_insert_key(tpr, 0.0, Vector3(sc, sc, sc));
						lib->add_animation("Prebuild", pre);

						Ref<Animation> build;
						build.instantiate();
						build->set_length(2.0);
						build->set_loop_mode(Animation::LOOP_NONE);
						int ts = build->add_track(Animation::TYPE_SCALE_3D);
						build->track_set_path(ts, NodePath("."));
						build->track_insert_key(ts, 0.0, Vector3(sc, sc, sc));
						build->track_insert_key(ts, 2.0, Vector3(sc, sc, sc));
						int tp = build->add_track(Animation::TYPE_POSITION_3D);
						build->track_set_path(tp, NodePath("."));
						build->track_insert_key(tp, 0.0, Vector3(-1.1f, e.y_offset - rise, 0));
						build->track_insert_key(tp, 2.0, Vector3(-1.1f, e.y_offset, 0));
						lib->add_animation("Build", build);

						Ref<Animation> death;
						death.instantiate();
						death->set_length(2.0);
						death->set_loop_mode(Animation::LOOP_NONE);
						int tds = death->add_track(Animation::TYPE_SCALE_3D);
						death->track_set_path(tds, NodePath("."));
						death->track_insert_key(tds, 0.0, Vector3(sc, sc, sc));
						death->track_insert_key(tds, 0.7, Vector3(sc, sc, sc));
						int tdr = death->add_track(Animation::TYPE_ROTATION_3D);
						death->track_set_path(tdr, NodePath("."));
						death->track_insert_key(tdr, 0.0, Quaternion());
						death->track_insert_key(tdr, 0.22, Quaternion::from_euler(Vector3(0.06f, 0.12f, -0.04f)));
						death->track_insert_key(tdr, 0.70, Quaternion::from_euler(Vector3(0.28f, 0.55f, -0.16f)));
						int tdp = death->add_track(Animation::TYPE_POSITION_3D);
						death->track_set_path(tdp, NodePath("."));
						death->track_insert_key(tdp, 0.0, Vector3(-1.1f, e.y_offset, 0));
						death->track_insert_key(tdp, 0.70, Vector3(-1.1f, e.y_offset - rise * 0.95f, 0));
						death->track_insert_key(tdp, 2.0, Vector3(-1.1f, e.y_offset - rise * 0.95f, 0));
						lib->add_animation("Death", death);
					}
				}
			}
			if (anim_ok()) {
				anim->set_active(true);
				if (speed_sl)
					anim->set_speed_scale((float)speed_sl->get_value());
			}
		}
	}

	// Units only: optional crowd bake on the side. Buildings must not spawn a
	// second idle house — it sits on the plot and hides Prebuild/Build/Death.
	if (cur_cat != "buildings" && (e.crowd_mesh.is_valid() || e.mesh.is_valid())) {
		crowd_mi = memnew(MeshInstance3D);
		crowd_mi->set_mesh(e.crowd_mesh.is_valid() ? e.crowd_mesh : e.mesh);
		crowd_mi->set_scale(Vector3(sc, sc, sc));
		crowd_mi->set_position(Vector3(1.4f, e.y_offset, 0));
		crowd_mi->set_visible(false);
		preview_root->add_child(crowd_mi);
	}

	if (cur_cat == "buildings" && preview_root &&
			!cur_id.begins_with("foundation_") && !cur_id.begins_with("scaffold_") &&
			!cur_id.begins_with("rubble_") && !cur_id.ends_with("_rubble")) {
		uint8_t btype = rtso::BLD_HOUSE;
		String base = cur_id;
		for (int c = 0; c < rtso::CIV_COUNT; ++c) {
			String prefix = String(rtso::civ_info((uint8_t)c).code) + "_";
			if (cur_id.begins_with(prefix)) {
				base = cur_id.substr(prefix.length());
				break;
			}
		}
		for (int t = 0; t < rtso::BLD_TYPE_COUNT; ++t) {
			if (base == String(rtso::building_type_info((uint8_t)t).model_id)) {
				btype = (uint8_t)t;
				break;
			}
		}
		auto spawn_site = [&](const char *id) -> Node * {
			String sid = id;
			const rtso::ModelLibrary::Entry &se = rtso::ModelLibrary::get(String("buildings"), sid);
			Node *n = nullptr;
			if (se.scene.is_valid()) {
				n = se.scene->instantiate();
				rtso::ModelLibrary::apply_imported_material_fixups(n);
				preview_player_color(n);
			} else if (se.mesh.is_valid()) {
				MeshInstance3D *mi = memnew(MeshInstance3D);
				mi->set_mesh(se.mesh);
				n = mi;
			}
			if (!n) return nullptr;
			preview_root->add_child(n);
			if (Node3D *n3 = Object::cast_to<Node3D>(n)) {
				float s = se.scale > 0.05f ? se.scale : 1.0f;
				n3->set_scale(Vector3(s, s, s));
				n3->set_position(Vector3(-1.1f, se.y_offset, 0));
				n3->set_meta("rest_y", se.y_offset);
				n3->set_visible(false);
			}
			return n;
		};
		site_foundation = spawn_site(rtso::civ_building_foundation(btype));
		site_scaffold = spawn_site(rtso::civ_building_scaffold(btype));
		uint8_t rciv = 0;
		if (civ_box && civ_box->get_item_count() > 0)
			rciv = (uint8_t)civ_box->get_item_id(civ_box->get_selected());
		site_rubble = spawn_site(rtso::civ_building_rubble(rciv, btype));
	}

	refresh_clips();
	apply_visibility();
	refresh_diag();
	int start = 0;
	for (int i = 0; i < clip_names.size(); ++i) {
		if (clip_names[i] == "Prebuild") {
			start = i;
			break;
		}
	}
	if (clip_names.size() > 0)
		play_clip_at(start);
	frame_camera();
}

void RtsoAssetViewer::apply_visibility() {
	if (preview_scene) {
		if (Node3D *n3 = Object::cast_to<Node3D>(preview_scene))
			n3->set_visible(chk_full && chk_full->is_pressed());
	}
	if (crowd_mi)
		crowd_mi->set_visible(chk_crowd && chk_crowd->is_pressed());
	apply_building_clip_visuals();
}

void RtsoAssetViewer::apply_building_clip_visuals() {
	bool site_asset = cur_id.begins_with("foundation_") || cur_id.begins_with("scaffold_") ||
			cur_id.begins_with("rubble_") || cur_id.ends_with("_rubble");
	if (cur_cat != "buildings" || site_asset)
		return;
	bool pre = (cur_clip == "Prebuild" || cur_clip == "Foundation");
	bool building = (cur_clip == "Build");
	bool death = (cur_clip == "Death");
	float death_t = 0.0f;
	if (death && anim_ok() && anim->is_playing())
		death_t = (float)anim->get_current_animation_position();
	else if (death && anim_ok())
		death_t = (float)anim->get_current_animation_position();
	bool collapse = death && death_t < 0.70f;
	bool show_house = (collapse || (!pre && !death)) && (chk_full && chk_full->is_pressed());
	if (Node3D *n3 = Object::cast_to<Node3D>(preview_scene))
		n3->set_visible(show_house);
	if (crowd_mi)
		crowd_mi->set_visible(false);
	if (Node3D *n3 = Object::cast_to<Node3D>(site_foundation))
		n3->set_visible(pre || building);
	if (Node3D *n3 = Object::cast_to<Node3D>(site_scaffold))
		n3->set_visible(building);
	if (Node3D *n3 = Object::cast_to<Node3D>(site_rubble)) {
		bool show_rubble = death && death_t >= 0.55f;
		n3->set_visible(show_rubble);
		if (show_rubble) {
			float rest_y = n3->has_meta("rest_y") ? (float)n3->get_meta("rest_y") : n3->get_position().y;
			float sink = Math::clamp((death_t - 0.55f) / 1.45f, 0.0f, 1.0f);
			n3->set_position(Vector3(-1.1f, rest_y - sink * 1.15f, 0));
		}
	}
}

void RtsoAssetViewer::refresh_clips() {
	clip_names.clear();
	clip_index = -1;
	if (clip_list)
		clip_list->clear();

	if (!anim_ok()) {
		if (clip_list)
			clip_list->add_item("(no AnimationPlayer)");
		refresh_now_playing();
		return;
	}

	PackedStringArray names = anim->get_animation_list();
	for (int i = 0; i < names.size(); ++i) {
		String n = names[i];
		if (n.is_empty() || n == "RESET" || n.begins_with("("))
			continue;
		clip_names.push_back(n);
		Ref<Animation> a = anim->get_animation(n);
		char line[128];
		std::snprintf(line, sizeof(line), "%s   (%.2fs)",
				n.utf8().get_data(), a.is_valid() ? a->get_length() : 0.0);
		String label = String(line);
		if (clip_list)
			clip_list->add_item(label);
	}
	if (clip_names.size() == 0) {
		if (clip_list)
			clip_list->add_item("(no clips)");
	}
	refresh_now_playing();
}

void RtsoAssetViewer::refresh_now_playing() {
	if (!now_playing)
		return;
	if (clip_index < 0 || clip_index >= clip_names.size()) {
		now_playing->set_text(anim_ok() ? "Select an animation" : "No AnimationPlayer on this asset");
		return;
	}
	Ref<Animation> a = anim_ok() ? anim->get_animation(clip_names[clip_index]) : Ref<Animation>();
	char buf[160];
	std::snprintf(buf, sizeof(buf), "Playing  %s  ·  %.2fs  ·  %d / %d",
			clip_names[clip_index].utf8().get_data(),
			a.is_valid() ? a->get_length() : 0.0,
			clip_index + 1, clip_names.size());
	now_playing->set_text(String(buf));
}

void RtsoAssetViewer::play_clip_at(int index) {
	if (!anim_ok() || index < 0 || index >= clip_names.size())
		return;
	clip_index = index;
	cur_clip = clip_names[index];
	if (clip_list && clip_list->get_item_count() == clip_names.size())
		clip_list->select(index);
	refresh_now_playing();
	apply_building_clip_visuals();
	_on_play();
	frame_camera();
}

void RtsoAssetViewer::refresh_diag() {
	if (!diag) return;
	String path = String("res://assets/models/") + cur_cat + "/" + cur_id + ".glb";
	bool exists = FileAccess::file_exists(path);
	const rtso::ModelLibrary::Entry &e = rtso::ModelLibrary::get(cur_cat, cur_id);
	int full_parts = count_meshes(preview_scene);
	int crowd_surf = 0;
	AABB crowd_aabb;
	if (e.crowd_mesh.is_valid()) {
		crowd_surf = e.crowd_mesh->get_surface_count();
		crowd_aabb = e.crowd_mesh->get_aabb();
	} else if (e.mesh.is_valid()) {
		crowd_surf = e.mesh->get_surface_count();
		crowd_aabb = e.mesh->get_aabb();
	}

	char buf[1400];
	std::snprintf(buf, sizeof(buf),
			"%s / %s\n"
			"GLB: %s\n"
			"  exists=%s  imported_scene=%s\n"
			"entry.valid=%s  animated=%s\n"
			"scale=%.3f  y_offset=%.3f\n\n"
			"FULL SCENE (left)\n"
			"  mesh instances: %d\n\n"
			"CROWD MESH (right)\n"
			"  surfaces: %d\n"
			"  aabb h=%.2f  (%.2f,%.2f,%.2f)\n\n",
			cur_cat.utf8().get_data(), cur_id.utf8().get_data(),
			path.utf8().get_data(),
			exists ? "yes" : "NO FILE",
			e.scene.is_valid() ? "yes" : "no",
			e.valid ? "yes" : "NO",
			e.animated ? "yes" : "no",
			e.scale, e.y_offset,
			full_parts,
			crowd_surf,
			crowd_aabb.size.y,
			crowd_aabb.size.x, crowd_aabb.size.y, crowd_aabb.size.z);

	String text = String(buf);
	if (!exists)
		text += "FLAG: missing GLB — procedural fallback.\n";
	if (e.valid && !e.scene.is_valid())
		text += "FLAG: no PackedScene — capsule/box, not the actor.\n";
	if (full_parts >= 6 && crowd_surf == 1)
		text += "NOTE: crowd is 1 surface (MultiMesh). Compare left vs right for missing parts.\n";
	if (crowd_aabb.size.y > 0.01f && crowd_aabb.size.y < 0.45f)
		text += "FLAG: crowd AABB very short — mesh may be incomplete.\n";
	if (cur_id.ends_with("cavalry") || cur_id == "cavalry")
		text += "NOTE: cavalry GLB is a horse skeleton; the rider is a second skin driven by sit/attack clips.\n";
	if (civ_box && cat_box && (cat_box->get_selected() == 0 || cat_box->get_selected() == 2))
		text += String("Civ: ") + String(rtso::civ_info((uint8_t)civ_box->get_item_id(civ_box->get_selected())).name) + "\n";
	if (cur_cat == "buildings" && !cur_id.begins_with("foundation_") && !cur_id.begins_with("scaffold_") &&
			!cur_id.begins_with("rubble_") && !cur_id.ends_with("_rubble"))
		text += "Phases: Idle = finished. Prebuild = plot. Build = plot + rising. Death = collapse then rubble sink.\n";

	if (clip_names.size() > 0) {
		text += "\nUse the list or [ ] to change clips.\n";
	} else {
		text += "\nNo clips on this asset.\n";
	}
	text += "\nGreen pole = 1.8 m human height.";
	diag->set_text(text);
}

static void accumulate_aabb(Node *n, AABB &bb, bool &any) {
	if (!n) return;
	if (GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(n)) {
		AABB local = gi->get_aabb();
		if (Node3D *n3 = Object::cast_to<Node3D>(n))
			local = n3->get_global_transform().xform(local);
		if (!any) {
			bb = local;
			any = true;
		} else {
			bb = bb.merge(local);
		}
	}
	for (int i = 0; i < n->get_child_count(); ++i)
		accumulate_aabb(n->get_child(i), bb, any);
}

void RtsoAssetViewer::frame_camera() {
	AABB bb;
	bool any = false;
	if (preview_scene) {
		if (Node3D *n3 = Object::cast_to<Node3D>(preview_scene)) {
			if (n3->is_visible_in_tree())
				accumulate_aabb(preview_scene, bb, any);
		}
	}
	if (crowd_mi && crowd_mi->is_visible())
		accumulate_aabb(crowd_mi, bb, any);
	auto acc_vis = [&](Node *n) {
		if (Node3D *n3 = Object::cast_to<Node3D>(n)) {
			if (n3->is_visible())
				accumulate_aabb(n, bb, any);
		}
	};
	acc_vis(site_foundation);
	acc_vis(site_scaffold);
	acc_vis(site_rubble);
	if (!any) {
		orbit_target = Vector3(0, 0.9f, 0);
		orbit_dist = 8.0f;
	} else {
		orbit_target = bb.position + bb.size * 0.5f;
		orbit_dist = std::max(3.5f, bb.size.length() * 1.6f);
	}
	update_camera();
}

void RtsoAssetViewer::update_camera() {
	if (!cam) return;
	orbit_pitch = Math::clamp(orbit_pitch, 0.08f, 1.35f);
	orbit_dist = Math::clamp(orbit_dist, 1.5f, 40.0f);
	Vector3 off(
			orbit_dist * Math::cos(orbit_pitch) * Math::sin(orbit_yaw),
			orbit_dist * Math::sin(orbit_pitch),
			orbit_dist * Math::cos(orbit_pitch) * Math::cos(orbit_yaw));
	Vector3 pos = orbit_target + off;
	cam->set_global_position(pos);
	if (pos.distance_squared_to(orbit_target) > 0.0001f)
		cam->look_at(orbit_target);
}

void RtsoAssetViewer::_process(double delta) {
	(void)delta;
	if (cur_clip == "Death")
		apply_building_clip_visuals();
	if (!anim_ok() || seeking || !seek_sl) return;
	if (!anim->is_playing() || cur_clip.is_empty()) return;
	Ref<Animation> a = anim->get_animation(cur_clip);
	if (!a.is_valid() || a->get_length() <= 0.001) return;
	double t = anim->get_current_animation_position();
	seek_sl->set_value(t / a->get_length());
}

bool RtsoAssetViewer::pointer_over_ui() const {
	Viewport *vp = get_viewport();
	if (!vp)
		return false;
	Control *h = vp->gui_get_hovered_control();
	return h != nullptr;
}

void RtsoAssetViewer::_unhandled_input(const Ref<InputEvent> &event) {
	Ref<InputEventKey> key = event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if (key->get_keycode() == KEY_ESCAPE) {
			_on_back();
			return;
		}
		if (key->get_keycode() == KEY_F) {
			frame_camera();
			return;
		}
		if (key->get_keycode() == KEY_SPACE) {
			if (anim_ok() && anim->is_playing())
				_on_pause();
			else
				_on_play();
			return;
		}
		if (key->get_keycode() == KEY_BRACKETLEFT || key->get_keycode() == KEY_LEFT) {
			_on_clip_prev();
			return;
		}
		if (key->get_keycode() == KEY_BRACKETRIGHT || key->get_keycode() == KEY_RIGHT) {
			_on_clip_next();
			return;
		}
		if (key->get_keycode() >= KEY_1 && key->get_keycode() <= KEY_9) {
			play_clip_at((int)(key->get_keycode() - KEY_1));
			return;
		}
	}
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid()) {
		if (mb->get_button_index() == MOUSE_BUTTON_LEFT) {
			if (mb->is_pressed() && pointer_over_ui())
				return;
			dragging = mb->is_pressed();
		}
		if (mb->get_button_index() == MOUSE_BUTTON_WHEEL_UP && mb->is_pressed()) {
			orbit_dist *= 0.9f;
			update_camera();
		}
		if (mb->get_button_index() == MOUSE_BUTTON_WHEEL_DOWN && mb->is_pressed()) {
			orbit_dist *= 1.1f;
			update_camera();
		}
		return;
	}
	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && dragging) {
		Vector2 rel = mm->get_relative();
		orbit_yaw -= rel.x * 0.008f;
		orbit_pitch += rel.y * 0.008f;
		update_camera();
	}
}

void RtsoAssetViewer::_on_cat_selected(int index) {
	(void)index;
	rebuild_item_list();
	if (item_list->get_item_count() > 0) {
		item_list->select(0);
		_on_item_selected(0);
	}
}

static String unit_id_without_civ(const String &id) {
	for (int c = 0; c < rtso::CIV_COUNT; ++c) {
		String prefix = String(rtso::civ_info((uint8_t)c).code) + "_";
		if (id.begins_with(prefix))
			return id.substr(prefix.length());
	}
	return id;
}

void RtsoAssetViewer::_on_civ_selected(int index) {
	(void)index;
	String keep_base;
	int keep = item_list ? item_list->get_selected_items().size() : 0;
	if (keep > 0 && item_list->get_selected_items()[0] >= 0 &&
			(size_t)item_list->get_selected_items()[0] < items.size()) {
		keep_base = unit_id_without_civ(items[(size_t)item_list->get_selected_items()[0]].id);
	}
	rebuild_item_list();
	int pick = 0;
	if (!keep_base.is_empty()) {
		for (size_t i = 0; i < items.size(); ++i) {
			if (unit_id_without_civ(items[i].id) == keep_base) {
				pick = (int)i;
				break;
			}
		}
	}
	if (item_list->get_item_count() > 0) {
		item_list->select(pick);
		_on_item_selected(pick);
	}
}

void RtsoAssetViewer::_on_item_selected(int index) {
	if (index < 0 || (size_t)index >= items.size()) return;
	cur_cat = items[(size_t)index].cat;
	cur_id = items[(size_t)index].id;
	load_preview();
}

void RtsoAssetViewer::_on_clip_selected(int index) {
	play_clip_at(index);
}

void RtsoAssetViewer::_on_clip_clicked(int index, Vector2, int mouse_button_index) {
	if (mouse_button_index != MOUSE_BUTTON_LEFT)
		return;
	play_clip_at(index);
}

void RtsoAssetViewer::_on_clip_prev() {
	if (clip_names.size() == 0)
		return;
	int i = clip_index < 0 ? 0 : clip_index - 1;
	if (i < 0)
		i = clip_names.size() - 1;
	play_clip_at(i);
}

void RtsoAssetViewer::_on_clip_next() {
	if (clip_names.size() == 0)
		return;
	int i = clip_index < 0 ? 0 : clip_index + 1;
	if (i >= clip_names.size())
		i = 0;
	play_clip_at(i);
}

void RtsoAssetViewer::_on_play() {
	if (!anim_ok() || cur_clip.is_empty() || !anim->has_animation(cur_clip)) return;
	Ref<Animation> a = anim->get_animation(cur_clip);
	if (a.is_valid())
		a->set_loop_mode(chk_loop && chk_loop->is_pressed() ? Animation::LOOP_LINEAR : Animation::LOOP_NONE);
	anim->play(cur_clip);
	if (speed_sl)
		anim->set_speed_scale((float)speed_sl->get_value());
	refresh_now_playing();
}

void RtsoAssetViewer::_on_pause() {
	if (anim_ok()) anim->pause();
}

void RtsoAssetViewer::_on_rest() {
	if (!anim_ok()) return;
	anim->stop();
	if (!cur_clip.is_empty() && anim->has_animation(cur_clip))
		anim->seek(0.0, true);
}

void RtsoAssetViewer::_on_back() {
	if (SceneTree *tree = get_tree()) {
		rtso::hide_node_popups(tree->get_root());
		tree->change_scene_to_file("res://scenes/menu/main_menu.tscn");
	}
}

void RtsoAssetViewer::_on_toggle_view(bool) {
	apply_visibility();
}

void RtsoAssetViewer::_on_speed_changed(float v) {
	if (anim_ok()) anim->set_speed_scale(v);
}

void RtsoAssetViewer::_on_seek_started() {
	seeking = true;
}

void RtsoAssetViewer::_on_seek_changed(float v) {
	if (!anim_ok() || cur_clip.is_empty() || !anim->has_animation(cur_clip)) return;
	Ref<Animation> a = anim->get_animation(cur_clip);
	if (!a.is_valid()) return;
	anim->seek(v * a->get_length(), true);
	if (!Input::get_singleton()->is_mouse_button_pressed(MOUSE_BUTTON_LEFT))
		seeking = false;
}
