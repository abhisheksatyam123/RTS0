#pragma once

#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/popup.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/window.hpp>

namespace rtso {

// Godot 4.3 Window popups connect focus_entered / tree_exited on the root
// Window. Clearing FLAG_POPUP *after* enter_tree (or freeing while it is
// still set) disconnects with an empty callable. Only flip the flag while
// the popup is still outside the tree.

inline void prepare_popup_window(godot::Window *w) {
	if (w == nullptr || w->is_inside_tree()) {
		return;
	}
	w->set_exclusive(false);
	w->set_transient(false);
	w->set_flag(godot::Window::FLAG_NO_FOCUS, true);
	w->set_flag(godot::Window::FLAG_POPUP, false);
}

inline void prepare_option_button(godot::OptionButton *ob) {
	if (ob == nullptr || ob->is_inside_tree()) {
		return;
	}
	prepare_popup_window(ob->get_popup());
}

inline void prepare_line_edit(godot::LineEdit *le) {
	if (le == nullptr) {
		return;
	}
	le->set_context_menu_enabled(false);
}

inline void parent_option_button(godot::Node *parent, godot::OptionButton *ob) {
	prepare_option_button(ob);
	if (parent) {
		parent->add_child(ob);
	}
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
