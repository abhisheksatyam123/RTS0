#ifndef RTSO_MAIN_NODE_H
#define RTSO_MAIN_NODE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/variant/plane.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/vector4.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/h_flow_container.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/canvas_layer.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/audio_stream_player3d.hpp>
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/time.hpp>
#include <map>
#include <set>
#include "l10n.h"
#include "safe_select.h"
#include "top_panel.h"
#include "production_queue.h"
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/item_list.hpp>
#include "../network/net_host.h"
#include "../network/turn_manager.h"
#include "../network/lobby_client.h"
#include "../core/memory/arena.h"
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/confirmation_dialog.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/h_separator.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input.hpp>
#include <unordered_map>
#include <utility>
#include <memory>
#include <vector>

#include "../simulation/simulation.h"
#include "../animation/animation_controller.h"
#include "world_view_node.h"
#include "camera_rig.h"
#include "selection_rect.h"
#include "debug_overlay.h"
#include "minimap_view.h"
#include "chat_window.h"
#include "diplomacy_dialog.h"
#include "hotkeys_godot.h"
#include "attack_detection.h"
#include "command_queue_view.h"
#include "inspector_view.h"
#include "objectives_dialog.h"
#include "tutorial_panel.h"
#include "cinema_view.h"
#include "panel_entities_view.h"
#include "research_progress_view.h"
#include "debug_overlays.h"
#include "dev_overlay_view.h"
#include "minimap_panel_view.h"
#include "control_groups_view.h"
#include "range_overlays.h"
#include "rally_point_renderer.h"
#include "music_manager.h"
#include "session_audio.h"

using namespace godot;

class RtsoMain : public Node3D, public RtsoChatHost, public RtsoDiplomacyHost, public RtsoProductionQueueHost, public RtsoInspectorHost, public RtsoCommandPanelHost {
	GDCLASS(RtsoMain, Node3D);

	rtso::Simulation *sim = nullptr;
	RtsoWorldView *view = nullptr;
	RtsoCameraRig *cam_rig = nullptr;
	RtsoSelectionRect *sel_rect = nullptr;
	RtsoDebugOverlay *debug_overlay = nullptr;
	RtsoMiniMapView *minimap_view = nullptr;
	CanvasLayer *ui_layer = nullptr;
	AudioStreamPlayer *voice_player = nullptr;
	AudioStreamPlayer *alarm_player = nullptr;
	AudioStreamPlayer *sfx_player = nullptr;
	AudioStreamPlayer *click_player_ = nullptr; // ui_button_click.ogg
	int ui_clicks_ = 0; // tests: clicks played
	uint64_t ui_click_frame_ = UINT64_MAX; // the frame the last click played
	void hook_ui_click_sounds(Node *n);
	AudioStreamPlayer *ambient_player = nullptr;
	AudioStreamPlayer3D *spatial_sfx_player = nullptr;

	// 1. Top HUD Bar
	Panel *top_hud_panel = nullptr;
	RtsoTopPanel top_panel_;
	// 0.28 MiniMapPanel's idle worker, diplomacy colours and flare buttons.
	rtso::RtsoMiniMapPanel minimap_panel_;
	// input.js INPUT_FLARE: the flare button (or session.flareactivate) armed a flare.
	bool flare_armed_ = false;
	Button *speed_btn = nullptr;
	Button *menu_btn = nullptr;
	RtsoSafeSelect *map_selector = nullptr;
	RtsoSafeSelect *civ_selector = nullptr;
	RtsoSafeSelect *ai_diff_selector = nullptr;
	RtsoSafeSelect *ai_behavior_selector = nullptr;
	RtsoSafeSelect *victory_selector = nullptr;
	Button *lobby_btn = nullptr;
	Button *voice_btn = nullptr;
	Panel *lobby_panel = nullptr;
	ItemList *lobby_rooms = nullptr;
	LineEdit *lobby_name = nullptr;
	LineEdit *lobby_host = nullptr;
	LineEdit *lobby_port = nullptr;
	LineEdit *lobby_password = nullptr; // 0 A.D. password-protected game (host sets, joiner gives)
	size_t kicks_shown_ = 0;           // NetHost::kicks() already announced
	Label *lobby_status = nullptr;
	Button *lobby_observe = nullptr; // join a running match as an observer (0 A.D. observer late join)
	Label *lobby_players = nullptr;  // in-game player list (0 A.D. NMT_PLAYER_ASSIGNMENT)
	uint64_t players_shown_ = 0;     // the player list version lobby_players shows
	// Host, before Start: 0 A.D.'s player assignment, a slot dropdown per client.
	VBoxContainer *lobby_seats = nullptr;
	std::string lobby_seats_key_;           // the clients and slots lobby_seats was built for
	std::map<uint32_t, int> seat_choice_;   // client -> slot the host gave it, SEAT_OBSERVER to watch
	std::set<uint32_t> seat_seen_;          // clients the host has auto-assigned (0 A.D. onClientJoin)
	std::map<std::string, uint8_t> seat_last_; // a client name's last slot (0 A.D. lastAssigned)
	static constexpr int SEAT_AUTO = 100, SEAT_OBSERVER = 99;
	void refresh_lobby_seats();
	void prepare_lobby_seats(rtso::GameSetupParams &setup, bool commit);
	void lobby_effective_seats(uint8_t out[rtso::NET_SETUP_CLIENTS]);
	// Networked game setup before Start (0 A.D. NMT_GAME_SETUP / NMT_READY).
	// The host's MatchSettings is the setup; the host sends it whenever it
	// changes and clears every ready flag when a setting or a seat changes.
	// A client writes the host's setup into its own MatchSettings.
	rtso::NetSetupState net_setup_;      // host: the setup it sent last; client: the host's latest
	std::string net_setup_key_;          // host: the settings and seats net_setup_ was versioned for
	std::string net_setup_sent_;         // host: what it sent last, but for the sequence
	uint8_t net_ready_ = 0;              // client: 0 not ready, 1 ready for net_ready_version_, 2 stay ready
	uint32_t net_ready_version_ = 0;
	void match_settings_setup(rtso::GameSetupParams &setup);
	void host_net_setup_tick(bool force);
	void client_net_setup_tick();
	const char *apply_net_setup_request(uint32_t client, const std::string &request);
	void store_net_setup_in_match_settings(const rtso::GameSetupParams &setup);
	void assign_lobby_seat(int client, int choice);
	void start_net_speed(uint16_t speed);
	Node *net_setup_overlay_ = nullptr;     // scenes/menu/gamesetup.tscn, networked
	CanvasLayer *net_setup_layer_ = nullptr;
	bool net_setup_overlay_unsupported_ = false;
	Button *lobby_start_btn_ = nullptr;
	void update_net_setup_overlay();
	// gamesetup_mp.js pageConnecting: "Connecting to server..." (then
	// "Registering with server...") with Cancel over the screen until the
	// host lets us in, and while a rejoin loads the host's game.
	void update_net_connect_overlay();
	Node *net_connect_overlay_ = nullptr; // scenes/menu/net_connecting.gd
	CanvasLayer *net_connect_layer_ = nullptr;
	// The network warnings over the setup page and the Connecting box before Start.
	CanvasLayer *net_warnings_layer_ = nullptr;
	rtso::NetHost net_host_;
	rtso::TurnManager turn_mgr_;
	rtso::Arena net_arena_{64 * 1024};
	std::vector<rtso::LobbyRoomInfo> lobby_room_cache_;
	std::string lobby_http_host_ = "127.0.0.1";
	uint16_t lobby_http_port_ = 8088;

	// 2. Bottom Inspector Panel (inspector_view.h)
	RtsoInspectorView inspector_;
	Button *template_info_btn_ = nullptr;
	// Options page settings the session uses (refresh_user_options).
	struct UserOptions {
		uint32_t generation = 0;
		int batch_size = 5;      // the batch Shift trains: round(batch_scroll), at least 1
		int batch_default = 5;   // gui.session.batchtrainingsize
		float batch_scroll = 5;  // g_BatchSize: Shift+wheel over the roster moves it by 1 / scrollbatchratio
		float wounded_fraction = 0.33f;
		float flare_time = 6.0f;
		float drag_delta = 4.0f;
		bool attack_notice = true;
		bool tribute_notice = true;
		bool barter_notice = true;
		uint8_t phase_notice = 1; // gui.session.notifications.phase: 0 none, 1 completed, 2 all
	} user_opts_;
	void refresh_user_options();

	// 3. Command & Action Panel (command_queue_view.h): stances, formations, production
	// buttons, the roster list and the production queue.
	RtsoCommandPanelView command_panel_;

	// 4. Banner Overlay
	Panel *banner_panel = nullptr;
	Label *banner_label = nullptr;
	// Map trigger messages (0 A.D. GuiInterface AddTimeNotification), centred under the top bar.
	RichTextLabel *time_notification_label_ = nullptr; // notificationText: player names in their colours
	// The sim's "text" notices (SessionNotice::TEXT) shown under it until end_ms (match time).
	struct TextNotice {
		String text;
		uint32_t end_ms;
	};
	std::vector<TextNotice> text_notices_;
	std::unordered_map<uint32_t, uint32_t> spawn_notified_; // building -> match ms of its last blocked-spawn notice
	// 0.28 NetworkDelayOverlay: "Delay to live stream" while this peer holds more
	// than MAX_LIVE_DELAY turns it hasn't run yet; checked once a second.
	Label *net_delay_label_ = nullptr;
	double net_delay_check_s_ = 0.0;
	int debug_net_delay_turns_ = -1; // test hook: stands in for turns_behind()
	void update_net_delay_overlay(double delta);
	void debug_set_net_delay(int turns) { debug_net_delay_turns_ = turns; net_delay_check_s_ = 1.0; }
	String debug_net_delay_text() const { return net_delay_label_ && net_delay_label_->is_visible() ? net_delay_label_->get_text() : String(); }

	// 5/6. In-Game Chat Overlay + Diplomacy & Tribute Dialog: P4.1 leaf
	// widgets (chat_window.h / diplomacy_dialog.h) own their panel trees;
	// the bound handler methods below delegate into them.
	RtsoChatWindow chat_window_;
	RtsoDiplomacyDialog diplo_dialog_;
	rtso::RtsoObjectivesDialog objectives_dialog_;
	rtso::RtsoTutorialPanel tutorial_panel_;
	rtso::RtsoCinemaView cinema_view_;
	bool campaign_win_noted_ = false; // summary_screen.gd mark_campaign_won ran for this match
	bool lobby_report_noted_ = false; // LobbySession._check_report ran on this seat's win or defeat
	rtso::RtsoPanelEntities panel_entities_; // heroes and relics under the top panel
	rtso::RtsoResearchProgress research_progress_; // technologies being researched, on the right
	rtso::RtsoDevOverlay dev_overlay_; // Alt+D developer overlay
	rtso::RtsoDebugOverlays debug_overlays_; // its pathfinder, obstruction, unit motion and range overlays
	int render_debug_mode_ = 0; // its Render Debug Mode (RtsoDevOverlay::RenderDebugMode)
	rtso::RtsoControlGroups control_groups_; // the control groups' buttons on the left edge

	// 3D Placement Ghost & Rally Marker
	MeshInstance3D *ghost_mesh = nullptr;
	Ref<StandardMaterial3D> ghost_mat_ok;
	Ref<StandardMaterial3D> ghost_mat_bad;
	RtsoRangeOverlays range_overlays_;
	RtsoRallyPoints rally_points_;
	std::vector<MeshInstance3D *> wall_ghosts;

	bool build_mode = false;
	bool wall_drag_mode = false;
	Vector3 wall_drag_start_pos;
	// 0 A.D. placementSupport.angle: [ and ] turn the preview by pi/12, and a click
	// held and dragged turns it toward the cursor (input.js INPUT_BUILDING_DRAG).
	float placement_angle = rtso::Simulation::DEFAULT_BUILDING_ROT;
	bool build_click_pending = false;
	Vector2 build_click_screen;
	Vector3 build_click_ground;
	int build_click_cell_x = 0, build_click_cell_y = 0;
	bool attack_move_mode = false;
	bool patrol_mode = false;
	bool guard_mode = false;
	bool call_to_arms_mode = false; // the Call to arms button: the next right-click picks the spot
	// The Garrison and Repair buttons (unit_actions.js ACTION_GARRISON / ACTION_REPAIR): the
	// next right-click garrisons or repairs, as holding session.garrison / session.repair does.
	bool garrison_mode = false;
	bool turret_mode = false; // "occupy-turret" armed (ACTION_OCCUPY_TURRET)
	bool repair_mode = false;
	uint8_t current_build_type = rtso::BLD_HOUSE;
	uint32_t selected_building_id = 0;
	// Width in world units (= 0 A.D. tiles) of generated random maps: 128 Tiny
	// to 512 Giant. Set from MatchSettings.map_size; scenario maps use their own.
	int random_map_size = 128;
	int random_map_players = 2; // seated slots: random maps get one base per player
	uint8_t random_map_teams[8] = {}; // 1-based setup team per seat (0 = none), for team placement
	// A scenario's own civs and starting resources (0 A.D. ScriptSettings PlayerData, e.g. the
	// tutorials): they win over Game Setup's, as 0 A.D. locks them on such maps. -1: none.
	int8_t map_civ_[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
	int32_t map_res_[8][4] = {}; // food, wood, stone, metal
	bool map_has_res_[8] = {};
	// The map's fixed teams (0 A.D. PlayerData Team; 0 none, else team + 1), when it has any.
	uint8_t map_team_[8] = {};
	bool map_has_teams_ = false;
	// The scenario's PopulationLimit (0 A.D. ScriptSettings, locked in its game setup); -1: none.
	int map_pop_limit_ = -1;
	int map_player_pop_limit_[8] = {}; // a scenario player's own PopulationLimit; 0 = none
	void apply_map_player_overrides();

	bool dragging = false;
	// Lockstep gate (2026-10-06 OOS fix): host and joiner boot INDEPENDENT
	// random worlds, so pre-Start lockstep turns hash divergent state (first
	// live OOS at turn 1) and reset() at asymmetric wall times offsets the
	// peers' turn counters. Turns/sync stay paused until NMT_GAME_START is
	// applied here: host sets true in _on_lobby_start_pressed, joiner in
	// apply_net_game_start; both set false at Host/Join press.
	bool net_game_started_ = false;
	bool net_status_shown_ = false; // a network warning is on lobby_status
	float game_speed = 1.0f;
	// The match clock is the simulation's (OverlayCounterElapsedTime: g_SimState.timeElapsed),
	// so it stops while paused or waiting for turns and follows loads, rewinds and rejoins.
	double match_elapsed_time() const { return sim ? (double)sim->stats().ticks * rtso::Simulation::FIXED_STEP : 0.0; }

	Vector2 drag_start;
	PackedInt32Array selected_ids;

	// Selection input parity (0 A.D. gui/session/input.js):
	//   Ctrl+0-9 saves / bare 0-9 recalls the selection — input.js:1684 performGroup.
	//   Shift+right-click queues the order (session.queue) — input.js:1345; the sim
	//   keeps each unit's order queue (Simulation::order_* `queued`).
	//   Double-click selects same-type units on screen — input.js:1022.
	PackedInt32Array selected_groups[10];
	bool double_click_pending = false;
	int click_count_ = 1; // 1, 2 (double) or 3 (triple) for the pending left click
	uint64_t last_double_click_ms_ = 0;
	Vector2 last_double_click_pos_;
	// Control-group double press (0 A.D. doublePressTime = 500 ms, input.js:90).
	int last_group_pressed_ = -1;
	uint64_t last_group_press_ms_ = 0;
	// camera.jump.N / camera.jump.set.N bookmarks (F5-F8, Ctrl+F5-F8).
	struct CameraBookmark {
		bool set = false;
		Vector3 pos;
	};
	CameraBookmark camera_bookmarks_[10]; // camera.jump.1-10
	// g_JumpCameraLast: where the camera was before the last jump, for the jump back.
	CameraBookmark camera_jump_last_;
	// Single-player pause from the pause hotkey (Pause, Shift+Space).
	bool hotkey_paused_ = false;
	bool app_unfocused_ = false; // the window lost focus (pauseonfocusloss)
	// Where the local player was last attacked (camera.lastattackfocus = Space).
	bool has_last_attack_ = false;
	Vector3 last_attack_pos_;
	uint32_t last_attack_unit_ = 0; // the attacked unit, which camera.lastattackfocus follows
	// Under-attack alerts (0 A.D. AttackDetection.js + messages.js "attack"):
	// last seen health of each local entity, to spot damage between frames.
	rtso::AttackDetection attack_detection_;
	uint8_t attack_watch_player_ = 0xFF;
	// findIdleUnit cycling (0 A.D. lastIdleUnit / lastIdleClasses, input.js:1645).
	uint32_t last_idle_unit_ = 0;
	int last_idle_kind_ = -1;
	size_t idle_class_index_ = 0; // input.js currIdleClassIndex

	double accumulator = 0.0;
	rtso::AnimationController anim_ctrl;

	// Mobile Viewport Safe Insets (G57)
	float safe_inset_left_ = 0.0f;
	float safe_inset_top_ = 0.0f;
	float safe_inset_right_ = 0.0f;
	float safe_inset_bottom_ = 0.0f;
	void apply_safe_area_insets();
	void set_safe_area_insets(float left, float top, float right, float bottom);
	godot::Rect2 get_safe_area_insets() const {
		return godot::Rect2(safe_inset_left_, safe_inset_top_, safe_inset_right_, safe_inset_bottom_);
	}
	godot::Vector4 debug_top_hud_offsets() const;
	godot::Vector2 debug_bottom_panel_offsets() const;

protected:
	static void _bind_methods();

public:
	RtsoMain();
	~RtsoMain();

	void _notification(int p_what);
	void _ready() override;
	void _process(double delta) override;
	void _unhandled_input(const Ref<InputEvent> &event) override;

	void _on_minimap_clicked(Vector2 world_pos);
	void _on_minimap_order(Vector2 world_pos);
	void trigger_flare(float x, float z);
	Color player_flare_color(uint8_t pid) const;
	void _on_flare_requested(Vector2 world_pos);
	// g_EntityCommands (unit_actions.js): the command row's presses and their hotkeys.
	void _on_entity_command(const String &name);
	void request_delete();
	void _on_delete_confirmed();
	void _on_idle_worker_clicked();
	void _on_speed_button_pressed();
	void _on_speed_selected(int id);
	void set_game_speed(float speed);
	float get_game_speed() const { return game_speed; }
	Dictionary get_overlay_times() const;
	void toggle_pause();
	void toggle_summary();
	void quicksave();
	void quickload();
	void show_notice(const String &text);
	// CLogger's rendered messages (CLogger.cpp RENDER_TIMEOUT 10 s, RENDER_LIMIT 20).
	struct LogLine {
		String text;
		bool error;
		uint64_t until_ms;
	};
	std::vector<LogLine> log_lines_;
	RichTextLabel *log_label_ = nullptr;
	void log_render(const String &text, bool error = false);
	void update_log_messages(bool changed = false);
	uint64_t notice_until_ = 0; // ticks_msec when show_notice's banner hides
	static String speed_text(float speed);
	static String speed_title(float speed);
	static Array speed_choices(bool fast_forward);
	bool fast_forward_allowed() const;
	bool is_game_paused() const { return hotkey_paused_; }
	bool can_pause() const;
	// Engine.HasXmppClient: a game started from the lobby screen.
	bool has_lobby_client() const; // Engine.HasXmppClient: signed in to the lobby
	void open_lobby_page(); // MenuButtons Lobby: page_lobby.xml with {"dialog": true}
	RtsoSafeSelect *speed_menu_ = nullptr; // GameSpeedControl's gameSpeed dropdown
	void _on_menu_button_pressed();
	void _on_ungarrison_pressed();
	void _on_remove_guard_pressed();
	void _on_call_to_arms_pressed();
	void _on_template_info_pressed();
	void _on_template_details_input(const Ref<InputEvent> &event, const String &kind, int id, int civ);
	void _on_portrait_gui_input(const Ref<InputEvent> &event);
	void _on_alert_pressed(int end);
	void _on_autoqueue_pressed(int on);
	void _on_selection_group_clicked(int key);
	void _on_selection_group_gui_input(const Ref<InputEvent> &event, int key);
	void _on_panel_entity_gui_input(const Ref<InputEvent> &event, int slot);
	void _on_research_progress_pressed(int slot);
	void toggle_dev_overlay();
	void _on_dev_command(int command);
	void _on_render_debug_mode(int index);
	void refresh_dev_overlay();
	Dictionary debug_dev_overlay() const;
	void _on_control_group_gui_input(const Ref<InputEvent> &event, int button);
	// input.js performGroup: select, add or snap (select and move the camera) group `grp`.
	enum GroupAction { GROUP_SELECT, GROUP_ADD, GROUP_SNAP };
	void perform_group(int grp, GroupAction action);
	void select_group(int key, bool drop);
	void _on_garrison_group_clicked(int key);
	void _on_barter_sell_selected(int resource);
	void _on_pack_cancel_pressed(int pack);
	Array debug_barter_panel();
	Array debug_garrison_icons() const;
	void _on_stance_selected(int stance);
	void _on_formation_selected(int formation);
	// AutoFormation.js setDefault: right-click on a formation button; the hotkey
	// session.toggledefaultformation switches between none and the last one used.
	void _on_formation_gui_input(const Ref<InputEvent> &event, int formation);
	void set_default_formation(uint8_t formation);
	void toggle_default_formation();
	// AutoFormation.getDefault on a walk or patrol: units in no formation are put
	// into the default one first (Commands.js GetFormationUnitAIs).
	void apply_default_formation(const uint32_t *ids, size_t n);
	uint8_t last_default_formation_ = rtso::FORMATION_BOX;
	void _on_train_pressed(int utype);
	void _on_roster_item_clicked(int index, Vector2 at, int mouse_button);
	void _on_roster_gui_input(const Ref<InputEvent> &event);
	void _on_build_btn_pressed(int btype);
	void _on_tech_pressed(int tech_id);
	void _on_tech_pressed_at(int tech_id, int64_t building_id);
	void _on_upgrade_pressed(int choice);
	void _on_unit_upgrade_pressed(int to);
	void _on_gate_lock_pressed(int lock);
	void _on_dropsite_sharing_pressed(int shared);
	void _on_pack_pressed(int pack);
	void _on_map_selected(int index);
	void _on_civ_selected(int index);
	void _on_ai_diff_selected(int index);
	void _on_ai_behavior_selected(int index);
	void _on_victory_selected(int index);
	void _on_lobby_button_pressed();
	void _on_voice_button_pressed();
	void _on_lobby_host_pressed();
	void _on_lobby_join_pressed();
	void _on_lobby_start_pressed();
	void _on_lobby_seat_selected(int index, int client);
	void apply_net_game_start();
	void show_net_loading_page(const std::string &map_id);
	// The loading page stays while a client loads the host's saved game (its
	// world comes over the network after NMT_SAVED_GAME_START).
	bool net_loading_hold() const { return net_saved_loading_ && !net_game_started_ && !net_disconnected_; }
	void apply_match_settings();
	void sync_victory_selector();
	void apply_game_setup(const rtso::GameSetupParams &setup);
	void _on_lobby_refresh_pressed();
	void _on_lobby_observe_toggled(bool on);
	// The menu lobby (scenes/menu/lobby.gd) hands over a host or join through
	// MatchSettings net_*; this starts it once the match scene is up.
	void _start_menu_net();
	// For LobbySession's heartbeat: whether we host, how many play, whether it started.
	Dictionary get_lobby_state() const;
	// NAT traversal for LobbySession (0 A.D. StunClient + hole punching):
	// {stun (NetHost::stunState), public_ip, public_port, local_ip, local_port}.
	Dictionary get_net_nat_state() const;
	void net_query_stun(const String &server, int port);
	void net_hole_punch(const String &ip, int port);
	void net_retarget_host(const String &ip, int port);
	// LobbySession.verify_join's answer: the lobby name the ticket belongs to ("" unknown).
	void net_lobby_auth_result(int peer, const String &ticket, const String &name);
	// Networked Game Setup for scenes/menu/gamesetup.gd: see net_setup_.
	bool is_net_pregame() const;
	Dictionary get_net_setup_state() const;
	void push_net_setup();
	void assign_net_client(int client, int slot);
	void request_net_player_setting(const String &key, int value);
	void set_net_ready(int status);
	void leave_net_game();
	// 0.28 gamesetup ChatPanel: the chat before Start, shown on the setup page.
	Dictionary get_setup_chat() const; // {rev, text}: the chat log's bbcode
	void send_setup_chat(const String &text);
	// The lockstep state hash this client computed for a turn (0 if not kept).
	int64_t get_turn_hash(int64_t turn) const;
	void refresh_lobby_rooms();
	bool net_lockstep() const { return net_host_.getRole() != rtso::NetRole::None; }
	// Orders go through the TurnManager: always in multiplayer, and offline too
	// (0 A.D. CLocalTurnManager), so every match records a replay.
	bool turn_cmds() const override { return net_lockstep() || local_turns_; }
	// The player this client controls: its lockstep slot in multiplayer, player 0 otherwise.
	// In a replay it is the player whose panels the viewer follows.
	// An observer or defeated player who picked a player view follows that
	// player's panels (0 A.D. g_ViewedPlayer), but orders nothing.
	uint8_t local_player() const override {
		if (turn_mgr_.is_replaying()) return replay_hud_player_;
		if (observer_view_ < rtso::MAX_PLAYERS && spectating()) return observer_view_;
		return seat_player();
	}
	// The seat this client plays: its lockstep slot in multiplayer, Game Setup's
	// assigned slot offline (player 0 by default); 0xFF for an observer.
	uint8_t seat_player() const {
		if (net_lockstep() || turn_mgr_.perspective_changed()) return turn_mgr_.local_player();
		return local_observer_ ? (uint8_t)0xFF : turn_mgr_.offline_seat();
	}
	// This client only watches: an observer, or a player who was defeated
	// (0 A.D. g_IsObserver). Not in a replay, which has its own picker.
	bool spectating() const;
	// 0 A.D. controlsPlayer(g_ViewedPlayer), the gate on every order (input.js
	// doAction, MiniMap.js): not in a replay, not an observer or defeated, and
	// viewing our own side. A winner who stayed still plays (only "defeated"
	// loses control), while the diplomacy rows stay closed to them (isPlayerObserver).
	bool orders_allowed() const;
	// An observer or replay viewer on the whole-map view (g_ViewedPlayer -1): every player's
	// units are selectable (selection.js treats only Gaia's as unowned).
	bool observer_all_view() const {
		return (spectating() || turn_mgr_.is_replaying()) && view_player() == rtso::OBSERVER_VIEW;
	}
	// Whose sight the world, minimap and fog show: the local player, or the
	// perspective a replay viewer or an observer picked (a player, or
	// rtso::OBSERVER_VIEW, the whole map).
	uint8_t view_player() const override {
		if (turn_mgr_.is_replaying()) return replay_view_;
		if (spectating()) return observer_view_;
		return local_player();
	}
	// 0 A.D. PlayerName: a human's lobby name in multiplayer, else Game Setup's name
	// (AI slots get one from their civ's AINames), else "Player N".
	String player_display_name(uint8_t pid) const override;
	bool player_offline(uint8_t pid) const override;
	bool controls_player(uint8_t pid) const override {
		return pid < rtso::MAX_PLAYERS && !turn_mgr_.is_replaying() && !spectating() && pid == seat_player();
	}
	String ai_settings_text(uint8_t pid) const override;
	// soundNotification("nick"): the chime, at most once per 3 s of mentions.
	void sound_notification_nick();
	bool chat_open_page(); // Chat.openPage(""): the chat hotkey; false once disconnected
	uint64_t nick_notify_ms_ = 0;
	bool mass_tribute_held() const override { return rtso::hotkeys::held("session.masstribute"); }
	String player_names_[rtso::MAX_PLAYERS];
	void apply_player_names(const Array &names);
	void post_lockstep(uint8_t cmd_type, const uint32_t *ids, size_t n,
	                   float x = 0, float z = 0, uint32_t tid = 0, uint8_t param = 0);
	// P4.1 host hooks for chat_window.h / diplomacy_dialog.h: thin inline
	// forwards to the existing sim/net path (NMT_CHAT + cmd 22 unchanged).
	String chat_civ_name() { return rtso::civ_info(sim->player_civ(local_player())).name; }
	String chat_user(const String &sender_guid) override;
	// network.js getMismatchMessage("engine", ...) when the host's version is known.
	String mismatch_detail() const;
	// NMT_CHAT (the chat commands are chat_network_command).
	void chat_send_wire(const std::string &wire);
	bool chat_try_cheat(const String &text);
	bool chat_network_command(const String &text) override;
	bool chat_reaches(uint8_t from, uint8_t to, const std::string &tag, uint32_t to_client = UINT32_MAX) const;
	bool chat_is_addressee(const String &sender_guid, const String &tag) override;
	bool chat_local_observer() override;
	bool chat_setup_verbatim() override;
	std::vector<std::pair<String, String>> chat_private_targets() override;
	uint8_t chat_player_of(const String &sender_guid); // the sender's seat, 0xFF for an observer
	uint8_t chat_player_of_client(uint32_t client) const;
	bool player_finished(uint8_t pid) const;
	bool observing_seat() const;
	uint8_t own_seat() const;
	void notify_setup_joins(uint64_t before, uint64_t after);
	// 0.28 gamesetup ChatMessages: joined, left, ready and not ready lines.
	void post_setup_chat(const rtso::NetSetupState &before, const rtso::NetSetupState &after);
	String setup_client_name(const rtso::NetSetupState &n, uint32_t client) const;
	void post_kick_chat();
	// gui.session.formationwalkonly: this order takes its units out of their formations.
	bool order_disbands_formation(uint8_t cmd_type) const;
	// gui.session.selectformationasone: the unit and the rest of its formation.
	std::vector<int32_t> formation_mates(int32_t uid) const;
	rtso::Simulation *ui_sim() override { return sim; }
	void follow_unit(uint32_t id) override {
		if (cam_rig) cam_rig->set_follow_id(0);
		follow_focus(id, false);
	}
	uint32_t ui_selected_building_id() const override { return selected_building_id; }
	const PackedInt32Array &ui_selected_ids() const override { return selected_ids; }
	std::vector<uint32_t> ui_selected_structures() const override { return selected_structures(); }
	std::vector<uint32_t> ui_selected_garrison_holders() const override { return selected_garrison_holders(); }
	const rtso::FogMirages *ui_fog_mirages() const override { return view ? &view->fog_mirages() : nullptr; }
	bool ui_garrison_panel_shown() const override;
	bool ui_observing() const override { return turn_mgr_.is_replaying() || spectating(); }
	bool ui_commands_player(uint8_t pid) const override { return pid == seat_player() && orders_allowed(); }
	uint32_t ui_inspected_foreign(bool *building) const override;
	bool ui_trains_units(uint32_t id) const override { return building_trains_units(id); }
	// Tests: the training roster's rows (name, disabled).
	Array debug_roster() {
		update_command_panel();
		Array out;
		if (ItemList *l = command_panel_.roster_list())
			for (int i = 0; i < l->get_item_count(); ++i) {
				Dictionary d;
				d["text"] = l->get_item_text(i);
				d["disabled"] = l->is_item_disabled(i);
				out.push_back(d);
			}
		return out;
	}
	int ui_batch_size() const override { return user_opts_.batch_size; }
	int ui_pending_batch(uint16_t tmpl) const override;
	int ui_trainer_count(uint16_t tmpl, uint32_t holder) const override { return (int)structure_trainers(tmpl, holder).size(); }
	void _on_barter_requested(int sell_type, int buy_type, int amount);
	Dictionary get_trade_dialog_state() const;
	void _on_trading_goods_requested(const PackedInt32Array &shares);

	// ---- barter_panel.xml parity (docs/REMAINING_GAPS.md:187) -------------
	// Runtime quick-amount (100/500) buy/sell buttons + live price/fee labels
	// layered over scenes/session/barter_dialog.tscn from the C++ owner dir.
	// Every button funnels into _on_barter_requested -> post_lockstep(23)/
	// cmd_barter (main_node.cpp) — no new cmd types, wire format stable.
	struct BarterQuickBtn {
		Button *btn = nullptr;
		int sell = -1;
		int buy = -1;
		int amount = 0;
	};
	std::vector<BarterQuickBtn> barter_quick_;
	CanvasLayer *barter_dialog_ = nullptr;
	Label *barter_price_label_ = nullptr;
	Label *barter_fee_label_ = nullptr;
	void setup_barter_dialog_extras();
	void refresh_barter_dialog();
	int barter_quote(int sell, int buy, int amount) const; // mirrors received

	void _on_queue_slot_gui_input(const Ref<InputEvent> &event, int slot_idx);
	void _on_queue_slot_cancel(int slot_idx);

	void toggle_chat();
	void _on_chat_send_pressed();
	void _on_chat_filter_selected(int index); // ChatHistory.onSelectionChange
	void _on_chat_history_toggled(bool on); // ChatWindow.onToggleExtended: saved to chat.session.extended
	// ChatMessageFilters "Players chat": isPlayerObserver is false, so not a seat whose game is over.
	bool chat_sender_is_player(const String &sender_guid) override {
		const uint8_t p = chat_player_of(sender_guid);
		return p < rtso::MAX_PLAYERS && !player_finished(p);
	}
	void _on_chat_text_submitted(const String &text);
	void _on_chat_input_gui(const Ref<InputEvent> &event);
	std::vector<String> chat_autocomplete_names() override;
	void _on_chat_channel_btn_pressed(int channel_idx);

	void toggle_diplomacy();
	void toggle_objectives();
	Dictionary get_match_settings_info() const;
	String match_settings_description() const;
	String match_settings_player_state() const;
	void _on_tutorial_ready_pressed();
	void resign();
	void _toggle_session_dialog(String name);
	double last_flare_time_ = -10.0; // input.js g_LastFlareTime
	// messages.js g_FlareRateLimitLastTimes: when the last incoming flares were drawn (ms).
	std::vector<uint64_t> flare_received_ms_;
	int get_target_marker_count() const { return (int)range_overlays_.marker_count(); } // tests
	int get_target_marker_actor_count() const { return range_overlays_.marker_actor_count(); } // tests
	PackedFloat32Array debug_range_rings() const { // tests: radii of the selection's range rings
		PackedFloat32Array out;
		for (float r : range_overlays_.ring_radii()) out.push_back(r);
		return out;
	}
	bool select_first_own(bool building); // tests
	int get_work_sound_count() const { return session_audio_.work_sounds_played(); } // tests
	void debug_gather_nearest();
	// 0 A.D. unit_actions "setup-trade-route": traders take a route in two right-clicks,
	// the origin market, then the destination (lockstep command 35 sends both).
	// Returns false when no selected trader trades at b; *tooltip gets the cursor text.
	bool trade_route_click(const rtso::Simulation::Building &b, bool execute, bool queued, String *tooltip, const char **cursor = nullptr);
	Dictionary debug_trade_route_clicks();
	Dictionary debug_trade_tip(int64_t market);
	Dictionary debug_trade_click(int64_t market, bool queued);
	Dictionary debug_tutorial_panel() const { return tutorial_panel_.debug_state(); }
	Dictionary debug_guard_step(int step);
	PackedStringArray debug_train_buttons(int btype);
	Dictionary debug_command_button(const String &prefix);
	Dictionary debug_selection_grid(bool select_all);
	void debug_set_carry(int64_t unit_id, int res, int amount);
	void debug_set_resource(int res, float amount);
	Dictionary debug_player_state(int player);
	bool debug_placing() const;
	PackedInt32Array debug_highlight() const { return highlight_ids_; }
	bool debug_structure_outlined(int64_t id) const;
	int64_t debug_building_queue(int64_t id) const {
		if (sim)
			for (const auto &b : sim->buildings())
				if (b.id == (uint32_t)id) {
					int64_t n = 0;
					for (const auto &q : b.queue) n += q.count > 0 ? q.count : 1;
					return n;
				}
		return -1;
	}
	// Rally point flags drawn this frame (rally_point_renderer.h).
	int debug_rally_flags() const { return rally_points_.flag_count(); }
	int debug_status_sprites() const { return view ? view->status_sprite_count() : 0; }
	PackedStringArray debug_status_rank_icons() const { return view ? view->status_rank_icons() : PackedStringArray(); }
	int debug_status_xp_bars() const { return view ? view->status_xp_bars() : 0; }
	PackedStringArray debug_status_aura_sources() const { return view ? view->status_aura_sources() : PackedStringArray(); }
	PackedInt32Array debug_selected_structures() const {
		PackedInt32Array out;
		for (uint32_t id : selected_structures()) out.push_back((int32_t)id);
		return out;
	}
	int64_t debug_guard_target(int64_t uid) const { return sim ? (int64_t)sim->unit_guard_target((uint32_t)uid) : 0; }
	// The unit's current order: kind (UnitOrderKind), target and whether it may capture (allowCapture),
	// with how it moves (formation, speed, action state, velocity, waypoints left).
	Dictionary debug_unit_order(int64_t uid) const {
		Dictionary d;
		if (!sim) return d;
		const rtso::UnitOrder o = sim->unit_order((uint32_t)uid);
		d["kind"] = (int)o.kind;
		d["target"] = (int64_t)o.target;
		d["capture"] = (o.flags & rtso::ORDER_FLAG_ALLOW_CAPTURE) != 0;
		d["formation"] = (int64_t)sim->formation_of_unit((uint32_t)uid);
		d["has_order"] = sim->unit_has_order((uint32_t)uid);
		auto uv = sim->units();
		for (size_t i = 0; i < uv.count; ++i)
			if (uv.id[i] == (uint32_t)uid) {
				d["speed"] = uv.speed[i];
				d["state"] = (int)uv.action_state[i];
				d["type"] = (int)uv.unit_type[i];
				d["vel"] = Vector2(uv.vel_x[i], uv.vel_z[i]);
				const float *xs = nullptr, *zs = nullptr;
				d["waypoints"] = sim->unit_waypoints_left(i, &xs, &zs);
				if (xs) d["next_wp"] = Vector2(xs[0], zs[0]);
			}
		return d;
	}
	// Tests: where a unit stands on screen (viewport pixels), or (-1, -1).
	// Tests: a unit's ground position (x, z), or where it is walking to with `goal`;
	// (0, 0) when it is gone.
	Vector2 debug_unit_ground(int64_t uid, bool goal) const {
		if (!sim) return Vector2();
		auto uv = sim->units();
		for (size_t i = 0; i < uv.count; ++i)
			if (uv.id[i] == (uint32_t)uid && uv.alive[i])
				return goal ? Vector2(uv.target_x[i], uv.target_z[i]) : Vector2(uv.pos_x[i], uv.pos_z[i]);
		return Vector2();
	}
	Vector2 debug_unit_screen(int64_t uid) const {
		if (!sim || !cam_rig || !cam_rig->get_camera()) return Vector2(-1, -1);
		auto uv = sim->units();
		for (size_t i = 0; i < uv.count; ++i)
			if (uv.id[i] == (uint32_t)uid && uv.alive[i])
				return cam_rig->get_camera()->unproject_position(Vector3(uv.pos_x[i], sim->get_terrain_height(uv.pos_x[i], uv.pos_z[i]), uv.pos_z[i]));
		return Vector2(-1, -1);
	}
	Vector2 debug_unit_pos(int64_t uid) const { // tests: any player's unit, world x/z
		if (!sim) return Vector2();
		auto uv = sim->units();
		for (size_t i = 0; i < uv.count; ++i)
			if (uv.id[i] == (uint32_t)uid) return Vector2(uv.pos_x[i], uv.pos_z[i]);
		return Vector2();
	}
	Vector2 debug_resource_screen(int64_t rid) const {
		if (!sim || !cam_rig || !cam_rig->get_camera()) return Vector2(-1, -1);
		auto rv = sim->resources();
		for (size_t r = 0; r < rv.count; ++r)
			if (rv.id[r] == (uint32_t)rid && rv.alive[r])
				return cam_rig->get_camera()->unproject_position(Vector3(rv.x[r], sim->get_terrain_height(rv.x[r], rv.z[r]), rv.z[r]));
		return Vector2(-1, -1);
	}
	// Tests: where a structure's centre is on screen, or (-1, -1).
	Vector2 debug_building_screen(int64_t bid) const {
		if (!sim || !cam_rig || !cam_rig->get_camera()) return Vector2(-1, -1);
		for (const auto &b : sim->buildings())
			if (b.id == (uint32_t)bid)
				return cam_rig->get_camera()->unproject_position(Vector3(b.x, sim->get_terrain_height(b.x, b.z), b.z));
		return Vector2(-1, -1);
	}
	Vector2 debug_ground_at(Vector2 screen) const {
		const Vector3 g = get_ground_click_pos(screen);
		return Vector2(g.x, g.z);
	}
	// Tests: structure `id`'s world position and the viewed player's fog state there
	// (Simulation::FOW_*), empty when there is no such structure.
	Dictionary debug_building_fow(int64_t id) const {
		Dictionary d;
		if (!sim) return d;
		for (const auto &b : sim->buildings())
			if (b.id == (uint32_t)id) {
				d["pos"] = Vector2(b.x, b.z);
				d["fow"] = (int64_t)sim->fow_state_at(b.x, b.z, view_player());
			}
		return d;
	}
	// Tests: unit `id`'s pick box as drawn this frame (feet, height), empty when not drawn.
	Dictionary debug_unit_pick_box(int64_t id) const {
		Dictionary d;
		if (!view) return d;
		for (const auto &b : view->unit_pick_boxes())
			if (b.id == (uint32_t)id) {
				d["base"] = b.base;
				d["height"] = b.height;
				d["half_w"] = b.half_w;
				d["half_d"] = b.half_d;
			}
		return d;
	}
	// Tests: the click point under `screen` in 3D, the terrain height there and the drawn sea
	// level (-1e9 without water).
	Dictionary debug_click_surface(Vector2 screen) const {
		Dictionary d;
		const Vector3 g = get_ground_click_pos(screen);
		d["pos"] = g;
		d["terrain"] = sim ? sim->get_terrain_height(g.x, g.z) : 0.0f;
		d["water"] = sim && sim->water_height() > -0.5f ? sim->water_height() + rtso::NAV_WATER_MARGIN : -1e9f;
		return d;
	}
	Dictionary debug_inspector(const String &what);
	// Tests: another player's unit or structure `id` shown as a click on it would (the inspector).
	void debug_inspect_entity(int64_t id); // tests: the selection details of a unit, units, a structure, a resource or an animal
	Dictionary debug_civic_centre_roof();
	Dictionary debug_selection_state();
	Array debug_command_row();
	void _on_top_counter_gui_input(const Ref<InputEvent> &event);
	PackedInt32Array debug_spawn_units(const String &template_id, int count, int player = -1, int near_player = -1);
	void debug_complete_research(int player, int tech); // tests: the technology finishes for the player
	void debug_set_capture_points(int64_t building_id, int player, float points); // tests: taken from the owner
	PackedFloat32Array debug_capture_points(int64_t building_id) const; // tests: each player's points
	Vector2 debug_spawn_offset(const String &template_id, int count, int player = -1) const;
	PackedInt32Array debug_spawn_fauna(int fauna_type, int count, bool owned, float dx = 16.0f, float dz = 16.0f);
	int64_t debug_spawn_mine(float x, float z, int max_gatherers);
	String debug_unit_model_path(int civ, int unit_type);
	String debug_template_model_path(const String &template_id);
	Dictionary debug_snap_placement(int btype, float x, float z, bool hover, float stale_angle);
	int debug_put_inside(const PackedInt32Array &ids, int64_t holder);
	void debug_select_entity(int64_t id);
	void debug_select_units(const PackedInt32Array &ids);
	void debug_select_structures(const PackedInt32Array &ids);
	int64_t debug_spawn_relic(float dx, float dz);
	void debug_capture_relic(const PackedInt32Array &ids, int64_t relic);
	int64_t debug_civic_centre_id(int player) const;
	int64_t debug_spawn_building(int type, float dx, float dz, int player = -1, float progress = 1.0f); // progress < 1: a foundation
	Dictionary debug_research_any();
	Array debug_building_looks() const; // [id, type, model, x, z] of each map structure drawn by its own template
	Dictionary debug_structure_types(bool with_civ = false);
	void debug_delete_units(const PackedInt32Array &ids);
	void debug_add_xp(const PackedInt32Array &ids, float xp); // tests: Promotion IncreaseXp
	Dictionary debug_diplomacy() const { return diplo_dialog_.debug_state(); } // tests: the diplomacy dialog's rows
	void debug_set_observer_view(int player) { set_observer_view(player); } // tests: an observer follows a player (-1: none)
	Dictionary debug_dialogs();
	uint32_t building_under_mouse(Vector2 mouse_pos, float *hit_t = nullptr, bool any_player = false) const;
	size_t own_unit_under_mouse(Vector2 mouse_pos, bool any_player = false, float radius = 2.0f) const;
	uint32_t entity_under_mouse(Vector2 mouse, bool with_props = true) const; // Engine.PickEntityAtPoint
	std::vector<int32_t> band_box_ids(Vector2 a, Vector2 b) const;
	// input.js INPUT_UNIT_POSITION: a right press with units selected, and the ground line
	// drawn while the button is held.
	struct RightLine {
		bool active = false, drawing = false;
		Vector2 start;
		std::vector<Vector2> pts;
	} right_line_;
	bool replaying_right_ = false;
	PackedVector2Array last_line_targets_; // tests: the last walk-custom positions
	PackedInt32Array last_line_entities_;
	void finish_right_line(const Ref<InputEventMouseButton> &release);
	// input.js INPUT_PRESELECTEDACTION: a command button armed an action for the next left click.
	bool debug_armed() const { return any_armed_mode(); } // tests: an armed action (input.js preSelectedAction)
	bool any_armed_mode() const {
		return attack_move_mode || patrol_mode || guard_mode || call_to_arms_mode || garrison_mode || turret_mode || repair_mode;
	}
	bool armed_target_ok(const Vector3 &at) const;
	// unit_actions.js "guard" target under the cursor: an own or allied unit (not a selected
	// one) or structure; 0 when none.
	uint32_t guard_target_at(const Vector3 &at) const;
	// unit_actions.js "remove-guard": some selected unit is guarding.
	bool selection_guarding() const;
	bool run_armed_click(const Ref<InputEventMouseButton> &press);
	bool minimap_unit_order(Vector2 world_pos);
	Dictionary debug_right_line() const {
		Dictionary d;
		d["active"] = right_line_.active;
		d["drawing"] = right_line_.drawing;
		d["points"] = (int64_t)right_line_.pts.size();
		d["targets"] = last_line_targets_;
		d["entities"] = last_line_entities_;
		return d;
	}
	// input.js setHighlightList: the entity under the cursor, or what the band box would pick.
	void update_highlight();
	PackedInt32Array highlight_ids_;
	// The hover pick (PickEntityAtPoint) is redone when the mouse or camera moves, or
	// every kHoverRepickMs while they stand still (units walk under the cursor).
	PackedInt32Array hover_pick_;
	uint32_t hover_target_ = 0; // the same pick with treasures and scenery: what a right click acts on
	Vector2 hover_mouse_ = Vector2(-1e9f, -1e9f);
	Transform3D hover_cam_;
	uint64_t hover_at_ms_ = 0;
	static constexpr uint64_t kHoverRepickMs = 200;
	struct BatchTraining {
		bool active = false;
		bool from_unit = false; // trained by a unit (Trainer on a unit), else a structure
		uint32_t holder = 0;
		uint16_t tmpl = 0;
		int batches = 0;
		int allowed = -1; // canBeAddedCount at the last click, -1 = no limit
	};
	BatchTraining batch_training_;
	void add_training(bool from_unit, uint32_t holder, uint16_t tmpl);
	void flush_training_batch();
	int template_can_be_added(uint16_t tmpl) const;
	bool can_afford_template(uint16_t tmpl, int count) const;
	uint32_t press_pick_unit_ = 0; // the own unit under the left press (input.js clickedEntity)
	Dictionary debug_unset_rally_step(int step);
	// Tests: `building_id`'s owner sets its rally point at (x, z) (no lockstep).
	void debug_set_rally(int64_t building_id, float x, float z);
	Label *cursor_tip_ = nullptr;
	// 0.28 placementTooltip (input.js updateBuildingPlacementPreview): why the structure or
	// wall can't go where the cursor is, or a wall's cost; wall_fail_tip_ ("Cannot build wall
	// here!") holds until the cursor moves.
	RichTextLabel *placement_tip_ = nullptr; // markup: the wall cost's header font and icons
	bool placement_tip_error_ = false;
	String wall_fail_tip_;
	Vector2 wall_fail_mouse_;
	void set_placement_tip(const String &text, bool error);
	String placement_failure_text(int result, uint8_t btype, float x, float z) const;
	Dictionary debug_placement_tip() const;
	Dictionary debug_cursor_tip();
	void _on_tree_node_added(Node *n);
	void _on_ui_click();
	void _on_ui_click_item(int) { _on_ui_click(); }
	int debug_ui_clicks() const { return ui_clicks_; }
	bool debug_overlay_visible() const { return debug_overlay && debug_overlay->is_visible(); }
	static void reset_default_cursor();
	int voices_played_ = 0; // tests: unit voice lines started
	int debug_voices() const { return voices_played_; }
	// Tests: the control groups and camera as a save stores them, and setting the camera.
	Dictionary debug_saved_gui() const {
		Dictionary d;
		Array groups;
		for (int g = 0; g < 10; ++g) groups.push_back(selected_groups[g]);
		d["groups"] = groups;
		if (cam_rig) d["camera"] = cam_rig->camera_data();
		return d;
	}
	void debug_set_camera_data(const Dictionary &c) { if (cam_rig) cam_rig->set_camera_data(c); }
	// Menu.xml: the menu panel hangs under the Menu button.
	Rect2 menu_button_rect() const { return menu_btn ? menu_btn->get_global_rect() : Rect2(); }
	// ChatInput.getOpenHotkeyTooltip (also the menu's Chat entry, MenuButtons.js).
	String chat_open_hotkey_tooltip() const;
	// input.js: session.flare held arms a flare, never in a replay (!g_IsReplay).
	bool flare_held() const;
	bool selected_building_shoots() const; // own, with BuildingAI arrows (try_focus_fire)
	void refresh_chat_tooltips();
	void _refresh_menu_tooltip();
	void open_template_viewer(const String &kind, int id, int civ);
	bool is_chat_open() const { return chat_window_.is_open(); }
	Dictionary debug_chat_tooltips() {
		Dictionary d; d["input"] = chat_window_.input_tooltip(); d["open"] = chat_open_hotkey_tooltip();
		PackedStringArray names;
		for (const String &n : chat_autocomplete_names()) names.push_back(n);
		d["names"] = names;
		return d;
	}
	void _on_diplo_stance_changed(int stance, int player_idx);
	void _on_diplo_tribute_pressed(int player_idx, int res_type);
	void _on_diplo_spy_pressed(int player_idx);
	void _on_diplo_attack_request_pressed(int player_idx);

	bool load_map(String map_id);
	bool load_map_seeded(String map_id, uint32_t seed);
	PackedStringArray get_available_maps() const;
	void set_civ(int civ_idx);
	Dictionary get_civ_info(int civ_idx) const;
	Dictionary get_tech_tree() const;
	Dictionary get_match_summary() const;
	// Named saves (0 A.D. gui/savedgames): writes path and its .json metadata.
	bool save_game_as(String name, String description);
	// Replays (0 A.D. ps/Replay.cpp + gui/replaymenu).
	bool is_replay_playback() const { return turn_mgr_.is_replaying(); }
	Dictionary get_replay_status() const;
	void set_replay_view(int player); // -1: observer
	// What this build reads, for the replay menu's compatibility check.
	static int get_save_version();
	static int get_replay_format();
	void _on_replay_view_selected(int index);
	bool save_game(String path = "user://saves/quicksave.rts0sav", String description = "");
	bool load_game(String path = "user://saves/quicksave.rts0sav");
	PackedByteArray get_fow_grid() const;
	void order_guard_units(uint32_t target_id);
	void spawn_large_battle(int total_units = 1000);

	void set_use_english_voice_orders(bool enable) { use_english_voice_orders_ = enable; }
	bool is_use_english_voice_orders() const { return use_english_voice_orders_; }

private:
	Vector3 get_ground_click_pos(Vector2 mouse_pos) const;
	std::pair<int, int> mouse_cell(Vector2 mouse_pos) const;
	void clear_selection();
	bool selection_can_garrison(uint32_t building_id) const;
	bool selection_can_occupy_turret(uint32_t building_id) const;
	// unit_actions.js on an enemy structure or Gaia scenery: session.attack (F) asks "attack"
	// (specificness 9) and session.capture (C) "capture" (10) first; else the first of the two
	// possible in specificness order, attack before capture. CLICK_NONE: neither (a walk).
	enum StructureClick { CLICK_NONE = 0, CLICK_ATTACK, CLICK_CAPTURE };
	StructureClick structure_click(uint32_t building_id) const;
	StructureClick scenery_click(uint32_t scenery_id) const;
	uint32_t transport_under(float x, float z) const;
	// unit_actions.js "set-rallypoint" target under the cursor: a resource, else a structure
	// (not one of the selected).
	// cmd and ent: the RallyCommand and unit or treasure the target alone doesn't give.
	void rally_target_at(const Vector3 &at, uint32_t &res, uint32_t &bld, uint32_t *ent = nullptr, uint8_t *cmd = nullptr) const;
	// GetTradingRouteGain for a default route from the selected market to market `to`, for
	// the trader it trains (a merchant ship at a dock). False when either is gone.
	bool rally_route_gain(uint32_t to, rtso::Simulation::TradeGain &g) const;
	bool selection_can_board(uint32_t holder) const;
	void update_hud();
	void update_inspector();
	void prune_selection();
	// selection.js: any number of the player's structures can be selected. selected_building_id
	// is the first (the one the single-structure panels show), extra_structures_ the others.
	std::vector<uint32_t> extra_structures_;
	uint32_t extras_primary_ = 0; // the selected_building_id the extras belong to
	std::vector<uint32_t> selected_structures() const;
	std::vector<uint32_t> selected_garrison_holders() const; // the selection's holders with units inside
	std::vector<uint32_t> selected_structures_like_primary() const;
	void set_selected_structures(const std::vector<uint32_t> &ids);
	void prune_structure_selection();
	// getBuildingsWhichCanTrainEntity: the selected structures that train `tmpl` now.
	std::vector<uint32_t> structure_trainers(uint16_t tmpl, uint32_t holder) const;
	std::vector<uint32_t> band_box_structures(Vector2 a, Vector2 b) const;
	uint32_t own_fauna_at(float x, float z) const;
	void update_command_panel();
	void enter_build_mode(bool enable, uint8_t btype = rtso::BLD_HOUSE);
	// Commands.js "construct-wall" from wall_drag_start_pos to `end` (input.js tryPlaceWall): false when no piece can go down.
	bool try_place_wall(const Vector3 &end, bool queued);
	uint32_t first_selected_builder() const;
	void update_ghost_mesh();
	// The placement rotation for a building at (x, z): docks face the sea (GetDockAngle).
	float placement_angle_at(uint8_t btype, float x, float z) const;
	// 0 A.D. input.js snap to edges: with isSnapToEdgesEnabled, turns placement_angle to the
	// nearest structure's sides (ObstructionSnap.getPosition) and moves (cell_x, cell_y) to the
	// building cell nearest the flush position.
	bool snap_placement_to_edges(uint8_t btype, int &cell_x, int &cell_y, bool hover = false);
	bool debug_force_snap_ = false; // debug_snap_placement: snapping on whatever the option and hotkey say
	// GetFoundationSnapData snapEntities: a wall end within half a tower of one of
	// the player's wall towers snaps onto it.
	void try_train_unit(rtso::UnitType utype);
	void try_train_template(uint32_t building_id, uint16_t tmpl, int count = 1);
	// The batch the next train click queues: gui.session.batchtrainingsize while Shift is held.
	int train_batch() const;
	void try_breed(uint32_t building_id, uint8_t ftype, int count = 1);
	uint32_t scenery_at(float x, float z) const; // Gaia scenery under a ground point, 0 if none
	void try_train_from_unit(uint32_t unit_id, uint16_t tmpl, int count);
	void cycle_idle_worker();
	enum IdleKind { IDLE_WORKER, IDLE_WARRIOR, IDLE_ANY, IDLE_BUILDER };
	void find_idle_unit(IdleKind kind);
	bool over_own_rally_structure(const Vector3 &at, bool need_rally = true) const;
	// 0 A.D. "focus-fire": the selected arrow-shooting structure aims at the enemy under `at`.
	bool try_focus_fire(const Vector3 &at);
	void detect_attacks(const std::vector<rtso::Simulation::AttackedEvent> &attacks);
	// 0 A.D. ChatMessageFormatSimulation tribute, barter, phase and diplomacy lines.
	void show_session_notices(const std::vector<rtso::Simulation::SessionNotice> &notices);
	// messages.js "dialog" notifications: a trigger script's yes-no dialog (triggers_demo).
	void show_trigger_dialogs(const std::vector<rtso::Simulation::TriggerDialog> &dialogs);
	String colored_player_name(uint8_t pid) const; // colorizePlayernameByID
	void toggle_diplomacy_colors();
	void update_cursor();
	std::string cursor_name_;
	void notify_attack(Vector3 pos, uint8_t attacker, bool structure, uint32_t target = 0);
	void _on_chat_line_input(const Ref<InputEvent> &event, const String &action);
	Array debug_chat_overlay() const { return chat_window_.overlay_state(); }
	// ChatHistory's lines as the history page draws them (BBCode).
	String debug_chat_history() const { return chat_window_.history(); }
	// Tests: the camera's target on the ground and the unit it follows.
	Dictionary debug_camera() const {
		Dictionary d;
		if (!cam_rig) return d;
		d["target"] = Vector2(cam_rig->get_target_pos().x, cam_rig->get_target_pos().z);
		d["follow"] = (int64_t)cam_rig->follow_id();
		d["zoom"] = cam_rig->get_zoom();
		return d;
	}
	void play_voice(const char *path);
	// Tells the loading page how far setup got (signal load_progress).
	void load_progress(int percent, const char *description) { emit_signal("load_progress", percent, String(description)); }
	void play_civ_voice(const char *action, uint32_t unit_id = 0);
	// selection.js addList: the select sound of the first entity this change added (none, none).
	void play_select_added(const PackedInt32Array &before_units, const std::vector<uint32_t> &before_structs);
	// SoundGroups/select of an animal, a supply or a treasure (selection.js plays Gaia's too).
	void play_fauna_select(uint8_t fauna_type);
	void play_inspected_select();
	// The unit a left click selects (input.js PickEntityAtPoint): the visible unit nearest the
	// cursor within 2 units, if it is selectable; SIZE_MAX otherwise.
	size_t click_pick_unit(Vector2 mouse_pos, bool any_player) const;
	void update_music_state();
	// The session music's tick, driven by the MenuAudio autoload (methods bound for
	// GDScript live in session_bindings.cpp) so tracks fade and advance while paused.
	void tick_session_music(double delta) { music_.tick(delta); }
	Dictionary get_audio_state() const;
	bool debug_combat_sound(float x, float z) { return sim && session_audio_.debug_combat_sound(sim, local_player(), x, z); }
	rtso::RtsoMusic music_;
	rtso::RtsoSessionAudio session_audio_;
	uint8_t music_civ_ = 0xFF;
	uint32_t defeated_heard_ = 0; // players whose defeat this client already heard
	bool music_primed_ = false;
	PackedInt32Array delete_pending_;
	void play_alarm(const char *path);
	void render_flare(float x, float z, uint8_t seat, const String &guid);
	void play_sfx(const char *path);
	void play_spatial_sfx(const char *path, Vector3 pos);

	bool use_english_voice_orders_ = true; // Default to English orders

	// ---- Match archive: replays, saves and statistics (match_archive.cpp) ----
	bool local_turns_ = true;
	bool summary_shown_ = false; // per match (was a function static: only the first match got one)
	bool finished_asked_ = false; // QuitConfirmationDefeat shown for the local player
	bool seat_was_playing_ = false; // the local seat was seen still playing this session
	uint32_t match_seed_ = 0;
	bool replay_recording_ = false;
	bool replay_finished_ = false;
	String replay_dir_;            // user://replays/<id>/ of the game being recorded
	String watched_replay_dir_;    // the replay being watched (0 A.D. Engine.GetCurrentReplayDirectory)
	Dictionary replay_meta_;       // its metadata.json, minus what is filled at write time
	uint32_t replay_written_turn_ = 0;
	CanvasLayer *replay_layer_ = nullptr; // holds the replay's player view row
	uint32_t replay_defeated_mask_ = 0; // players seen defeated, for onPlayersFinished
	OptionButton *replay_view_picker_ = nullptr;
	Label *replay_observer_label_ = nullptr; // PlayerViewControl's "Observer Mode"
	bool replay_view_gaia_ = false;          // Gaia picked (its view is the observer's)
	bool replay_oos_shown_ = false;          // OutOfSyncReplay has asked
	uint32_t net_oos_shown_turn_ = 0;        // OutOfSyncNetwork showed this turn (0 none)
	static constexpr int kReplayViewGaiaId = 100;
	uint8_t replay_view_ = rtso::OBSERVER_VIEW; // replay perspective (0 A.D. replay "Player view")
	// An observer's perspective (0 A.D. session "viewPlayer" dropdown): the whole
	// map, or one player's sight and panels. Presentation only.
	uint8_t observer_view_ = rtso::OBSERVER_VIEW;
	// PlayerViewControl.onPlayersFinished: the players already defeated or victorious last
	// frame (-1 before the first), so a loss of the viewed player switches to Observer once.
	int finished_seen_ = -1;
	void follow_viewed_player_finish();
	// The developer overlay's Change perspective: the player view dropdown shows for a
	// player too, and picking in it takes that player over (PlayerViewControl.setChangePerspective).
	bool change_perspective_ = false;
	// The developer overlay's culling boxes (GameView.cpp Culling/LockCullCamera,
	// SceneRenderer.cpp DisplayFrustum/RenderDebugBounds): culling on by default,
	// the cull camera following the view camera until locked. Presentation only.
	bool culling_enabled_ = true;
	bool lock_cull_camera_ = false;
	// Lock cull camera's frozen frustum: outward planes for the animation culling,
	// corners for the frustum overlay (SceneRenderer.cpp draws m_CullCamera).
	std::vector<godot::Plane> locked_cull_planes_;
	std::vector<godot::Vector3> locked_cull_corners_;
	void lock_cull_camera();
	void sync_cull_camera();
	// The developer overlay's time warp (TimeWarp.js, offline only): a snapshot every
	// 10 turns, session.timewarp.rewind loads the latest (and drops it), and
	// session.timewarp.fastforward runs the game at 20x while held.
	bool time_warp_ = false;
	bool time_warp_ff_ = false;
	uint32_t time_warp_turn_ = UINT32_MAX;
	uint32_t time_warp_serial_ = 0;
	std::vector<String> time_warp_snapshots_;
	void set_time_warp(bool on);
	void time_warp_tick();
public:
	bool time_warp_rewind();
	int time_warp_snapshot_count() const { return (int)time_warp_snapshots_.size(); }
	// The developer overlay's Activate Rejoin Test (0.28 Engine.ActivateRejoinTest), once its
	// box's OK is pressed: after the next turn a copy of the game is rejoined from a snapshot
	// (rejoin_sim_), then every turn is replayed onto the copy too and the two compared, until
	// the match ends. It can't be turned off.
	void activate_rejoin_test();
	Dictionary get_rejoin_test() const;
private:
	bool rejoin_test_ = false;
	bool random_map_ = false; // the match's map was generated (0.28 mapType "random")
	std::unique_ptr<rtso::Simulation> rejoin_sim_;
	std::unique_ptr<rtso::TurnManager> rejoin_tm_;
	uint32_t rejoin_from_turn_ = 0, rejoin_turns_ = 0, rejoin_oos_turn_ = 0;
	void rejoin_test_turn();
public:
	// 0.28's simulation test flags (main.cpp, readme.txt), from the command line or
	// set_sim_tests: {"ooslog": bool, "serializationtest": bool, "rejointest": int turn
	// (-1 off), "unique_logs": bool}. -ooslog writes the state, readable (.txt) and
	// binary (.dat), after every turn to user://sim_log/<pid>/; -serializationtest
	// loads a copy of the game from a snapshot each turn, runs the next turn on both and
	// compares them, a mismatch printed, logged to sim_log/<pid>/serializationtest/ and
	// the test stopped; -rejointest=N starts the rejoin test at turn N; -unique-logs
	// names the OOS dump oos_dump_<unix time>_<pid>.
	void set_sim_tests(const Dictionary &d);
	Dictionary get_sim_tests() const;
private:
	bool ooslog_ = false, serialization_test_ = false;
	int rejoin_test_turn_ = -1;
	String sim_log_dir_;
	std::unique_ptr<rtso::Simulation> serial_sim_;
	std::unique_ptr<rtso::TurnManager> serial_tm_;
	uint32_t serial_turns_ = 0, serial_fail_turn_ = 0;
	void sim_tests_turn();
	int perspective_pick_ = -1; // the dropdown item picked, until the lockstep turn seats us there
	OptionButton *observer_view_picker_ = nullptr;
	Label *observer_mode_label_ = nullptr;
	// 0 A.D. top_panel/FollowPlayer (g_FollowPlayer): an observer or replay viewer
	// watching a player can have the camera follow that player's commands and attacks.
	bool follow_player_ = false;
	CheckBox *observer_follow_box_ = nullptr;
	CheckBox *replay_follow_box_ = nullptr;
	CheckBox *new_follow_player_box();
	// PlayerViewControl.xml and FollowPlayer.xml: the picker, "Observer Mode" and Follow Player
	// placed in the top panel's frame; returns the row holding them.
	Control *new_player_view_row(OptionButton *picker, Label *observer_label, CheckBox *follow) const;
	int followed_view() const;
	void set_follow_player(bool on);
	bool is_follow_player() const { return follow_player_; }
	void follow_player_tick();
	void follow_focus(uint32_t id, bool select);
	void follow_command(const rtso::NetCommand &c);
	bool observer_view_gaia_ = false; // Gaia picked (its view is the observer's)
	void sync_observer_view_picker();
	OptionButton *new_player_view_picker() const;
	void refresh_player_view_colors(OptionButton *picker) const;
	void update_observer_view_picker();
	void set_observer_view(int player);
	void _on_observer_view_selected(int index);
	uint8_t replay_hud_player_ = 0;
	// Single player with no Human slot (0 A.D. Game Setup lets every slot be AI):
	// you watch the AIs play with the map revealed and give no orders.
	bool local_observer_ = false;
	// 0 A.D. PauseOverlay: "Game Paused", "Click to Resume Game" for your own pause,
	// and in multiplayer "Paused by" the pausing players.
	Button *pause_overlay_ = nullptr;
	Label *pause_resume_label_ = nullptr;
	RichTextLabel *pause_by_label_ = nullptr;
	void update_pause_overlay();
	bool close_open_dialogs(); // session.js closeOpenDialogs; true when one was open
	// Nothing for Escape to close or cancel: no session dialog or menu, no placement, no
	// armed action and an empty selection. A phone's Back button then opens the menu.
	bool nothing_to_cancel();
	void _on_civ_icon_gui_input(const Ref<InputEvent> &event); // CivIcon.onPress
	void _on_flare_button_pressed(); // MiniMapFlareButton.onPress
	bool selected_building_trains_units() const; // its Trainer has entities (session.autorallypoint)
	bool building_trains_units(uint32_t id) const;
	Dictionary debug_selected_building() const;
	Dictionary debug_queue_slot(int i);
	Dictionary debug_minimap_panel() const;
	// An Attacked message on `target` (a unit) from `attacker_owner`, through the session's AttackDetection.
	void debug_attacked(int64_t target, int64_t attacker_owner);
	void debug_defeat_player(int64_t pid); // tests: a player loses (single player only)
	void _on_pause_overlay_pressed();
	// 0 A.D. NetworkStatusOverlay: "Waiting for players to connect:" with the
	// players still loading, "Synchronizing gameplay with other players…" while
	// a rejoin loads, and "disconnected": the host is gone, so the match stops
	// here. Exit leaves for the summary (endGame(true)).
	Control *net_status_overlay_ = nullptr;
	Label *net_status_title_ = nullptr;
	Button *net_status_exit_ = nullptr;
	Button *net_status_faq_ = nullptr;
	void _on_open_faq_pressed();
	Label *net_status_label_ = nullptr;
	RichTextLabel *net_loading_clients_ = nullptr; // loadingClientsText
	bool net_disconnected_ = false; // session.js g_Disconnected
	bool net_saved_loading_ = false; // client: loading the host's saved game, or a rejoin's world, under the loading page
	int net_rejoin_load_frames_ = 0; // frames the rejoin loading page draws before the world loads
	void update_net_status_overlay();
	void _on_disconnected_exit_pressed();
	// 0 A.D. gameStateNotifications (overlay.netwarnings): network.js's
	// warnings, one per client, each shown for NETWORK_WARNING_SHOW_MS, the
	// local one ("server") first; checked once a second as NetClient and
	// NetServer CheckConnection(s) do.
	RichTextLabel *net_warnings_label_ = nullptr;
	std::map<std::string, std::pair<uint64_t, String>> net_warnings_; // guid -> (added ms, text)
	uint64_t net_warnings_checked_ms_ = 0;
	void update_net_warnings();
	void apply_net_options();
	// 0 A.D. ChatMessageFormatNetwork: join, rejoin, left and kick lines.
	std::vector<rtso::PlayerAssignmentMsg::Entry> chat_assignments_;
	uint64_t chat_assignments_seen_ = 0;
	size_t kicks_chatted_ = 0;
	size_t rejoined_chatted_ = 0;
	void post_network_chat();
	bool loading_replay_start_ = false; // load_game is restoring a replay's starting save
	void build_replay_view_picker();
	void begin_recording_from_save(const String &save_path);
	Dictionary match_settings_snapshot(uint32_t seed) const;
	void apply_settings_snapshot(const Dictionary &settings);
	Array player_list() const;
	void begin_match_recording(const rtso::GameSetupParams *mp_setup);
	void write_replay(bool ended = false, bool force = false); // ended: leaving the match (0.28 endGame), which adds the summary; force: even under 3 s
	void stop_replay_recording(bool ended = false);
	bool start_pending_replay();
	bool load_pending_save();
	// 0 A.D. "Host Saved Game": the save the host loaded before hosting; Start
	// then continues it for everyone (host_start_saved_game).
	String net_saved_game_;
	// The save's seats a human may take (lower-case name in the save, or empty
	// for an AI or defeated player), by player id; filled with net_saved_game_.
	std::vector<String> net_saved_seats_;
	bool net_saved_setup() const { return !net_saved_game_.is_empty() && !net_game_started_; }
	void host_start_saved_game();
	void tick_match_archive();
	Dictionary statistics_summary() const;
	Dictionary summary_gui(bool in_game) const;
	void quit_confirmation(const String &kind, bool won = false, bool ask_exit = true);
	void tick_quit_confirmation();
	bool write_save_metadata(const String &path, const String &description) const;
	static Dictionary saved_gui(const String &path);
	void restore_saved_gui(const Dictionary &gui);
	bool load_world(const String &path, const Dictionary &gui);
	// The quicksave's GUI data (its control groups), kept for this match like 0.28's
	// CTurnManager::m_QuickSaveMetadata.
	Dictionary quicksave_gui_;
	bool quicksaved_ = false;
	void write_save_thumbnail(const String &path);
};

#endif
