#ifndef RTSO_CHAT_WINDOW_H
#define RTSO_CHAT_WINDOW_H

// P4 stage-1 leaf widget: in-game chat overlay (0 A.D.
// gui/session/chat/ChatWindow.js + ChatBounce.js parity). Owns the panel
// tree, the addressee/channel row and the NMT_CHAT send path. Signal
// targets stay on the bound RtsoMain methods (main_node.h), and all
// net/audio/sim access goes through RtsoChatHost — wire bytes unchanged.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <godot_cpp/classes/time.hpp>

#include <deque>
#include <string>

#include "popup_safe.h"
#include "safe_select.h"

using namespace godot;

// Hooks the chat widget needs from the RtsoMain coordinator. The first two
// are existing RtsoMain methods (declared in main_node.h) overriding these
// directly; the last two are thin inline forwards to the existing sim/net
// path, so no protocol behavior changes.
class RtsoChatHost {
public:
	virtual ~RtsoChatHost() = default;
	virtual bool net_lockstep() const = 0; // existing RtsoMain hook
	virtual void play_sfx(const char *path) = 0; // existing RtsoMain hook
	virtual String chat_civ_name() = 0; // sim->player_civ() display name
	// The sender's name in their player colour as bbcode (0 A.D. colorizePlayernameByGUID);
	// an empty guid is the local player.
	virtual String chat_user(const String &sender_guid) = 0;
	// -> NetHost::sendChat (net_host.cpp:327), 0 A.D. NMT_CHAT parity.
	virtual void chat_send_wire(const std::string &wire) = 0;
	// 0 A.D. Cheats.executeCheat: true when `text` was a cheat code (it is not sent as chat).
	virtual bool chat_try_cheat(const String &text) = 0;
};

class RtsoChatWindow {
public:
	// owner: signal target object (RtsoMain, whose bound methods receive the
	// connections below). parent: ui CanvasLayer. host: coordinator hooks.
	void build(Object *owner, Node *parent, RtsoChatHost *host) {
		host_ = host;
		// =========================================================================
		// 5. IN-GAME CHAT OVERLAY (LineEdit + All/Allies/Team channels)
		// =========================================================================
		panel_ = memnew(Panel);
		panel_->set_anchor(SIDE_LEFT, 0.0f);
		panel_->set_anchor(SIDE_TOP, 0.0f);
		panel_->set_offset(SIDE_LEFT, 16.0f);
		panel_->set_offset(SIDE_RIGHT, 400.0f);
		panel_->set_offset(SIDE_TOP, 60.0f);
		panel_->set_offset(SIDE_BOTTOM, 260.0f);

		Ref<StyleBoxFlat> chat_style;
		chat_style.instantiate();
		chat_style->set_bg_color(Color(0.06f, 0.07f, 0.10f, 0.94f));
		chat_style->set_corner_radius_all(6);
		chat_style->set_border_width_all(1);
		chat_style->set_border_color(Color(0.35f, 0.38f, 0.48f, 0.85f));
		panel_->add_theme_stylebox_override("panel", chat_style);

		VBoxContainer *chat_vbox = memnew(VBoxContainer);
		chat_vbox->set_anchor(SIDE_LEFT, 0.0f);
		chat_vbox->set_anchor(SIDE_RIGHT, 1.0f);
		chat_vbox->set_anchor(SIDE_TOP, 0.0f);
		chat_vbox->set_anchor(SIDE_BOTTOM, 1.0f);
		chat_vbox->set_offset(SIDE_LEFT, 8.0f);
		chat_vbox->set_offset(SIDE_RIGHT, -8.0f);
		chat_vbox->set_offset(SIDE_TOP, 6.0f);
		chat_vbox->set_offset(SIDE_BOTTOM, -6.0f);
		chat_vbox->add_theme_constant_override("separation", 4);
		panel_->add_child(chat_vbox);

		HBoxContainer *chat_top_bar = memnew(HBoxContainer);
		chat_top_bar->set_custom_minimum_size(Vector2(0, 24));
		Label *chat_title = memnew(Label);
		chat_title->set_text(""); // 0.28's chat window has no title
		chat_title->add_theme_font_size_override("font_size", 12);
		chat_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		chat_top_bar->add_child(chat_title);

		Button *chat_close = memnew(Button);
		chat_close->set_custom_minimum_size(Vector2(22, 22));
		chat_close->set_text("✕");
		chat_close->add_theme_font_size_override("font_size", 10);
		chat_close->connect("pressed", Callable(owner, "toggle_chat"));
		chat_top_bar->add_child(chat_close);
		chat_vbox->add_child(chat_top_bar);

		log_ = memnew(RichTextLabel);
		log_->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		log_->set_use_bbcode(true);
		log_->set_scroll_follow(true);
		log_->add_theme_font_size_override("normal_font_size", 11);
		chat_vbox->add_child(log_);

		// Chat addressee selector row above chat input (0 A.D. gui/session/chat/ChatWindow.js:35-58 parity)
		HBoxContainer *chat_addressee_bar = memnew(HBoxContainer);
		chat_addressee_bar->set_custom_minimum_size(Vector2(0, 24));

		Label *to_lbl = memnew(Label);
		to_lbl->set_text("To:");
		to_lbl->add_theme_font_size_override("font_size", 10);
		to_lbl->add_theme_color_override("font_color", Color(0.75f, 0.78f, 0.88f));
		chat_addressee_bar->add_child(to_lbl);

		channel_ = memnew(RtsoSafeSelect);
		channel_->set_custom_minimum_size(Vector2(110, 22));
		channel_->add_item("All", 0);
		channel_->add_item("Allies", 1);
		channel_->add_item("Enemies", 2);
		for (int p = 1; p <= 8; ++p) {
			channel_->add_item("Player-" + String::num_int64(p), 2 + p);
		}
		channel_->add_theme_font_size_override("font_size", 10);
		chat_addressee_bar->add_child(channel_);

		auto add_quick_btn = [&](const String &label, int ch_idx) {
			Button *b = memnew(Button);
			b->set_custom_minimum_size(Vector2(42, 20));
			b->set_text(label);
			b->add_theme_font_size_override("font_size", 9);
			b->connect("pressed", Callable(owner, "_on_chat_channel_btn_pressed").bind(ch_idx));
			chat_addressee_bar->add_child(b);
		};
		add_quick_btn("All", 0);
		add_quick_btn("Ally", 1);
		add_quick_btn("Enemy", 2);

		chat_vbox->add_child(chat_addressee_bar);

		HBoxContainer *chat_input_bar = memnew(HBoxContainer);
		chat_input_bar->set_custom_minimum_size(Vector2(0, 26));
		input_ = memnew(LineEdit);
		input_->set_placeholder("Type message...");
		input_->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		input_->add_theme_font_size_override("font_size", 11);
		input_->connect("text_submitted", Callable(owner, "_on_chat_text_submitted"));
		rtso::parent_line_edit(chat_input_bar, input_);

		send_btn_ = memnew(Button);
		send_btn_->set_custom_minimum_size(Vector2(55, 24));
		send_btn_->set_text("Send");
		send_btn_->add_theme_font_size_override("font_size", 11);
		send_btn_->connect("pressed", Callable(owner, "_on_chat_send_pressed"));
		chat_input_bar->add_child(send_btn_);
		chat_vbox->add_child(chat_input_bar);

		panel_->set_visible(false);
		parent->add_child(panel_);

		// 0 A.D. gui/session/chat/ChatOverlay.js: while the chat window is closed,
		// the last 20 chat and notification lines show over the map for 30 s.
		overlay_ = memnew(RichTextLabel);
		overlay_->set_anchor(SIDE_LEFT, 0.0f);
		overlay_->set_anchor(SIDE_TOP, 0.0f);
		overlay_->set_offset(SIDE_LEFT, 16.0f);
		overlay_->set_offset(SIDE_RIGHT, 520.0f);
		overlay_->set_offset(SIDE_TOP, 64.0f);
		overlay_->set_offset(SIDE_BOTTOM, 64.0f + 20 * 18.0f);
		overlay_->set_use_bbcode(true);
		overlay_->set_scroll_active(false);
		overlay_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		overlay_->add_theme_font_size_override("normal_font_size", 13);
		overlay_->add_theme_color_override("font_outline_color", Color(0, 0, 0, 1));
		overlay_->add_theme_constant_override("outline_size", 4);
		parent->add_child(overlay_);
	}

	bool is_ready() const { return log_ != nullptr; }
	bool is_open() const { return panel_ != nullptr && panel_->is_visible(); }
	void close() {
		if (panel_) panel_->set_visible(false);
	}

	void toggle() {
		if (!panel_) return;
		bool vis = !panel_->is_visible();
		panel_->set_visible(vis);
		if (vis && input_) {
			input_->grab_focus();
		}
	}

	void select_channel(int channel_idx) {
		if (channel_) {
			channel_->select(channel_idx);
		}
	}

	// Send path: local echo + NMT_CHAT broadcast (channel travels as wire
	// tag, ChatBounce.js:15-38 parity — byte-identical to pre-split code).
	void send_pressed() {
		if (!host_ || !input_ || !log_) return;
		String text = input_->get_text().strip_edges();
		if (!text.is_empty() && host_->chat_try_cheat(text)) {
			input_->clear();
			if (panel_) panel_->set_visible(false);
			return;
		}
		if (!text.is_empty()) {
			int ch = channel_ ? channel_->get_selected() : 0;
			String wire_tag = "";
			String context;
			if (ch == 1) {
				wire_tag = "[Allies] ";
				context = "Ally";
			} else if (ch == 2) {
				wire_tag = "[Enemies] ";
				context = "Enemy";
			} else if (ch >= 3) {
				wire_tag = "[Player-" + String::num_int64(ch - 2) + "] ";
				context = "Private";
			}
			const String line = format_line(context, host_->chat_user(""), text);
			log_->append_text(stamp() + line + "\n");
			post_overlay(stamp() + line);
			input_->clear();
			host_->play_sfx("res://assets/sounds/ui/chat.ogg");
			// Network: 0 A.D. NMT_CHAT broadcast (NetHost::sendChat net_host.cpp:327).
			// Channel travels as wire tag (0 A.D. gui/session/chat/ChatBounce.js:15-38 parity).
			// Parsed client-side in onChat display without breaking protocol compatibility.
			if (host_->net_lockstep()) {
				std::string wire = (wire_tag + text).utf8().get_data();
				host_->chat_send_wire(wire);
			}
		}
		if (panel_) panel_->set_visible(false);
	}

	// Incoming NMT_CHAT display: channel-tag parse + RichTextLabel append
	// (ChatBounce.js parity). No-op until build() has run.
	void append_incoming(const String &sender_guid, const String &raw_msg) {
		if (!log_) return;
		String msg = raw_msg;
		// The wire tag is the addressee; 0.28 shows it as the context (ChatAddressees.js).
		String context;
		if (msg.begins_with("[Allies]")) {
			context = "Ally";
			msg = msg.substr(8).strip_edges();
		} else if (msg.begins_with("[Team]")) {
			context = "Ally";
			msg = msg.substr(6).strip_edges();
		} else if (msg.begins_with("[Enemies]")) {
			context = "Enemy";
			msg = msg.substr(9).strip_edges();
		} else if (msg.begins_with("[Player-")) {
			int close_b = msg.find("]");
			if (close_b > 0) {
				context = "Private";
				msg = msg.substr(close_b + 1).strip_edges();
			}
		}
		const String line = stamp() + format_line(context, host_ ? host_->chat_user(sender_guid) : sender_guid, msg);
		log_->append_text(line + String("\n"));
		post_overlay(line);
	}

	// Raw bbcode line (Petra log, diplomacy/tribute feedback).
	void append_line(const String &bbcode) {
		const String line = bbcode.is_empty() ? bbcode : stamp() + bbcode;
		if (log_) log_->append_text(line);
		post_overlay(line.strip_edges(false, true));
	}

	// Test hooks: the log as plain text, and the input line.
	String log_text() const { return log_ ? log_->get_parsed_text() : String(); }
	LineEdit *input() const { return input_; }

	// 0 A.D. chat.timestamp: each line opens with the local time, "[HH:MM]".
	void set_timestamps(bool on) { timestamps_ = on; }

	// Drops overlay lines older than 30 s; hides the overlay while the chat
	// window (which shows the full history) is open. Call once per frame.
	void tick_overlay() {
		if (!overlay_) return;
		const uint64_t now = Time::get_singleton()->get_ticks_msec();
		bool changed = false;
		while (!overlay_lines_.empty() && now - overlay_lines_.front().second >= kOverlayTimeoutMs) {
			overlay_lines_.pop_front();
			changed = true;
		}
		if (changed) rebuild_overlay();
		overlay_->set_visible(!is_open() && !overlay_lines_.empty());
	}

private:
	// ChatMessageFormatPlayer strings: "(context) <user> message", or with /me
	// "(context) * user message". The text is escaped (escapeText).
	static String format_line(const String &context, const String &user, String text) {
		const bool me = text.begins_with("/me ");
		if (me) text = text.substr(4);
		text = text.replace("[", "[lb]");
		const String ctx = context.is_empty() ? String() : "(" + context + ") ";
		return ctx + (me ? "* " + user : "<" + user + ">") + " " + text;
	}

	String stamp() const {
		if (!timestamps_) return String();
		const Dictionary t = Time::get_singleton()->get_time_dict_from_system();
		return "[color=#aaaaaa][lb]" + String::num_int64((int64_t)t["hour"]).pad_zeros(2) + ":" +
				String::num_int64((int64_t)t["minute"]).pad_zeros(2) + "][/color] ";
	}

	static constexpr size_t kOverlayLines = 20; // ChatOverlay.js chatLinesNumber
	static constexpr uint64_t kOverlayTimeoutMs = 30000; // ChatOverlay.js chatTimeout

	void post_overlay(const String &bbcode) {
		if (!overlay_ || bbcode.is_empty()) return;
		overlay_lines_.emplace_back(bbcode, Time::get_singleton()->get_ticks_msec());
		while (overlay_lines_.size() > kOverlayLines) overlay_lines_.pop_front();
		rebuild_overlay();
	}
	void rebuild_overlay() {
		String text;
		for (const auto &l : overlay_lines_) text += l.first + String("\n");
		overlay_->set_text(text);
	}

	RichTextLabel *overlay_ = nullptr;
	std::deque<std::pair<String, uint64_t>> overlay_lines_;
	Panel *panel_ = nullptr;
	RichTextLabel *log_ = nullptr;
	RtsoSafeSelect *channel_ = nullptr;
	LineEdit *input_ = nullptr;
	Button *send_btn_ = nullptr;
	RtsoChatHost *host_ = nullptr;
	bool timestamps_ = true; // chat.timestamp (default.cfg: true)
};

#endif
