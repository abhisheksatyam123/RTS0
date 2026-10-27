// P4.6: RtsoMain's session dialogs, overlays, pause controls, and reference viewers.
// Coordinates objectives/match settings, diplomacy/tribute dialog, pause overlay,
// game speed menu, and template viewer dialogs.

#include <algorithm>
#include <vector>
#include "main_node.h"
#include "l10n.h"
#include "display_colors.h"
#include "popup_safe.h"
#include "session_skin.h"
#include <godot_cpp/classes/font_variation.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/color_rect.hpp>

using namespace godot;
using rtso::sprintf0;

// DiplomacyColors.toggle (session.diplomacycolors, the dialog and minimap buttons).
void RtsoMain::toggle_diplomacy_colors() {
	rtso::display_colors::toggle();
	if (minimap_view) minimap_view->queue_redraw();
	if (diplo_dialog_.is_open()) diplo_dialog_.refresh();
	// registerDiplomacyColorsChangeHandler(PlayerViewControl.rebuild).
	refresh_player_view_colors(replay_view_picker_);
	refresh_player_view_colors(observer_view_picker_);
}

// 0 A.D. ChatMessageFormatSimulation: who sees which tribute, barter, phase and
// diplomacy line. Observers (and defeated players) see more, as g_IsObserver does.
void RtsoMain::show_trigger_dialogs(const std::vector<rtso::Simulation::TriggerDialog> &dialogs) {
	// messages.js "dialog": a message box with the two buttons, for the players it names.
	// Its "dialog-answer" command only reaches a script's OnPlayerCommand, which no
	// ported script reads, so the answer just closes it.
	const uint8_t me = seat_player(); // messages.js: player == Engine.GetPlayerID(), not the viewed one
	if (!sim || turn_mgr_.is_replaying() || me >= rtso::MAX_PLAYERS) return;
	for (const auto &d : dialogs) {
		if (!((d.players >> me) & 1u)) continue;
		ConfirmationDialog *dlg = memnew(ConfirmationDialog);
		dlg->set_title("");
		dlg->set_text(String::utf8(d.caption.c_str()));
		dlg->set_ok_button_text(String::utf8(d.button1.c_str()));
		dlg->set_cancel_button_text(String::utf8(d.button2.c_str()));
		dlg->get_ok_button()->set_tooltip_text(String::utf8(d.tooltip1.c_str()));
		dlg->get_cancel_button()->set_tooltip_text(String::utf8(d.tooltip2.c_str()));
		dlg->set_exclusive(false);
		dlg->connect("confirmed", callable_mp((Node *)dlg, &Node::queue_free));
		dlg->connect("canceled", callable_mp((Node *)dlg, &Node::queue_free));
		add_child(dlg);
		dlg->popup_centered();
	}
}


// 0 A.D. 0.28 simulation/data/settings/game_speeds.json: the session's speed
// dropdown. The Fast Forward speeds are offered only while this client has no
// active player (GameSpeedControl.js): a replay, an observer, or a player who
// has been defeated or has won.
struct GameSpeed {
	float speed;
	const char *title;
	bool fast_forward;
};
static const GameSpeed kGameSpeeds[] = {
	{0.1f, "Turtle (0.1×)", false},
	{0.25f, "Slow (0.25×)", false},
	{0.5f, "Leisurely (0.5×)", false},
	{0.75f, "Relaxed (0.75×)", false},
	{1.0f, "Normal (1×)", false},
	{1.25f, "Fast (1.25×)", false},
	{1.5f, "Very Fast (1.5×)", false},
	{2.0f, "Insane (2×)", false},
	{5.0f, "Fast Forward (5×)", true},
	{10.0f, "Fast Forward (10×)", true},
	{20.0f, "Fast Forward (20×)", true},
};

String RtsoMain::speed_text(float speed) { return String::num(speed, 2) + String::utf8("×"); }

String RtsoMain::speed_title(float speed) {
	for (const GameSpeed &g : kGameSpeeds)
		if (g.speed == speed) return String::utf8(g.title);
	return speed_text(speed);
}

bool RtsoMain::fast_forward_allowed() const { return turn_mgr_.is_replaying() || spectating(); }

Array RtsoMain::speed_choices(bool fast_forward) {
	Array out;
	for (const GameSpeed &g : kGameSpeeds)
		if (fast_forward || !g.fast_forward) out.push_back(g.speed);
	return out;
}

// GameSpeedControl: the speed button toggles the gameSpeed dropdown, a ModernDropDown at
// 100%-390 40 100%-230 65 ("Choose game speed"), which stays up showing the speed in use.
void RtsoMain::_on_speed_button_pressed() {
	if (!speed_btn || !ui_layer) return;
	if (!speed_menu_) {
		speed_menu_ = memnew(RtsoSafeSelect);
		speed_menu_->set_name("GameSpeedDropdown");
		speed_menu_->set_theme(rtso::session_skin());
		speed_menu_->set_anchor(SIDE_LEFT, 1.0f);
		speed_menu_->set_anchor(SIDE_RIGHT, 1.0f);
		speed_menu_->set_offset(SIDE_LEFT, -390.0f);
		speed_menu_->set_offset(SIDE_RIGHT, -230.0f);
		speed_menu_->set_offset(SIDE_TOP, 40.0f);
		speed_menu_->set_offset(SIDE_BOTTOM, 65.0f);
		speed_menu_->set_meta("tooltip_style", "session");
		// ModernDropDown: sans-bold-14 in a 25 px box. Linux Biolinum's line is about twice
		// the point size, so it is trimmed to fit, as the summary's dropdowns are.
		Ref<FontVariation> bold;
		bold.instantiate();
		Ref<Font> face = ResourceLoader::get_singleton()->load("res://assets/fonts/LinBiolinum_RBah.ttf");
		bold->set_base_font(face.is_valid() ? face : ThemeDB::get_singleton()->get_fallback_font());
		bold->set_spacing(TextServer::SPACING_TOP, -6);
		bold->set_spacing(TextServer::SPACING_BOTTOM, -5);
		speed_menu_->add_theme_font_override("font", bold);
		speed_menu_->add_theme_font_size_override("font_size", 14);
		speed_menu_->set_visible(false);
		speed_menu_->connect("item_selected", Callable(this, "_on_speed_selected"));
		ui_layer->add_child(speed_menu_);
	}
	speed_menu_->set_tooltip_text(rtso::tr0("Choose game speed"));
	speed_menu_->clear();
	const Array speeds = speed_choices(fast_forward_allowed());
	int sel = -1;
	for (int i = 0; i < speeds.size(); ++i) {
		speed_menu_->add_item(speed_title((float)speeds[i]), i);
		if ((float)speeds[i] == game_speed) sel = i;
	}
	if (sel < 0)
		for (int i = 0; i < speeds.size(); ++i)
			if ((float)speeds[i] == 1.0f) sel = i;
	speed_menu_->select(sel);
	speed_menu_->set_visible(!speed_menu_->is_visible());
}

void RtsoMain::_on_speed_selected(int index) {
	const Array speeds = speed_choices(fast_forward_allowed());
	if (index >= 0 && index < speeds.size()) set_game_speed((float)speeds[index]);
}

// Offline and in replays only: networked games run at the host's speed.
void RtsoMain::set_game_speed(float speed) {
	if (net_lockstep() || !(speed > 0.0f)) return;
	const Array speeds = speed_choices(fast_forward_allowed());
	game_speed = std::clamp(speed, (float)speeds[0], (float)speeds[speeds.size() - 1]);
	if (speed_btn && speed_btn->get_button_icon().is_null()) speed_btn->set_text(speed_text(game_speed));
}


// PauseControl.js canPause(explicit): an observer, or a player whose game is
// over, who is not the host cannot pause a multiplayer match (resuming their
// own pause stays allowed). The player an observer views does not count.
// Nor can anyone pause before the match is live (g_IsNetworkedActive: the
// "Waiting for players to connect:" stage), which NetServer would ignore.
bool RtsoMain::can_pause() const {
	if (net_disconnected_) return false; // g_IsNetworkedActive turned false
	if (hotkey_paused_ || !net_lockstep() || turn_mgr_.is_replaying()) return true;
	if (!net_game_started_ || turn_mgr_.current_turn() <= rtso::TurnManager::COMMAND_DELAY) return false;
	return !observing_seat() || net_host_.isHost();
}

void RtsoMain::toggle_pause() {
	if (!can_pause()) return;
	hotkey_paused_ = !hotkey_paused_;
	if (net_lockstep()) turn_mgr_.set_local_paused(hotkey_paused_);
	update_pause_overlay();
	// MenuButtons Pause: its caption follows the pause (registerPauseHandler).
	if (Node *menu = get_node_or_null(NodePath("PauseMenu"))) menu->call("rebuild_pause");
}

// PauseOverlay.rebuild: shown for your own pause and while another client
// pauses, until the connection is lost; the resume line only for your own; "Paused by" in multiplayer, each
// name in its player's colour (colorizePlayernameByGUID: white for an observer).
void RtsoMain::update_pause_overlay() {
	if (!pause_overlay_) return;
	const bool networked = net_lockstep() && !turn_mgr_.is_replaying();
	String who;
	bool others = false;
	if (networked)
		for (uint32_t c : turn_mgr_.paused_clients()) {
			others = others || c != turn_mgr_.local_client_id();
			String name;
			uint8_t pl = 0xFF;
			for (const auto &e : net_host_.playerAssignment())
				if (e.client == c) {
					if (!e.name.empty()) name = String::utf8(e.name.c_str());
					pl = e.player;
				}
			if (pl == 0xFF) pl = turn_mgr_.player_for_client(c);
			const bool seated = pl < rtso::MAX_PLAYERS;
			if (name.is_empty()) name = seated ? player_display_name(pl) : tr("Unknown Player");
			const uint32_t rgb = seated && sim ? rtso::display_colors::rgb(*sim, pl) : 0xFFFFFFu;
			// translateWithContext("Separator for a list of players", ", "): the catalogues keep
			// context entries under their bare msgid (tools/fetch_0ad_l10n.py).
			who += String(who.is_empty() ? "" : rtso::tr0(", ")) + "[color=#" + String::num_int64(rgb, 16).lpad(6, "0") + "]" +
					name.replace("[", "[lb]") + "[/color]";
		}
	const bool show = !net_disconnected_ && (hotkey_paused_ || others); // hidden once g_Disconnected
	if (pause_overlay_->is_visible() != show) pause_overlay_->set_visible(show);
	if (!show) return;
	pause_resume_label_->set_visible(hotkey_paused_);
	const String by = networked && !who.is_empty()
			? "[center]" + sprintf0(tr("Paused by %(players)s"), {{"players", who}}) + "[/center]" : String();
	if (pause_by_label_->get_text() != by) pause_by_label_->set_text(by);
}

void RtsoMain::_on_pause_overlay_pressed() {
	if (hotkey_paused_) toggle_pause();
}


// 0 A.D. Menu.toggle: the menu slides open over the running game; only its
// Pause button (and the pages it opens in single player) pause.
void RtsoMain::_on_menu_button_pressed() {
	if (Node *pause = get_node_or_null(NodePath("PauseMenu"))) pause->call("toggle");
}

// Template Viewer for the selection: the selected structure, else the first
// selected unit's roster template (0 A.D. selection_details.js "i" button).
void RtsoMain::_on_template_info_pressed() {
	if (!sim || !ui_layer) return;
	String kind;
	int id = -1, civ = 0;
	if (selected_building_id != 0) {
		for (const auto &b : sim->buildings()) {
			if (b.id != selected_building_id) continue;
			kind = "structure";
			id = b.type;
			// ViewerPage.js: the civ of the structure's own template (a captured one keeps its builder's).
			civ = rtso::is_playable_civ(b.civ) ? b.civ
					: b.team == rtso::GAIA_PLAYER ? sim->player_civ(local_player()) : sim->player_civ(b.team);
			break;
		}
	} else if (!selected_ids.is_empty()) {
		auto uv = sim->units();
		const uint32_t want = (uint32_t)(int64_t)selected_ids[0];
		for (size_t i = 0; i < uv.count; ++i) {
			if (uv.id[i] != want) continue;
			if (uv.tmpl && uv.tmpl[i] != rtso::CIV_UNIT_TEMPLATE_NONE) {
				kind = "unit";
				id = uv.tmpl[i];
			}
			break;
		}
	}
	if (kind.is_empty()) {
		show_notice("No details for this selection.");
		return;
	}
	open_template_viewer(kind, id, civ);
}

// selection_panels.js showTemplateDetails: the Template Viewer over the game; a
// single-player game waits while it is open (implicitPause, then resumeGame).
void RtsoMain::open_template_viewer(const String &kind, int id, int civ) {
	Ref<Script> viewer = ResourceLoader::get_singleton()->load("res://scenes/reference/template_viewer.gd");
	if (!viewer.is_valid() || !ui_layer) return;
	Node *v = Object::cast_to<Node>(viewer->call("open", ui_layer, kind, id, civ));
	SceneTree *tree = get_tree();
	if (!v || !tree || net_lockstep() || tree->is_paused()) return;
	tree->set_pause(true);
	v->connect("tree_exited", Callable(tree, "set_pause").bind(false), CONNECT_ONE_SHOT);
}

// A command panel button's right click (selection_panels.js onPressRight showTemplateDetails):
// the Template Viewer for its structure, technology or unit.
void RtsoMain::_on_template_details_input(const Ref<InputEvent> &event, const String &kind, int id, int civ) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || !mb->is_pressed() || mb->get_button_index() != MOUSE_BUTTON_RIGHT || !ui_layer) return;
	open_template_viewer(kind, id, civ);
}


// 0 A.D. MatchSettingsDialog.toggle.
void RtsoMain::toggle_objectives() {
	const bool open = objectives_dialog_.is_open();
	close_open_dialogs();
	if (!open) objectives_dialog_.open(match_settings_description(), match_settings_player_state());
}

// The settings getGameDescription describes: the running match's own (saved with it)
// and the Game Setup ones only the map generator used (in every replay and save).
Dictionary RtsoMain::get_match_settings_info() const {
	Dictionary d;
	if (!sim) return d;
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings")))
		for (const char *k : {"map_size", "start_res", "team_placement", "trigger_difficulty", "biome", "rated"}) d[k] = ms->get(k);
	// gamesettings PlayerPlacement.pickRandomItems: a Random pick shows the pattern in play.
	if (sim->played_team_placement()) d["team_placement"] = (int)sim->played_team_placement();
	// gamedescription.js Rated game tests PlayerData.length == 2, which only Game Setup's
	// list meets: the session's PlayerData holds Gaia too (LobbyRatingReporter.js counts a
	// 1v1 as 3), so in a match, replay or save only a lobby game has the line.
	int players = 1;
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) players += sim->player(p).active ? 1 : 0;
	d["player_count"] = players;
	Node *ls = get_node_or_null(NodePath("/root/LobbySession"));
	d["lobby_game"] = net_lockstep() && ls && bool(ls->call("signed_in"));
	d["map_id"] = String::utf8(sim->current_map_id().c_str());
	d["map_name"] = String::utf8(sim->current_map_name().c_str());
	d["victory_mask"] = (int)sim->victory_conditions(); // rtso::VictoryKind bits
	d["wonder_min"] = (int)std::lround(sim->wonder_duration() / 60.0f);
	d["relic_min"] = (int)std::lround(sim->relic_duration() / 60.0f);
	d["relic_count"] = sim->relic_count();
	d["regicide_garrison"] = sim->regicide_garrison();
	d["lock_teams"] = sim->teams_locked();
	d["last_man_standing"] = sim->last_man_standing();
	d["ceasefire_min"] = (int)std::lround(sim->ceasefire_seconds() / 60.0f);
	d["pop_cap"] = sim->pop_cap_setting(); // negative: World or Team Population
	d["nomad"] = sim->nomad();
	d["disable_treasures"] = sim->disable_treasures();
	d["explore_map"] = sim->explore_map();
	d["ally_view"] = sim->ally_view();
	// The match's setting (g_InitAttributes), not the live state the reveal-map cheats change.
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	d["reveal_map"] = ms ? bool(ms->get("reveal_map")) : sim->reveal_map();
	d["cheats"] = sim->cheats_enabled();
	return d;
}

String RtsoMain::match_settings_description() const {
	Ref<Script> desc = ResourceLoader::get_singleton()->load("res://scenes/session/game_description.gd");
	return desc.is_valid() ? (String)desc->call("describe", get_match_settings_info()) : String();
}

// MatchSettingsDialog.rebuild: g_PlayerStateMessages for a player who has won or lost.
// It names the local seat (Engine.GetPlayerID()), whoever is viewed: none in a replay or
// for an observer.
String RtsoMain::match_settings_player_state() const {
	if (turn_mgr_.is_replaying()) return String();
	const uint8_t me = seat_player();
	if (!sim || me >= rtso::MAX_PLAYERS) return String();
	if (sim->is_victory_for(me)) return tr("You have won!");
	if (sim->is_defeated(me)) return tr("You have been defeated!");
	return String();
}

// session.js closeOpenDialogs: the menu, chat, diplomacy, trade and match
// settings close together; opening one of them closes the others first.
// CivIcon.onPress: a left click on the top panel's civ emblem reopens the
// reference page last viewed (the Structure Tree at first).
void RtsoMain::_on_civ_icon_gui_input(const Ref<InputEvent> &event) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || !mb->is_pressed() || mb->get_button_index() != MOUSE_BUTTON_LEFT) return;
	if (Node *tree = get_node_or_null(NodePath("TechTreeDialog"))) tree->call("open_last");
}

bool RtsoMain::close_open_dialogs() {
	bool any = false;
	if (Node *menu = get_node_or_null(NodePath("PauseMenu")))
		if (Object::cast_to<CanvasItem>(menu) ? Object::cast_to<CanvasItem>(menu)->is_visible() : bool(menu->get("visible"))) {
			_on_menu_button_pressed();
			any = true;
		}
	if (chat_window_.is_open()) {
		chat_window_.close();
		any = true;
	}
	if (diplo_dialog_.is_open()) {
		diplo_dialog_.close();
		any = true;
	}
	if (objectives_dialog_.is_open()) {
		objectives_dialog_.close();
		any = true;
	}
	if (Node *barter = get_node_or_null(NodePath("BarterDialog")))
		if (bool(barter->call("is_open"))) {
			barter->call("toggle");
			any = true;
		}
	return any;
}

bool RtsoMain::nothing_to_cancel() {
	if (build_mode || any_armed_mode() || !selected_ids.is_empty() || selected_building_id != 0) return false;
	if (chat_window_.is_open() || diplo_dialog_.is_open() || objectives_dialog_.is_open()) return false;
	if (Node *menu = get_node_or_null(NodePath("PauseMenu")))
		if (bool(menu->get("visible"))) return false;
	if (Node *barter = get_node_or_null(NodePath("BarterDialog")))
		if (bool(barter->call("is_open"))) return false;
	return true;
}

// 0 A.D. messages.js onTutorialReady: "dialog-answer" to the trigger script; the
// last goal's Leave opens the session menu (0 A.D. ends the game there).
void RtsoMain::_on_tutorial_ready_pressed() {
	if (!sim || !sim->tutorial_ready_button()) return;
	// messages.js: on the leave goal the button reads Quit and calls endGame(true).
	if (tutorial_panel_.on_last_goal(*sim)) {
		Ref<Script> q = ResourceLoader::get_singleton()->load("res://scenes/session/quit_confirmation.gd");
		if (q.is_valid()) q->call("end_game", this, true);
		return;
	}
	post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_TUTORIAL_READY, nullptr, 0, 0.0f, 0.0f, 0, 0);
}

// Top panel IconButtons: DiplomacyDialogButton, TradeDialogButton, MatchSettingsDialogButton.
void RtsoMain::_toggle_session_dialog(String name) {
	if (name == "DiplomacyDialog") toggle_diplomacy();
	else if (name == "Objectives") toggle_objectives();
	else if (Node *n = get_node_or_null(NodePath(name))) {
		if (!(n->has_method("is_open") && bool(n->call("is_open")))) close_open_dialogs();
		n->call("toggle");
	}
}

void RtsoMain::toggle_diplomacy() {
	if (!diplo_dialog_.is_open()) close_open_dialogs();
	diplo_dialog_.toggle();
}

void RtsoMain::_on_diplo_stance_changed(int stance, int player_idx) {
	diplo_dialog_.stance_changed(stance, player_idx);
}

void RtsoMain::_on_diplo_tribute_pressed(int player_idx, int res_type) {
	diplo_dialog_.tribute_pressed(player_idx, res_type);
}

void RtsoMain::_on_diplo_spy_pressed(int player_idx) {
	diplo_dialog_.spy_pressed(player_idx);
}

void RtsoMain::_on_diplo_attack_request_pressed(int player_idx) {
	diplo_dialog_.attack_request_pressed(player_idx);
}

