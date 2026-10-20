#ifndef RTSO_CHAT_WINDOW_H
#define RTSO_CHAT_WINDOW_H

// P4 stage-1 leaf widget: in-game chat overlay (0 A.D.
// gui/session/chat/ChatWindow.js + ChatBounce.js parity). Owns the panel
// tree, the addressee/channel row and the NMT_CHAT send path. Signal
// targets stay on the bound RtsoMain methods (main_node.h), and all
// net/audio/sim access goes through RtsoChatHost — wire bytes unchanged.

#include <map>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
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
#include <utility>
#include <map>
#include <vector>

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
	// ChatHistory "Players chat": the sender plays (not an observer).
	virtual bool chat_sender_is_player(const String &sender_guid) { (void)sender_guid; return true; }
	// -> NetHost::sendChat (net_host.cpp:327), 0 A.D. NMT_CHAT parity.
	virtual void chat_send_wire(const std::string &wire) = 0;
	// 0 A.D. Cheats.executeCheat: true when `text` was a cheat code (it is not sent as chat).
	virtual bool chat_try_cheat(const String &text) = 0;
	// 0 A.D. g_NetworkCommands (gui/common/network.js): true when `text` was
	// /kick, /ban, /kickspecs, /banspecs, /list or /clear (it is not sent as chat).
	virtual bool chat_network_command(const String &text) = 0;
	// ChatAddressees.js isAddressee for the local player: tag is the wire tag ("[Allies]",
	// "[Enemies]", "[Observers]", "[Player-N]", "[Observer-C]") of a message from sender_guid.
	virtual bool chat_is_addressee(const String &sender_guid, const String &tag) = 0;
	// The local client is an observer (no seat): no Allies or Enemies, private messages only to observers.
	virtual bool chat_local_observer() = 0;
	// ChatAddressees.onUpdatePlayers: the other connected humans a private message can go to,
	// as (wire tag, coloured name): "[Player-N]" for player id N - 1, "[Observer-C]" for
	// the observer on client C.
	virtual std::vector<std::pair<String, String>> chat_private_targets() = 0;
	// ChatInput.autoComplete: the names of g_PlayerAssignments (every connected human).
	virtual std::vector<String> chat_autocomplete_names() { return {}; }
	// 0.28 game setup chat (ChatInputPanel, ClientChat): no addressees and no /me;
	// every line goes to everyone and shows as typed.
	virtual bool chat_setup_verbatim() { return false; }
};

// functions_utility.js tryAutoComplete: the last word of `text` becomes the one
// entry that starts with it (any case), when exactly one does.
inline String rtso_try_autocomplete(const String &text, const std::vector<String> &words) {
	PackedStringArray split = text.split(" ");
	if (split.is_empty()) return text;
	const String last = split[split.size() - 1].to_lower();
	if (last.is_empty()) return text;
	std::vector<String> found;
	for (const String &w : words) {
		if (!w.to_lower().begins_with(last)) continue;
		found.push_back(w);
		if (found.size() > 1) break;
	}
	if (found.size() != 1) return text;
	split.remove_at(split.size() - 1);
	String head = String(" ").join(split);
	return (head.is_empty() ? String() : head + " ") + found[0];
}

// ChatHistory.js Filters, fixed per message when it arrives.
enum RtsoChatFilter : uint16_t {
	CHAT_FILTER_CHAT = 1 << 0, // "message"
	CHAT_FILTER_PLAYER = 1 << 1, // from a player, not an observer
	CHAT_FILTER_ALLY = 1 << 2,
	CHAT_FILTER_ENEMY = 1 << 3,
	CHAT_FILTER_OBSERVER = 1 << 4,
	CHAT_FILTER_PRIVATE = 1 << 5,
	CHAT_FILTER_GAME = 1 << 6, // notifications from the game
	CHAT_FILTER_NETWORK = 1 << 7, // notifications from the network (multiplayer only)
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
		// chat_window.xml chatDialogPanel: a 432 px dialog in the middle of the screen,
		// sized by set_extended (chatDialogPanelLarge / chatDialogPanelSmall).
		panel_ = memnew(Panel);
		panel_->set_anchor(SIDE_LEFT, 0.5f);
		panel_->set_anchor(SIDE_RIGHT, 0.5f);
		panel_->set_offset(SIDE_LEFT, -216.0f);
		panel_->set_offset(SIDE_RIGHT, 216.0f);

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
		// chat_window.xml chatHistoryFilter: "Filter:" and ChatHistory's filters.
		filter_bar_ = memnew(HBoxContainer);
		filter_bar_->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		Label *filter_lbl = memnew(Label);
		filter_lbl->set_text("Filter:");
		filter_lbl->add_theme_font_size_override("font_size", 10);
		filter_lbl->add_theme_color_override("font_color", Color(0.75f, 0.78f, 0.88f));
		filter_bar_->add_child(filter_lbl);
		filter_select_ = memnew(RtsoSafeSelect);
		filter_select_->set_custom_minimum_size(Vector2(170, 22));
		filter_select_->add_theme_font_size_override("font_size", 10);
		refill_filters(false);
		filter_select_->connect("item_selected", Callable(owner, "_on_chat_filter_selected"));
		filter_bar_->add_child(filter_select_);
		chat_top_bar->add_child(filter_bar_);
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

		to_lbl->set_custom_minimum_size(Vector2(50, 0));
		channel_ = memnew(RtsoSafeSelect);
		channel_->set_custom_minimum_size(Vector2(110, 22));
		channel_->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		refill_addressees();
		channel_->add_theme_font_size_override("font_size", 10);
		chat_addressee_bar->add_child(channel_);

		chat_vbox->add_child(chat_addressee_bar);

		// "Text:" and the input, then Cancel, the History tickbox and Send.
		HBoxContainer *chat_input_bar = memnew(HBoxContainer);
		chat_input_bar->set_custom_minimum_size(Vector2(0, 26));
		Label *text_lbl = memnew(Label);
		text_lbl->set_text("Text:");
		text_lbl->set_custom_minimum_size(Vector2(50, 0));
		text_lbl->add_theme_font_size_override("font_size", 10);
		text_lbl->add_theme_color_override("font_color", Color(0.75f, 0.78f, 0.88f));
		chat_input_bar->add_child(text_lbl);
		input_ = memnew(LineEdit);
		input_->set_placeholder("Type message...");
		input_->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		input_->add_theme_font_size_override("font_size", 11);
		input_->connect("text_submitted", Callable(owner, "_on_chat_text_submitted"));
		input_->connect("gui_input", Callable(owner, "_on_chat_input_gui"));
		input_->set_max_length(1024); // chat_window.xml chatInput max_length
		rtso::parent_line_edit(chat_input_bar, input_);

		chat_vbox->add_child(chat_input_bar);

		HBoxContainer *chat_button_bar = memnew(HBoxContainer);
		chat_button_bar->set_custom_minimum_size(Vector2(0, 30));
		Button *cancel = memnew(Button);
		cancel->set_text("Cancel");
		cancel->set_custom_minimum_size(Vector2(130, 28));
		cancel->add_theme_font_size_override("font_size", 11);
		cancel->connect("pressed", Callable(owner, "toggle_chat"));
		chat_button_bar->add_child(cancel);
		send_btn_ = memnew(Button);
		send_btn_->set_custom_minimum_size(Vector2(130, 28));
		send_btn_->set_text("Send");
		send_btn_->add_theme_font_size_override("font_size", 11);
		send_btn_->connect("pressed", Callable(owner, "_on_chat_send_pressed"));
		history_box_ = memnew(CheckBox);
		history_box_->set_text("History");
		history_box_->add_theme_font_size_override("font_size", 10);
		history_box_->set_tooltip_text("Show the chat history.");
		history_box_->connect("toggled", Callable(owner, "_on_chat_history_toggled"));
		Control *gap_l = memnew(Control);
		gap_l->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		Control *gap_r = memnew(Control);
		gap_r->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		chat_button_bar->add_child(gap_l);
		chat_button_bar->add_child(history_box_);
		chat_button_bar->add_child(gap_r);
		chat_button_bar->add_child(send_btn_);
		chat_vbox->add_child(chat_button_bar);

		panel_->set_visible(false);
		parent->add_child(panel_);
		set_extended(true);

		// 0 A.D. gui/session/chat/ChatOverlay.js: while the chat window is closed,
		// the last 20 chat and notification lines show over the map for 30 s.
		// ChatOverlay.js chatLines: one line each; a line with a callback is a button with a
		// tooltip, the others are ghosts (clicks reach the map under them).
		owner_ = owner;
		overlay_ = memnew(VBoxContainer);
		overlay_->add_theme_constant_override("separation", 0);
		overlay_->set_anchor(SIDE_LEFT, 0.0f);
		overlay_->set_anchor(SIDE_TOP, 0.0f);
		overlay_->set_offset(SIDE_LEFT, 16.0f);
		overlay_->set_offset(SIDE_RIGHT, 520.0f);
		// session.xml chatPanel starts below the hero and relic buttons (PanelEntities.xml).
		overlay_->set_offset(SIDE_TOP, 114.0f);
		overlay_->set_offset(SIDE_BOTTOM, 114.0f + 20 * 18.0f);
		overlay_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		parent->add_child(overlay_);
	}

	bool is_ready() const { return log_ != nullptr; }
	bool is_open() const { return panel_ != nullptr && panel_->is_visible(); }
	// ChatInput.onHotkeyChange: the input and Send button share one tooltip.
	String input_tooltip() const { return input_ ? input_->get_tooltip_text() : String(); }
	void set_input_tooltip(const String &tip) {
		if (input_) input_->set_tooltip_text(tip);
		if (send_btn_) send_btn_->set_tooltip_text(tip);
	}
	// ChatInput.onTab -> autoCompleteText: Tab completes the word before the caret.
	void tab_complete() {
		if (!input_ || !host_) return;
		input_->accept_event();
		const String text = input_->get_text();
		const int caret = input_->get_caret_column();
		const String before = text.substr(0, caret);
		const String done = rtso_try_autocomplete(before, host_->chat_autocomplete_names());
		input_->set_text(done + text.substr(caret));
		input_->set_caret_column(caret + done.length() - before.length());
	}
	// ChatWindow.closePage: Cancel or sending empties the input.
	void close() {
		if (panel_) panel_->set_visible(false);
		if (input_) input_->clear();
	}
	// Chat.openPage(command): the chat hotkeys always open the window, on Everyone
	// unless they name an addressee (session.teamchat, session.privatechat).
	void open_page() {
		if (!panel_) return;
		panel_->set_visible(true);
		refill_addressees();
		refill_filters(host_ && host_->net_lockstep());
		select_tag("");
		if (input_) input_->grab_focus();
	}

	void toggle() {
		if (!panel_) return;
		bool vis = !panel_->is_visible();
		if (!vis) {
			close();
			return;
		}
		panel_->set_visible(vis);
		if (vis) refill_addressees();
		if (vis) refill_filters(host_ && host_->net_lockstep());
		if (vis && input_) {
			input_->grab_focus();
		}
	}

	// The quick buttons and hotkeys: 0 everyone, 1 allies, 2 enemies, 3 observers.
	void select_channel(int channel_idx) {
		if (!channel_) return;
		static const char *kTags[4] = {"", "[Allies]", "[Enemies]", "[Observers]"};
		if (channel_idx >= 0 && channel_idx < 4) select_tag(kTags[channel_idx]);
	}
	// session.teamchat: Allies, or Observers for an observer (ChatWindow.js openChat).
	void select_team() { select_tag(host_ && host_->chat_local_observer() ? "[Observers]" : "[Allies]"); }
	// session.privatechat: the last private addressee (ChatInput.js), else everyone.
	void select_last_private() { select_tag(last_private_); }

	// ChatAddressees.onUpdatePlayers: Everyone, Allies and Enemies (not for observers),
	// Observers, then each other connected human by name; the selection is kept.
	void refill_addressees() {
		if (!channel_) return;
		const String keep = selected_tag();
		channel_->clear();
		tags_.clear();
		auto add = [&](const String &label, const String &tag) {
			channel_->add_item(label, (int)tags_.size());
			tags_.push_back(tag);
		};
		add("Everyone", "");
		const bool observer = host_ && host_->chat_local_observer();
		if (!observer) {
			add("Allies", "[Allies]");
			add("Enemies", "[Enemies]");
		}
		add("Observers", "[Observers]");
		bool kept = keep.is_empty() || keep.begins_with("[") && !keep.begins_with("[Player-") && !keep.begins_with("[Observer-");
		// A player's entry is its name after a box in its colour (colorizePlayernameHelper("■")),
		// an observer's the bare name.
		if (host_)
			for (const auto &t : host_->chat_private_targets()) {
				names_[t.first] = plain(t.second);
				const int c = t.second.find("[color=#");
				if (t.first.begins_with("[Player-") && c >= 0) {
					channel_->add_icon_item(color_box(Color::html(t.second.substr(c + 8, 6))), names_[t.first], (int)tags_.size());
					tags_.push_back(t.first);
				} else {
					add(names_[t.first], t.first);
				}
				kept = kept || t.first == keep;
			}
		// ChatAddressees.onUpdatePlayers: a private addressee who went offline stays
		// selected as "[OFFLINE] name" rather than falling back to everyone.
		if (!kept && names_.count(keep)) add("[OFFLINE] " + names_[keep], keep);
		select_tag(keep);
	}

	// Send path: local echo + NMT_CHAT broadcast (channel travels as wire
	// tag, ChatBounce.js:15-38 parity — byte-identical to pre-split code).
	void send_pressed() {
		if (!host_ || !input_ || !log_) return;
		String text = input_->get_text().strip_edges();
		if (!text.is_empty() && (host_->chat_network_command(text) || host_->chat_try_cheat(text))) {
			input_->clear();
			if (panel_) panel_->set_visible(false);
			return;
		}
		if (!text.is_empty()) {
			// ChatMessageFormatPlayer.parseMessageAddressee: a typed /allies, /enemies,
			// /observers or /msg <name> picks the addressee of this message, otherwise
			// the dropdown's. Any other command (but /me) goes to nobody.
			String tag = selected_tag();
			const bool observer = host_->chat_local_observer();
			const String cmd = text.begins_with("/") ? text.get_slice(" ", 0) : String();
			const String after = text.substr(cmd.length()).strip_edges();
			if (cmd == "/allies" && !observer) { tag = "[Allies]"; text = after; }
			else if (cmd == "/enemies" && !observer) { tag = "[Enemies]"; text = after; }
			else if (cmd == "/observers") { tag = "[Observers]"; text = after; }
			else if (cmd == "/msg") {
				// Chat.getReceiverGUIDs / matchUsername: the longest name the rest starts with.
				String best_tag, best_name;
				for (const auto &t : host_->chat_private_targets()) {
					const String name = plain(t.second);
					if (after.begins_with(name) && name.length() > best_name.length()) { best_tag = t.first; best_name = name; }
				}
				if (best_tag.is_empty()) return refuse("Unknown chat addressee: " + after);
				tag = best_tag;
				text = after.substr(best_name.length()).strip_edges();
			} else if (!cmd.is_empty() && cmd != "/me") {
				return refuse("Unknown chat command: " + cmd);
			}
			if (text.is_empty()) return;
			if (is_private(tag) && !is_online(tag)) return refuse("Unknown chat addressee: " + names_[tag]);
			const String context = tag == "[Allies]" ? "Ally" : tag == "[Enemies]" ? "Enemy" : tag == "[Observers]" ? "Observer"
					: tag.is_empty() ? "" : "Private";
			if (context == "Private") last_private_ = tag;
			const String wire_tag = tag.is_empty() ? String() : tag + " ";
			const String line = format_line(context, host_->chat_user(""), text);
			add_log(line + String("\n"), message_bits(context, !observer));
			post_overlay(line);
			input_->clear();
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
	// Returns whether the message was shown (it reached one of its addressees).
	bool append_incoming(const String &sender_guid, const String &raw_msg) {
		if (!log_) return false;
		if (host_ && host_->chat_setup_verbatim()) {
			const String line = format_line("", host_->chat_user(sender_guid), raw_msg, false);
			add_log(line + String("\n"), message_bits("", host_->chat_sender_is_player(sender_guid)));
			post_overlay(line);
			return true;
		}
		String msg = raw_msg;
		// The wire tag is the addressee; 0.28 shows it as the context (ChatAddressees.js)
		// and shows the message only to its addressees (isAddressee).
		String context, tag;
		if (msg.begins_with("[Allies]")) {
			context = "Ally";
			tag = "[Allies]";
			msg = msg.substr(8).strip_edges();
		} else if (msg.begins_with("[Team]")) {
			context = "Ally";
			tag = "[Allies]";
			msg = msg.substr(6).strip_edges();
		} else if (msg.begins_with("[Enemies]")) {
			context = "Enemy";
			tag = "[Enemies]";
			msg = msg.substr(9).strip_edges();
		} else if (msg.begins_with("[Observers]")) {
			context = "Observer";
			tag = "[Observers]";
			msg = msg.substr(11).strip_edges();
		} else if (msg.begins_with("[Player-") || msg.begins_with("[Observer-")) {
			int close_b = msg.find("]");
			if (close_b > 0) {
				context = "Private";
				tag = msg.substr(0, close_b + 1);
				msg = msg.substr(close_b + 1).strip_edges();
			}
		}
		if (!tag.is_empty() && host_ && !host_->chat_is_addressee(sender_guid, tag)) return false;
		// chat.timestamp stamps the history only (ChatHistory.js); the overlay shows the bare text.
		const String line = format_line(context, host_ ? host_->chat_user(sender_guid) : sender_guid, msg);
		add_log(line + String("\n"), message_bits(context, !host_ || host_->chat_sender_is_player(sender_guid)));
		post_overlay(line);
		return true;
	}

	// ChatInputPanel.submitChatInput in the game setup: a network command, or
	// Engine.SendNetworkChat(text) to everyone exactly as typed.
	void send_setup(const String &typed) {
		if (!log_ || !host_) return;
		const String text = typed.strip_edges();
		if (text.is_empty() || host_->chat_network_command(text)) return;
		const String line = format_line("", host_->chat_user(""), text, false);
		add_log(line + String("\n"), message_bits("", !host_->chat_local_observer()));
		post_overlay(line);
		if (host_->net_lockstep()) host_->chat_send_wire(text.utf8().get_data());
	}

	// A message from the local player that isn't typed (the developer overlay's
	// notices): shown as their chat line, and sent to the other players.
	void post_own(const String &text) {
		if (!log_ || !host_) return;
		const String line = format_line("", host_->chat_user(""), text);
		add_log(line + String("\n"), message_bits("", !host_->chat_local_observer()));
		post_overlay(line);
		if (host_->net_lockstep()) host_->chat_send_wire(text.utf8().get_data());
	}

	// Raw bbcode line (Petra log, diplomacy/tribute feedback). With an action, the overlay
	// line is clickable (ChatOverlay.js onPress): the owner's _on_chat_line_input gets it.
	void append_line(const String &bbcode, const String &action = String(), const String &tooltip = String()) {
		append_kind(bbcode, CHAT_FILTER_GAME, action, tooltip);
	}
	// A line of a given filter kind: a network notice (ChatMessageFormatNetwork), or an
	// AI's chat (a "message" from a player, with its context's bit).
	void append_kind(const String &bbcode, uint16_t bits, const String &action = String(), const String &tooltip = String()) {
		if (log_ && !bbcode.is_empty()) add_log(bbcode, bits);
		post_overlay(bbcode.strip_edges(false, true), action, tooltip);
	}
	static uint16_t message_bits(const String &context, bool from_player) {
		uint16_t bits = CHAT_FILTER_CHAT | (from_player ? CHAT_FILTER_PLAYER : 0);
		if (context == "Ally") bits |= CHAT_FILTER_ALLY;
		else if (context == "Enemy") bits |= CHAT_FILTER_ENEMY;
		else if (context == "Observer") bits |= CHAT_FILTER_OBSERVER;
		else if (context == "Private") bits |= CHAT_FILTER_PRIVATE;
		return bits;
	}

	// 0 A.D. clearChatMessages (/clear): empties the on-screen overlay; the history stays.
	void clear() {
		overlay_lines_.clear();
		if (overlay_) rebuild_overlay();
	}

	// The log's bbcode, for the networked game setup's chat panel (0.28 shows
	// the same chat there before Start), and a counter that moves on each change.
	const String &history() const { return history_; }
	uint64_t history_rev() const { return history_rev_; }

	// Test hooks: the log as plain text, and the input line.
	String log_text() const { return log_ ? log_->get_parsed_text() : String(); }
	LineEdit *input() const { return input_; }
	size_t overlay_count() const { return overlay_lines_.size(); }
	int filter_count() const { return filter_select_ ? filter_select_->get_item_count() : 0; }
	String addressee_list() const { // the dropdown's labels, "|"-joined (an icon item: "■" first)
		String out;
		for (int i = 0; channel_ && i < channel_->get_item_count(); ++i)
			out += String(i ? "|" : "") + (channel_->get_item_icon(i).is_valid() ? String::utf8("■ ") : String()) + channel_->get_item_text(i);
		return out;
	}
	String addressee_label() const { return channel_ && channel_->get_selected() >= 0 ? channel_->get_item_text(channel_->get_selected()) : String(); }

	// 0 A.D. chat.timestamp: each line opens with the local time, "[HH:MM]".
	// It is drawn with the history, so turning it on or off changes the old lines too.
	void set_timestamps(bool on) {
		if (timestamps_ == on) return;
		timestamps_ = on;
		redraw_log();
	}
	// ChatHistory filter: the dropdown's index into kFilters.
	void set_filter(int index) {
		filter_ = index;
		if (filter_select_ && filter_select_->get_selected() != index) filter_select_->select(index);
		redraw_log();
	}
	int filter() const { return filter_; }
	// ChatWindow "History" (chat.session.extended): the history and its filter show,
	// else only the addressee and the input.
	void set_extended(bool on) {
		extended_ = on;
		if (log_) log_->set_visible(on);
		if (filter_bar_) filter_bar_->set_visible(on);
		if (history_box_) history_box_->set_pressed_no_signal(on);
		// ChatWindow.resizeChatWindow: chatDialogPanelLarge "50%-216 22%-75 50%+216 78%-85",
		// chatDialogPanelSmall "50%-216 50%-140 50%+216 50%-20".
		if (panel_) {
			panel_->set_anchor(SIDE_TOP, on ? 0.22f : 0.5f);
			panel_->set_anchor(SIDE_BOTTOM, on ? 0.78f : 0.5f);
			panel_->set_offset(SIDE_TOP, on ? -75.0f : -140.0f);
			panel_->set_offset(SIDE_BOTTOM, on ? -85.0f : -20.0f);
		}
	}
	bool extended() const { return extended_; }
	// "Network notifications" is hidden unless in multiplayer (Engine.HasNetClient).
	void refill_filters(bool network) {
		if (!filter_select_) return;
		const int n = (int)std::size(kFilters) - (network ? 0 : 1);
		if (filter_select_->get_item_count() == n) return;
		filter_select_->clear();
		for (int i = 0; i < n; ++i) filter_select_->add_item(kFilters[i].label, i);
		if (filter_ >= n) filter_ = 0;
		filter_select_->select(filter_);
		redraw_log();
	}
	// ChatHistory.prototype.Filters: the label and the bit a line needs (0: every line).
	struct FilterDef {
		const char *label;
		uint16_t need;
	};
	static constexpr FilterDef kFilters[] = {
		{"Chat and notifications", 0}, {"Chat messages", CHAT_FILTER_CHAT}, {"Players chat", CHAT_FILTER_PLAYER},
		{"Ally chat", CHAT_FILTER_ALLY}, {"Enemy chat", CHAT_FILTER_ENEMY}, {"Observer chat", CHAT_FILTER_OBSERVER},
		{"Private chat", CHAT_FILTER_PRIVATE}, {"Game notifications", CHAT_FILTER_GAME},
		{"Network notifications", CHAT_FILTER_NETWORK}};

	// Drops overlay lines older than 30 s; hides the overlay while the chat
	// window (which shows the full history) is open. Call once per frame.
	void tick_overlay() {
		if (!overlay_) return;
		const uint64_t now = Time::get_singleton()->get_ticks_msec();
		bool changed = false;
		while (!overlay_lines_.empty() && now - overlay_lines_.front().at >= kOverlayTimeoutMs) {
			overlay_lines_.pop_front();
			changed = true;
		}
		if (changed) rebuild_overlay();
		overlay_->set_visible(!is_open() && !overlay_lines_.empty());
	}

private:
	// ChatMessageFormatPlayer strings: "(context) <user> message", or with /me
	// "(context) * user message". The text is escaped (escapeText).
	static String format_line(const String &context, const String &user, String text, bool actions = true) {
		const bool me = actions && text.begins_with("/me ");
		if (me) text = text.substr(4);
		text = text.replace("[", "[lb]");
		const String ctx = context.is_empty() ? String() : "(" + context + ") ";
		return ctx + (me ? "* " + user : "<" + user + ">") + " " + text;
	}

	static bool is_private(const String &tag) { return tag.begins_with("[Player-") || tag.begins_with("[Observer-"); }
	bool is_online(const String &tag) const {
		if (!host_) return false;
		for (const auto &t : host_->chat_private_targets())
			if (t.first == tag) return true;
		return false;
	}
	// A message that reaches nobody: the warning 0.28 prints, in the log, and the
	// window closes with the input cleared.
	void refuse(const String &why) {
		add_log("[color=#ffcc44]" + why.replace("[", "[lb]") + "[/color]\n", CHAT_FILTER_GAME);
		close();
	}

	String selected_tag() const {
		const int i = channel_ ? channel_->get_selected() : -1;
		return i >= 0 && i < (int)tags_.size() ? tags_[(size_t)i] : String();
	}
	void select_tag(const String &tag) {
		if (!channel_) return;
		for (size_t i = 0; i < tags_.size(); ++i)
			if (tags_[i] == tag) { channel_->select((int)i); return; }
		if (!tags_.empty()) channel_->select(0);
	}
public:
	// A bbcode name without its colour tags.
	static Ref<ImageTexture> color_box(const Color &c) {
		Ref<Image> img = Image::create_empty(10, 10, false, Image::FORMAT_RGBA8);
		img->fill(c);
		return ImageTexture::create_from_image(img);
	}
	static String plain(const String &bb) {
		String out;
		bool in_tag = false;
		for (int i = 0; i < bb.length(); ++i) {
			const char32_t c = bb[i];
			if (c == '[') in_tag = true;
			else if (c == ']') in_tag = false;
			else if (!in_tag) out += String::chr(c);
		}
		return out;
	}

private:


	static constexpr size_t kOverlayLines = 20; // ChatOverlay.js chatLinesNumber
	static constexpr uint64_t kOverlayTimeoutMs = 30000; // ChatOverlay.js chatTimeout

	struct OverlayLine {
		String bbcode;
		uint64_t at;
		String action, tooltip;
	};
	void post_overlay(const String &bbcode, const String &action = String(), const String &tooltip = String()) {
		if (!overlay_ || bbcode.is_empty()) return;
		overlay_lines_.push_back({ bbcode, Time::get_singleton()->get_ticks_msec(), action, tooltip });
		while (overlay_lines_.size() > kOverlayLines) overlay_lines_.pop_front();
		rebuild_overlay();
	}
	void rebuild_overlay() {
		while (overlay_->get_child_count() > 0) {
			Node *c = overlay_->get_child(0);
			overlay_->remove_child(c);
			c->queue_free();
		}
		for (const auto &l : overlay_lines_) {
			RichTextLabel *line = memnew(RichTextLabel);
			line->set_use_bbcode(true);
			line->set_fit_content(true);
			line->set_scroll_active(false);
			line->set_autowrap_mode(TextServer::AUTOWRAP_OFF);
			line->add_theme_font_size_override("normal_font_size", 13);
			line->add_theme_color_override("font_outline_color", Color(0, 0, 0, 1));
			line->add_theme_constant_override("outline_size", 4);
			line->set_text(l.bbcode);
			line->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
			if (l.action.is_empty() || !owner_) {
				line->set_mouse_filter(Control::MOUSE_FILTER_IGNORE); // ghost
			} else {
				line->set_mouse_filter(Control::MOUSE_FILTER_STOP);
				line->set_default_cursor_shape(Control::CURSOR_POINTING_HAND);
				line->set_tooltip_text(l.tooltip);
				line->connect("gui_input", Callable(owner_, "_on_chat_line_input").bind(l.action));
			}
			overlay_->add_child(line);
		}
	}
public:
	// Tests: the overlay lines as plain text, with their action and tooltip.
	Array overlay_state() const {
		Array out;
		if (!overlay_) return out;
		for (int i = 0; i < overlay_->get_child_count(); ++i) {
			RichTextLabel *line = Object::cast_to<RichTextLabel>(overlay_->get_child(i));
			if (!line || line->is_queued_for_deletion()) continue;
			Dictionary d;
			d["text"] = line->get_parsed_text();
			d["clickable"] = line->get_mouse_filter() == Control::MOUSE_FILTER_STOP;
			d["tooltip"] = line->get_tooltip_text();
			d["node"] = line;
			out.push_back(d);
		}
		return out;
	}
private:
	Object *owner_ = nullptr;

	std::vector<String> tags_;  // the wire tag of each addressee entry
	String last_private_;       // session.privatechat reopens it
	std::map<String, String> names_; // each private tag's plain name, kept after they leave
	VBoxContainer *overlay_ = nullptr;
	std::deque<OverlayLine> overlay_lines_;
	Panel *panel_ = nullptr;
	RichTextLabel *log_ = nullptr;
	String history_;
	uint64_t history_rev_ = 0;
	struct Entry {
		String text, time;
		uint16_t bits;
	};
	std::vector<Entry> entries_;
	int filter_ = 0;
	bool extended_ = false;
	HBoxContainer *filter_bar_ = nullptr;
	RtsoSafeSelect *filter_select_ = nullptr;
	CheckBox *history_box_ = nullptr;
	bool shows(const Entry &e) const {
		const uint16_t need = filter_ >= 0 && filter_ < (int)std::size(kFilters) ? kFilters[filter_].need : 0;
		return need == 0 || (e.bits & need);
	}
	String draw(const Entry &e) const {
		return timestamps_ ? "[color=#aaaaaa][lb]" + e.time + "][/color] " + e.text : e.text;
	}
	void redraw_log() {
		if (!log_) return;
		log_->clear();
		for (const Entry &e : entries_)
			if (shows(e)) log_->append_text(draw(e));
	}
	// ChatHistory.onChatMessage: the line, its "[HH:mm]" and the filters it passes.
	void add_log(const String &text, uint16_t bits) {
		const Dictionary t = Time::get_singleton()->get_time_dict_from_system();
		entries_.push_back({text, String::num_int64((int64_t)t["hour"]).pad_zeros(2) + ":" +
				String::num_int64((int64_t)t["minute"]).pad_zeros(2), bits});
		if (shows(entries_.back())) log_->append_text(draw(entries_.back()));
		history_ += draw(entries_.back());
		++history_rev_;
	}
	RtsoSafeSelect *channel_ = nullptr;
	LineEdit *input_ = nullptr;
	Button *send_btn_ = nullptr;
	RtsoChatHost *host_ = nullptr;
	bool timestamps_ = true; // chat.timestamp (default.cfg: true)
};

#endif
