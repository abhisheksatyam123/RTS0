#ifndef RTSO_REFERENCE_DATA_H
#define RTSO_REFERENCE_DATA_H

// Game data for the reference pages (port of 0 A.D. gui/reference/civinfo,
// gui/reference/structree and gui/reference/viewer, GPL-2.0-or-later): each
// civ's team bonus, heroes with their auras, the structure tree (what every
// structure trains and researches, by phase) and the Template Viewer's stats
// for one unit, structure or technology, all built from the civ roster,
// building and tech tables the simulation uses, so the pages can't drift from
// the game. 0 A.D.'s tooltip and history text for the same entities is in
// data/reference/templates.json (tools/extract_0ad_reference.py).

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;

class RtsoReference : public RefCounted {
	GDCLASS(RtsoReference, RefCounted)

protected:
	static void _bind_methods();

public:
	// [{id, code, name, emblem}] in RTS0 civ id order.
	Array get_civs() const;
	// {name, code, emblem, bonus_text, team_bonuses: [{name, description}],
	//  heroes: [{name, specific_name, phase, cost, auras: [{name, description}]}]}
	Dictionary get_civ_info(int civ) const;
	// Every structure the civ can build, in building id order:
	// [{type, name, portrait, phase, cost, units: [{tmpl, name, specific_name, portrait, phase, cost, flags}],
	//   techs: [{id, name, description, phase, cost}], upgrades: [{type, name}]}].
	Array get_structure_tree(int civ) const;
	// Structure Tree "Trainer Units": the civ's units that train, research or upgrade,
	// [{tmpl, name, ..., units: [...], techs: [...], upgrades: [...]}].
	Array get_trainer_units(int civ) const;
	// Template Viewer (gui/reference/viewer ViewerPage): every stat RTS0 plays
	// with for a roster unit template, a structure (for a civ) or a technology.
	// Each has kind ("unit", "structure", "tech"), name, portrait, phase,
	// cost {food, wood, stone, metal}, cost_text, time; the rest by kind.
	Array get_catafalques() const;
	Dictionary get_unit(int tmpl) const;
	Dictionary get_fauna(int fauna_type) const;
	Dictionary get_structure(int type, int civ) const;
	Dictionary get_tech(int id, int civ) const;
	// Phase icon path (1 village, 2 town, 3 city).
	static String phase_icon(int phase);
};

#endif
