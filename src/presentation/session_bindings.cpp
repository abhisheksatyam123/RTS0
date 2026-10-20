// RtsoMain's Godot bindings: every method, property and signal GDScript sees (P4.6,
// split out of main_node.cpp).

#include "main_node.h"
#include "l10n.h"
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/os.hpp>
#include <unordered_set>
#include <cstring>
#include <godot_cpp/classes/theme.hpp>
#include "popup_safe.h"
#include "simulation/petra_chat.h"
#include "simulation/rms_generator.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/script.hpp>
#include "user_config.h"
#include "display_colors.h"
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include "../core/diagnostics/logger.h"
#include "../core/diagnostics/perf_tracker.h"
#include "../core/job_system.h"
#include "../graphics/assets/model_library.h"
#include <sstream>
using namespace godot;
using rtso::sprintf0;

void RtsoMain::_bind_methods() {
	ClassDB::bind_method(D_METHOD("enter_build_mode", "enable", "btype"), &RtsoMain::enter_build_mode, DEFVAL(rtso::BLD_HOUSE));
	ClassDB::bind_method(D_METHOD("clear_selection"), &RtsoMain::clear_selection);
	ClassDB::bind_method(D_METHOD("_on_minimap_clicked", "world_pos"), &RtsoMain::_on_minimap_clicked);
	ClassDB::bind_method(D_METHOD("_on_flare_requested", "world_pos"), &RtsoMain::_on_flare_requested);
	ClassDB::bind_method(D_METHOD("_on_minimap_order", "world_pos"), &RtsoMain::_on_minimap_order);
	ClassDB::bind_method(D_METHOD("_on_idle_worker_clicked"), &RtsoMain::_on_idle_worker_clicked);
	ClassDB::bind_method(D_METHOD("_on_speed_button_pressed"), &RtsoMain::_on_speed_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_speed_selected", "id"), &RtsoMain::_on_speed_selected);
	ClassDB::bind_method(D_METHOD("set_game_speed", "speed"), &RtsoMain::set_game_speed);
	ClassDB::bind_method(D_METHOD("get_game_speed"), &RtsoMain::get_game_speed);
	ClassDB::bind_method(D_METHOD("get_overlay_times"), &RtsoMain::get_overlay_times);
	ClassDB::bind_method(D_METHOD("toggle_pause"), &RtsoMain::toggle_pause);
	ClassDB::bind_method(D_METHOD("_refresh_menu_tooltip"), &RtsoMain::_refresh_menu_tooltip);
	ClassDB::bind_method(D_METHOD("can_pause"), &RtsoMain::can_pause);
	ClassDB::bind_method(D_METHOD("quicksave"), &RtsoMain::quicksave);
	ClassDB::bind_method(D_METHOD("quickload"), &RtsoMain::quickload);
	ClassDB::bind_method(D_METHOD("is_game_paused"), &RtsoMain::is_game_paused);
	ClassDB::bind_method(D_METHOD("_on_menu_button_pressed"), &RtsoMain::_on_menu_button_pressed);
	ClassDB::bind_method(D_METHOD("get_audio_state"), &RtsoMain::get_audio_state);
	ClassDB::bind_method(D_METHOD("debug_combat_sound", "x", "z"), &RtsoMain::debug_combat_sound);
	ClassDB::bind_method(D_METHOD("get_trade_dialog_state"), &RtsoMain::get_trade_dialog_state);
	ClassDB::bind_method(D_METHOD("_on_trading_goods_requested", "shares"), &RtsoMain::_on_trading_goods_requested);
	ClassDB::bind_method(D_METHOD("_on_ungarrison_pressed"), &RtsoMain::_on_ungarrison_pressed);
	ClassDB::bind_method(D_METHOD("_on_remove_guard_pressed"), &RtsoMain::_on_remove_guard_pressed);
	ClassDB::bind_method(D_METHOD("_on_call_to_arms_pressed"), &RtsoMain::_on_call_to_arms_pressed);
	ClassDB::bind_method(D_METHOD("_on_entity_command", "name"), &RtsoMain::_on_entity_command);
	ClassDB::bind_method(D_METHOD("_on_pause_overlay_pressed"), &RtsoMain::_on_pause_overlay_pressed);
	ClassDB::bind_method(D_METHOD("_on_disconnected_exit_pressed"), &RtsoMain::_on_disconnected_exit_pressed);
	ClassDB::bind_method(D_METHOD("_on_open_faq_pressed"), &RtsoMain::_on_open_faq_pressed);
	ClassDB::bind_method(D_METHOD("_on_delete_confirmed"), &RtsoMain::_on_delete_confirmed);
	ClassDB::bind_method(D_METHOD("_on_template_info_pressed"), &RtsoMain::_on_template_info_pressed);
	ClassDB::bind_method(D_METHOD("_on_template_details_input", "event", "kind", "id", "civ"), &RtsoMain::_on_template_details_input);
	ClassDB::bind_method(D_METHOD("_on_portrait_gui_input", "event"), &RtsoMain::_on_portrait_gui_input);
	ClassDB::bind_method(D_METHOD("select_first_own", "building"), &RtsoMain::select_first_own);
	ClassDB::bind_method(D_METHOD("_on_selection_group_clicked", "key"), &RtsoMain::_on_selection_group_clicked);
	ClassDB::bind_method(D_METHOD("_on_selection_group_gui_input", "event", "key"), &RtsoMain::_on_selection_group_gui_input);
	ClassDB::bind_method(D_METHOD("_on_panel_entity_gui_input", "event", "slot"), &RtsoMain::_on_panel_entity_gui_input);
	ClassDB::bind_method(D_METHOD("_on_research_progress_pressed", "slot"), &RtsoMain::_on_research_progress_pressed);
	ClassDB::bind_method(D_METHOD("toggle_dev_overlay"), &RtsoMain::toggle_dev_overlay);
	ClassDB::bind_method(D_METHOD("_on_dev_command", "command"), &RtsoMain::_on_dev_command);
	ClassDB::bind_method(D_METHOD("_on_render_debug_mode", "index"), &RtsoMain::_on_render_debug_mode);
	ClassDB::bind_method(D_METHOD("time_warp_rewind"), &RtsoMain::time_warp_rewind);
	ClassDB::bind_method(D_METHOD("time_warp_snapshot_count"), &RtsoMain::time_warp_snapshot_count);
	ClassDB::bind_method(D_METHOD("activate_rejoin_test"), &RtsoMain::activate_rejoin_test);
	ClassDB::bind_method(D_METHOD("get_rejoin_test"), &RtsoMain::get_rejoin_test);
	ClassDB::bind_method(D_METHOD("set_sim_tests", "tests"), &RtsoMain::set_sim_tests);
	ClassDB::bind_method(D_METHOD("get_sim_tests"), &RtsoMain::get_sim_tests);
	ClassDB::bind_method(D_METHOD("debug_dev_overlay"), &RtsoMain::debug_dev_overlay);
	ClassDB::bind_method(D_METHOD("debug_set_net_delay", "turns"), &RtsoMain::debug_set_net_delay);
	ClassDB::bind_method(D_METHOD("debug_net_delay_text"), &RtsoMain::debug_net_delay_text);
	ClassDB::bind_method(D_METHOD("_on_control_group_gui_input", "event", "button"), &RtsoMain::_on_control_group_gui_input);
	ClassDB::bind_method(D_METHOD("_on_garrison_group_clicked", "key"), &RtsoMain::_on_garrison_group_clicked);
	ClassDB::bind_method(D_METHOD("_on_stance_selected", "stance"), &RtsoMain::_on_stance_selected);
	ClassDB::bind_method(D_METHOD("_on_formation_selected", "formation"), &RtsoMain::_on_formation_selected);
	ClassDB::bind_method(D_METHOD("_on_formation_gui_input", "event", "formation"), &RtsoMain::_on_formation_gui_input);
	ClassDB::bind_method(D_METHOD("_on_train_pressed", "utype"), &RtsoMain::_on_train_pressed);
	ClassDB::bind_method(D_METHOD("_on_roster_item_clicked", "index", "at", "mouse_button"), &RtsoMain::_on_roster_item_clicked);
	ClassDB::bind_method(D_METHOD("_on_roster_gui_input", "event"), &RtsoMain::_on_roster_gui_input);
	ClassDB::bind_method(D_METHOD("_on_build_btn_pressed", "btype"), &RtsoMain::_on_build_btn_pressed);
	ClassDB::bind_method(D_METHOD("_on_tech_pressed", "tech_id"), &RtsoMain::_on_tech_pressed);
	ClassDB::bind_method(D_METHOD("_on_tech_pressed_at", "tech_id", "building_id"), &RtsoMain::_on_tech_pressed_at);
	ClassDB::bind_method(D_METHOD("_on_upgrade_pressed", "choice"), &RtsoMain::_on_upgrade_pressed);
	ClassDB::bind_method(D_METHOD("_on_gate_lock_pressed", "lock"), &RtsoMain::_on_gate_lock_pressed);
	ClassDB::bind_method(D_METHOD("_on_dropsite_sharing_pressed", "shared"), &RtsoMain::_on_dropsite_sharing_pressed);
	ClassDB::bind_method(D_METHOD("_on_pack_pressed", "pack"), &RtsoMain::_on_pack_pressed);
	ClassDB::bind_method(D_METHOD("_on_unit_upgrade_pressed", "to"), &RtsoMain::_on_unit_upgrade_pressed);
	ClassDB::bind_method(D_METHOD("_on_alert_pressed", "end"), &RtsoMain::_on_alert_pressed);
	ClassDB::bind_method(D_METHOD("_on_autoqueue_pressed", "on"), &RtsoMain::_on_autoqueue_pressed);
	ClassDB::bind_method(D_METHOD("save_game", "path", "description"), &RtsoMain::save_game, DEFVAL("user://saves/quicksave.rts0sav"), DEFVAL(""));
	ClassDB::bind_method(D_METHOD("save_game_as", "name", "description"), &RtsoMain::save_game_as);
	ClassDB::bind_method(D_METHOD("get_match_statistics"), &RtsoMain::statistics_summary);
	ClassDB::bind_method(D_METHOD("toggle_summary"), &RtsoMain::toggle_summary);
	ClassDB::bind_method(D_METHOD("close_open_dialogs"), &RtsoMain::close_open_dialogs);
	ClassDB::bind_method(D_METHOD("_on_civ_icon_gui_input", "event"), &RtsoMain::_on_civ_icon_gui_input);
	ClassDB::bind_method(D_METHOD("_on_flare_button_pressed"), &RtsoMain::_on_flare_button_pressed);
	ClassDB::bind_method(D_METHOD("debug_minimap_panel"), &RtsoMain::debug_minimap_panel);
	ClassDB::bind_method(D_METHOD("debug_attacked", "target", "attacker_owner"), &RtsoMain::debug_attacked);
	ClassDB::bind_method(D_METHOD("debug_selected_building"), &RtsoMain::debug_selected_building);
	ClassDB::bind_method(D_METHOD("debug_queue_slot", "i"), &RtsoMain::debug_queue_slot);
	ClassDB::bind_method(D_METHOD("debug_placement_tip"), &RtsoMain::debug_placement_tip);
	ClassDB::bind_method(D_METHOD("debug_cursor_tip"), &RtsoMain::debug_cursor_tip);
	ClassDB::bind_method(D_METHOD("debug_ui_clicks"), &RtsoMain::debug_ui_clicks);
	ClassDB::bind_method(D_METHOD("debug_overlay_visible"), &RtsoMain::debug_overlay_visible);
	ClassDB::bind_method(D_METHOD("debug_voices"), &RtsoMain::debug_voices);
	ClassDB::bind_method(D_METHOD("menu_button_rect"), &RtsoMain::menu_button_rect);
	ClassDB::bind_method(D_METHOD("chat_open_hotkey_tooltip"), &RtsoMain::chat_open_hotkey_tooltip);
	ClassDB::bind_method(D_METHOD("debug_chat_tooltips"), &RtsoMain::debug_chat_tooltips);
	ClassDB::bind_method(D_METHOD("debug_roster"), &RtsoMain::debug_roster);
	ClassDB::bind_method(D_METHOD("debug_saved_gui"), &RtsoMain::debug_saved_gui);
	ClassDB::bind_method(D_METHOD("debug_set_camera_data", "camera"), &RtsoMain::debug_set_camera_data);
	ClassDB::bind_method(D_METHOD("_on_tree_node_added", "node"), &RtsoMain::_on_tree_node_added);
	ClassDB::bind_method(D_METHOD("_on_ui_click"), &RtsoMain::_on_ui_click);
	ClassDB::bind_method(D_METHOD("_on_ui_click_item", "index"), &RtsoMain::_on_ui_click_item);
	ClassDB::bind_method(D_METHOD("get_summary_gui", "in_game"), &RtsoMain::summary_gui);
	ClassDB::bind_method(D_METHOD("get_replay_status"), &RtsoMain::get_replay_status);
	ClassDB::bind_method(D_METHOD("set_replay_view", "player"), &RtsoMain::set_replay_view);
	ClassDB::bind_method(D_METHOD("_on_replay_view_selected", "index"), &RtsoMain::_on_replay_view_selected);
	ClassDB::bind_method(D_METHOD("_on_observer_view_selected", "index"), &RtsoMain::_on_observer_view_selected);
	ClassDB::bind_method(D_METHOD("set_follow_player", "on"), &RtsoMain::set_follow_player);
	ClassDB::bind_method(D_METHOD("is_follow_player"), &RtsoMain::is_follow_player);
	ClassDB::bind_static_method("RtsoMain", D_METHOD("get_save_version"), &RtsoMain::get_save_version);
	ClassDB::bind_static_method("RtsoMain", D_METHOD("get_replay_format"), &RtsoMain::get_replay_format);
	ClassDB::bind_method(D_METHOD("is_replay_playback"), &RtsoMain::is_replay_playback);
	ClassDB::bind_method(D_METHOD("load_game", "path"), &RtsoMain::load_game, DEFVAL("user://saves/quicksave.rts0sav"));
	ClassDB::bind_method(D_METHOD("get_fow_grid"), &RtsoMain::get_fow_grid);
	ClassDB::bind_method(D_METHOD("order_guard_units", "target_id"), &RtsoMain::order_guard_units);
	ClassDB::bind_method(D_METHOD("spawn_large_battle", "total_units"), &RtsoMain::spawn_large_battle, DEFVAL(1000));
	ClassDB::bind_method(D_METHOD("_on_map_selected", "index"), &RtsoMain::_on_map_selected);
	ClassDB::bind_method(D_METHOD("load_map", "map_id"), &RtsoMain::load_map);
	ClassDB::bind_method(D_METHOD("get_available_maps"), &RtsoMain::get_available_maps);
	ClassDB::bind_method(D_METHOD("_on_civ_selected", "index"), &RtsoMain::_on_civ_selected);
	ClassDB::bind_method(D_METHOD("_on_ai_diff_selected", "index"), &RtsoMain::_on_ai_diff_selected);
	ClassDB::bind_method(D_METHOD("_on_ai_behavior_selected", "index"), &RtsoMain::_on_ai_behavior_selected);
	ClassDB::bind_method(D_METHOD("_on_victory_selected", "index"), &RtsoMain::_on_victory_selected);
	ClassDB::bind_method(D_METHOD("_on_barter_requested", "sell_type", "buy_type", "amount"), &RtsoMain::_on_barter_requested);
	ClassDB::bind_method(D_METHOD("_on_barter_sell_selected", "resource"), &RtsoMain::_on_barter_sell_selected);
	ClassDB::bind_method(D_METHOD("_on_pack_cancel_pressed", "pack"), &RtsoMain::_on_pack_cancel_pressed);
	ClassDB::bind_method(D_METHOD("debug_barter_panel"), &RtsoMain::debug_barter_panel);
	ClassDB::bind_method(D_METHOD("set_civ", "civ_idx"), &RtsoMain::set_civ);
	ClassDB::bind_method(D_METHOD("get_civ_info", "civ_idx"), &RtsoMain::get_civ_info);
	ClassDB::bind_method(D_METHOD("get_tech_tree"), &RtsoMain::get_tech_tree);
	ClassDB::bind_method(D_METHOD("get_match_summary"), &RtsoMain::get_match_summary);

	ClassDB::bind_method(D_METHOD("_on_queue_slot_gui_input", "event", "slot_idx"), &RtsoMain::_on_queue_slot_gui_input);
	ClassDB::bind_method(D_METHOD("_on_queue_slot_cancel", "slot_idx"), &RtsoMain::_on_queue_slot_cancel);
	ClassDB::bind_method(D_METHOD("toggle_chat"), &RtsoMain::toggle_chat);
	ClassDB::bind_method(D_METHOD("_on_chat_send_pressed"), &RtsoMain::_on_chat_send_pressed);
	ClassDB::bind_method(D_METHOD("_on_chat_filter_selected", "index"), &RtsoMain::_on_chat_filter_selected);
	ClassDB::bind_method(D_METHOD("_on_chat_history_toggled", "on"), &RtsoMain::_on_chat_history_toggled);
	ClassDB::bind_method(D_METHOD("_on_chat_text_submitted", "text"), &RtsoMain::_on_chat_text_submitted);
	ClassDB::bind_method(D_METHOD("_on_chat_input_gui", "event"), &RtsoMain::_on_chat_input_gui);
	ClassDB::bind_method(D_METHOD("_on_chat_channel_btn_pressed", "channel_idx"), &RtsoMain::_on_chat_channel_btn_pressed);
	ClassDB::bind_method(D_METHOD("toggle_diplomacy"), &RtsoMain::toggle_diplomacy);
	ClassDB::bind_method(D_METHOD("toggle_diplomacy_colors"), &RtsoMain::toggle_diplomacy_colors);
	ClassDB::bind_method(D_METHOD("toggle_objectives"), &RtsoMain::toggle_objectives);
	ClassDB::bind_method(D_METHOD("get_match_settings_info"), &RtsoMain::get_match_settings_info);
	ClassDB::bind_method(D_METHOD("_on_tutorial_ready_pressed"), &RtsoMain::_on_tutorial_ready_pressed);
	ClassDB::bind_method(D_METHOD("resign"), &RtsoMain::resign);
	ClassDB::bind_method(D_METHOD("chat_try_cheat", "text"), &RtsoMain::chat_try_cheat);
	ClassDB::bind_method(D_METHOD("tick_session_music", "delta"), &RtsoMain::tick_session_music);
	ClassDB::bind_method(D_METHOD("_toggle_session_dialog", "name"), &RtsoMain::_toggle_session_dialog);
	ClassDB::bind_method(D_METHOD("get_target_marker_count"), &RtsoMain::get_target_marker_count);
	ClassDB::bind_method(D_METHOD("get_target_marker_actor_count"), &RtsoMain::get_target_marker_actor_count);
	ClassDB::bind_method(D_METHOD("debug_range_rings"), &RtsoMain::debug_range_rings);
	ClassDB::bind_method(D_METHOD("get_work_sound_count"), &RtsoMain::get_work_sound_count);
	ClassDB::bind_method(D_METHOD("debug_gather_nearest"), &RtsoMain::debug_gather_nearest);
	ClassDB::bind_method(D_METHOD("debug_trade_route_clicks"), &RtsoMain::debug_trade_route_clicks);
	ClassDB::bind_method(D_METHOD("debug_trade_tip", "market"), &RtsoMain::debug_trade_tip);
	ClassDB::bind_method(D_METHOD("debug_trade_click", "market", "queued"), &RtsoMain::debug_trade_click);
	ClassDB::bind_method(D_METHOD("debug_tutorial_panel"), &RtsoMain::debug_tutorial_panel);
	ClassDB::bind_method(D_METHOD("debug_guard_step", "step"), &RtsoMain::debug_guard_step);
	ClassDB::bind_method(D_METHOD("debug_train_buttons", "btype"), &RtsoMain::debug_train_buttons);
	ClassDB::bind_method(D_METHOD("debug_command_button", "prefix"), &RtsoMain::debug_command_button);
	ClassDB::bind_method(D_METHOD("debug_selection_grid", "select_all"), &RtsoMain::debug_selection_grid);
	ClassDB::bind_method(D_METHOD("debug_set_carry", "unit_id", "res", "amount"), &RtsoMain::debug_set_carry);
	ClassDB::bind_method(D_METHOD("debug_set_resource", "res", "amount"), &RtsoMain::debug_set_resource);
	ClassDB::bind_method(D_METHOD("debug_placing"), &RtsoMain::debug_placing);
	ClassDB::bind_method(D_METHOD("debug_highlight"), &RtsoMain::debug_highlight);
	ClassDB::bind_method(D_METHOD("debug_selected_structures"), &RtsoMain::debug_selected_structures);
	ClassDB::bind_method(D_METHOD("debug_rally_flags"), &RtsoMain::debug_rally_flags);
	ClassDB::bind_method(D_METHOD("debug_status_sprites"), &RtsoMain::debug_status_sprites);
	ClassDB::bind_method(D_METHOD("debug_status_rank_icons"), &RtsoMain::debug_status_rank_icons);
	ClassDB::bind_method(D_METHOD("debug_status_xp_bars"), &RtsoMain::debug_status_xp_bars);
	ClassDB::bind_method(D_METHOD("debug_status_aura_sources"), &RtsoMain::debug_status_aura_sources);
	ClassDB::bind_method(D_METHOD("debug_building_queue", "id"), &RtsoMain::debug_building_queue);
	ClassDB::bind_method(D_METHOD("debug_structure_outlined", "id"), &RtsoMain::debug_structure_outlined);
	ClassDB::bind_method(D_METHOD("debug_guard_target", "unit_id"), &RtsoMain::debug_guard_target);
	ClassDB::bind_method(D_METHOD("debug_unit_order", "unit_id"), &RtsoMain::debug_unit_order);
	ClassDB::bind_method(D_METHOD("_on_chat_line_input", "event", "action"), &RtsoMain::_on_chat_line_input);
	ClassDB::bind_method(D_METHOD("debug_chat_overlay"), &RtsoMain::debug_chat_overlay);
	ClassDB::bind_method(D_METHOD("debug_camera"), &RtsoMain::debug_camera);
	ClassDB::bind_method(D_METHOD("notify_attack", "pos", "attacker", "structure", "target"), &RtsoMain::notify_attack, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("debug_unit_screen", "unit_id"), &RtsoMain::debug_unit_screen);
	ClassDB::bind_method(D_METHOD("debug_armed"), &RtsoMain::debug_armed);
	ClassDB::bind_method(D_METHOD("debug_unit_pos", "unit_id"), &RtsoMain::debug_unit_pos);
	ClassDB::bind_method(D_METHOD("debug_resource_screen", "resource_id"), &RtsoMain::debug_resource_screen);
	ClassDB::bind_method(D_METHOD("debug_unit_ground", "unit_id", "goal"), &RtsoMain::debug_unit_ground, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("debug_right_line"), &RtsoMain::debug_right_line);
	ClassDB::bind_method(D_METHOD("debug_ground_at", "screen"), &RtsoMain::debug_ground_at);
	ClassDB::bind_method(D_METHOD("debug_building_screen", "building_id"), &RtsoMain::debug_building_screen);
	ClassDB::bind_method(D_METHOD("debug_inspector", "what"), &RtsoMain::debug_inspector);
	ClassDB::bind_method(D_METHOD("debug_inspect_entity", "id"), &RtsoMain::debug_inspect_entity);
	ClassDB::bind_method(D_METHOD("debug_civic_centre_roof"), &RtsoMain::debug_civic_centre_roof);
	ClassDB::bind_method(D_METHOD("debug_garrison_icons"), &RtsoMain::debug_garrison_icons);
	ClassDB::bind_method(D_METHOD("debug_selection_state"), &RtsoMain::debug_selection_state);
	ClassDB::bind_method(D_METHOD("debug_command_row"), &RtsoMain::debug_command_row);
	ClassDB::bind_method(D_METHOD("debug_spawn_units", "template_id", "count", "player", "near"), &RtsoMain::debug_spawn_units, DEFVAL(-1), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_complete_research", "player", "tech"), &RtsoMain::debug_complete_research);
	ClassDB::bind_method(D_METHOD("debug_spawn_offset", "template_id", "count", "player"), &RtsoMain::debug_spawn_offset, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_spawn_fauna", "fauna_type", "count", "owned", "dx", "dz"), &RtsoMain::debug_spawn_fauna, DEFVAL(16.0f), DEFVAL(16.0f));
	ClassDB::bind_method(D_METHOD("debug_spawn_mine", "x", "z", "max_gatherers"), &RtsoMain::debug_spawn_mine);
	ClassDB::bind_method(D_METHOD("debug_unit_model_path", "civ", "unit_type"), &RtsoMain::debug_unit_model_path);
	ClassDB::bind_method(D_METHOD("debug_template_model_path", "template_id"), &RtsoMain::debug_template_model_path);
	ClassDB::bind_method(D_METHOD("debug_snap_placement", "btype", "x", "z", "hover", "stale_angle"), &RtsoMain::debug_snap_placement);
	ClassDB::bind_method(D_METHOD("debug_put_inside", "ids", "holder"), &RtsoMain::debug_put_inside);
	ClassDB::bind_method(D_METHOD("debug_select_entity", "id"), &RtsoMain::debug_select_entity);
	ClassDB::bind_method(D_METHOD("debug_select_structures", "ids"), &RtsoMain::debug_select_structures);
	ClassDB::bind_method(D_METHOD("debug_spawn_relic", "dx", "dz"), &RtsoMain::debug_spawn_relic);
	ClassDB::bind_method(D_METHOD("debug_capture_relic", "ids", "relic"), &RtsoMain::debug_capture_relic);
	ClassDB::bind_method(D_METHOD("debug_select_units", "ids"), &RtsoMain::debug_select_units);
	ClassDB::bind_method(D_METHOD("debug_civic_centre_id", "player"), &RtsoMain::debug_civic_centre_id);
	ClassDB::bind_method(D_METHOD("debug_spawn_building", "type", "dx", "dz", "player"), &RtsoMain::debug_spawn_building, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_research_any"), &RtsoMain::debug_research_any);
	ClassDB::bind_method(D_METHOD("debug_structure_types", "with_civ"), &RtsoMain::debug_structure_types, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("_on_top_counter_gui_input", "event"), &RtsoMain::_on_top_counter_gui_input);
	ClassDB::bind_method(D_METHOD("debug_delete_units", "ids"), &RtsoMain::debug_delete_units);
	ClassDB::bind_method(D_METHOD("debug_add_xp", "ids", "xp"), &RtsoMain::debug_add_xp);
	ClassDB::bind_method(D_METHOD("debug_diplomacy"), &RtsoMain::debug_diplomacy);
	ClassDB::bind_method(D_METHOD("chat_open_page"), &RtsoMain::chat_open_page);
	ClassDB::bind_method(D_METHOD("is_chat_open"), &RtsoMain::is_chat_open);
	ClassDB::bind_method(D_METHOD("open_template_viewer", "kind", "id", "civ"), &RtsoMain::open_template_viewer);
	ClassDB::bind_method(D_METHOD("debug_set_observer_view", "player"), &RtsoMain::debug_set_observer_view);
	ClassDB::bind_method(D_METHOD("debug_dialogs"), &RtsoMain::debug_dialogs);
	ClassDB::bind_method(D_METHOD("debug_unset_rally_step", "step"), &RtsoMain::debug_unset_rally_step);
	ClassDB::bind_method(D_METHOD("debug_set_rally", "building_id", "x", "z"), &RtsoMain::debug_set_rally);
	ClassDB::bind_method(D_METHOD("_on_diplo_stance_changed", "stance", "player_idx"), &RtsoMain::_on_diplo_stance_changed);
	ClassDB::bind_method(D_METHOD("_on_diplo_tribute_pressed", "player_idx", "res_type"), &RtsoMain::_on_diplo_tribute_pressed);
	ClassDB::bind_method(D_METHOD("_on_diplo_spy_pressed", "player_idx"), &RtsoMain::_on_diplo_spy_pressed);
	ClassDB::bind_method(D_METHOD("_on_diplo_attack_request_pressed", "player_idx"), &RtsoMain::_on_diplo_attack_request_pressed);
	ClassDB::bind_method(D_METHOD("set_use_english_voice_orders", "enable"), &RtsoMain::set_use_english_voice_orders);
	ClassDB::bind_method(D_METHOD("is_use_english_voice_orders"), &RtsoMain::is_use_english_voice_orders);
	ClassDB::bind_method(D_METHOD("_on_lobby_button_pressed"), &RtsoMain::_on_lobby_button_pressed);
	ClassDB::bind_method(D_METHOD("has_lobby_client"), &RtsoMain::has_lobby_client);
	ClassDB::bind_method(D_METHOD("open_lobby_page"), &RtsoMain::open_lobby_page);
	ClassDB::bind_method(D_METHOD("_on_voice_button_pressed"), &RtsoMain::_on_voice_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_lobby_host_pressed"), &RtsoMain::_on_lobby_host_pressed);
	ClassDB::bind_method(D_METHOD("_on_lobby_join_pressed"), &RtsoMain::_on_lobby_join_pressed);
	ClassDB::bind_method(D_METHOD("_on_lobby_start_pressed"), &RtsoMain::_on_lobby_start_pressed);
	ClassDB::bind_method(D_METHOD("is_net_pregame"), &RtsoMain::is_net_pregame);
	ClassDB::bind_method(D_METHOD("get_net_setup_state"), &RtsoMain::get_net_setup_state);
	ClassDB::bind_method(D_METHOD("push_net_setup"), &RtsoMain::push_net_setup);
	ClassDB::bind_method(D_METHOD("assign_net_client", "client", "slot"), &RtsoMain::assign_net_client);
	ClassDB::bind_method(D_METHOD("request_net_player_setting", "key", "value"), &RtsoMain::request_net_player_setting);
	ClassDB::bind_method(D_METHOD("set_net_ready", "status"), &RtsoMain::set_net_ready);
	ClassDB::bind_method(D_METHOD("leave_net_game"), &RtsoMain::leave_net_game);
	ClassDB::bind_method(D_METHOD("get_setup_chat"), &RtsoMain::get_setup_chat);
	ClassDB::bind_method(D_METHOD("send_setup_chat", "text"), &RtsoMain::send_setup_chat);
	ClassDB::bind_method(D_METHOD("_on_lobby_seat_selected", "index", "client"), &RtsoMain::_on_lobby_seat_selected);
	ClassDB::bind_method(D_METHOD("apply_net_game_start"), &RtsoMain::apply_net_game_start);
	ClassDB::bind_method(D_METHOD("load_map_seeded", "map_id", "seed"), &RtsoMain::load_map_seeded);
	// 0 A.D. GUI onGameLoadProgress(progression, description): the loading page
	// (scenes/menu/loading.gd) listens while the match is set up.
	ADD_SIGNAL(MethodInfo("net_setup_changed"));
	ADD_SIGNAL(MethodInfo("net_setup_refused", PropertyInfo(Variant::STRING, "reason")));
	ADD_SIGNAL(MethodInfo("load_progress", PropertyInfo(Variant::INT, "percent"), PropertyInfo(Variant::STRING, "description")));
	ClassDB::bind_method(D_METHOD("_on_lobby_refresh_pressed"), &RtsoMain::_on_lobby_refresh_pressed);
	ClassDB::bind_method(D_METHOD("_on_lobby_observe_toggled", "on"), &RtsoMain::_on_lobby_observe_toggled);
	ClassDB::bind_method(D_METHOD("_start_menu_net"), &RtsoMain::_start_menu_net);
	ClassDB::bind_method(D_METHOD("get_lobby_state"), &RtsoMain::get_lobby_state);
	ClassDB::bind_method(D_METHOD("get_net_nat_state"), &RtsoMain::get_net_nat_state);
	ClassDB::bind_method(D_METHOD("net_query_stun", "server", "port"), &RtsoMain::net_query_stun);
	ClassDB::bind_method(D_METHOD("net_hole_punch", "ip", "port"), &RtsoMain::net_hole_punch);
	ClassDB::bind_method(D_METHOD("net_retarget_host", "ip", "port"), &RtsoMain::net_retarget_host);
	ClassDB::bind_method(D_METHOD("net_lobby_auth_result", "peer", "ticket", "name"), &RtsoMain::net_lobby_auth_result);
	ClassDB::bind_method(D_METHOD("get_turn_hash", "turn"), &RtsoMain::get_turn_hash);

	const StringName cls = get_class_static();
	auto bind_enum = [&](const char *enum_name, const char *name, int64_t value) {
		ClassDB::bind_integer_constant(cls, enum_name, name, value);
	};
	bind_enum("UnitType", "VILLAGER", rtso::UNIT_VILLAGER);
	bind_enum("UnitType", "SWORDSMAN", rtso::UNIT_SWORDSMAN);
	bind_enum("UnitType", "ARCHER", rtso::UNIT_ARCHER);
	bind_enum("UnitType", "SPEARMAN", rtso::UNIT_SPEARMAN);
	bind_enum("UnitType", "CAVALRY", rtso::UNIT_CAVALRY);
	bind_enum("UnitType", "TRADER", rtso::UNIT_TRADER);
	bind_enum("UnitType", "CATAPULT", rtso::UNIT_CATAPULT);
	bind_enum("UnitType", "HERO", rtso::UNIT_HERO);
	bind_enum("UnitType", "TRIREME", rtso::UNIT_TRIREME);
	bind_enum("UnitType", "ELEPHANT", rtso::UNIT_ELEPHANT);
	bind_enum("UnitType", "SIEGE_RAM", rtso::UNIT_SIEGE_RAM);
	bind_enum("UnitType", "HEALER", rtso::UNIT_HEALER);
	bind_enum("UnitType", "CHARIOT", rtso::UNIT_CHARIOT);
	bind_enum("UnitType", "WAR_DOG", rtso::UNIT_WAR_DOG);
	bind_enum("UnitType", "BOLT_SHOOTER", rtso::UNIT_BOLT_SHOOTER);
	bind_enum("UnitType", "FISHING_BOAT", rtso::UNIT_FISHING_BOAT);
	bind_enum("UnitType", "MERCHANT_SHIP", rtso::UNIT_MERCHANT_SHIP);
	bind_enum("UnitType", "FEMALE_CITIZEN", rtso::UNIT_FEMALE_CITIZEN);
	bind_enum("UnitType", "CARNYX", rtso::UNIT_CARNYX);
	bind_enum("UnitType", "WORKER_ELEPHANT", rtso::UNIT_WORKER_ELEPHANT);
	bind_enum("UnitType", "ARCHER_MAIDEN", rtso::UNIT_ARCHER_MAIDEN);
	bind_enum("UnitType", "CAMEL", rtso::UNIT_CAMEL);
	bind_enum("UnitType", "SCYTHED_CHARIOT", rtso::UNIT_SCYTHED_CHARIOT);
	bind_enum("UnitType", "MINISTER", rtso::UNIT_MINISTER);
	bind_enum("UnitType", "CATAFALQUE", rtso::UNIT_CATAFALQUE);
	bind_enum("UnitType", "WAGON", rtso::UNIT_WAGON);
	bind_enum("UnitType", "NONE", rtso::UNIT_TYPE_NONE);

	bind_enum("BuildingType", "TOWN_CENTER", rtso::BLD_TOWN_CENTER);
	bind_enum("BuildingType", "HOUSE", rtso::BLD_HOUSE);
	bind_enum("BuildingType", "BARRACKS", rtso::BLD_BARRACKS);
	bind_enum("BuildingType", "STOREHOUSE", rtso::BLD_STOREHOUSE);
	bind_enum("BuildingType", "TOWER", rtso::BLD_TOWER);
	bind_enum("BuildingType", "FARMSTEAD", rtso::BLD_FARMSTEAD);
	bind_enum("BuildingType", "MARKET", rtso::BLD_MARKET);
	bind_enum("BuildingType", "FORTRESS", rtso::BLD_FORTRESS);
	bind_enum("BuildingType", "GATE", rtso::BLD_GATE);
	bind_enum("BuildingType", "PALISADE_GATE", rtso::BLD_PALISADE_GATE);
	bind_enum("BuildingType", "SIEGE_WALL", rtso::BLD_SIEGE_WALL);
	bind_enum("BuildingType", "SIEGE_WALL_GATE", rtso::BLD_SIEGE_WALL_GATE);
	bind_enum("BuildingType", "TEMPLE", rtso::BLD_TEMPLE);
	bind_enum("BuildingType", "WONDER", rtso::BLD_WONDER);
	bind_enum("BuildingType", "DOCK", rtso::BLD_DOCK);
	bind_enum("BuildingType", "WALL_SEGMENT", rtso::BLD_WALL_SEGMENT);
	bind_enum("BuildingType", "CORRAL", rtso::BLD_CORRAL);
	bind_enum("BuildingType", "FIELD", rtso::BLD_FIELD);
	bind_enum("BuildingType", "STABLE", rtso::BLD_STABLE);
	bind_enum("BuildingType", "RANGE", rtso::BLD_RANGE);
	bind_enum("BuildingType", "FORGE", rtso::BLD_FORGE);
	bind_enum("BuildingType", "ARSENAL", rtso::BLD_ARSENAL);
	bind_enum("BuildingType", "SENTRY_TOWER", rtso::BLD_SENTRY_TOWER);
	bind_enum("BuildingType", "OUTPOST", rtso::BLD_OUTPOST);
	bind_enum("BuildingType", "PALISADE", rtso::BLD_PALISADE);
	bind_enum("BuildingType", "WALL_TOWER", rtso::BLD_WALL_TOWER);
	bind_enum("BuildingType", "ELEPHANT_STABLE", rtso::BLD_ELEPHANT_STABLE);
	bind_enum("BuildingType", "THEATER", rtso::BLD_THEATER);
	bind_enum("BuildingType", "EMBASSY", rtso::BLD_EMBASSY);
	bind_enum("BuildingType", "GYMNASIUM", rtso::BLD_GYMNASIUM);
	bind_enum("BuildingType", "PRYTANEION", rtso::BLD_PRYTANEION);
	bind_enum("BuildingType", "GEROUSIA", rtso::BLD_GEROUSIA);
	bind_enum("BuildingType", "SYSSITON", rtso::BLD_SYSSITON);
	bind_enum("BuildingType", "TACHARA", rtso::BLD_TACHARA);
	bind_enum("BuildingType", "ARMY_CAMP", rtso::BLD_ARMY_CAMP);
	bind_enum("BuildingType", "AMPHITHEATER", rtso::BLD_AMPHITHEATER);
	bind_enum("BuildingType", "LIBRARY", rtso::BLD_LIBRARY);
	bind_enum("BuildingType", "KENNEL", rtso::BLD_KENNEL);
	bind_enum("BuildingType", "ASSEMBLY", rtso::BLD_ASSEMBLY);
	bind_enum("BuildingType", "MONUMENT", rtso::BLD_MONUMENT);
	bind_enum("BuildingType", "PALACE", rtso::BLD_PALACE);
	bind_enum("BuildingType", "PILLAR", rtso::BLD_PILLAR);
	bind_enum("BuildingType", "PYRAMID_LARGE", rtso::BLD_PYRAMID_LARGE);
	bind_enum("BuildingType", "PYRAMID_SMALL", rtso::BLD_PYRAMID_SMALL);
	bind_enum("BuildingType", "MERCENARY_CAMP", rtso::BLD_MERCENARY_CAMP);
	bind_enum("BuildingType", "LIGHTHOUSE", rtso::BLD_LIGHTHOUSE);
	bind_enum("BuildingType", "MILITARY_COLONY", rtso::BLD_MILITARY_COLONY);
	bind_enum("BuildingType", "ACADEMY", rtso::BLD_ACADEMY);
	bind_enum("BuildingType", "MINISTRY", rtso::BLD_MINISTRY);
	bind_enum("BuildingType", "CAMP_BLEMMYE", rtso::BLD_CAMP_BLEMMYE);
	bind_enum("BuildingType", "CAMP_NOBA", rtso::BLD_CAMP_NOBA);
	bind_enum("BuildingType", "TEMPLE_AMUN", rtso::BLD_TEMPLE_AMUN);
	bind_enum("BuildingType", "TEMPLE_VESTA", rtso::BLD_TEMPLE_VESTA);
	bind_enum("BuildingType", "TEMPLE_MARS", rtso::BLD_TEMPLE_MARS);
	bind_enum("BuildingType", "ICE_HOUSE", rtso::BLD_ICE_HOUSE);
	bind_enum("BuildingType", "SUPER_DOCK", rtso::BLD_SUPER_DOCK);

	bind_enum("Civilization", "ATHENIANS", rtso::CIV_ATHENIANS);
	bind_enum("Civilization", "SPARTANS", rtso::CIV_SPARTANS);
	bind_enum("Civilization", "PERSIANS", rtso::CIV_PERSIANS);
	bind_enum("Civilization", "ROMANS", rtso::CIV_ROMANS);
	bind_enum("Civilization", "CARTHAGINIANS", rtso::CIV_CARTHAGINIANS);
	bind_enum("Civilization", "MACEDONIANS", rtso::CIV_MACEDONIANS);
	bind_enum("Civilization", "BRITONS", rtso::CIV_BRITONS);
	bind_enum("Civilization", "GAULS", rtso::CIV_GAULS);
	bind_enum("Civilization", "IBERIANS", rtso::CIV_IBERIANS);
	bind_enum("Civilization", "MAURYANS", rtso::CIV_MAURYANS);
	bind_enum("Civilization", "KUSHITES", rtso::CIV_KUSHITES);
	bind_enum("Civilization", "PTOLEMIES", rtso::CIV_PTOLEMIES);
	bind_enum("Civilization", "SELEUCIDS", rtso::CIV_SELEUCIDS);
	bind_enum("Civilization", "HAN", rtso::CIV_HAN);
	bind_enum("Civilization", "GERMANS", rtso::CIV_GERMANS);
}
