#ifndef RTSO_LOBBY_DIALOG_H
#define RTSO_LOBBY_DIALOG_H

// Multiplayer lobby panel: the room list, name, host, port and password
// fields and the Host / Join / Observe / Refresh / Start buttons, opened from
// the top-bar Lobby button. The leaf owns the widget tree only. The lobby
// logic (rooms, seats, handshake) stays on RtsoMain in session_lobby.cpp, and
// bind_signals() connects each button to its existing RtsoMain method by name,
// so the wire and handshake behaviour are unchanged.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include "popup_safe.h"

using namespace godot;

class RtsoLobbyDialog : public RefCounted {
	GDCLASS(RtsoLobbyDialog, RefCounted)

protected:
	static void _bind_methods() {
		ADD_SIGNAL(MethodInfo("host_requested"));
		ADD_SIGNAL(MethodInfo("join_requested"));
		ADD_SIGNAL(MethodInfo("start_requested"));
		ADD_SIGNAL(MethodInfo("refresh_requested"));
		ADD_SIGNAL(MethodInfo("observe_toggled", PropertyInfo(Variant::BOOL, "on")));
		ClassDB::bind_method(D_METHOD("press_host"), &RtsoLobbyDialog::press_host);
		ClassDB::bind_method(D_METHOD("press_join"), &RtsoLobbyDialog::press_join);
		ClassDB::bind_method(D_METHOD("press_start"), &RtsoLobbyDialog::press_start);
		ClassDB::bind_method(D_METHOD("press_refresh"), &RtsoLobbyDialog::press_refresh);
		ClassDB::bind_method(D_METHOD("toggle_observe", "on"), &RtsoLobbyDialog::toggle_observe);
	}

public:
	void press_host() { emit_signal("host_requested"); }
	void press_join() { emit_signal("join_requested"); }
	void press_start() { emit_signal("start_requested"); }
	void press_refresh() { emit_signal("refresh_requested"); }
	void toggle_observe(bool on) { emit_signal("observe_toggled", on); }

	// The widgets RtsoMain keeps pointers to (session_lobby.cpp reads and writes them).
	struct Widgets {
		Panel *panel = nullptr;
		LineEdit *name = nullptr;
		LineEdit *host = nullptr;
		LineEdit *port = nullptr;
		LineEdit *password = nullptr;
		ItemList *rooms = nullptr;
		Button *observe = nullptr;
		Button *start = nullptr;
		Label *status = nullptr;
		Label *players = nullptr;
		VBoxContainer *seats = nullptr;
	};

	// Builds the panel under parent (the UI layer), hidden until the Lobby button opens it.
	Widgets build(Node *parent) {
		w_.panel = memnew(Panel);
		w_.panel->set_visible(false);
		w_.panel->set_anchor(SIDE_LEFT, 0.0f);
		w_.panel->set_anchor(SIDE_RIGHT, 0.0f);
		w_.panel->set_anchor(SIDE_TOP, 0.0f);
		w_.panel->set_anchor(SIDE_BOTTOM, 0.0f);
		w_.panel->set_offset(SIDE_LEFT, 16.0f);
		w_.panel->set_offset(SIDE_RIGHT, 420.0f);
		w_.panel->set_offset(SIDE_TOP, 52.0f);
		w_.panel->set_offset(SIDE_BOTTOM, 320.0f);
		parent->add_child(w_.panel);

		VBoxContainer *box = memnew(VBoxContainer);
		box->set_anchors_preset(Control::PRESET_FULL_RECT);
		box->set_offset(SIDE_LEFT, 8);
		box->set_offset(SIDE_RIGHT, -8);
		box->set_offset(SIDE_TOP, 8);
		box->set_offset(SIDE_BOTTOM, -8);
		w_.panel->add_child(box);

		Label *title = memnew(Label);
		title->set_text("Multiplayer lobby");
		box->add_child(title);

		HBoxContainer *name_row = memnew(HBoxContainer);
		w_.name = memnew(LineEdit);
		w_.name->set_placeholder("Player name");
		w_.name->set_text("Player");
		w_.name->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		rtso::parent_line_edit(name_row, w_.name);
		w_.host = memnew(LineEdit);
		w_.host->set_placeholder("127.0.0.1");
		w_.host->set_text("127.0.0.1");
		w_.host->set_custom_minimum_size(Vector2(110, 0));
		rtso::parent_line_edit(name_row, w_.host);
		w_.port = memnew(LineEdit);
		w_.port->set_placeholder("20595");
		w_.port->set_text("20595");
		w_.port->set_custom_minimum_size(Vector2(70, 0));
		rtso::parent_line_edit(name_row, w_.port);
		w_.password = memnew(LineEdit);
		w_.password->set_placeholder("Password");
		w_.password->set_secret(true);
		w_.password->set_custom_minimum_size(Vector2(90, 0));
		rtso::parent_line_edit(name_row, w_.password);
		box->add_child(name_row);

		w_.rooms = memnew(ItemList);
		w_.rooms->set_custom_minimum_size(Vector2(0, 140));
		box->add_child(w_.rooms);

		HBoxContainer *actions = memnew(HBoxContainer);
		host_b_ = memnew(Button);
		host_b_->set_text("Host");
		actions->add_child(host_b_);
		join_b_ = memnew(Button);
		join_b_->set_text("Join");
		actions->add_child(join_b_);
		w_.observe = memnew(Button);
		w_.observe->set_text("Observe");
		w_.observe->set_toggle_mode(true);
		w_.observe->set_tooltip_text("Watch instead of playing: host or join a match (or one in progress) as an observer");
		actions->add_child(w_.observe);
		refresh_b_ = memnew(Button);
		refresh_b_->set_text("Refresh");
		actions->add_child(refresh_b_);
		w_.start = memnew(Button);
		w_.start->set_text("Start");
		w_.start->set_tooltip_text("Host: regenerate map with a fresh seed and broadcast map+seed (NMT_GAME_START)");
		actions->add_child(w_.start);
		box->add_child(actions);

		w_.status = memnew(Label);
		w_.status->set_text("Lobby HTTP 127.0.0.1:8088 - run tools/lobby_server.py");
		box->add_child(w_.status);
		w_.players = memnew(Label);
		w_.players->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		box->add_child(w_.players);
		w_.seats = memnew(VBoxContainer);
		box->add_child(w_.seats);
		return w_;
	}

	// Each button emits one of the leaf's signals; bind_signals() sends every
	// signal to the RtsoMain method that handles it, the one it always called.
	void bind_signals(Object *owner) {
		host_b_->connect("pressed", Callable(this, "press_host"));
		join_b_->connect("pressed", Callable(this, "press_join"));
		w_.observe->connect("toggled", Callable(this, "toggle_observe"));
		refresh_b_->connect("pressed", Callable(this, "press_refresh"));
		w_.start->connect("pressed", Callable(this, "press_start"));
		connect("host_requested", Callable(owner, "_on_lobby_host_pressed"));
		connect("join_requested", Callable(owner, "_on_lobby_join_pressed"));
		connect("start_requested", Callable(owner, "_on_lobby_start_pressed"));
		connect("refresh_requested", Callable(owner, "_on_lobby_refresh_pressed"));
		connect("observe_toggled", Callable(owner, "_on_lobby_observe_toggled"));
	}

	const Widgets &widgets() const { return w_; }

private:
	Widgets w_;
	Button *host_b_ = nullptr;
	Button *join_b_ = nullptr;
	Button *refresh_b_ = nullptr;
};

#endif
