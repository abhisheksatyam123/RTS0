#pragma once

#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/popup.hpp>

namespace rtso {

inline void prepare_line_edit(godot::LineEdit *le) {
	if (le == nullptr) {
		return;
	}
	le->set_context_menu_enabled(false);
}

inline void parent_line_edit(godot::Node *parent, godot::LineEdit *le) {
	prepare_line_edit(le);
	if (parent) {
		parent->add_child(le);
	}
}

inline void hide_node_popups(godot::Node *n) {
	if (n == nullptr) {
		return;
	}
	if (auto *p = godot::Object::cast_to<godot::Popup>(n)) {
		p->hide();
	}
	for (int i = 0; i < n->get_child_count(); ++i) {
		hide_node_popups(n->get_child(i));
	}
}

}  // namespace rtso
