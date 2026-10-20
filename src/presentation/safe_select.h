#ifndef RTSO_SAFE_SELECT_H
#define RTSO_SAFE_SELECT_H

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/string.hpp>
#include <vector>

using namespace godot;

// Dropdown that never creates a Window/Popup. Godot 4.3 OptionButton popups
// connect focus_entered / tree_exited on the root Window with callable_mp, then
// disconnect with an empty callable on hide/teardown.
class RtsoSafeSelect : public Button {
	GDCLASS(RtsoSafeSelect, Button);

	struct Item {
		String text;
		int id = 0;
		Ref<Texture2D> icon;
		String tooltip;
		Color color = Color(0, 0, 0, 0); // a=0: the theme's font colour
	};
	std::vector<Item> items;
	int selected = -1;
	bool suppress = false;
	bool tinted = false; // set_text_color overrides the font colours
	PanelContainer *menu = nullptr;
	ItemList *list = nullptr;

protected:
	static void _bind_methods();

public:
	RtsoSafeSelect();
	~RtsoSafeSelect();

	void add_item(const String &text, int id = -1);
	void add_icon_item(const Ref<Texture2D> &icon, const String &text, int id = -1);
	void select(int index);
	void set_item_tooltip(int index, const String &tooltip); // shown on hover in the open list
	godot::String get_item_tooltip(int index) const;
	// setStringTags(caption, {color}): the item's text colour, in the list and on the button.
	void set_item_color(int index, const Color &color);
	Color get_item_color(int index) const;
	int get_selected() const { return selected; }
	int get_item_id(int index) const;
	godot::String get_item_text(int index) const;
	int get_item_index(int id) const; // OptionButton.get_item_index: -1 when no item has the id
	int get_item_count() const { return (int)items.size(); }
	Ref<Texture2D> get_item_icon(int index) const { return index >= 0 && index < (int)items.size() ? items[index].icon : Ref<Texture2D>(); }
	void clear();

	void _notification(int p_what);
	void _input(const Ref<InputEvent> &event) override;

	void _on_pressed();
	void _on_list_selected(int index);

private:
	void ensure_menu();
	void refresh_button();
	void set_text_color(const Color &color);
	void open_menu();
	void close_menu();
	void rebuild_list();
};

#endif
