#include "asset_viewer.h"
#include "../graphics/assets/model_library.h"
#include "../simulation/sim_types.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/animation.hpp>
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

void RtsoAssetViewer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_cat_selected", "index"), &RtsoAssetViewer::_on_cat_selected);
	ClassDB::bind_method(D_METHOD("_on_item_selected", "index"), &RtsoAssetViewer::_on_item_selected);
	ClassDB::bind_method(D_METHOD("_on_clip_selected", "index"), &RtsoAssetViewer::_on_clip_selected);
	ClassDB::bind_method(D_METHOD("_on_clip_box_selected", "index"), &RtsoAssetViewer::_on_clip_box_selected);
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

	cat_box = memnew(OptionButton);
	cat_box->add_item("Units");
	cat_box->add_item("Fauna");
	cat_box->add_item("Buildings");
	cat_box->add_item("Vegetation");
	cat_box->connect("item_selected", Callable(this, "_on_cat_selected"));
	lv->add_child(cat_box);

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
	chk_crowd->set_pressed(true);
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

	clip_box = memnew(OptionButton);
	clip_box->set_custom_minimum_size(Vector2(0, 32));
	clip_box->connect("item_selected", Callable(this, "_on_clip_box_selected"));
	rv->add_child(clip_box);

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
	if (cat == 0) {
		for (int t = 0; t < rtso::UNIT_TYPE_COUNT; ++t) {
			const auto &info = rtso::unit_type_info((uint8_t)t);
			CatalogItem it;
			it.cat = "units";
			it.id = info.model_id;
			it.label = String(info.name) + "  (" + String(info.model_id) + ")";
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
			it.id = info.model_id;
			it.label = String(info.name) + "  (" + String(info.model_id) + ")";
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
			preview_root->add_child(preview_scene);
			if (Node3D *n3 = Object::cast_to<Node3D>(preview_scene)) {
				n3->set_scale(Vector3(sc, sc, sc));
				n3->set_position(Vector3(-1.1f, e.y_offset, 0));
			}
			anim = find_anim(preview_scene);
			if (anim_ok()) {
				anim->set_active(true);
				if (speed_sl)
					anim->set_speed_scale((float)speed_sl->get_value());
			}
		}
	}

	if (e.crowd_mesh.is_valid() || e.mesh.is_valid()) {
		crowd_mi = memnew(MeshInstance3D);
		crowd_mi->set_mesh(e.crowd_mesh.is_valid() ? e.crowd_mesh : e.mesh);
		crowd_mi->set_scale(Vector3(sc, sc, sc));
		crowd_mi->set_position(Vector3(1.4f, e.y_offset, 0));
		preview_root->add_child(crowd_mi);
	}

	refresh_clips();
	apply_visibility();
	refresh_diag();
	frame_camera();
	if (clip_names.size() > 0)
		play_clip_at(0);
}

void RtsoAssetViewer::apply_visibility() {
	if (preview_scene) {
		if (Node3D *n3 = Object::cast_to<Node3D>(preview_scene))
			n3->set_visible(chk_full && chk_full->is_pressed());
	}
	if (crowd_mi)
		crowd_mi->set_visible(chk_crowd && chk_crowd->is_pressed());
}

void RtsoAssetViewer::refresh_clips() {
	clip_names.clear();
	clip_index = -1;
	if (clip_list)
		clip_list->clear();
	if (clip_box)
		clip_box->clear();

	if (!anim_ok()) {
		if (clip_list)
			clip_list->add_item("(no AnimationPlayer)");
		if (clip_box)
			clip_box->add_item("(no AnimationPlayer)");
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
		if (clip_box)
			clip_box->add_item(label);
	}
	if (clip_names.size() == 0) {
		if (clip_list)
			clip_list->add_item("(no clips)");
		if (clip_box)
			clip_box->add_item("(no clips)");
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
	if (clip_box && clip_box->get_item_count() == clip_names.size())
		clip_box->select(index);
	refresh_now_playing();
	_on_play();
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
	if (cur_id == "cavalry")
		text += "NOTE: cavalry.glb is a horse skeleton; rider is sit-baked (cavalry_idle) onto the rider joint.\n";

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
	accumulate_aabb(preview_scene, bb, any);
	accumulate_aabb(crowd_mi, bb, any);
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

void RtsoAssetViewer::_on_item_selected(int index) {
	if (index < 0 || (size_t)index >= items.size()) return;
	cur_cat = items[(size_t)index].cat;
	cur_id = items[(size_t)index].id;
	load_preview();
}

void RtsoAssetViewer::_on_clip_selected(int index) {
	play_clip_at(index);
}

void RtsoAssetViewer::_on_clip_box_selected(int index) {
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
	if (SceneTree *tree = get_tree())
		tree->change_scene_to_file("res://scenes/menu/main_menu.tscn");
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
