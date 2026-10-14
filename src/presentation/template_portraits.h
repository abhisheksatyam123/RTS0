#pragma once

// 0 A.D. 0.28's session portraits (Identity/Icon) for the selection panel, the selection
// grid and the production queue: each roster unit, civ structure and Gaia entity shows its
// template's own icon from art/textures/ui/session/portraits, copied to
// assets/textures/ui/archive/portraits by tools/extract_0ad_portraits.py. The icon names come
// from data/reference/templates.json (tools/extract_0ad_reference.py). Where a template has
// no icon, RTS0's class portrait (assets/textures/ui/portraits) stands in.

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>

#include "../simulation/simulation.h"

#ifndef RTSO_UI_LOAD_TEXTURE
#define RTSO_UI_LOAD_TEXTURE
namespace rtso {
inline godot::Ref<godot::Texture2D> load_ui_texture(const godot::String &path) {
	if (godot::ResourceLoader::get_singleton()->exists(path)) {
		godot::Ref<godot::Texture2D> tex = godot::ResourceLoader::get_singleton()->load(path);
		if (tex.is_valid()) return tex;
	}
	return godot::Ref<godot::Texture2D>();
}
} // namespace rtso
#endif

namespace rtso {
namespace portraits {

inline const godot::Dictionary &reference() {
	using namespace godot;
	static Dictionary all;
	static bool loaded = false;
	if (!loaded) {
		loaded = true;
		Ref<FileAccess> f = FileAccess::open("res://data/reference/templates.json", FileAccess::READ);
		if (f.is_valid()) all = Dictionary(JSON::parse_string(f->get_as_text()));
	}
	return all;
}

// An Identity/Icon path ("units/athen/infantry_spearman.png"), or an empty texture.
inline godot::Ref<godot::Texture2D> icon(const godot::String &path) {
	if (path.is_empty()) return godot::Ref<godot::Texture2D>();
	return load_ui_texture("res://assets/textures/ui/archive/portraits/" + path);
}

inline godot::Ref<godot::Texture2D> class_portrait(const char *portrait_id) {
	return load_ui_texture(godot::String("res://assets/textures/ui/portraits/") + (portrait_id ? portrait_id : "") + ".png");
}

inline godot::Dictionary section_entry(const char *section, const godot::String &key) {
	const godot::Dictionary sec = reference().get(section, godot::Dictionary());
	return sec.get(key, godot::Dictionary());
}
inline godot::String section_icon(const char *section, const godot::String &key) {
	return section_entry(section, key).get("icon", godot::String());
}

// The templates.json entries (generic_name, tooltip, classes, icon...) of an entity's template.
inline godot::Dictionary unit_entry(uint16_t tmpl) {
	if (tmpl >= CIV_UNIT_TEMPLATE_COUNT) return godot::Dictionary();
	return section_entry("units", godot::String::utf8(civ_unit_template(tmpl).id));
}
inline godot::Dictionary structure_entry(uint8_t civ, uint8_t type) {
	if (civ >= CIV_COUNT) return godot::Dictionary();
	const godot::Dictionary by_civ = godot::Dictionary(reference().get("structures", godot::Dictionary()))
			.get(godot::String(civ_info(civ).code), godot::Dictionary());
	return by_civ.get(godot::String::num_int64(type), godot::Dictionary());
}
inline godot::Dictionary fauna_entry(uint8_t fauna_type) {
	return section_entry("gaia", godot::String("gaia/") + fauna_type_info(fauna_type).template_name);
}
inline godot::Dictionary treasure_entry(uint8_t treasure_type) {
	const godot::Dictionary e = section_entry("gaia", godot::String("gaia/treasure/") + treasure_type_info(treasure_type).template_name);
	return e.is_empty() ? section_entry("gaia", "template_gaia_treasure") : e;
}
// A resource entity's Gaia template: every gaia/tree and gaia/fruit template inherits
// template_gaia_tree's or template_gaia_fruit's icon; mines template_gaia_ore (metal) or
// template_gaia_rock (stone); ruins template_gaia_ruins. Fish, carcasses and fields have none here.
inline godot::String resource_key(uint8_t type, uint8_t subtype, uint8_t flora) {
	switch (subtype) {
	case RES_SUB_RUINS: return "template_gaia_ruins";
	case RES_SUB_FISH: case RES_SUB_MEAT: case RES_SUB_GRAIN: return godot::String();
	default: break;
	}
	switch (type) {
	case RES_WOOD: return "template_gaia_tree";
	case RES_GOLD: return "template_gaia_ore";
	case RES_STONE: return "template_gaia_rock";
	default: break;
	}
	const bool fruit_tree = flora >= FLORA_TEMPLATE_FIRST ? flora_template_info(flora - FLORA_TEMPLATE_FIRST).fruit : true;
	return fruit_tree ? "template_gaia_fruit" : "template_gaia_tree";
}
inline godot::Dictionary resource_entry(uint8_t type, uint8_t subtype, uint8_t flora) {
	const godot::String key = resource_key(type, subtype, flora);
	return key.is_empty() ? godot::Dictionary() : section_entry("gaia", key);
}

// A roster unit's icon, else its class portrait.
inline godot::Ref<godot::Texture2D> unit(uint16_t tmpl, uint8_t type) {
	godot::Ref<godot::Texture2D> t = icon(unit_entry(tmpl).get("icon", godot::String()));
	if (t.is_valid()) return t;
	return class_portrait(unit_type_portrait(type < UNIT_TYPE_COUNT ? type : 0));
}

// A civ's structure of this BuildingType, else the type's portrait.
inline godot::Ref<godot::Texture2D> structure(uint8_t civ, uint8_t type) {
	godot::Ref<godot::Texture2D> t = icon(structure_entry(civ, type).get("icon", godot::String()));
	if (t.is_valid()) return t;
	return class_portrait(building_type_info(type).portrait_id);
}

// A Gaia template's icon ("gaia/fauna_deer", "gaia/treasure/wood", "template_gaia_tree").
inline godot::Ref<godot::Texture2D> gaia(const godot::String &template_name) {
	return icon(section_icon("gaia", template_name));
}

inline godot::Ref<godot::Texture2D> fauna(uint8_t fauna_type) {
	godot::Ref<godot::Texture2D> t = icon(fauna_entry(fauna_type).get("icon", godot::String()));
	return t.is_valid() ? t : icon("gaia/fauna_generic.png");
}

inline godot::Ref<godot::Texture2D> treasure(uint8_t treasure_type) {
	return icon(treasure_entry(treasure_type).get("icon", godot::String()));
}

// A resource entity: its Gaia template's icon (resource_key); fish and carcasses the fish and
// generic animal icons; a field its structure.
inline godot::Ref<godot::Texture2D> resource(uint8_t type, uint8_t subtype, uint8_t flora) {
	switch (subtype) {
	case RES_SUB_FISH: return icon("gaia/fauna_fish.png");
	case RES_SUB_MEAT: return icon("gaia/fauna_generic.png");
	case RES_SUB_GRAIN: return icon("structures/field.png");
	default: return gaia(resource_key(type, subtype, flora));
	}
}

} // namespace portraits
} // namespace rtso
