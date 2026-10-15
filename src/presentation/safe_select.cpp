#include "safe_select.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <algorithm>

using namespace godot;

void RtsoSafeSelect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_item", "text", "id"), &RtsoSafeSelect::add_item, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("add_icon_item", "icon", "text", "id"), &RtsoSafeSelect::add_icon_item, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("select", "index"), &RtsoSafeSelect::select);
	ClassDB::bind_method(D_METHOD("set_item_tooltip", "index", "tooltip"), &RtsoSafeSelect::set_item_tooltip);
	ClassDB::bind_method(D_METHOD("get_item_tooltip", "index"), &RtsoSafeSelect::get_item_tooltip);
	ClassDB::bind_method(D_METHOD("get_selected"), &RtsoSafeSelect::get_selected);
	ClassDB::bind_method(D_METHOD("get_item_id", "index"), &RtsoSafeSelect::get_item_id);
	ClassDB::bind_method(D_METHOD("get_item_text", "index"), &RtsoSafeSelect::get_item_text);
	ClassDB::bind_method(D_METHOD("get_item_index", "id"), &RtsoSafeSelect::get_item_index);
	ClassDB::bind_method(D_METHOD("get_item_count"), &RtsoSafeSelect::get_item_count);
	ClassDB::bind_method(D_METHOD("clear"), &RtsoSafeSelect::clear);
	ClassDB::bind_method(D_METHOD("_on_pressed"), &RtsoSafeSelect::_on_pressed);
	ClassDB::bind_method(D_METHOD("_on_list_selected", "index"), &RtsoSafeSelect::_on_list_selected);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "selected"), "select", "get_selected");
	ADD_SIGNAL(MethodInfo("item_selected", PropertyInfo(Variant::INT, "index")));
}

RtsoSafeSelect::RtsoSafeSelect() {
	set_text(" ");
	set_clip_text(true);
	set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	set_focus_mode(FOCUS_NONE);
	// Item icons (civ emblems are 256 px) stay the size of a text line.
	add_theme_constant_override("icon_max_width", 20);
	connect("pressed", Callable(this, "_on_pressed"));
}

RtsoSafeSelect::~RtsoSafeSelect() {}

void RtsoSafeSelect::ensure_menu() {
	if (menu) {
		return;
	}
	menu = memnew(PanelContainer);
	menu->set_visible(false);
	menu->set_as_top_level(true);
	menu->set_mouse_filter(MOUSE_FILTER_STOP);
	menu->set_z_index(64);
	Ref<StyleBoxFlat> sb;
	sb.instantiate();
	sb->set_bg_color(Color(0.08f, 0.09f, 0.12f, 0.98f));
	sb->set_border_width_all(1);
	sb->set_border_color(Color(0.45f, 0.42f, 0.32f, 1.0f));
	sb->set_content_margin_all(3);
	menu->add_theme_stylebox_override("panel", sb);
	add_child(menu);

	list = memnew(ItemList);
	list->set_allow_reselect(true);
	list->set_auto_height(false);
	list->set_fixed_icon_size(Vector2i(18, 18));
	list->connect("item_selected", Callable(this, "_on_list_selected"));
	menu->add_child(list);
}

void RtsoSafeSelect::add_item(const String &text, int id) {
	if (id < 0) {
		id = (int)items.size();
	}
	items.push_back({text, id, Ref<Texture2D>(), String()});
	if (selected < 0) {
		select(0);
	} else if (list && menu && menu->is_visible()) {
		rebuild_list();
	}
}

void RtsoSafeSelect::add_icon_item(const Ref<Texture2D> &icon, const String &text, int id) {
	if (id < 0) {
		id = (int)items.size();
	}
	items.push_back({text, id, icon, String()});
	if (selected < 0) {
		select(0);
	} else if (list && menu && menu->is_visible()) {
		rebuild_list();
	}
}

void RtsoSafeSelect::select(int index) {
	if (index < 0 || index >= (int)items.size()) {
		return;
	}
	selected = index;
	refresh_button();
}

void RtsoSafeSelect::set_item_tooltip(int index, const String &tooltip) {
	if (index < 0 || index >= (int)items.size()) {
		return;
	}
	items[(size_t)index].tooltip = tooltip;
}

String RtsoSafeSelect::get_item_tooltip(int index) const {
	if (index < 0 || index >= (int)items.size()) {
		return String();
	}
	return items[(size_t)index].tooltip;
}

String RtsoSafeSelect::get_item_text(int index) const {
	if (index < 0 || index >= (int)items.size()) {
		return String();
	}
	return items[(size_t)index].text;
}

int RtsoSafeSelect::get_item_id(int index) const {
	if (index < 0 || index >= (int)items.size()) {
		return 0;
	}
	return items[(size_t)index].id;
}

int RtsoSafeSelect::get_item_index(int id) const {
	for (size_t i = 0; i < items.size(); ++i) {
		if (items[i].id == id) {
			return (int)i;
		}
	}
	return -1;
}

void RtsoSafeSelect::clear() {
	items.clear();
	selected = -1;
	set_text(" ");
	set_button_icon(Ref<Texture2D>());
	close_menu();
}

void RtsoSafeSelect::refresh_button() {
	if (selected < 0 || selected >= (int)items.size()) {
		set_text(" ");
		set_button_icon(Ref<Texture2D>());
		return;
	}
	const Item &it = items[(size_t)selected];
	set_text(it.text);
	set_button_icon(it.icon);
}

void RtsoSafeSelect::rebuild_list() {
	if (!list) {
		return;
	}
	suppress = true;
	list->clear();
	for (const Item &it : items) {
		const int row = list->add_item(it.text, it.icon);
		list->set_item_tooltip_enabled(row, !it.tooltip.is_empty());
		list->set_item_tooltip(row, it.tooltip);
	}
	if (selected >= 0 && selected < (int)items.size()) {
		list->select(selected);
	}
	suppress = false;
}

void RtsoSafeSelect::open_menu() {
	if (items.empty() || is_disabled()) {
		return;
	}
	ensure_menu();
	rebuild_list();

	const float row = 22.0f;
	const float h = std::min((float)items.size() * row + 8.0f, 280.0f);
	const float w = std::max(get_size().x, 120.0f);
	Vector2 g = get_global_position();
	Vector2 sz = get_size();
	float y = g.y + sz.y;
	if (Viewport *vp = get_viewport()) {
		Vector2 vis = vp->get_visible_rect().size;
		if (y + h > vis.y - 8.0f) {
			y = std::max(8.0f, g.y - h);
		}
	}
	menu->set_size(Vector2(w, h));
	menu->set_global_position(Vector2(g.x, y));
	menu->set_visible(true);
	set_process_input(true);
}

void RtsoSafeSelect::close_menu() {
	if (menu) {
		menu->set_visible(false);
	}
	set_process_input(false);
}

void RtsoSafeSelect::_on_pressed() {
	if (!menu || !menu->is_visible()) {
		open_menu();
	} else {
		close_menu();
	}
}

void RtsoSafeSelect::_on_list_selected(int index) {
	if (suppress) {
		return;
	}
	select(index);
	close_menu();
	emit_signal("item_selected", index);
}

void RtsoSafeSelect::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE || p_what == NOTIFICATION_VISIBILITY_CHANGED) {
		if (!is_visible_in_tree()) {
			close_menu();
		}
	}
}

void RtsoSafeSelect::_input(const Ref<InputEvent> &event) {
	if (!menu || !menu->is_visible()) {
		return;
	}
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || !mb->is_pressed()) {
		return;
	}
	const Vector2 p = mb->get_global_position();
	const Rect2 menu_rect(menu->get_global_position(), menu->get_size());
	const Rect2 btn_rect(get_global_position(), get_size());
	if (!menu_rect.has_point(p) && !btn_rect.has_point(p)) {
		close_menu();
	}
}
