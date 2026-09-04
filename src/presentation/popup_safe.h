#pragma once

#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/popup.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace rtso {

// Godot 4.3 Popup::_deinitialize_visible_parents disconnects the root Window
// with an empty Callable, which prints
// "Attempt to disconnect a nonexistent connection ... focus_entered / tree_exited".
// LineEdit/SpinBox always own a hidden PopupMenu; tooltips use PopupPanel.
// Strip those nodes and never call Popup::hide() from here.

inline void strip_engine_popups(godot::Node *n) {
	if (n == nullptr) {
		return;
	}
	godot::TypedArray<godot::Node> found = n->find_children("*", "Popup", true, false);
	for (int i = 0; i < found.size(); ++i) {
		godot::Node *p = godot::Object::cast_to<godot::Node>(found[i]);
		if (!p) {
			continue;
		}
		if (godot::Node *par = p->get_parent()) {
			par->remove_child(p);
		}
		p->queue_free();
	}
}

inline void prepare_line_edit(godot::LineEdit *le) {
	if (le == nullptr) {
		return;
	}
	le->set_context_menu_enabled(false);
	le->set_middle_mouse_paste_enabled(false);
	strip_engine_popups(le);
}

inline void parent_line_edit(godot::Node *parent, godot::LineEdit *le) {
	prepare_line_edit(le);
	if (parent) {
		parent->add_child(le);
	}
	strip_engine_popups(le);
}

inline void hide_node_popups(godot::Node *n) {
	(void)n;
}

}  // namespace rtso
