#ifndef RTSO_STATUS_SPRITES_H
#define RTSO_STATUS_SPRITES_H

// The status bar sprites of 0 A.D.'s StatusBars.js: health (RTS0's flat
// colours), experience, pack and upgrade progress, resource supply, the
// per-player capture bar, the icons of the auras reaching an entity, and its
// rank. Each is a camera-facing quad (CCmpOverlayRenderer sprites face the
// camera) built into one ImmediateMesh per texture every frame; only entities
// with status bars on, or reached by a shown entity's aura, get any.

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

using namespace godot;

class RtsoStatusSprites {
public:
	// StatusBars.js bar types: art/textures/ui/session/icons/<type>_{bg,fg}.png.
	enum Bar { BAR_PACK = 0, BAR_UPGRADE, BAR_SUPPLY, BAR_HEALTH, BAR_COUNT };

	void build(Node *parent) {
		mesh_.instantiate();
		inst_ = memnew(MeshInstance3D);
		inst_->set_mesh(mesh_);
		inst_->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		parent->add_child(inst_);
		static const char *kBars[BAR_COUNT] = { "archive/icons/pack", "archive/icons/upgrade", "archive/icons/supply", "icons/health" };
		for (int b = 0; b < BAR_COUNT; ++b) {
			bar_tex_[b][0] = texture(String("res://assets/textures/ui/") + kBars[b] + "_bg.png");
			bar_tex_[b][1] = texture(String("res://assets/textures/ui/") + kBars[b] + "_fg.png");
		}
		capture_tex_ = texture("res://assets/textures/ui/archive/icons/capture_bar.png");
		Ref<FileAccess> f = FileAccess::open("res://data/aura_icons.json", FileAccess::READ);
		if (f.is_valid()) {
			Dictionary icons = Dictionary(JSON::parse_string(f->get_as_text())).get("icons", Dictionary());
			Array keys = icons.keys();
			for (int i = 0; i < keys.size(); ++i)
				aura_icon_name_[String(keys[i]).utf8().get_data()] = String(icons[keys[i]]);
		}
	}

	// The icon texture index of a civ_hero_aura id, or -1 (no overlayIcon).
	int aura_icon(const char *aura_id) {
		auto it = aura_icon_name_.find(aura_id);
		if (it == aura_icon_name_.end()) return -1;
		return icon_index("res://assets/textures/ui/archive/auras/" + it->second + ".png");
	}

	// An icon texture by path (rank icons, aura icons).
	int icon_index(const String &path) {
		for (size_t i = 0; i < icon_names_.size(); ++i)
			if (icon_names_[i] == path) return (int)i;
		icon_names_.push_back(path);
		icon_tex_.push_back(texture(path));
		return (int)icon_tex_.size() - 1;
	}

	String icon_path(int idx) const { return idx >= 0 && idx < (int)icon_names_.size() ? icon_names_[idx] : String(); }

	void begin_frame(const Camera3D *cam) {
		if (cam) {
			const Basis b = cam->get_global_transform().basis;
			right_ = b.get_column(0).normalized();
			up_ = b.get_column(1).normalized();
		}
		for (auto &q : quads_) q.clear();
		sprites_ = 0;
	}

	// AddBar: the background, and the foreground up to `amount` (0..1), centred on `at`.
	void bar(Bar type, const Vector3 &at, float width, float height, float amount) {
		const float a = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
		quad(slot_of(bar_tex_[type][0]), at, -0.5f * width, 0.5f * width, -0.5f * height, 0.5f * height, Color(1, 1, 1));
		if (a > 0.0f)
			quad(slot_of(bar_tex_[type][1]), at, -0.5f * width, width * (a - 0.5f), -0.5f * height, 0.5f * height, Color(1, 1, 1));
	}

	// AddCaptureBar: one segment per player holding points, in their colour.
	void capture_part(const Vector3 &at, float width, float height, float from, float to, const Color &c) {
		if (to <= from) return;
		quad(slot_of(capture_tex_), at, -0.5f * width + from * width, -0.5f * width + to * width, -0.5f * height, 0.5f * height, c);
	}

	// AddAuraIcons: a square icon `size` wide centred `dx` to the side of `at`.
	void icon(int idx, const Vector3 &at, float dx, float size) {
		if (idx < 0 || idx >= (int)icon_tex_.size()) return;
		quad(slot_of(icon_tex_[idx]), at, dx - 0.5f * size, dx + 0.5f * size, -0.5f * size, 0.5f * size, Color(1, 1, 1));
	}

	void end_frame() {
		if (!inst_) return;
		mesh_->clear_surfaces();
		for (size_t s = 0; s < quads_.size(); ++s) {
			const auto &q = quads_[s];
			if (q.empty()) continue;
			mesh_->surface_begin(Mesh::PRIMITIVE_TRIANGLES, mats_[s]);
			for (const Vertex &v : q) {
				mesh_->surface_set_color(v.c);
				mesh_->surface_set_uv(v.uv);
				mesh_->surface_add_vertex(v.p);
			}
			mesh_->surface_end();
		}
	}

	// Sprites drawn this frame (tests).
	int sprite_count() const { return sprites_; }

	void cleanup() {
		for (int b = 0; b < BAR_COUNT; ++b) {
			bar_tex_[b][0].unref();
			bar_tex_[b][1].unref();
		}
		capture_tex_.unref();
		icon_tex_.clear();
		slot_tex_.clear();
		mats_.clear();
		quads_.clear();
	}

private:
	struct Vertex {
		Vector3 p;
		Vector2 uv;
		Color c;
	};

	static Ref<Texture2D> texture(const String &path) {
		ResourceLoader *rl = ResourceLoader::get_singleton();
		return rl->exists(path) ? Ref<Texture2D>(rl->load(path)) : Ref<Texture2D>();
	}

	// One material (and ImmediateMesh surface) per texture: drawn over the
	// world like 0 A.D.'s overlay sprites, tinted by the vertex colour.
	int slot_of(const Ref<Texture2D> &tex) {
		for (size_t i = 0; i < slot_tex_.size(); ++i)
			if (slot_tex_[i] == tex) return (int)i;
		Ref<StandardMaterial3D> m;
		m.instantiate();
		m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		m->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		m->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
		m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		m->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR);
		if (tex.is_valid()) m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, tex);
		m->set_render_priority(std::min(2 + (int)slot_tex_.size(), 100)); // backgrounds (asked for first) under foregrounds
		slot_tex_.push_back(tex);
		mats_.push_back(m);
		quads_.emplace_back();
		return (int)slot_tex_.size() - 1;
	}

	void quad(int slot, const Vector3 &at, float x0, float x1, float y0, float y1, const Color &c) {
		auto &q = quads_[slot];
		const Vector3 a = at + right_ * x0 + up_ * y0, b = at + right_ * x1 + up_ * y0;
		const Vector3 d = at + right_ * x0 + up_ * y1, e = at + right_ * x1 + up_ * y1;
		q.push_back({ a, Vector2(0, 1), c });
		q.push_back({ b, Vector2(1, 1), c });
		q.push_back({ e, Vector2(1, 0), c });
		q.push_back({ a, Vector2(0, 1), c });
		q.push_back({ e, Vector2(1, 0), c });
		q.push_back({ d, Vector2(0, 0), c });
		++sprites_;
	}

	Ref<ImmediateMesh> mesh_;
	MeshInstance3D *inst_ = nullptr;
	Vector3 right_ = Vector3(1, 0, 0), up_ = Vector3(0, 1, 0);
	Ref<Texture2D> bar_tex_[BAR_COUNT][2];
	Ref<Texture2D> capture_tex_;
	std::unordered_map<std::string, String> aura_icon_name_;
	std::vector<String> icon_names_;
	std::vector<Ref<Texture2D>> icon_tex_;
	std::vector<Ref<Texture2D>> slot_tex_;
	std::vector<Ref<StandardMaterial3D>> mats_;
	std::vector<std::vector<Vertex>> quads_;
	int sprites_ = 0;
};

#endif
