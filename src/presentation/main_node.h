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
#include <godot_cpp/classes/option_button.hpp>
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

#include "../simulation/simulation.h"
#include "../animation/animation_controller.h"
#include "world_view_node.h"
#include "camera_rig.h"
#include "selection_rect.h"
#include "debug_overlay.h"
#include "minimap_view.h"

using namespace godot;

class RtsoMain : public Node3D {
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
	TextureRect *civ_emblem = nullptr;
	TextureRect *phase_emblem = nullptr;
	TextureRect *res_icons[4] = {nullptr};
	Label *res_labels[4] = {nullptr};
	TextureRect *pop_icon = nullptr;
	Label *pop_label = nullptr;
	Button *idle_worker_btn = nullptr;
	Label *clock_label = nullptr;
	Label *wave_label = nullptr;
	Button *speed_btn = nullptr;
	Button *menu_btn = nullptr;
	OptionButton *map_selector = nullptr;
	OptionButton *civ_selector = nullptr;
	OptionButton *ai_diff_selector = nullptr;
	OptionButton *ai_behavior_selector = nullptr;
	OptionButton *victory_selector = nullptr;
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

	struct QueueSlotUI {
		Panel *panel = nullptr;
		TextureRect *icon = nullptr;
		ProgressBar *progress = nullptr;
		Label *chip = nullptr;
	};
	QueueSlotUI queue_slots[8];

	// 4. Banner Overlay
	Panel *banner_panel = nullptr;
	Label *banner_label = nullptr;

	// 5. In-Game Chat Overlay
	Panel *chat_panel = nullptr;
	RichTextLabel *chat_log = nullptr;
	OptionButton *chat_channel = nullptr;
	LineEdit *chat_input = nullptr;
	Button *chat_send_btn = nullptr;

	// 6. Diplomacy & Tribute Dialog (8-player matrix)
	Panel *diplomacy_panel = nullptr;
	OptionButton *diplo_stances[8] = {nullptr};
	Label *diplo_player_names[8] = {nullptr};
	Button *diplo_close_btn = nullptr;

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

	bool dragging = false;
	bool paused_via_menu = false;
	float game_speed = 1.0f;
	double match_elapsed_time = 0.0;
	size_t last_idle_index = 0;

	Vector2 drag_start;
	PackedInt32Array selected_ids;

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
	void _on_idle_worker_clicked();
	void _on_speed_button_pressed();
	void _on_menu_button_pressed();
	void _on_ungarrison_pressed();
	void _on_stance_selected(int stance);
	void _on_formation_selected(int formation);
	void _on_train_pressed(int utype);
	void _on_build_btn_pressed(int btype);
	void _on_tech_pressed(int tech_id);
	void _on_map_selected(int index);
	void _on_civ_selected(int index);
	void _on_ai_diff_selected(int index);
	void _on_ai_behavior_selected(int index);
	void _on_victory_selected(int index);
	void _on_lobby_button_pressed();
	void _on_voice_button_pressed();
	void _on_lobby_host_pressed();
	void _on_lobby_join_pressed();
	void _on_lobby_refresh_pressed();
	void refresh_lobby_rooms();
	bool net_lockstep() const { return net_host_.getRole() != rtso::NetRole::None; }
	void post_lockstep(uint8_t cmd_type, const uint32_t *ids, size_t n,
	                   float x = 0, float z = 0, uint32_t tid = 0, uint8_t param = 0);
	void _on_barter_requested(int sell_type, int buy_type, int amount);

	void _on_queue_slot_gui_input(const Ref<InputEvent> &event, int slot_idx);
	void _on_queue_slot_cancel(int slot_idx);

	void toggle_chat();
	void _on_chat_send_pressed();
	void _on_chat_text_submitted(const String &text);

	void toggle_diplomacy();
	void _on_diplo_stance_changed(int stance, int player_idx);
	void _on_diplo_tribute_pressed(int player_idx, int res_type);

	bool load_map(String map_id);
	PackedStringArray get_available_maps() const;
	void set_civ(int civ_idx);
	Dictionary get_civ_info(int civ_idx) const;
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
	void update_hud();
	void update_inspector();
	void update_command_panel();
	void enter_build_mode(bool enable, uint8_t btype = rtso::BLD_HOUSE);
	void update_ghost_mesh();
	void try_train_unit(rtso::UnitType utype);
	void cycle_idle_worker();
	void play_voice(const char *path);
	void play_civ_voice(const char *action);
	void play_alarm(const char *path);
	void play_sfx(const char *path);
	void play_spatial_sfx(const char *path, Vector3 pos);

	bool use_english_voice_orders_ = true; // Default to English orders
};

#endif
