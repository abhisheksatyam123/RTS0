#ifndef RTSO_MAIN_NODE_H
#define RTSO_MAIN_NODE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
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
	AudioStreamPlayer *music_player = nullptr;
	AudioStreamPlayer *battle_music_player = nullptr;
	AudioStreamPlayer *voice_player = nullptr;
	AudioStreamPlayer *alarm_player = nullptr;
	AudioStreamPlayer *sfx_player = nullptr;
	AudioStreamPlayer *ambient_player = nullptr;
	AudioStreamPlayer *ambient_wind_player = nullptr;
	AudioStreamPlayer *ambient_water_player = nullptr;
	AudioStreamPlayer3D *spatial_sfx_player = nullptr;
	float combat_intensity = 0.0f;

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
	Label *lobby_status = nullptr;
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

	// 3. Command & Action Panel (Right of Inspector)
	Panel *command_panel = nullptr;
	HBoxContainer *stances_bar = nullptr;
	HBoxContainer *actions_bar = nullptr;
	HBoxContainer *production_bar = nullptr;
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

	// 5/6. In-Game Chat Overlay + Diplomacy & Tribute Dialog: P4.1 leaf
	// widgets (chat_window.h / diplomacy_dialog.h) own their panel trees;
	// the bound handler methods below delegate into them.
	RtsoChatWindow chat_window_;
	RtsoDiplomacyDialog diplo_dialog_;

	// 3D Placement Ghost & Rally Marker
	MeshInstance3D *ghost_mesh = nullptr;
	Ref<StandardMaterial3D> ghost_mat_ok;
	Ref<StandardMaterial3D> ghost_mat_bad;
	MeshInstance3D *rally_flag = nullptr;
	std::vector<MeshInstance3D *> wall_ghosts;

	bool build_mode = false;
	bool wall_drag_mode = false;
	Vector3 wall_drag_start_pos;
	bool attack_move_mode = false;
	bool patrol_mode = false;
	bool guard_mode = false;
	uint8_t current_build_type = rtso::BLD_HOUSE;
	uint32_t selected_building_id = 0;
	// Width in world units (= 0 A.D. tiles) of generated random maps: 128 Tiny
	// to 512 Giant. Set from MatchSettings.map_size; scenario maps use their own.
	int random_map_size = 128;

	bool dragging = false;
	bool paused_via_menu = false;
	// Lockstep gate (2026-10-06 OOS fix): host and joiner boot INDEPENDENT
	// random worlds, so pre-Start lockstep turns hash divergent state (first
	// live OOS at turn 1) and reset() at asymmetric wall times offsets the
	// peers' turn counters. Turns/sync stay paused until NMT_GAME_START is
	// applied here: host sets true in _on_lobby_start_pressed, joiner in
	// apply_net_game_start; both set false at Host/Join press.
	bool net_game_started_ = false;
	float game_speed = 1.0f;
	double match_elapsed_time = 0.0;
	size_t last_idle_index = 0;

	Vector2 drag_start;
	PackedInt32Array selected_ids;

	// Selection input parity (0 A.D. gui/session/input.js):
	//   Ctrl+0-9 saves / bare 0-9 recalls the selection — input.js:1684 performGroup.
	//   Shift+right-click stages waypoints (session.queue) — input.js:1345; RTS0's
	//   UnitView keeps a single target_x/z, so pending points are staged here and
	//   issued from _process as the active one is reached.
	//   Double-click selects same-type units on screen — input.js:1022.
	PackedInt32Array selected_groups[10];
	struct QueuedWaypoint {
		std::vector<Vector3> pts; // pts[active] = destination last issued to the sim
		int active = 0;
	};
	std::unordered_map<uint32_t, QueuedWaypoint> waypoint_queue;
	bool double_click_pending = false;

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
	void _on_flare_requested(Vector2 world_pos);
	void _on_idle_worker_clicked();
	void _on_speed_button_pressed();
	void _on_menu_button_pressed();
	void _on_ungarrison_pressed();
	void _on_stance_selected(int stance);
	void _on_formation_selected(int formation);
	void _on_train_pressed(int utype);
	void _on_roster_item_clicked(int index, Vector2 at, int mouse_button);
	void _on_build_btn_pressed(int btype);
	void _on_tech_pressed(int tech_id);
	void _on_upgrade_pressed(int cancel);
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
	void apply_net_game_start();
	void apply_match_settings();
	void sync_victory_selector();
	void apply_game_setup(const rtso::GameSetupParams &setup);
	void _on_lobby_refresh_pressed();
	void refresh_lobby_rooms();
	bool net_lockstep() const { return net_host_.getRole() != rtso::NetRole::None; }
	// The player this client controls: its lockstep slot in multiplayer, player 0 otherwise.
	uint8_t local_player() const { return net_lockstep() ? turn_mgr_.local_player() : (uint8_t)0; }
	void post_lockstep(uint8_t cmd_type, const uint32_t *ids, size_t n,
	                   float x = 0, float z = 0, uint32_t tid = 0, uint8_t param = 0);
	// P4.1 host hooks for chat_window.h / diplomacy_dialog.h: thin inline
	// forwards to the existing sim/net path (NMT_CHAT + cmd 22 unchanged).
	String chat_civ_name() { return rtso::civ_info(sim->player_civ(local_player())).name; }
	void chat_send_wire(const std::string &wire) { net_host_.sendChat(wire); }
	rtso::Simulation *ui_sim() override { return sim; }
	uint32_t ui_selected_building_id() const override { return selected_building_id; }
	void _on_barter_requested(int sell_type, int buy_type, int amount);

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
	void _on_diplo_stance_changed(int stance, int player_idx);
	void _on_diplo_tribute_pressed(int player_idx, int res_type);

	bool load_map(String map_id);
	bool load_map_seeded(String map_id, uint32_t seed);
	PackedStringArray get_available_maps() const;
	void set_civ(int civ_idx);
	Dictionary get_civ_info(int civ_idx) const;
	Dictionary get_tech_tree() const;
	Dictionary get_match_summary() const;
	bool save_game(String path = "user://saves/quicksave.rts0sav");
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
	void drop_staged_waypoints();
	void update_hud();
	void update_inspector();
	void update_command_panel();
	void enter_build_mode(bool enable, uint8_t btype = rtso::BLD_HOUSE);
	void update_ghost_mesh();
	void try_train_unit(rtso::UnitType utype);
	void try_train_template(uint32_t building_id, uint16_t tmpl);
	void update_roster_list(const rtso::Simulation::Building *b);
	void cycle_idle_worker();
	void play_voice(const char *path);
	void play_civ_voice(const char *action);
	void play_alarm(const char *path);
	void play_sfx(const char *path);
	void play_spatial_sfx(const char *path, Vector3 pos);

	bool use_english_voice_orders_ = true; // Default to English orders
};

#endif
