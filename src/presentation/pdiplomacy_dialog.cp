#include "diplomacy_dialog.h"

#include <godot_cpp/core/class_db.hpp>

namespace godot {

void RtsoDiplomacyDialog::_bind_methods() {
	ADD_SIGNAL(MethodInfo("stance_changed", PropertyInfo(Variant::INT, "stance"), PropertyInfo(Variant::INT, "player_idx")));
	ADD_SIGNAL(MethodInfo("tribute_sent", PropertyInfo(Variant::INT, "player_idx"), PropertyInfo(Variant::INT, "resource_type"), PropertyInfo(Variant::INT, "amount")));
	ADD_SIGNAL(MethodInfo("allied_victory_toggled", PropertyInfo(Variant::BOOL, "enabled")));
	ADD_SIGNAL(MethodInfo("dialog_closed"));
}

} // namespace godot
