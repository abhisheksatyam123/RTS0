#ifndef RTSO_REFERENCE_DATA_H
#define RTSO_REFERENCE_DATA_H

// Game data for the reference pages (port of 0 A.D. gui/reference/civinfo and
// gui/reference/structree, GPL-2.0-or-later): each civ's team bonus, heroes
// with their auras, and the structure tree (what every structure trains and
// researches, by phase) built from the civ roster, building and tech tables
// the simulation uses, so the pages can't drift from the game.

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
	// [{type, name, portrait, phase, cost, units: [{name, specific_name, phase, cost, flags}],
	//   techs: [{name, description, phase, cost}]}], structures in building id order.
	Array get_structure_tree(int civ) const;
};

#endif
