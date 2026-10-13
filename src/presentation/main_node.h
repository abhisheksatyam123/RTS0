#ifndef RTSO_MAIN_NODE_H
#define RTSO_MAIN_NODE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/button.hpp>
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
#include "selection_grid.h"
#include "objectives_dialog.h"
#include "tutorial_panel.h"
#include "range_overlays.h"
#include "music_manager.h"
#include "session_audio.h"

using namespace godot;

class RtsoMain : public Node3D, public RtsoChatHost, public RtsoDiplomacyHost, public RtsoProductionQueueHost {
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
	AudioStreamPlayer *ambient_player = nullptr;
	AudioStreamPlayer *ambient_wind_player = nullptr;
	AudioStreamPlayer *ambient_water_player = nullptr;
	AudioStreamPlayer3D *spatial_sfx_player = nullptr;

	// 1. Top HUD Bar
	Panel *top_hud_panel = nullptr;
	RtsoTopPanel top_panel_;
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
	void lobby_effective_seats(uint8_t out[8]);
	rtso::NetHost net_host_;
	rtso::TurnManager turn_mgr_;
	rtso::Arena net_arena_{64 * 1024};
	std::vector<rtso::LobbyRoomInfo> lobby_room_cache_;
	std::string lobby_http_host_ = "127.0.0.1";
	uint16_t lobby_http_port_ = 8088;

	// 2. Bottom Inspector Panel (5-Bar Stack: Health, Capture, XP, Stamina, Aura)
	Panel *inspector_panel = nullptr;
	TextureRect *inspector_portrait = nullptr;
	Label *inspector_title = nullptr;
	Label *inspector_class = nullptr;
	ProgressBar *inspector_hp_bar = nullptr;
	Label *inspector_hp_label = nullptr;
	ProgressBar *inspector_capture_bar = nullptr;
	Label *inspector_capture_label = nullptr;
	ProgressBar *inspector_xp_bar = nullptr;
	Label *inspector_xp_label = nullptr;
	ProgressBar *inspector_stamina_bar = nullptr;
	Label *inspector_stamina_label = nullptr;
	ProgressBar *inspector_aura_bar = nullptr;
	Label *inspector_aura_label = nullptr;
	Label *inspector_stats = nullptr;
	Label *inspector_extra = nullptr;
	Button *ungarrison_btn = nullptr;
	Button *template_info_btn_ = nullptr;
	// Options page settings the session uses (refresh_user_options).
	struct UserOptions {
		uint32_t generation = 0;
		int batch_size = 5;
		float wounded_fraction = 0.33f;
		float flare_time = 6.0f;
		float drag_delta = 4.0f;
		bool attack_notice = true;
		bool tribute_notice = true;
		bool barter_notice = true;
		uint8_t phase_notice = 1; // gui.session.notifications.phase: 0 none, 1 completed, 2 all
	} user_opts_;
	void refresh_user_options();
	RtsoSelectionGrid selection_grid_;
	RtsoSelectionGrid garrison_grid_;

	// 3. Command & Action Panel (Right of Inspector)
	Panel *command_panel = nullptr;
	HBoxContainer *stances_bar = nullptr;
	Button *stance_buttons_[5] = {}; // indexed by UnitStance
	HBoxContainer *actions_bar = nullptr;
	// Wraps its buttons; the scroll keeps a long build menu inside the panel.
	ScrollContainer *production_scroll = nullptr;
	HFlowContainer *production_bar = nullptr;
	HBoxContainer *queue_bar = nullptr;
	Label *command_title = nullptr;
	// Every civ roster unit and hero the selected building trains (0 A.D. names,
	// costs after team bonuses), above the command panel. Rebuilt only when
	// roster_list_key_ changes so hover and tooltips survive between frames.
	ItemList *roster_list = nullptr;
	uint64_t roster_list_key_ = ~0ull;

	RtsoProductionQueue production_queue_;

	// 4. Banner Overlay
	Panel *banner_panel = nullptr;
	Label *banner_label = nullptr;
	// Map trigger messages (0 A.D. GuiInterface AddTimeNotification), centred under the top bar.
	Label *time_notification_label_ = nullptr;

	// 5/6. In-Game Chat Overlay + Diplomacy & Tribute Dialog: P4.1 leaf
	// widgets (chat_window.h / diplomacy_dialog.h) own their panel trees;
	// the bound handler methods below delegate into them.
	RtsoChatWindow chat_window_;
	RtsoDiplomacyDialog diplo_dialog_;
	rtso::RtsoObjectivesDialog objectives_dialog_;
	rtso::RtsoTutorialPanel tutorial_panel_;

	// 3D Placement Ghost & Rally Marker
	MeshInstance3D *ghost_mesh = nullptr;
	Ref<StandardMaterial3D> ghost_mat_ok;
	Ref<StandardMaterial3D> ghost_mat_bad;
	MeshInstance3D *rally_flag = nullptr;
	RtsoRangeOverlays range_overlays_;
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
	void apply_map_player_overrides();

	bool dragging = false;
	bool paused_via_menu = false;
	void set_menu_paused(bool paused);
	// Lockstep gate (2026-10-06 OOS fix): host and joiner boot INDEPENDENT
	// random worlds, so pre-Start lockstep turns hash divergent state (first
	// live OOS at turn 1) and reset() at asymmetric wall times offsets the
	// peers' turn counters. Turns/sync stay paused until NMT_GAME_START is
	// applied here: host sets true in _on_lobby_start_pressed, joiner in
	// apply_net_game_start; both set false at Host/Join press.
	bool net_game_started_ = false;
	bool net_status_shown_ = false; // a network warning is on lobby_status
	float game_speed = 1.0f;
	double match_elapsed_time = 0.0;

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
	CameraBookmark camera_bookmarks_[4];
	// Single-player pause from the pause hotkey (Pause, Shift+Space).
	bool hotkey_paused_ = false;
	// Where the local player was last attacked (camera.lastattackfocus = Space).
	bool has_last_attack_ = false;
	Vector3 last_attack_pos_;
	// Under-attack alerts (0 A.D. AttackDetection.js + messages.js "attack"):
	// last seen health of each local entity, to spot damage between frames.
	rtso::AttackDetection attack_detection_;
	uint8_t attack_watch_player_ = 0xFF;
	// findIdleUnit cycling (0 A.D. lastIdleUnit / lastIdleClasses, input.js:1645).
	uint32_t last_idle_unit_ = 0;
	int last_idle_kind_ = -1;

	double accumulator = 0.0;
	rtso::AnimationController anim_ctrl;

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
	void _on_flare_requested(Vector2 world_pos);
	// g_EntityCommands (unit_actions.js): the command row's buttons and their hotkeys.
	void _on_entity_command(const String &name);
	void request_delete();
	void add_entity_command(const char *name, const char *label, const char *hotkey, const String &tip,
			const char *icon, bool enabled = true, int count = 0);
	void _on_delete_confirmed();
	void _on_idle_worker_clicked();
	void _on_speed_button_pressed();
	void _on_speed_selected(int id);
	void set_game_speed(float speed);
	float get_game_speed() const { return game_speed; }
	void toggle_pause();
	void toggle_summary();
	void quicksave();
	void quickload();
	void show_notice(const String &text);
	uint64_t notice_until_ = 0; // ticks_msec when show_notice's banner hides
	void _on_replay_speed_selected(int index);
	static String speed_text(float speed);
	static Array speed_choices(bool replay);
	bool is_game_paused() const { return hotkey_paused_ || paused_via_menu; }
	PopupMenu *speed_menu_ = nullptr;
	void _on_menu_button_pressed();
	void _on_ungarrison_pressed();
	void _on_remove_guard_pressed();
	void _on_call_to_arms_pressed();
	void _on_template_info_pressed();
	void _on_alert_pressed(int end);
	void _on_autoqueue_pressed(int on);
	void _on_selection_group_clicked(int key);
	void _on_selection_group_gui_input(const Ref<InputEvent> &event, int key);
	void select_group(int key, bool drop);
	void _on_garrison_group_clicked(int key);
	void _on_stance_selected(int stance);
	void _on_formation_selected(int formation);
	void _on_train_pressed(int utype);
	void _on_roster_item_clicked(int index, Vector2 at, int mouse_button);
	void _on_build_btn_pressed(int btype);
	void _on_tech_pressed(int tech_id);
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
	uint8_t local_player() const {
		if (turn_mgr_.is_replaying()) return replay_hud_player_;
		if (observer_view_ < rtso::MAX_PLAYERS && spectating()) return observer_view_;
		return seat_player();
	}
	// The seat this client plays: its lockstep slot in multiplayer, player 0
	// offline; 0xFF for an observer (multiplayer, or every slot AI).
	uint8_t seat_player() const {
		if (net_lockstep()) return turn_mgr_.local_player();
		return local_observer_ ? (uint8_t)0xFF : (uint8_t)0;
	}
	// This client only watches: an observer, or a player who was defeated
	// (0 A.D. g_IsObserver). Not in a replay, which has its own picker.
	bool spectating() const;
	// Whose sight the world, minimap and fog show: the local player, or the
	// perspective a replay viewer or an observer picked (a player, or
	// rtso::OBSERVER_VIEW, the whole map).
	uint8_t view_player() const {
		if (turn_mgr_.is_replaying()) return replay_view_;
		if (spectating()) return observer_view_;
		return local_player();
	}
	// 0 A.D. PlayerName: a human's lobby name in multiplayer, else Game Setup's name
	// (AI slots get one from their civ's AINames), else "Player N".
	String player_display_name(uint8_t pid) const override;
	bool mass_tribute_held() const override { return rtso::hotkeys::held("session.masstribute"); }
	String player_names_[rtso::MAX_PLAYERS];
	void apply_player_names(const Array &names);
	void post_lockstep(uint8_t cmd_type, const uint32_t *ids, size_t n,
	                   float x = 0, float z = 0, uint32_t tid = 0, uint8_t param = 0);
	// P4.1 host hooks for chat_window.h / diplomacy_dialog.h: thin inline
	// forwards to the existing sim/net path (NMT_CHAT + cmd 22 unchanged).
	String chat_civ_name() { return rtso::civ_info(sim->player_civ(local_player())).name; }
	String chat_user(const String &sender_guid) override;
	// NMT_CHAT, or a host's 0 A.D. chat command (/kick name, /ban name).
	void chat_send_wire(const std::string &wire);
	bool chat_try_cheat(const String &text);
	rtso::Simulation *ui_sim() override { return sim; }
	uint32_t ui_selected_building_id() const override { return selected_building_id; }
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
	void _on_chat_text_submitted(const String &text);
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
	int get_target_marker_count() const { return (int)range_overlays_.marker_count(); } // tests
	bool select_first_own(bool building); // tests
	int get_work_sound_count() const { return session_audio_.work_sounds_played(); } // tests
	void debug_gather_nearest();
	// 0 A.D. unit_actions "setup-trade-route": traders take a route in two right-clicks,
	// the origin market, then the destination (lockstep command 35 sends both).
	// Returns false when no selected trader trades at b; *tooltip gets the cursor text.
	bool trade_route_click(const rtso::Simulation::Building &b, bool execute, bool queued, String *tooltip);
	Dictionary debug_trade_route_clicks();
	Dictionary debug_guard_step(int step);
	PackedStringArray debug_train_buttons(int btype);
	Dictionary debug_command_button(const String &prefix);
	Dictionary debug_selection_grid(bool select_all);
	Dictionary debug_civic_centre_roof();
	Dictionary debug_selection_state();
	Array debug_command_row();
	void debug_delete_units(const PackedInt32Array &ids);
	Dictionary debug_dialogs();
	uint32_t building_under_mouse(Vector2 mouse_pos, float *hit_t = nullptr) const;
	Dictionary debug_unset_rally_step(int step);
	uint32_t trade_origin_market_ = 0;
	Label *cursor_tip_ = nullptr;
	std::vector<uint32_t> trade_origin_traders_;
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
	void update_hud();
	void update_inspector();
	// 0 A.D. selects what the player doesn't own too (another player's unit or
	// structure, an animal, a tree or a mine) to show its details, with no commands
	// (EntitySelection; the command panels need the player's own entities).
	enum InspectKind : uint8_t { INSPECT_NONE, INSPECT_UNIT, INSPECT_BUILDING, INSPECT_FAUNA, INSPECT_RESOURCE };
	uint8_t inspected_kind_ = INSPECT_NONE;
	uint32_t inspected_id_ = 0;
	bool inspect_at(const Vector3 &click_pos);
	bool fill_inspected_details();  // false when the inspected entity is gone or out of sight
	bool fill_livestock_details();  // the player's own selected livestock
	void prune_selection();
	uint32_t own_fauna_at(float x, float z) const;
	void update_command_panel();
	void keep_command_buttons(Control *bar, int old_count);
	void enter_build_mode(bool enable, uint8_t btype = rtso::BLD_HOUSE);
	void update_ghost_mesh();
	// The placement rotation for a building at (x, z): docks face the sea (GetDockAngle).
	float placement_angle_at(uint8_t btype, float x, float z) const;
	// GetFoundationSnapData snapEntities: a wall end within half a tower of one of
	// the player's wall towers snaps onto it.
	void try_train_unit(rtso::UnitType utype);
	void try_train_template(uint32_t building_id, uint16_t tmpl, int count = 1);
	// The batch the next train click queues: gui.session.batchtrainingsize while Shift is held.
	int train_batch() const;
	void try_breed(uint32_t building_id, uint8_t ftype, int count = 1);
	uint32_t scenery_at(float x, float z) const; // Gaia scenery under a ground point, 0 if none
	static constexpr int ROSTER_ANIMAL = 0x10000; // roster_list metadata flag: a corral animal
	static constexpr int ROSTER_TECH = 0x20000;   // roster_list metadata flag: a training unit's technology
	void update_roster_list(const rtso::Simulation::Building *b);
	bool update_unit_roster(uint32_t unit_id);
	int add_roster_item(uint8_t pid, uint16_t tmpl, String &tip);
	void try_train_from_unit(uint32_t unit_id, uint16_t tmpl, int count);
	uint32_t trainer_unit_id_ = 0; // the selected training unit whose roster is shown, else 0
	void cycle_idle_worker();
	enum IdleKind { IDLE_WORKER, IDLE_WARRIOR, IDLE_ANY, IDLE_BUILDER };
	void find_idle_unit(IdleKind kind);
	bool over_own_rally_structure(const Vector3 &at) const;
	void detect_attacks(const std::vector<rtso::Simulation::AttackedEvent> &attacks);
	// 0 A.D. ChatMessageFormatSimulation tribute, barter, phase and diplomacy lines.
	void show_session_notices(const std::vector<rtso::Simulation::SessionNotice> &notices);
	String colored_player_name(uint8_t pid) const; // colorizePlayernameByID
	void toggle_diplomacy_colors();
	void update_cursor();
	std::string cursor_name_;
	void notify_attack(Vector3 pos, uint8_t attacker, bool structure);
	void play_voice(const char *path);
	// Tells the loading page how far setup got (signal load_progress).
	void load_progress(int percent, const char *description) { emit_signal("load_progress", percent, String(description)); }
	void play_civ_voice(const char *action);
	void update_music_state();
	Dictionary get_audio_state() const;
	rtso::RtsoMusic music_;
	rtso::RtsoSessionAudio session_audio_;
	uint8_t music_civ_ = 0xFF;
	ConfirmationDialog *delete_dialog_ = nullptr; // DeleteSelectionConfirmation
	PackedInt32Array delete_pending_;
	void play_alarm(const char *path);
	void play_sfx(const char *path);
	void play_spatial_sfx(const char *path, Vector3 pos);

	bool use_english_voice_orders_ = true; // Default to English orders

	// ---- Match archive: replays, saves and statistics (match_archive.cpp) ----
	bool local_turns_ = true;
	bool summary_shown_ = false; // per match (was a function static: only the first match got one)
	bool finished_asked_ = false; // QuitConfirmationDefeat shown for the local player
	uint32_t match_seed_ = 0;
	bool replay_recording_ = false;
	bool replay_finished_ = false;
	String replay_dir_;            // user://replays/<id>/ of the game being recorded
	String watched_replay_dir_;    // the replay being watched (0 A.D. Engine.GetCurrentReplayDirectory)
	Dictionary replay_meta_;       // its metadata.json, minus what is filled at write time
	uint32_t replay_written_turn_ = 0;
	Label *replay_label_ = nullptr;
	OptionButton *replay_view_picker_ = nullptr;
	OptionButton *replay_speed_picker_ = nullptr;
	Button *replay_pause_btn_ = nullptr;
	uint8_t replay_view_ = rtso::OBSERVER_VIEW; // replay perspective (0 A.D. replay "Player view")
	// An observer's perspective (0 A.D. session "viewPlayer" dropdown): the whole
	// map, or one player's sight and panels. Presentation only.
	uint8_t observer_view_ = rtso::OBSERVER_VIEW;
	OptionButton *observer_view_picker_ = nullptr;
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
	Label *pause_by_label_ = nullptr;
	void update_pause_overlay();
	bool close_open_dialogs(); // session.js closeOpenDialogs; true when one was open
	void _on_pause_overlay_pressed();
	// 0 A.D. NetworkStatusOverlay: "Waiting for players to connect:" with the
	// players still loading, "Synchronizing gameplay with other players…" while
	// a rejoin loads, and "disconnected": the host is gone, so the match stops
	// here. Exit leaves for the summary (endGame(true)).
	Control *net_status_overlay_ = nullptr;
	Label *net_status_label_ = nullptr;
	RichTextLabel *net_loading_clients_ = nullptr; // loadingClientsText
	bool net_disconnected_ = false; // session.js g_Disconnected
	void update_net_status_overlay();
	void _on_disconnected_exit_pressed();
	// 0 A.D. ChatMessageFormatNetwork: join, rejoin, left and kick lines.
	std::vector<rtso::PlayerAssignmentMsg::Entry> chat_assignments_;
	uint64_t chat_assignments_seen_ = 0;
	size_t kicks_chatted_ = 0;
	void post_network_chat();
	bool loading_replay_start_ = false; // load_game is restoring a replay's starting save
	void build_replay_view_picker();
	void begin_recording_from_save(const String &save_path);
	Dictionary match_settings_snapshot(uint32_t seed) const;
	void apply_settings_snapshot(const Dictionary &settings);
	Array player_list() const;
	void begin_match_recording(const rtso::GameSetupParams *mp_setup);
	void write_replay();
	void stop_replay_recording();
	bool start_pending_replay();
	bool load_pending_save();
	void tick_match_archive();
	Dictionary statistics_summary() const;
	Dictionary summary_gui(bool in_game) const;
	void quit_confirmation(const String &kind, bool won = false, bool ask_exit = true);
	void tick_quit_confirmation();
	bool write_save_metadata(const String &path, const String &description) const;
	void write_save_thumbnail(const String &path);
};

#endif
