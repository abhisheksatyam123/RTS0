#include "main_node.h"
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/os.hpp>
#include <unordered_set>
#include <cstring>
#include <godot_cpp/classes/theme.hpp>
#include "popup_safe.h"
#include "simulation/petra_chat.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
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

// 0 A.D. Trainer lists (civ_unit_template(..).trainers): does a structure of type
// `btype` train any of `civ`'s units of class `utype`?
static bool building_trains_class(uint8_t civ, uint8_t utype, uint8_t btype) {
	for (uint16_t i : rtso::civ_roster_ids(civ)) {
		const rtso::CivUnitTemplate &u = rtso::civ_unit_template(i);
		if (u.unit_type == utype && !(u.flags & rtso::ROSTER_SIEGE_TOWER) && (u.trainers & rtso::bld_bit(btype))) return true;
	}
	return false;
}

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
	ClassDB::bind_method(D_METHOD("toggle_pause"), &RtsoMain::toggle_pause);
	ClassDB::bind_method(D_METHOD("quicksave"), &RtsoMain::quicksave);
	ClassDB::bind_method(D_METHOD("quickload"), &RtsoMain::quickload);
	ClassDB::bind_method(D_METHOD("_on_replay_speed_selected", "index"), &RtsoMain::_on_replay_speed_selected);
	ClassDB::bind_method(D_METHOD("is_game_paused"), &RtsoMain::is_game_paused);
	ClassDB::bind_method(D_METHOD("_on_menu_button_pressed"), &RtsoMain::_on_menu_button_pressed);
	ClassDB::bind_method(D_METHOD("get_audio_state"), &RtsoMain::get_audio_state);
	ClassDB::bind_method(D_METHOD("get_trade_dialog_state"), &RtsoMain::get_trade_dialog_state);
	ClassDB::bind_method(D_METHOD("_on_trading_goods_requested", "shares"), &RtsoMain::_on_trading_goods_requested);
	ClassDB::bind_method(D_METHOD("_on_ungarrison_pressed"), &RtsoMain::_on_ungarrison_pressed);
	ClassDB::bind_method(D_METHOD("_on_remove_guard_pressed"), &RtsoMain::_on_remove_guard_pressed);
	ClassDB::bind_method(D_METHOD("_on_call_to_arms_pressed"), &RtsoMain::_on_call_to_arms_pressed);
	ClassDB::bind_method(D_METHOD("_on_entity_command", "name"), &RtsoMain::_on_entity_command);
	ClassDB::bind_method(D_METHOD("_on_pause_overlay_pressed"), &RtsoMain::_on_pause_overlay_pressed);
	ClassDB::bind_method(D_METHOD("_on_disconnected_exit_pressed"), &RtsoMain::_on_disconnected_exit_pressed);
	ClassDB::bind_method(D_METHOD("_on_delete_confirmed"), &RtsoMain::_on_delete_confirmed);
	ClassDB::bind_method(D_METHOD("_on_template_info_pressed"), &RtsoMain::_on_template_info_pressed);
	ClassDB::bind_method(D_METHOD("select_first_own", "building"), &RtsoMain::select_first_own);
	ClassDB::bind_method(D_METHOD("_on_selection_group_clicked", "key"), &RtsoMain::_on_selection_group_clicked);
	ClassDB::bind_method(D_METHOD("_on_selection_group_gui_input", "event", "key"), &RtsoMain::_on_selection_group_gui_input);
	ClassDB::bind_method(D_METHOD("_on_garrison_group_clicked", "key"), &RtsoMain::_on_garrison_group_clicked);
	ClassDB::bind_method(D_METHOD("_on_stance_selected", "stance"), &RtsoMain::_on_stance_selected);
	ClassDB::bind_method(D_METHOD("_on_formation_selected", "formation"), &RtsoMain::_on_formation_selected);
	ClassDB::bind_method(D_METHOD("_on_train_pressed", "utype"), &RtsoMain::_on_train_pressed);
	ClassDB::bind_method(D_METHOD("_on_roster_item_clicked", "index", "at", "mouse_button"), &RtsoMain::_on_roster_item_clicked);
	ClassDB::bind_method(D_METHOD("_on_build_btn_pressed", "btype"), &RtsoMain::_on_build_btn_pressed);
	ClassDB::bind_method(D_METHOD("_on_tech_pressed", "tech_id"), &RtsoMain::_on_tech_pressed);
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
	ClassDB::bind_method(D_METHOD("get_summary_gui", "in_game"), &RtsoMain::summary_gui);
	ClassDB::bind_method(D_METHOD("get_replay_status"), &RtsoMain::get_replay_status);
	ClassDB::bind_method(D_METHOD("set_replay_view", "player"), &RtsoMain::set_replay_view);
	ClassDB::bind_method(D_METHOD("_on_replay_view_selected", "index"), &RtsoMain::_on_replay_view_selected);
	ClassDB::bind_method(D_METHOD("_on_observer_view_selected", "index"), &RtsoMain::_on_observer_view_selected);
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
	ClassDB::bind_method(D_METHOD("set_civ", "civ_idx"), &RtsoMain::set_civ);
	ClassDB::bind_method(D_METHOD("get_civ_info", "civ_idx"), &RtsoMain::get_civ_info);
	ClassDB::bind_method(D_METHOD("get_tech_tree"), &RtsoMain::get_tech_tree);
	ClassDB::bind_method(D_METHOD("get_match_summary"), &RtsoMain::get_match_summary);

	ClassDB::bind_method(D_METHOD("_on_queue_slot_gui_input", "event", "slot_idx"), &RtsoMain::_on_queue_slot_gui_input);
	ClassDB::bind_method(D_METHOD("_on_queue_slot_cancel", "slot_idx"), &RtsoMain::_on_queue_slot_cancel);
	ClassDB::bind_method(D_METHOD("toggle_chat"), &RtsoMain::toggle_chat);
	ClassDB::bind_method(D_METHOD("_on_chat_send_pressed"), &RtsoMain::_on_chat_send_pressed);
	ClassDB::bind_method(D_METHOD("_on_chat_text_submitted", "text"), &RtsoMain::_on_chat_text_submitted);
	ClassDB::bind_method(D_METHOD("_on_chat_channel_btn_pressed", "channel_idx"), &RtsoMain::_on_chat_channel_btn_pressed);
	ClassDB::bind_method(D_METHOD("toggle_diplomacy"), &RtsoMain::toggle_diplomacy);
	ClassDB::bind_method(D_METHOD("toggle_diplomacy_colors"), &RtsoMain::toggle_diplomacy_colors);
	ClassDB::bind_method(D_METHOD("toggle_objectives"), &RtsoMain::toggle_objectives);
	ClassDB::bind_method(D_METHOD("get_match_settings_info"), &RtsoMain::get_match_settings_info);
	ClassDB::bind_method(D_METHOD("_on_tutorial_ready_pressed"), &RtsoMain::_on_tutorial_ready_pressed);
	ClassDB::bind_method(D_METHOD("resign"), &RtsoMain::resign);
	ClassDB::bind_method(D_METHOD("_toggle_session_dialog", "name"), &RtsoMain::_toggle_session_dialog);
	ClassDB::bind_method(D_METHOD("get_target_marker_count"), &RtsoMain::get_target_marker_count);
	ClassDB::bind_method(D_METHOD("get_work_sound_count"), &RtsoMain::get_work_sound_count);
	ClassDB::bind_method(D_METHOD("debug_gather_nearest"), &RtsoMain::debug_gather_nearest);
	ClassDB::bind_method(D_METHOD("debug_trade_route_clicks"), &RtsoMain::debug_trade_route_clicks);
	ClassDB::bind_method(D_METHOD("debug_guard_step", "step"), &RtsoMain::debug_guard_step);
	ClassDB::bind_method(D_METHOD("debug_train_buttons", "btype"), &RtsoMain::debug_train_buttons);
	ClassDB::bind_method(D_METHOD("debug_command_button", "prefix"), &RtsoMain::debug_command_button);
	ClassDB::bind_method(D_METHOD("debug_selection_grid", "select_all"), &RtsoMain::debug_selection_grid);
	ClassDB::bind_method(D_METHOD("debug_civic_centre_roof"), &RtsoMain::debug_civic_centre_roof);
	ClassDB::bind_method(D_METHOD("debug_selection_state"), &RtsoMain::debug_selection_state);
	ClassDB::bind_method(D_METHOD("debug_command_row"), &RtsoMain::debug_command_row);
	ClassDB::bind_method(D_METHOD("debug_delete_units", "ids"), &RtsoMain::debug_delete_units);
	ClassDB::bind_method(D_METHOD("debug_dialogs"), &RtsoMain::debug_dialogs);
	ClassDB::bind_method(D_METHOD("debug_unset_rally_step", "step"), &RtsoMain::debug_unset_rally_step);
	ClassDB::bind_method(D_METHOD("_on_diplo_stance_changed", "stance", "player_idx"), &RtsoMain::_on_diplo_stance_changed);
	ClassDB::bind_method(D_METHOD("_on_diplo_tribute_pressed", "player_idx", "res_type"), &RtsoMain::_on_diplo_tribute_pressed);
	ClassDB::bind_method(D_METHOD("_on_diplo_spy_pressed", "player_idx"), &RtsoMain::_on_diplo_spy_pressed);
	ClassDB::bind_method(D_METHOD("_on_diplo_attack_request_pressed", "player_idx"), &RtsoMain::_on_diplo_attack_request_pressed);
	ClassDB::bind_method(D_METHOD("set_use_english_voice_orders", "enable"), &RtsoMain::set_use_english_voice_orders);
	ClassDB::bind_method(D_METHOD("is_use_english_voice_orders"), &RtsoMain::is_use_english_voice_orders);
	ClassDB::bind_method(D_METHOD("_on_lobby_button_pressed"), &RtsoMain::_on_lobby_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_voice_button_pressed"), &RtsoMain::_on_voice_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_lobby_host_pressed"), &RtsoMain::_on_lobby_host_pressed);
	ClassDB::bind_method(D_METHOD("_on_lobby_join_pressed"), &RtsoMain::_on_lobby_join_pressed);
	ClassDB::bind_method(D_METHOD("_on_lobby_start_pressed"), &RtsoMain::_on_lobby_start_pressed);
	ClassDB::bind_method(D_METHOD("_on_lobby_seat_selected", "index", "client"), &RtsoMain::_on_lobby_seat_selected);
	ClassDB::bind_method(D_METHOD("apply_net_game_start"), &RtsoMain::apply_net_game_start);
	ClassDB::bind_method(D_METHOD("load_map_seeded", "map_id", "seed"), &RtsoMain::load_map_seeded);
	// 0 A.D. GUI onGameLoadProgress(progression, description): the loading page
	// (scenes/menu/loading.gd) listens while the match is set up.
	ADD_SIGNAL(MethodInfo("load_progress", PropertyInfo(Variant::INT, "percent"), PropertyInfo(Variant::STRING, "description")));
	ClassDB::bind_method(D_METHOD("_on_lobby_refresh_pressed"), &RtsoMain::_on_lobby_refresh_pressed);
	ClassDB::bind_method(D_METHOD("_on_lobby_observe_toggled", "on"), &RtsoMain::_on_lobby_observe_toggled);
	ClassDB::bind_method(D_METHOD("_start_menu_net"), &RtsoMain::_start_menu_net);
	ClassDB::bind_method(D_METHOD("get_lobby_state"), &RtsoMain::get_lobby_state);
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

RtsoMain::RtsoMain() {
	sim = new rtso::Simulation();
}

RtsoMain::~RtsoMain() {
	net_host_.stop();
	delete sim;
}

void RtsoMain::_notification(int p_what) {
	// 0 A.D. "pauseonfocusloss": a single-player game pauses when the window loses focus.
	if (p_what == NOTIFICATION_APPLICATION_FOCUS_OUT && sim && !net_lockstep() && !is_game_paused() &&
			rtso::user_config::get_bool("pauseonfocusloss")) {
		toggle_pause();
	}
	if (p_what == NOTIFICATION_EXIT_TREE) {
		write_replay(); // leaving the match keeps its replay
		// Menus get the normal arrow back (update_cursor sets action cursors).
		if (!cursor_name_.empty()) Input::get_singleton()->set_custom_mouse_cursor(Ref<Resource>());
		if (SceneTree *t = get_tree()) {
			rtso::hide_node_popups(t->get_root());
		} else {
			rtso::hide_node_popups(this);
		}
	}
}

static Ref<Texture2D> load_tex(const String &path) {
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<Texture2D> tex = ResourceLoader::get_singleton()->load(path);
		if (tex.is_valid()) return tex;
	}
	return Ref<Texture2D>();
}

// A structure's icon: its portrait, else 0 A.D.'s session portrait from
// art/textures/ui/session/portraits/structures (the template's Identity/Icon,
// under its 0.28 file name where that differs from RTS0's portrait id).
static Ref<Texture2D> structure_icon(const char *portrait_id) {
	Ref<Texture2D> t = load_tex(String("res://assets/textures/ui/portraits/") + portrait_id + ".png");
	if (t.is_valid()) return t;
	static const char *const kIcons[][2] = {
		{"stable", "stable_01"}, {"palisade", "palisade_wall"}, {"forge", "blacksmith"}, {"wall_tower", "tower"},
		{"arsenal", "siege_workshop"}, {"elephant_stable", "stable_elephant"}, {"embassy", "embassy_celtic"},
		{"amphitheater", "theater"}, {"library", "library_scroll"}, {"military_colony", "military_settlement"},
		{"army_camp", "roman_camp"}, {"ice_house", "yakhchal"}, {"ministry", "imperial_ministry"},
		{"imperial_court", "palace"}, {"tachara", "palace"}, {"pillar", "ashoka_pillar"}, {"prytaneion", "tholos"},
		{"gerousia", "tholos"}, {"syssiton", "syssition"}, {"pyramid_large", "kush_pyramid_big"},
		{"pyramid_small", "kush_pyramid_small"}, {"super_dock", "uber_dock"}, {"temple_amun", "temple_epic"},
		{"temple_mars", "temple_epic"}, {"tower_great", "tower_artillery"}, {"council_chamber", "hall"},
		{"assembly", "hall"}, {"academy", "library_scroll"},
	};
	String file = portrait_id;
	for (const auto &m : kIcons)
		if (file == m[0]) { file = m[1]; break; }
	return load_tex("res://assets/textures/ui/archive/portraits/structures/" + file + ".png");
}

// Building names in data/maps/*.json "structures" and "gaia_structures" (tools/port_0ad_maps.py).
// BLD_TYPE_COUNT for a name it doesn't know.
static uint8_t map_building_type(const String &n) {
	if (n == "town_center" || n == "civic" || n == "civil_centre") return rtso::BLD_TOWN_CENTER;
	if (n == "house" || n == "apartment" || n == "hut" || n == "longhouse") return rtso::BLD_HOUSE;
	if (n == "barracks" || n == "hall" || n == "arsenal" || n == "elephant_stable") return rtso::BLD_BARRACKS;
	if (n == "stable") return rtso::BLD_STABLE;
	if (n == "range") return rtso::BLD_RANGE;
	if (n == "storehouse" || n == "forge") return rtso::BLD_STOREHOUSE;
	if (n == "farmstead" || n == "field" || n == "mill" || n == "rotarymill") return rtso::BLD_FARMSTEAD;
	if (n == "market") return rtso::BLD_MARKET;
	if (n == "tower" || n == "defense_tower") return rtso::BLD_TOWER;
	if (n == "sentry_tower") return rtso::BLD_SENTRY_TOWER;
	if (n == "outpost") return rtso::BLD_OUTPOST;
	if (n == "fortress" || n == "palisades_fort" || n == "keep") return rtso::BLD_FORTRESS;
	if (n == "gate" || n == "wall_gate") return rtso::BLD_GATE;
	if (n == "palisade_gate" || n == "palisades_gate") return rtso::BLD_PALISADE_GATE;
	if (n == "siege_wall" || n == "wallset_siege") return rtso::BLD_SIEGE_WALL;
	if (n == "siege_wall_gate") return rtso::BLD_SIEGE_WALL_GATE;
	if (n == "temple" || n == "tophet" || n == "shrine" || n == "pyramid") return rtso::BLD_TEMPLE;
	if (n == "wonder") return rtso::BLD_WONDER;
	if (n == "dock") return rtso::BLD_DOCK;
	if (n == "wall" || n == "wall_segment") return rtso::BLD_WALL_SEGMENT;
	if (n == "palisade") return rtso::BLD_PALISADE;
	if (n == "wall_tower") return rtso::BLD_WALL_TOWER;
	if (n == "corral") return rtso::BLD_CORRAL;
	if (n == "field_plot") return rtso::BLD_FIELD; // a Gaia field (map "field" stays a farmstead)
	if (n == "mercenary_camp") return rtso::BLD_MERCENARY_CAMP;
	return rtso::BLD_TYPE_COUNT;
}

// A map's wall, palisade or gate laid as a turned WallSet piece (port_0ad_maps.py wall_piece:
// "wallset" stone, palisade or siege; "piece" wall, tower or gate; Position "angle"; "length").
static uint32_t spawn_map_wall(rtso::Simulation &sim, const Dictionary &b, uint8_t player) {
	const String ws = b.get("wallset", "stone"), piece = b.get("piece", "wall");
	const uint8_t kind = ws == "palisade" ? rtso::WALLSET_PALISADE : ws == "siege" ? rtso::WALLSET_SIEGE : rtso::WALLSET_STONE;
	const uint8_t p = piece == "tower" ? rtso::Simulation::MAP_WALL_TOWER
			: piece == "gate" ? rtso::Simulation::MAP_WALL_GATE : rtso::Simulation::MAP_WALL_SEGMENT;
	return sim.spawn_map_wall_piece(player, kind, p, (float)b.get("x", 0.0), (float)b.get("z", 0.0), (float)b.get("angle", 0.0),
			(float)b.get("length", 9.0));
}

// A player structure's 0 A.D. template ("athen/field", "spart/gerousia"; port_0ad_maps.py),
// BLD_TYPE_COUNT when RTS0 has no such building.
static uint8_t map_building_template(const String &tmpl) {
	const String n = tmpl.get_slice("/", tmpl.get_slice_count("/") - 1);
	static const struct { const char *name; uint8_t type; } kNames[] = {
		{"civil_centre", rtso::BLD_TOWN_CENTER}, {"house", rtso::BLD_HOUSE}, {"barracks", rtso::BLD_BARRACKS},
		{"storehouse", rtso::BLD_STOREHOUSE}, {"farmstead", rtso::BLD_FARMSTEAD}, {"field", rtso::BLD_FIELD},
		{"market", rtso::BLD_MARKET}, {"temple", rtso::BLD_TEMPLE}, {"forge", rtso::BLD_FORGE},
		{"outpost", rtso::BLD_OUTPOST}, {"sentry_tower", rtso::BLD_SENTRY_TOWER}, {"defense_tower", rtso::BLD_TOWER},
		{"arsenal", rtso::BLD_ARSENAL}, {"stable", rtso::BLD_STABLE}, {"range", rtso::BLD_RANGE},
		{"dock", rtso::BLD_DOCK}, {"fortress", rtso::BLD_FORTRESS}, {"wonder", rtso::BLD_WONDER},
		{"corral", rtso::BLD_CORRAL},
	};
	for (const auto &k : kNames)
		if (n == k.name) return k.type;
	// Civ structures go by their model id (civ_building_data.h: "gerousia", "syssiton").
	for (uint8_t t = rtso::BLD_GYMNASIUM; t < rtso::BLD_TYPE_COUNT; ++t)
		if (n == rtso::building_type_info(t).model_id) return t;
	return rtso::BLD_TYPE_COUNT;
}

// Unit class names in data/maps/*.json "units" and "gaia_units".
static rtso::UnitType map_unit_type(const String &n) {
	static const struct { const char *name; rtso::UnitType type; } kTypes[] = {
		{"swordsman", rtso::UNIT_SWORDSMAN}, {"spearman", rtso::UNIT_SPEARMAN}, {"archer", rtso::UNIT_ARCHER},
		{"cavalry", rtso::UNIT_CAVALRY}, {"trader", rtso::UNIT_TRADER}, {"catapult", rtso::UNIT_CATAPULT},
		{"hero", rtso::UNIT_HERO}, {"trireme", rtso::UNIT_TRIREME}, {"elephant", rtso::UNIT_ELEPHANT},
		{"siege_ram", rtso::UNIT_SIEGE_RAM}, {"healer", rtso::UNIT_HEALER}, {"chariot", rtso::UNIT_CHARIOT},
		{"war_dog", rtso::UNIT_WAR_DOG}, {"bolt_shooter", rtso::UNIT_BOLT_SHOOTER}, {"fishing_boat", rtso::UNIT_FISHING_BOAT},
		{"merchant_ship", rtso::UNIT_MERCHANT_SHIP}, {"female_citizen", rtso::UNIT_FEMALE_CITIZEN}, {"carnyx", rtso::UNIT_CARNYX},
		{"worker_elephant", rtso::UNIT_WORKER_ELEPHANT}, {"archer_maiden", rtso::UNIT_ARCHER_MAIDEN}, {"camel", rtso::UNIT_CAMEL},
		{"scythed_chariot", rtso::UNIT_SCYTHED_CHARIOT}, {"minister", rtso::UNIT_MINISTER},
		{"catafalque", rtso::UNIT_CATAFALQUE}, {"wagon", rtso::UNIT_WAGON},
	};
	for (const auto &t : kTypes)
		if (n == t.name) return t.type;
	return rtso::UNIT_VILLAGER;
}

void RtsoMain::_ready() {
	rtso::Logger::info("Main", "RTS0 0 A.D. Reborn Ready - Full 0 A.D. UI & Simulation");
	// 0 A.D. writes oos_dump.txt/.dat to its log dir; ours is user://.
	turn_mgr_.set_oos_dump_dir(OS::get_singleton()->get_user_data_dir().utf8().get_data());

	// Incoming chat: 0 A.D. NetMessages.h NMT_CHAT parity. Fires on the main
	// thread from net_host_.poll() (_process), so appending to the chat
	// widget (chat_window.h) is safe.
	// net_host.cpp:540 dispatch -> onChat (:395) -> display here.
	net_host_.onChat([this](const rtso::ChatMsg &cm) {
		String msg = String::utf8(cm.message.c_str());
		// Tactical flare piggybacks NMT_CHAT (approximation of 0 A.D. minimap
		// flare): "[Flare] x,z" -> local map ping, not chat text.
		if (msg.begins_with("[Flare]")) {
			String coords = msg.substr(7).strip_edges();
			int32_t comma = coords.find(",");
			if (comma > 0) {
				float fx = coords.substr(0, comma).to_float();
				float fz = coords.substr(comma + 1).to_float();
				if (minimap_view) minimap_view->add_ping(Vector2(fx, fz), Color(1.0f, 0.45f, 0.1f));
				range_overlays_.add_marker(fx, fz, Color(1.0f, 0.45f, 0.1f), user_opts_.flare_time);
				play_alarm("res://assets/audio/interface/alarm/alarmally_1.ogg");
			}
			return;
		}
		// Channel-tag parse + RichTextLabel append live in the chat leaf
		// widget (chat_window.h append_incoming()); no-op until built.
		chat_window_.append_incoming(String::utf8(cm.sender_guid.c_str()), msg);
	});

	// Stash the GameSetup first: load_map_seeded needs view + cam_rig, which
	// are created below. apply_match_settings() runs after they exist.
	// (Map choice was previously dropped - map_index stored but never
	// consumed. Victory / Petra difficulty+behavior / starting resources /
	// per-slot civs apply after the map load because reset()/generate()
	// overwrite stocks.)
	load_progress(30, "Loading animations");
	anim_ctrl.load("res://assets/animations");

	bool has_light = false;
	for (int i = 0; i < get_child_count(); ++i) {
		if (Object::cast_to<DirectionalLight3D>(get_child(i))) {
			has_light = true;
			break;
		}
	}
	if (!has_light) {
		DirectionalLight3D *sun = memnew(DirectionalLight3D);
		sun->set_position(Vector3(0, 50, 0));
		sun->set_rotation_degrees(Vector3(-55, 35, 0));
		sun->set_color(Color(1.0f, 0.96f, 0.90f));
		sun->set_param(Light3D::PARAM_ENERGY, 1.4f);
		add_child(sun);
	}

	// 0 A.D. reads hotkeys from default.cfg, then the player's user.cfg.
	if (rtso::hotkeys::load_user_cfg()) print_line("[RTS0] Loaded hotkeys from user://config/user.cfg");

	cam_rig = memnew(RtsoCameraRig);
	add_child(cam_rig);

	view = memnew(RtsoWorldView);
	add_child(view);

	ui_layer = memnew(CanvasLayer);
	add_child(ui_layer);

	Ref<Theme> zeroad_theme;
	if (ResourceLoader::get_singleton()->exists("res://assets/themes/zeroad_theme.tres")) {
		zeroad_theme = ResourceLoader::get_singleton()->load("res://assets/themes/zeroad_theme.tres");
	}

	sel_rect = memnew(RtsoSelectionRect);
	ui_layer->add_child(sel_rect);

	debug_overlay = memnew(RtsoDebugOverlay);
	debug_overlay->set_visible(false); // fps.toggle (Alt+F) shows it, as 0 A.D.'s FPS counter
	ui_layer->add_child(debug_overlay);

	minimap_view = memnew(RtsoMiniMapView);
	minimap_view->connect("minimap_clicked", Callable(this, "_on_minimap_clicked"));
	minimap_view->connect("minimap_order", Callable(this, "_on_minimap_order"));
	ui_layer->add_child(minimap_view);

	// Minimap layer toggles (methods were ClassDB-bound but had no UI driver).
	// Row sits directly above the 190px minimap (see minimap ctor offsets).
	{
		HBoxContainer *mm_btns = memnew(HBoxContainer);
		mm_btns->set_anchor(SIDE_LEFT, Control::ANCHOR_BEGIN);
		mm_btns->set_anchor(SIDE_RIGHT, Control::ANCHOR_BEGIN);
		mm_btns->set_anchor(SIDE_TOP, Control::ANCHOR_END);
		mm_btns->set_anchor(SIDE_BOTTOM, Control::ANCHOR_END);
		mm_btns->set_offset(SIDE_LEFT, 14.0f);
		mm_btns->set_offset(SIDE_RIGHT, 204.0f);
		mm_btns->set_offset(SIDE_TOP, -230.0f);
		mm_btns->set_offset(SIDE_BOTTOM, -206.0f);
		mm_btns->add_theme_constant_override("separation", 4);
		struct MmBtn { const char *label; const char *method; };
		const MmBtn mm_btn_defs[4] = {
			// 3-state shroud cycle (RtsoMiniMapView::cycle_shroud, minimap_view.h ShroudMode):
			// On -> terrain-only -> off -> On. 0AD MiniMapPanel.js:8-11 panel buttons.
			{"Shroud", "cycle_shroud"},
			{"Terr", "toggle_territory"},
			{"Res", "toggle_resources"},
			{"Diplo", "toggle_diplomacy"},
		};
		for (const MmBtn &d : mm_btn_defs) {
			Button *b = memnew(Button);
			b->set_text(d.label);
			b->set_custom_minimum_size(Vector2(44, 22));
			b->set_focus_mode(Control::FOCUS_NONE);
			b->add_theme_font_size_override("font_size", 9);
			b->connect("pressed", Callable(minimap_view, d.method));
			mm_btns->add_child(b);
		}
		ui_layer->add_child(mm_btns);
	}

	// =========================================================================
	// 1. TOP HUD BAR PANEL (Civ Crest, 4 Resources, Pop, Idle, Clock, Speed, Menu)
	// =========================================================================
	top_hud_panel = memnew(Panel);
	top_hud_panel->set_anchor(SIDE_LEFT, 0.0f);
	top_hud_panel->set_anchor(SIDE_RIGHT, 1.0f);
	top_hud_panel->set_anchor(SIDE_TOP, 0.0f);
	top_hud_panel->set_anchor(SIDE_BOTTOM, 0.0f);
	top_hud_panel->set_offset(SIDE_LEFT, 12.0f);
	top_hud_panel->set_offset(SIDE_RIGHT, -12.0f);
	top_hud_panel->set_offset(SIDE_TOP, 8.0f);
	top_hud_panel->set_offset(SIDE_BOTTOM, 54.0f);

	Ref<StyleBoxFlat> top_style;
	top_style.instantiate();
	top_style->set_bg_color(Color(0.10f, 0.11f, 0.15f, 0.94f));
	top_style->set_corner_radius_all(3);
	top_style->set_border_width_all(2);
	top_style->set_border_color(Color(0.90f, 0.75f, 0.31f, 1.0f));
	top_hud_panel->add_theme_stylebox_override("panel", top_style);
	if (zeroad_theme.is_valid()) top_hud_panel->set_theme(zeroad_theme);
	ui_layer->add_child(top_hud_panel);

	HBoxContainer *top_hbox = memnew(HBoxContainer);
	top_hbox->set_anchor(SIDE_LEFT, 0.0f);
	top_hbox->set_anchor(SIDE_RIGHT, 1.0f);
	top_hbox->set_anchor(SIDE_TOP, 0.0f);
	top_hbox->set_anchor(SIDE_BOTTOM, 1.0f);
	top_hbox->set_offset(SIDE_LEFT, 8.0f);
	top_hbox->set_offset(SIDE_RIGHT, -8.0f);
	top_hbox->set_offset(SIDE_TOP, 4.0f);
	top_hbox->set_offset(SIDE_BOTTOM, -4.0f);
	top_hbox->add_theme_constant_override("separation", 4);
	top_hud_panel->add_child(top_hbox);

	top_panel_.build(this, top_hbox, sim->player_civ());
	{
		auto first_key = [](const char *name) {
			const auto *combos = rtso::hotkeys::table().get(name);
			return (combos && !combos->empty()) ? String::utf8(rtso::hotkeys::combo_to_string(combos->front()).c_str()) : String();
		};
		top_panel_.set_dialog_tooltips(first_key("session.gui.diplomacy.toggle"),
				first_key("session.gui.barter.toggle"),
				first_key(rtso::hotkeys::table().get("session.gui.matchsettings.toggle") ? "session.gui.matchsettings.toggle" : "session.gui.objectives.toggle"));
	}

	time_notification_label_ = memnew(Label);
	time_notification_label_->set_anchor(SIDE_LEFT, 0.0f);
	time_notification_label_->set_anchor(SIDE_RIGHT, 1.0f);
	time_notification_label_->set_anchor(SIDE_TOP, 0.0f);
	time_notification_label_->set_anchor(SIDE_BOTTOM, 0.0f);
	time_notification_label_->set_offset(SIDE_TOP, 62.0f);
	time_notification_label_->set_offset(SIDE_BOTTOM, 140.0f);
	time_notification_label_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	time_notification_label_->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	time_notification_label_->add_theme_font_size_override("font_size", 16);
	time_notification_label_->add_theme_color_override("font_color", Color(1.0f, 0.92f, 0.6f));
	time_notification_label_->add_theme_color_override("font_outline_color", Color(0.0f, 0.0f, 0.0f));
	time_notification_label_->add_theme_constant_override("outline_size", 4);
	time_notification_label_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	ui_layer->add_child(time_notification_label_);

	cursor_tip_ = memnew(Label);
	cursor_tip_->add_theme_font_size_override("font_size", 13);
	cursor_tip_->add_theme_color_override("font_color", Color(1.0f, 1.0f, 1.0f));
	cursor_tip_->add_theme_color_override("font_outline_color", Color(0.0f, 0.0f, 0.0f));
	cursor_tip_->add_theme_constant_override("outline_size", 4);
	cursor_tip_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	cursor_tip_->set_visible(false);
	ui_layer->add_child(cursor_tip_);

	// Speed & Menu
	speed_btn = memnew(Button);
	speed_btn->set_custom_minimum_size(Vector2(45, 28));
	speed_btn->set_text("1x");
	speed_btn->add_theme_font_size_override("font_size", 12);
	speed_btn->connect("pressed", Callable(this, "_on_speed_button_pressed"));
	top_hbox->add_child(speed_btn);

	menu_btn = memnew(Button);
	menu_btn->set_custom_minimum_size(Vector2(60, 28));
	menu_btn->set_text("Menu");
	menu_btn->add_theme_font_size_override("font_size", 12);
	menu_btn->connect("pressed", Callable(this, "_on_menu_button_pressed"));
	top_hbox->add_child(menu_btn);

	map_selector = memnew(RtsoSafeSelect);
	map_selector->set_custom_minimum_size(Vector2(165, 28));
	PackedStringArray av_maps = get_available_maps();
	for (int i = 0; i < av_maps.size(); ++i) {
		String mid = av_maps[i];
		String title = mid.replace("_", " ").capitalize();
		if (mid.begins_with("random_")) {
			title = "RMS: " + title.trim_prefix("Random ");
		}
		map_selector->add_item(title, i);
	}
	map_selector->connect("item_selected", Callable(this, "_on_map_selected"));
	map_selector->set_visible(false); // set in Game Setup; 0 A.D.'s top panel has no setup controls
	top_hbox->add_child(map_selector);

	civ_selector = memnew(RtsoSafeSelect);
	civ_selector->set_custom_minimum_size(Vector2(125, 28));
	civ_selector->add_item("Athenians", rtso::CIV_ATHENIANS);
	civ_selector->add_item("Spartans", rtso::CIV_SPARTANS);
	civ_selector->add_item("Persians", rtso::CIV_PERSIANS);
	civ_selector->add_item("Romans", rtso::CIV_ROMANS);
	civ_selector->add_item("Carthaginians", rtso::CIV_CARTHAGINIANS);
	civ_selector->add_item("Macedonians", rtso::CIV_MACEDONIANS);
	civ_selector->add_item("Britons", rtso::CIV_BRITONS);
	civ_selector->add_item("Gauls", rtso::CIV_GAULS);
	civ_selector->add_item("Iberians", rtso::CIV_IBERIANS);
	civ_selector->add_item("Mauryans", rtso::CIV_MAURYANS);
	civ_selector->add_item("Kushites", rtso::CIV_KUSHITES);
	civ_selector->add_item("Ptolemies", rtso::CIV_PTOLEMIES);
	civ_selector->add_item("Seleucids", rtso::CIV_SELEUCIDS);
	civ_selector->add_item("Han Chinese", rtso::CIV_HAN);
	civ_selector->add_item("Germans", rtso::CIV_GERMANS);
	civ_selector->connect("item_selected", Callable(this, "_on_civ_selected"));
	civ_selector->select((int)sim->player_civ());
	civ_selector->set_visible(false); // set in Game Setup; 0 A.D.'s top panel has no setup controls
	top_hbox->add_child(civ_selector);

	ai_diff_selector = memnew(RtsoSafeSelect);
	ai_diff_selector->set_custom_minimum_size(Vector2(100, 28));
	ai_diff_selector->add_item("AI Sandbox", rtso::PETRA_SANDBOX);
	ai_diff_selector->add_item("AI Very Easy", rtso::PETRA_VERY_EASY);
	ai_diff_selector->add_item("AI Easy", rtso::PETRA_EASY);
	ai_diff_selector->add_item("AI Medium", rtso::PETRA_MEDIUM);
	ai_diff_selector->add_item("AI Hard", rtso::PETRA_HARD);
	ai_diff_selector->add_item("AI Very Hard", rtso::PETRA_VERY_HARD);
	ai_diff_selector->select(rtso::PETRA_MEDIUM);
	ai_diff_selector->connect("item_selected", Callable(this, "_on_ai_diff_selected"));
	ai_diff_selector->set_visible(false); // set in Game Setup; 0 A.D.'s top panel has no setup controls
	top_hbox->add_child(ai_diff_selector);

	ai_behavior_selector = memnew(RtsoSafeSelect);
	ai_behavior_selector->set_custom_minimum_size(Vector2(110, 28));
	ai_behavior_selector->add_item("AI Balanced", rtso::PETRA_BEHAVIOR_BALANCED);
	ai_behavior_selector->add_item("AI Defensive", rtso::PETRA_BEHAVIOR_DEFENSIVE);
	ai_behavior_selector->add_item("AI Aggressive", rtso::PETRA_BEHAVIOR_AGGRESSIVE);
	ai_behavior_selector->add_item("AI Random", rtso::PETRA_BEHAVIOR_RANDOM);
	ai_behavior_selector->connect("item_selected", Callable(this, "_on_ai_behavior_selected"));
	ai_behavior_selector->set_visible(false); // set in Game Setup; 0 A.D.'s top panel has no setup controls
	top_hbox->add_child(ai_behavior_selector);

	victory_selector = memnew(RtsoSafeSelect);
	victory_selector->set_custom_minimum_size(Vector2(110, 28));
	victory_selector->add_item("Conquest", rtso::VICTORY_CONQUEST);
	victory_selector->add_item("Wonder", rtso::VICTORY_WONDER);
	victory_selector->add_item("Relics", rtso::VICTORY_RELIC);
	victory_selector->add_item("Endless", rtso::VICTORY_ENDLESS);
	victory_selector->add_item("Conquest Units", rtso::VICTORY_CONQUEST_UNITS);
	victory_selector->add_item("Conquest Structures", rtso::VICTORY_CONQUEST_STRUCTURES);
	victory_selector->add_item("Regicide", rtso::VICTORY_REGICIDE);
	// The victory condition is fixed in Game Setup; changing it mid-match would desync lockstep peers.
	victory_selector->set_disabled(true);
	victory_selector->set_tooltip_text("Victory condition (set in Game Setup)");
	victory_selector->connect("item_selected", Callable(this, "_on_victory_selected"));
	victory_selector->set_visible(false); // set in Game Setup; 0 A.D.'s top panel has no setup controls
	top_hbox->add_child(victory_selector);

	voice_btn = memnew(Button);
	voice_btn->set_custom_minimum_size(Vector2(72, 28));
	voice_btn->set_text("Voice: EN");
	voice_btn->add_theme_font_size_override("font_size", 12);
	voice_btn->connect("pressed", Callable(this, "_on_voice_button_pressed"));
	voice_btn->set_visible(false); // set in Game Setup; 0 A.D.'s top panel has no setup controls
	top_hbox->add_child(voice_btn);

	lobby_btn = memnew(Button);
	lobby_btn->set_custom_minimum_size(Vector2(64, 28));
	lobby_btn->set_text("Lobby");
	lobby_btn->add_theme_font_size_override("font_size", 12);
	lobby_btn->connect("pressed", Callable(this, "_on_lobby_button_pressed"));
	// 0 A.D.'s top panel shows its Lobby button only with a lobby connection
	// (Engine.HasXmppClient); here, in a game started from the lobby screen.
	lobby_btn->set_visible(false);
	top_hbox->add_child(lobby_btn);

	lobby_panel = memnew(Panel);
	lobby_panel->set_visible(false);
	lobby_panel->set_anchor(SIDE_LEFT, 0.0f);
	lobby_panel->set_anchor(SIDE_RIGHT, 0.0f);
	lobby_panel->set_anchor(SIDE_TOP, 0.0f);
	lobby_panel->set_anchor(SIDE_BOTTOM, 0.0f);
	lobby_panel->set_offset(SIDE_LEFT, 16.0f);
	lobby_panel->set_offset(SIDE_RIGHT, 420.0f);
	lobby_panel->set_offset(SIDE_TOP, 52.0f);
	lobby_panel->set_offset(SIDE_BOTTOM, 320.0f);
	ui_layer->add_child(lobby_panel);

	VBoxContainer *lobby_box = memnew(VBoxContainer);
	lobby_box->set_anchors_preset(Control::PRESET_FULL_RECT);
	lobby_box->set_offset(SIDE_LEFT, 8);
	lobby_box->set_offset(SIDE_RIGHT, -8);
	lobby_box->set_offset(SIDE_TOP, 8);
	lobby_box->set_offset(SIDE_BOTTOM, -8);
	lobby_panel->add_child(lobby_box);

	Label *lobby_title = memnew(Label);
	lobby_title->set_text("Multiplayer lobby");
	lobby_box->add_child(lobby_title);

	HBoxContainer *name_row = memnew(HBoxContainer);
	lobby_name = memnew(LineEdit);
	lobby_name->set_placeholder("Player name");
	lobby_name->set_text("Player");
	lobby_name->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	rtso::parent_line_edit(name_row, lobby_name);
	lobby_host = memnew(LineEdit);
	lobby_host->set_placeholder("127.0.0.1");
	lobby_host->set_text("127.0.0.1");
	lobby_host->set_custom_minimum_size(Vector2(110, 0));
	rtso::parent_line_edit(name_row, lobby_host);
	lobby_port = memnew(LineEdit);
	lobby_port->set_placeholder("20595");
	lobby_port->set_text("20595");
	lobby_port->set_custom_minimum_size(Vector2(70, 0));
	rtso::parent_line_edit(name_row, lobby_port);
	lobby_password = memnew(LineEdit);
	lobby_password->set_placeholder("Password");
	lobby_password->set_secret(true);
	lobby_password->set_custom_minimum_size(Vector2(90, 0));
	rtso::parent_line_edit(name_row, lobby_password);
	lobby_box->add_child(name_row);

	lobby_rooms = memnew(ItemList);
	lobby_rooms->set_custom_minimum_size(Vector2(0, 140));
	lobby_box->add_child(lobby_rooms);

	HBoxContainer *lobby_actions = memnew(HBoxContainer);
	Button *host_b = memnew(Button);
	host_b->set_text("Host");
	host_b->connect("pressed", Callable(this, "_on_lobby_host_pressed"));
	lobby_actions->add_child(host_b);
	Button *join_b = memnew(Button);
	join_b->set_text("Join");
	join_b->connect("pressed", Callable(this, "_on_lobby_join_pressed"));
	lobby_actions->add_child(join_b);
	lobby_observe = memnew(Button);
	lobby_observe->set_text("Observe");
	lobby_observe->set_toggle_mode(true);
	lobby_observe->set_tooltip_text("Watch instead of playing: host or join a match (or one in progress) as an observer");
	lobby_observe->connect("toggled", Callable(this, "_on_lobby_observe_toggled"));
	lobby_actions->add_child(lobby_observe);
	Button *ref_b = memnew(Button);
	ref_b->set_text("Refresh");
	ref_b->connect("pressed", Callable(this, "_on_lobby_refresh_pressed"));
	lobby_actions->add_child(ref_b);
	Button *start_b = memnew(Button);
	start_b->set_text("Start");
	start_b->set_tooltip_text("Host: regenerate map with a fresh seed and broadcast map+seed (NMT_GAME_START)");
	start_b->connect("pressed", Callable(this, "_on_lobby_start_pressed"));
	lobby_actions->add_child(start_b);
	lobby_box->add_child(lobby_actions);

	lobby_status = memnew(Label);
	lobby_status->set_text("Lobby HTTP 127.0.0.1:8088 - run tools/lobby_server.py");
	lobby_box->add_child(lobby_status);
	lobby_players = memnew(Label);
	lobby_players->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	lobby_box->add_child(lobby_players);
	lobby_seats = memnew(VBoxContainer);
	lobby_box->add_child(lobby_seats);

	// =========================================================================
	// 2. BOTTOM INSPECTOR PANEL (5-Bar Stack: Health, Capture, XP, Stamina, Aura)
	// =========================================================================
	inspector_panel = memnew(Panel);
	inspector_panel->set_anchor(SIDE_LEFT, 0.0f);
	inspector_panel->set_anchor(SIDE_RIGHT, 0.0f);
	inspector_panel->set_anchor(SIDE_TOP, 1.0f);
	inspector_panel->set_anchor(SIDE_BOTTOM, 1.0f);
	inspector_panel->set_offset(SIDE_LEFT, 214.0f);
	inspector_panel->set_offset(SIDE_RIGHT, 730.0f);
	inspector_panel->set_offset(SIDE_TOP, -198.0f);
	inspector_panel->set_offset(SIDE_BOTTOM, -10.0f);

	Ref<StyleBoxFlat> insp_style;
	insp_style.instantiate();
	insp_style->set_bg_color(Color(0.10f, 0.11f, 0.15f, 0.94f));
	insp_style->set_corner_radius_all(3);
	insp_style->set_border_width_all(2);
	insp_style->set_border_color(Color(0.90f, 0.75f, 0.31f, 1.0f));
	inspector_panel->add_theme_stylebox_override("panel", insp_style);
	if (zeroad_theme.is_valid()) inspector_panel->set_theme(zeroad_theme);
	ui_layer->add_child(inspector_panel);

	inspector_portrait = memnew(TextureRect);
	inspector_portrait->set_position(Vector2(10, 10));
	inspector_portrait->set_size(Vector2(84, 84));
	inspector_portrait->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	inspector_portrait->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_COVERED);
	inspector_panel->add_child(inspector_portrait);

	inspector_title = memnew(Label);
	inspector_title->set_position(Vector2(102, 6));
	inspector_title->set_size(Vector2(404, 18));
	inspector_title->add_theme_font_size_override("font_size", 13);
	inspector_title->add_theme_color_override("font_color", Color(1.0f, 0.88f, 0.4f));
	inspector_panel->add_child(inspector_title);

	inspector_class = memnew(Label);
	inspector_class->set_position(Vector2(102, 24));
	inspector_class->set_size(Vector2(404, 15));
	inspector_class->add_theme_font_size_override("font_size", 10);
	inspector_class->add_theme_color_override("font_color", Color(0.7f, 0.75f, 0.85f));
	inspector_panel->add_child(inspector_class);

	// Helper lambda to setup 5 inspector bars with PNG icons or fallback text-based glyph icon labels
	// (0 A.D. gui/session/selection_details.js icon parity: HP/CP/XP/STA/AURA)
	auto make_insp_bar = [&](ProgressBar *&pbar, Label *&plbl, int y, Color fill_col, const char *glyph_text, const char *icon_path = nullptr) {
		Label *glyph_lbl = memnew(Label);
		glyph_lbl->set_position(Vector2(102, y - 2));
		glyph_lbl->set_size(Vector2(42, 14));
		glyph_lbl->set_text(glyph_text);
		glyph_lbl->add_theme_font_size_override("font_size", 9);
		glyph_lbl->add_theme_color_override("font_color", fill_col);
		inspector_panel->add_child(glyph_lbl);

		if (icon_path && *icon_path) {
			Ref<Texture2D> icon_tex = load_tex(icon_path);
			if (icon_tex.is_valid()) {
				TextureRect *icon_rect = memnew(TextureRect);
				icon_rect->set_position(Vector2(102, y - 2));
				icon_rect->set_size(Vector2(14, 14));
				icon_rect->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
				icon_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
				icon_rect->set_texture(icon_tex);
				inspector_panel->add_child(icon_rect);
				glyph_lbl->set_visible(false);
			}
		}

		pbar = memnew(ProgressBar);
		pbar->set_position(Vector2(148, y));
		pbar->set_size(Vector2(358, 11));
		pbar->set_show_percentage(false);

		Ref<StyleBoxFlat> bg_sb;
		bg_sb.instantiate();
		bg_sb->set_bg_color(Color(0.10f, 0.12f, 0.16f, 0.85f));
		bg_sb->set_corner_radius_all(2);
		pbar->add_theme_stylebox_override("background", bg_sb);

		Ref<StyleBoxFlat> fg_sb;
		fg_sb.instantiate();
		fg_sb->set_bg_color(fill_col);
		fg_sb->set_corner_radius_all(2);
		pbar->add_theme_stylebox_override("fill", fg_sb);
		inspector_panel->add_child(pbar);

		plbl = memnew(Label);
		plbl->set_position(Vector2(148, y - 1));
		plbl->set_size(Vector2(358, 13));
		plbl->add_theme_font_size_override("font_size", 9);
		plbl->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		plbl->add_theme_color_override("font_color", Color(0.95f, 0.95f, 0.98f));
		inspector_panel->add_child(plbl);
	};

	// 1. Health Bar (Green)
	make_insp_bar(inspector_hp_bar, inspector_hp_label, 41, Color(0.25f, 0.75f, 0.35f), "HP", "res://assets/textures/ui/icons/insp_health_fg.png");
	// 2. Capture Points Bar (Cyan / Blue)
	make_insp_bar(inspector_capture_bar, inspector_capture_label, 54, Color(0.35f, 0.55f, 0.95f), "CP", "res://assets/textures/ui/icons/insp_capture_bar.png");
	// 3. Veterancy / Experience XP Bar (Gold / Amber)
	make_insp_bar(inspector_xp_bar, inspector_xp_label, 67, Color(0.95f, 0.75f, 0.20f), "XP", "res://assets/textures/ui/icons/insp_experience_fg.png");
	// 4. Stamina / Energy Bar (Teal / Mint)
	make_insp_bar(inspector_stamina_bar, inspector_stamina_label, 80, Color(0.20f, 0.85f, 0.75f), "STA", "res://assets/textures/ui/icons/insp_stamina_fg.png");
	// 5. Aura Influence Bar (Magenta / Violet) - no public.zip session icon, fallback to text glyph badge
	make_insp_bar(inspector_aura_bar, inspector_aura_label, 93, Color(0.85f, 0.35f, 0.85f), "AURA", nullptr);

	inspector_stats = memnew(Label);
	inspector_stats->set_position(Vector2(102, 108));
	inspector_stats->set_size(Vector2(404, 38));
	inspector_stats->add_theme_font_size_override("font_size", 10);
	inspector_stats->add_theme_color_override("font_color", Color(0.85f, 0.88f, 0.95f));
	inspector_panel->add_child(inspector_stats);

	inspector_extra = memnew(Label);
	inspector_extra->set_position(Vector2(10, 100));
	inspector_extra->set_size(Vector2(88, 48));
	inspector_extra->add_theme_font_size_override("font_size", 9);
	inspector_extra->add_theme_color_override("font_color", Color(0.4f, 0.95f, 0.6f));
	inspector_panel->add_child(inspector_extra);

	ungarrison_btn = memnew(Button);
	ungarrison_btn->set_position(Vector2(10, 150));
	ungarrison_btn->set_size(Vector2(88, 26));
	ungarrison_btn->set_text("Ungarrison (Z)");
	ungarrison_btn->add_theme_font_size_override("font_size", 10);
	ungarrison_btn->connect("pressed", Callable(this, "_on_ungarrison_pressed"));
	ungarrison_btn->set_visible(false);
	inspector_panel->add_child(ungarrison_btn);

	// 0 A.D. selection_details.js: the "i" button opens the Template Viewer
	// (gui/reference/viewer) for the selected unit or structure.
	template_info_btn_ = memnew(Button);
	template_info_btn_->set_position(Vector2(72, 72));
	template_info_btn_->set_size(Vector2(22, 22));
	template_info_btn_->set_text("i");
	template_info_btn_->add_theme_font_size_override("font_size", 12);
	template_info_btn_->set_focus_mode(Control::FOCUS_NONE);
	template_info_btn_->connect("pressed", Callable(this, "_on_template_info_pressed"));
	inspector_panel->add_child(template_info_btn_);

	// Multi-selection and garrison icon grids (0 A.D. selection_panels.js).
	selection_grid_.build(inspector_panel, Vector2(102, 60), Vector2(404, 120), this, "_on_selection_group_clicked", 10, 36.0f);
	selection_grid_.set_right_click("_on_selection_group_gui_input");
	garrison_grid_.build(inspector_panel, Vector2(102, 148), Vector2(404, 34), this, "_on_garrison_group_clicked", 12, 30.0f);

	// =========================================================================
	// 3. COMMAND & ACTION PANEL (Stances, Formations, Training, 8-Slot Queue)
	// =========================================================================
	command_panel = memnew(Panel);
	command_panel->set_anchor(SIDE_LEFT, 1.0f);
	command_panel->set_anchor(SIDE_RIGHT, 1.0f);
	command_panel->set_anchor(SIDE_TOP, 1.0f);
	command_panel->set_anchor(SIDE_BOTTOM, 1.0f);
	command_panel->set_offset(SIDE_LEFT, -584.0f);
	command_panel->set_offset(SIDE_RIGHT, -12.0f);
	command_panel->set_offset(SIDE_TOP, -198.0f);
	command_panel->set_offset(SIDE_BOTTOM, -10.0f);

	Ref<StyleBoxFlat> cmd_style;
	cmd_style.instantiate();
	cmd_style->set_bg_color(Color(0.10f, 0.11f, 0.15f, 0.94f));
	cmd_style->set_corner_radius_all(3);
	cmd_style->set_border_width_all(2);
	cmd_style->set_border_color(Color(0.90f, 0.75f, 0.31f, 1.0f));
	command_panel->add_theme_stylebox_override("panel", cmd_style);
	if (zeroad_theme.is_valid()) command_panel->set_theme(zeroad_theme);
	ui_layer->add_child(command_panel);

	roster_list = memnew(ItemList);
	roster_list->set_anchor(SIDE_LEFT, 1.0f);
	roster_list->set_anchor(SIDE_RIGHT, 1.0f);
	roster_list->set_anchor(SIDE_TOP, 1.0f);
	roster_list->set_anchor(SIDE_BOTTOM, 1.0f);
	roster_list->set_offset(SIDE_LEFT, -584.0f);
	roster_list->set_offset(SIDE_RIGHT, -12.0f);
	roster_list->set_offset(SIDE_TOP, -326.0f);
	roster_list->set_offset(SIDE_BOTTOM, -202.0f);
	roster_list->set_max_columns(3);
	roster_list->set_same_column_width(true);
	roster_list->set_fixed_column_width(180);
	roster_list->add_theme_font_size_override("font_size", 11);
	roster_list->add_theme_stylebox_override("panel", cmd_style);
	if (zeroad_theme.is_valid()) roster_list->set_theme(zeroad_theme);
	roster_list->set_visible(false);
	roster_list->connect("item_clicked", Callable(this, "_on_roster_item_clicked"));
	ui_layer->add_child(roster_list);

	command_title = memnew(Label);
	command_title->set_position(Vector2(10, 6));
	command_title->set_size(Vector2(552, 16));
	command_title->add_theme_font_size_override("font_size", 12);
	command_title->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.4f));
	command_title->set_text("Commands & Actions");
	command_panel->add_child(command_title);

	// Stances Bar (Aggressive, Defensive, Stand Ground, Hold, Passive)
	stances_bar = memnew(HBoxContainer);
	stances_bar->set_position(Vector2(10, 24));
	stances_bar->set_size(Vector2(552, 26));
	stances_bar->add_theme_constant_override("separation", 6);
	command_panel->add_child(stances_bar);

	// Indexed by UnitStance; laid out in 0 A.D. g_Stances order (violent, aggressive,
	// defensive, passive, standground: UnitAI's selectable stances when a template names
	// none), with selection_panels_helpers.js names and tooltips.
	const char *stance_names[5] = {"Aggressive", "Defensive", "Standground", "Violent", "Passive"};
	const char *stance_tips[5] = {
		"Attack nearby opponents",
		"Attack nearby opponents, chase a short distance and return to the original location",
		"Attack opponents in range, but don't move",
		"Attack nearby opponents, focus on attackers and chase while visible",
		"Flee if attacked"
	};
	// 0 A.D. stance icons (art/textures/ui/session/stances), same order as the names.
	const char *stance_icons[5] = {
		"res://assets/textures/ui/stances/aggressive.png",
		"res://assets/textures/ui/stances/defensive.png",
		"res://assets/textures/ui/stances/standground.png",
		"res://assets/textures/ui/stances/violent.png",
		"res://assets/textures/ui/stances/passive.png"
	};
	const int stance_order[5] = {rtso::STANCE_VIOLENT, rtso::STANCE_AGGRESSIVE, rtso::STANCE_DEFENSIVE,
			rtso::STANCE_PASSIVE, rtso::STANCE_STAND_GROUND};
	for (int s : stance_order) {
		Button *sb = memnew(Button);
		sb->set_custom_minimum_size(Vector2(95, 24));
		sb->set_text(stance_names[s]);
		sb->set_tooltip_text(String(stance_names[s]) + "\n" + stance_tips[s]);
		sb->set_toggle_mode(true); // pressed = some selected unit holds it (IsStanceSelected)
		Ref<Texture2D> s_ico = load_tex(stance_icons[s]);
		if (s_ico.is_valid()) {
			sb->set_button_icon(s_ico);
			sb->set_expand_icon(true);
		}
		sb->add_theme_font_size_override("font_size", 9);
		sb->connect("pressed", Callable(this, "_on_stance_selected").bind(s));
		stances_bar->add_child(sb);
		stance_buttons_[s] = sb;
	}
	stances_bar->set_visible(false);

	// Actions & Formations Bar
	actions_bar = memnew(HBoxContainer);
	actions_bar->set_position(Vector2(10, 52));
	actions_bar->set_size(Vector2(552, 26));
	actions_bar->add_theme_constant_override("separation", 2);
	command_panel->add_child(actions_bar);

	// Production Bar (Training & Tech)
	// The build menu has 25+ structures: the buttons wrap onto rows and
	// scroll inside the panel instead of running off the screen at 1280x720.
	production_scroll = memnew(ScrollContainer);
	production_scroll->set_position(Vector2(10, 80));
	production_scroll->set_size(Vector2(552, 40));
	production_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	command_panel->add_child(production_scroll);
	production_bar = memnew(HFlowContainer);
	production_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	production_bar->add_theme_constant_override("h_separation", 4);
	production_bar->add_theme_constant_override("v_separation", 2);
	production_scroll->add_child(production_bar);

	// Visual 8-Slot Production Queue Bar (ProductionQueue.js parity)
	queue_bar = memnew(HBoxContainer);
	queue_bar->set_position(Vector2(10, 122));
	queue_bar->set_size(Vector2(552, 54));
	queue_bar->add_theme_constant_override("separation", 6);
	command_panel->add_child(queue_bar);

	production_queue_.build(this, queue_bar, this);

	// =========================================================================
	// 4. BANNER OVERLAY (Victory / Defeat)
	// =========================================================================
	banner_panel = memnew(Panel);
	banner_panel->set_anchor(SIDE_LEFT, 0.5f);
	banner_panel->set_anchor(SIDE_RIGHT, 0.5f);
	banner_panel->set_anchor(SIDE_TOP, 0.5f);
	banner_panel->set_anchor(SIDE_BOTTOM, 0.5f);
	banner_panel->set_offset(SIDE_LEFT, -220.0f);
	banner_panel->set_offset(SIDE_RIGHT, 220.0f);
	banner_panel->set_offset(SIDE_TOP, -80.0f);
	banner_panel->set_offset(SIDE_BOTTOM, 80.0f);
	banner_panel->set_visible(false);

	Ref<StyleBoxFlat> banner_style;
	banner_style.instantiate();
	banner_style->set_bg_color(Color(0.05f, 0.06f, 0.09f, 0.95f));
	banner_style->set_corner_radius_all(10);
	banner_style->set_border_width_all(2);
	banner_style->set_border_color(Color(0.8f, 0.7f, 0.2f, 1.0f));
	banner_panel->add_theme_stylebox_override("panel", banner_style);
	ui_layer->add_child(banner_panel);

	banner_label = memnew(Label);
	banner_label->set_position(Vector2(20, 25));
	banner_label->set_size(Vector2(400, 110));
	banner_label->add_theme_font_size_override("font_size", 22);
	banner_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	banner_panel->add_child(banner_label);

	// PauseOverlay.xml: a dimmed full-screen button with the pause captions.
	pause_overlay_ = memnew(Button);
	pause_overlay_->set_anchors_preset(Control::PRESET_FULL_RECT);
	pause_overlay_->set_flat(true);
	pause_overlay_->set_focus_mode(Control::FOCUS_NONE);
	pause_overlay_->set_visible(false);
	pause_overlay_->connect("pressed", Callable(this, "_on_pause_overlay_pressed"));
	{
		ColorRect *dim = memnew(ColorRect);
		dim->set_anchors_preset(Control::PRESET_FULL_RECT);
		dim->set_color(Color(0, 0, 0, 0.45f));
		dim->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		pause_overlay_->add_child(dim);
		auto caption = [&](const String &text, float top, int size) {
			Label *l = memnew(Label);
			l->set_text(text);
			l->set_anchor(SIDE_LEFT, 0.3f);
			l->set_anchor(SIDE_RIGHT, 0.7f);
			l->set_anchor(SIDE_TOP, 0.4f);
			l->set_anchor(SIDE_BOTTOM, 0.4f);
			l->set_offset(SIDE_TOP, top);
			l->set_offset(SIDE_BOTTOM, top + size + 8);
			l->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			l->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
			l->add_theme_font_size_override("font_size", size);
			l->add_theme_color_override("font_outline_color", Color(0, 0, 0));
			l->add_theme_constant_override("outline_size", 4);
			l->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			pause_overlay_->add_child(l);
			return l;
		};
		caption(tr("Game Paused"), -20.0f, 28);
		pause_resume_label_ = caption(tr("Click to Resume Game"), 20.0f, 14);
		pause_by_label_ = caption("", 50.0f, 14);
	}
	ui_layer->add_child(pause_overlay_);

	// NetworkStatusOverlay.xml: the status text on sessionOverlayBackground,
	// over the whole screen, with the disconnectedExitButton below it.
	net_status_overlay_ = memnew(Control);
	net_status_overlay_->set_anchors_preset(Control::PRESET_FULL_RECT);
	net_status_overlay_->set_mouse_filter(Control::MOUSE_FILTER_STOP);
	net_status_overlay_->set_visible(false);
	{
		ColorRect *dim = memnew(ColorRect);
		dim->set_anchors_preset(Control::PRESET_FULL_RECT);
		dim->set_color(Color(0, 0, 0, 0.6f));
		dim->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		net_status_overlay_->add_child(dim);
		net_status_label_ = memnew(Label);
		net_status_label_->set_anchors_preset(Control::PRESET_FULL_RECT);
		net_status_label_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		net_status_label_->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		net_status_label_->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		net_status_label_->add_theme_font_size_override("font_size", 18);
		net_status_label_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		net_status_overlay_->add_child(net_status_label_);
		Button *exit = memnew(Button);
		exit->set_name("DisconnectedExitButton");
		exit->set_text(tr("Exit"));
		exit->set_anchors_preset(Control::PRESET_CENTER);
		exit->set_offset(SIDE_LEFT, -84);
		exit->set_offset(SIDE_RIGHT, 84);
		exit->set_offset(SIDE_TOP, 128);
		exit->set_offset(SIDE_BOTTOM, 160);
		exit->connect("pressed", Callable(this, "_on_disconnected_exit_pressed"));
		net_status_overlay_->add_child(exit);
	}
	ui_layer->add_child(net_status_overlay_);

	// 5/6. Chat overlay + diplomacy/tribute matrix: P4.1 leaf widgets
	// (chat_window.h / diplomacy_dialog.h own the panel trees; the bound
	// handler methods below delegate into them).
	chat_window_.build(this, ui_layer, this);
	diplo_dialog_.build(this, ui_layer, this, &chat_window_);
	objectives_dialog_.build(ui_layer, this, "toggle_objectives");
	tutorial_panel_.build(ui_layer, this, "_on_tutorial_ready_pressed");
	rtso::strip_engine_popups(ui_layer);

	// =========================================================================
	// 5. AUDIO PLAYERS (Voice, SFX, Alarm, Music)
	// =========================================================================
	voice_player = memnew(AudioStreamPlayer);
	voice_player->set_volume_db(-2.0f);
	// Busses for 0 A.D.'s sound.*gain options (created by scenes/menu/user_settings.gd).
	voice_player->set_bus("Action");
	add_child(voice_player);

	alarm_player = memnew(AudioStreamPlayer);
	alarm_player->set_volume_db(-4.0f);
	alarm_player->set_bus("UI");
	sfx_player = memnew(AudioStreamPlayer);
	sfx_player->set_volume_db(-3.0f);
	sfx_player->set_bus("Action");
	add_child(sfx_player);
	add_child(alarm_player);

	// 0 A.D. music.js playlists and in-game sound events (music_manager.h,
	// session_audio.h).
	music_.build(this);
	session_audio_.build(this);
	session_audio_.set_spatial([this](const Ref<AudioStream> &s, Vector3 pos, float db) {
		if (view) view->play_spatial_sound(s, pos, db, 75.0f, 1.0f);
	});

	// gui/session/Ambient.js: one looping dayscape track ("TODO: Let the map decide the tracks").
	{
		Ref<AudioStream> amb_stream = rtso::load_audio("res://assets/audio/ambient/dayscape/day_temperate_gen_03.ogg");
		if (amb_stream.is_null()) amb_stream = rtso::load_audio("res://assets/sounds/ambient/nature_ambient.ogg");
		if (amb_stream.is_valid()) {
			amb_stream->set("loop", true);
			ambient_player = memnew(AudioStreamPlayer);
			ambient_player->set_stream(amb_stream);
			ambient_player->set_autoplay(true);
			ambient_player->set_volume_db(-12.0f);
			ambient_player->set_bus("Ambient");
			add_child(ambient_player);
			ambient_player->play();
		}
	}

	if (ResourceLoader::get_singleton()->exists("res://assets/sounds/ambient/wind_ambient.ogg")) {
		Ref<AudioStream> wind_stream = ResourceLoader::get_singleton()->load("res://assets/sounds/ambient/wind_ambient.ogg");
		if (wind_stream.is_valid()) {
			ambient_wind_player = memnew(AudioStreamPlayer);
			ambient_wind_player->set_stream(wind_stream);
			ambient_wind_player->set_autoplay(true);
			ambient_wind_player->set_volume_db(-25.0f);
			ambient_wind_player->set_bus("Ambient");
			add_child(ambient_wind_player);
			ambient_wind_player->play();
		}
	}

	if (ResourceLoader::get_singleton()->exists("res://assets/sounds/ambient/water_coast.ogg")) {
		Ref<AudioStream> water_stream = ResourceLoader::get_singleton()->load("res://assets/sounds/ambient/water_coast.ogg");
		if (water_stream.is_valid()) {
			ambient_water_player = memnew(AudioStreamPlayer);
			ambient_water_player->set_stream(water_stream);
			ambient_water_player->set_autoplay(true);
			ambient_water_player->set_volume_db(-80.0f);
			ambient_water_player->set_bus("Ambient");
			add_child(ambient_water_player);
			ambient_water_player->play();
		}
	}

	spatial_sfx_player = memnew(AudioStreamPlayer3D);
	spatial_sfx_player->set_bus("Action");
	spatial_sfx_player->set_max_distance(75.0f);
	spatial_sfx_player->set_unit_size(10.0f);
	spatial_sfx_player->set_volume_db(0.0f);
	add_child(spatial_sfx_player);

	// =========================================================================
	// 6. GHOST BUILDING & RALLY FLAG PREVIEW
	// =========================================================================
	ghost_mat_ok.instantiate();
	ghost_mat_ok->set_albedo(Color(0.2f, 0.9f, 0.2f, 0.55f));
	ghost_mat_ok->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	ghost_mat_ok->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	ghost_mat_bad.instantiate();
	ghost_mat_bad->set_albedo(Color(0.9f, 0.2f, 0.2f, 0.55f));
	ghost_mat_bad->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	ghost_mat_bad->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	ghost_mesh = memnew(MeshInstance3D);
	ghost_mesh->set_visible(false);
	add_child(ghost_mesh);
	update_ghost_mesh();

	// 3D Rally Flag Marker
	rally_flag = memnew(MeshInstance3D);
	Ref<CylinderMesh> flag_c;
	flag_c.instantiate();
	flag_c->set_top_radius(0.08f);
	flag_c->set_bottom_radius(0.08f);
	flag_c->set_height(1.6f);
	rally_flag->set_mesh(flag_c);
	Ref<StandardMaterial3D> flag_mat;
	flag_mat.instantiate();
	flag_mat->set_albedo(Color(0.2f, 0.8f, 1.0f));
	rally_flag->set_material_override(flag_mat);
	rally_flag->set_visible(false);
	add_child(rally_flag);
	range_overlays_.build(this);

	// (Unit selection now happens in apply_match_settings, after the map
	// exists - the old block here ran before any map loaded and selected
	// nothing. apply_net_game_start re-selects on GameStart via
	// load_map_seeded -> clear_selection; selection follows in _process.)
	// G3 Barter dialog wiring: connect BarterDialog.barter_requested -> _on_barter_requested
	Node *barter = get_node_or_null(NodePath("BarterDialog"));
	if (barter) {
		barter->connect("barter_requested", Callable(this, "_on_barter_requested"));
		barter->connect("trading_goods_requested", Callable(this, "_on_trading_goods_requested"));
	}

	// A replay or a saved game picked in the menus brings its own setup;
	// otherwise Game Setup's match starts and is recorded as a replay.
	if (!start_pending_replay() && !load_pending_save()) {
		apply_match_settings();
		begin_match_recording(nullptr);
		call_deferred("_start_menu_net");
	}
}

void RtsoMain::play_voice(const char *path) {
	if (!voice_player || !path) return;
	String p = path;
	String final_path = p;
	if (!final_path.begins_with("res://")) {
		if (final_path.begins_with("audio/")) {
			final_path = "res://assets/sounds/" + final_path.substr(6);
		} else if (final_path.begins_with("assets/sounds/")) {
			final_path = "res://" + final_path;
		} else {
			final_path = "res://" + final_path;
		}
	}
	if (!ResourceLoader::get_singleton()->exists(final_path)) {
		if (ResourceLoader::get_singleton()->exists(p)) {
			final_path = p;
		} else if (ResourceLoader::get_singleton()->exists("res://" + p)) {
			final_path = "res://" + p;
		}
	}
	if (ResourceLoader::get_singleton()->exists(final_path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(final_path);
		if (s.is_valid()) {
			voice_player->set_stream(s);
			voice_player->play();
		}
	}
}

void RtsoMain::play_civ_voice(const char *action) {
	if (!voice_player) return;
	static uint32_t voice_rand_idx = 0;
	++voice_rand_idx;

	std::string act(action);

	if (use_english_voice_orders_) {
		// Pure English orders voice lines
		if (act == "select") {
			const char *opts[] = {
				"res://assets/sounds/voice/select_1.ogg",
				"res://assets/sounds/voice/select_2.ogg",
				"res://assets/sounds/voice/select_3.ogg"
			};
			play_voice(opts[voice_rand_idx % 3]);
		} else if (act == "move") {
			const char *opts[] = {
				"res://assets/sounds/voice/move_1.ogg",
				"res://assets/sounds/voice/move_2.ogg"
			};
			play_voice(opts[voice_rand_idx % 2]);
		} else if (act == "attack") {
			const char *opts[] = {
				"res://assets/sounds/voice/attack_1.ogg",
				"res://assets/sounds/voice/attack_2.ogg"
			};
			play_voice(opts[voice_rand_idx % 2]);
		} else if (act == "build") {
			play_voice("res://assets/sounds/voice/build_1.ogg");
		} else if (act == "gather") {
			play_voice("res://assets/sounds/voice/gather_1.ogg");
		} else if (act == "repair") {
			play_voice("res://assets/sounds/voice/repair_1.ogg");
		}
		return;
	}

	// 0 A.D. plays the voice of the selection's first unit (its template's
	// SoundGroups), in its civ's language and phenotype.
	if (!sim) return;
	if (selected_ids.is_empty()) {
		if (selected_building_id != 0 && act == "select")
			for (const auto &b : sim->buildings())
				if (b.id == selected_building_id) {
					session_audio_.play_building_select(b.type);
					break;
				}
		return;
	}
	const uint32_t first = (uint32_t)(int64_t)selected_ids[0];
	auto uv = sim->units();
	for (size_t i = 0; i < uv.count; ++i) {
		if (uv.id[i] != first) continue;
		// A male Civilian speaks with the civ's male citizen voice (rtso::civilian_is_male).
		const uint8_t voice_type = rtso::civilian_is_male(uv.tmpl[i], uv.id[i]) ? (uint8_t)rtso::UNIT_VILLAGER : uv.unit_type[i];
		session_audio_.play_voice(sim->player_civ(uv.team[i]), voice_type, act);
		return;
	}
}

// music.js states: civ peace tracks for the local player's civ, battle while
// BattleDetection says the player is fighting, victory or defeat at the end
// (with alarm_victory / alarm_defeated).
// Music state, current track and last sound event (headless smoke tests).
// Test hook (single player): sends the local idle workers to the nearest
// resource and centres the camera there, so smoke tests can hear gathering.
void RtsoMain::debug_gather_nearest() {
	if (!sim || net_lockstep()) return;
	std::vector<uint32_t> idle = sim->get_idle_workers(local_player());
	if (idle.empty()) return;
	auto uv = sim->units();
	float x = 0.0f, z = 0.0f;
	for (size_t i = 0; i < uv.count; ++i)
		if (uv.id[i] == idle.front()) { x = uv.pos_x[i]; z = uv.pos_z[i]; break; }
	auto rv = sim->resources();
	uint32_t best = 0;
	float best_d = 1e30f;
	for (size_t r = 0; r < rv.count; ++r) {
		if (!rv.alive[r] || rv.amount[r] <= 0 || rv.subtype[r] == rtso::RES_SUB_FISH) continue;
		const float d = (rv.x[r] - x) * (rv.x[r] - x) + (rv.z[r] - z) * (rv.z[r] - z);
		if (d < best_d) { best_d = d; best = rv.id[r]; }
	}
	if (!best) return;
	if (turn_cmds()) // offline local turns keep it in the replay
		post_lockstep(4, idle.data(), idle.size(), 0, 0, best, 0);
	else
		sim->order_gather(idle.data(), idle.size(), best, false);
	if (cam_rig) cam_rig->set_target_pos(Vector3(x, 0.0f, z));
}

bool RtsoMain::trade_route_click(const rtso::Simulation::Building &b, bool execute, bool queued, String *tooltip) {
	if (!sim || selected_ids.is_empty() || b.wrecked || b.build_progress < 1.0f) return false;
	const uint8_t me = local_player();
	auto uv = sim->units();
	std::vector<uint32_t> traders;
	uint8_t kind = rtso::UNIT_TRADER;
	for (int s = 0; s < selected_ids.size(); ++s) {
		const uint32_t id = (uint32_t)(int64_t)selected_ids[s];
		for (size_t i = 0; i < uv.count; ++i) {
			if (uv.id[i] != id) continue;
			// getActionInfo: a land market for Organic traders, a dock for ships, not an enemy's.
			if (uv.alive[i] && uv.team[i] == me && rtso::unit_is_trader(uv.unit_type[i]) &&
					sim->can_trade_at(me, b, uv.unit_type[i])) {
				traders.push_back(id);
				kind = uv.unit_type[i];
			}
			break;
		}
	}
	if (traders.empty()) return false;
	std::sort(traders.begin(), traders.end());

	const rtso::Simulation::Building *origin = nullptr;
	if (trade_origin_market_ && trade_origin_traders_ == traders)
		for (const auto &o : sim->buildings())
			if (o.id == trade_origin_market_ && !o.wrecked && sim->can_trade_at(me, o, kind)) origin = &o;

	auto gain_text = [&](const rtso::Simulation::Building &from, const rtso::Simulation::Building &to) {
		const auto g = sim->calculate_trade_gain(from, to, me, kind);
		String t = String::num_int64(g.trader);
		if (g.market1 || g.market2) t += " (+" + String::num_int64(g.market1 + g.market2) + " to the markets' owners)";
		return t;
	};

	if (!origin || origin->id == b.id) {
		// "set first" / "is first" (Trader.js SetTargetMarket with no first market yet).
		if (tooltip) *tooltip = origin ? String("Origin trade market.") : String("Right-click to set as origin trade market");
		if (execute) {
			trade_origin_market_ = b.id;
			trade_origin_traders_ = traders;
			chat_window_.append_line("Origin trade market set. Right-click another market to start trading.");
			play_civ_voice("trade");
		}
		return true;
	}
	const auto g = sim->calculate_trade_gain(*origin, b, me, kind);
	if (g.trader == 0) {
		if (tooltip) *tooltip = "This market is too close to the origin market.";
		if (execute) chat_window_.append_line("This market is too close to the origin market.");
		return true;
	}
	if (tooltip) *tooltip = "Right-click to set as destination trade market.\nGain: " + gain_text(*origin, b);
	if (execute) {
		if (turn_cmds())
			post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_SETUP_TRADE_ROUTE, traders.data(), traders.size(),
					(float)b.id, 0, origin->id, queued ? rtso::NET_PARAM_QUEUED : 0);
		else
			sim->order_trade(traders.data(), traders.size(), origin->id, b.id, queued);
		chat_window_.append_line("Trade route set. Gain: " + gain_text(*origin, b));
		play_civ_voice("trade");
		trade_origin_market_ = 0;
		trade_origin_traders_.clear();
	}
	return true;
}

// Test hook (single player): two markets and two traders by the local Town
// Center, the traders selected, then the two right-clicks of setup-trade-route.
Dictionary RtsoMain::debug_trade_route_clicks() {
	Dictionary d;
	if (!sim || net_lockstep()) return d;
	const uint8_t me = local_player();
	float cx = 0.0f, cz = 0.0f;
	for (const auto &b : sim->buildings())
		if (b.team == me && rtso::is_civic_centre(b.type)) { cx = b.x; cz = b.z; break; }
	const uint32_t m1 = sim->spawn_building_world(cx - 30.0f, cz, rtso::BLD_MARKET, me, 1.0f);
	const uint32_t m2 = sim->spawn_building_world(cx + 30.0f, cz + 10.0f, rtso::BLD_MARKET, me, 1.0f);
	clear_selection();
	for (int k = 0; k < 2; ++k)
		selected_ids.push_back((int32_t)sim->spawn_unit(cx - 25.0f, cz + 4.0f * k, me, rtso::UNIT_TRADER));
	const rtso::Simulation::Building *b1 = nullptr, *b2 = nullptr;
	for (const auto &b : sim->buildings()) {
		if (b.id == m1) b1 = &b;
		if (b.id == m2) b2 = &b;
	}
	if (!b1 || !b2) return d;
	String tip;
	trade_route_click(*b1, false, false, &tip);
	d["hover_first"] = tip;
	d["first"] = trade_route_click(*b1, true, false, nullptr);
	trade_route_click(*b2, false, false, &tip);
	d["hover_second"] = tip;
	d["second"] = trade_route_click(*b2, true, false, nullptr);
	d["origin_after"] = (int)trade_origin_market_;
	return d;
}

// Test hook: step 0 spawns a spearman guarding another and selects it, step 1
// presses Remove guard, any step reports the guard state and the entity commands.
Dictionary RtsoMain::debug_guard_step(int step) {
	Dictionary d;
	if (!sim || net_lockstep()) return d;
	const uint8_t me = local_player();
	if (step == 0) {
		float cx = 0.0f, cz = 0.0f;
		for (const auto &b : sim->buildings())
			if (b.team == me && rtso::is_civic_centre(b.type)) { cx = b.x; cz = b.z; break; }
		const uint32_t a = sim->spawn_unit(cx - 20.0f, cz, me, rtso::UNIT_SPEARMAN);
		const uint32_t b = sim->spawn_unit(cx - 22.0f, cz, me, rtso::UNIT_SPEARMAN);
		sim->order_guard(&a, 1, b);
		clear_selection();
		selected_ids.push_back((int32_t)a);
	} else if (step == 1) {
		_on_remove_guard_pressed();
	}
	d["guarding"] = !selected_ids.is_empty() && sim->unit_is_guarding((uint32_t)selected_ids[0]);
	update_command_panel();
	PackedStringArray buttons;
	for (int i = 0; i < production_bar->get_child_count(); ++i)
		if (Button *btn = Object::cast_to<Button>(production_bar->get_child(i))) buttons.push_back(btn->get_text());
	d["buttons"] = buttons;
	return d;
}

// Test hook: a finished structure of `btype` next to the Civic Centre, selected;
// returns the units it offers and the production buttons it shows.
PackedStringArray RtsoMain::debug_train_buttons(int btype) {
	PackedStringArray out;
	if (!sim || net_lockstep()) return out;
	const uint8_t me = local_player();
	float cx = 0.0f, cz = 0.0f;
	for (const auto &b : sim->buildings())
		if (b.team == me && rtso::is_civic_centre(b.type)) { cx = b.x; cz = b.z; break; }
	const uint32_t id = sim->spawn_building_world(cx + 24.0f, cz + 24.0f, (uint8_t)btype, me, 1.0f);
	clear_selection();
	selected_building_id = id;
	update_command_panel();
	// The units it trains (the civ's roster list), then its other buttons.
	if (roster_list && roster_list->is_visible())
		for (int i = 0; i < roster_list->get_item_count(); ++i) out.push_back(roster_list->get_item_text(i));
	for (int i = 0; i < production_bar->get_child_count(); ++i)
		if (Button *btn = Object::cast_to<Button>(production_bar->get_child(i))) out.push_back(btn->get_text());
	return out;
}

// Test hook: selects the local player's first unit that can build (step one
// call) and reports the command button whose text starts with `prefix`: its
// instance id, global rect and whether build placement is active.
Dictionary RtsoMain::debug_command_button(const String &prefix) {
	Dictionary d;
	if (!sim) return d;
	if (selected_ids.is_empty()) {
		auto uview = sim->units();
		for (size_t i = 0; i < uview.count; ++i) {
			if (!uview.alive[i] || uview.team[i] != local_player()) continue;
			if (uview.unit_type[i] != rtso::UNIT_FEMALE_CITIZEN && uview.unit_type[i] != rtso::UNIT_VILLAGER) continue;
			clear_selection();
			selected_ids.push_back((int32_t)uview.id[i]);
			break;
		}
	}
	d["build_mode"] = build_mode;
	for (int i = 0; i < production_bar->get_child_count(); ++i) {
		Button *btn = Object::cast_to<Button>(production_bar->get_child(i));
		if (!btn) continue;
		const String label = btn->has_meta("label") ? String(btn->get_meta("label")) : btn->get_text();
		if (!label.begins_with(prefix)) continue;
		d["id"] = (int64_t)btn->get_instance_id();
		d["rect"] = btn->get_global_rect();
		d["disabled"] = btn->is_disabled();
		break;
	}
	return d;
}

// Test hook: clears the selection and returns the screen point of the local
// Civic Centre's roof (its centre, a little above the ground) and its id, plus
// what is selected now.
Dictionary RtsoMain::debug_civic_centre_roof() {
	Dictionary d;
	d["selected_building"] = (int64_t)selected_building_id;
	if (!sim || !cam_rig || !cam_rig->get_camera()) return d;
	for (const auto &b : sim->buildings()) {
		if (b.team != local_player() || !rtso::is_civic_centre(b.type)) continue;
		const auto model = rtso::ModelLibrary::get("buildings", String(rtso::civ_building_model(sim->team_civ(b.team), b.type)));
		const float height = model.valid ? model.y_offset + model.aabb.size.y * (model.scale > 0.05f ? model.scale : 1.0f) : 3.0f;
		const Vector3 roof(b.x, sim->get_terrain_height(b.x, b.z) + height * 0.8f, b.z);
		d["height"] = height;

		cam_rig->set_target_pos(Vector3(b.x, 0.0f, b.z));
		d["id"] = (int64_t)b.id;
		d["roof"] = cam_rig->get_camera()->unproject_position(roof);
		d["ground_under_roof"] = get_ground_click_pos(cam_rig->get_camera()->unproject_position(roof));
		d["centre"] = Vector2(b.x, b.z);
		d["base_radius"] = b.half_size + 0.6f; // the ground test round the base
		break;
	}
	return d;
}

// Test hook: with select_all, selects every unit the local player owns; reports
// the selection size and the selection panel's group icons (global rects).
Dictionary RtsoMain::debug_selection_grid(bool select_all) {
	Dictionary d;
	if (!sim) return d;
	if (select_all) {
		clear_selection();
		auto uview = sim->units();
		for (size_t i = 0; i < uview.count; ++i)
			if (uview.alive[i] && uview.team[i] == local_player()) selected_ids.push_back((int32_t)uview.id[i]);
	}
	d["selected"] = selected_ids.size();
	Array icons;
	if (GridContainer *g = selection_grid_.grid()) {
		for (int i = 0; i < g->get_child_count(); ++i)
			if (Button *b = Object::cast_to<Button>(g->get_child(i))) {
				Dictionary e;
				e["rect"] = b->get_global_rect();
				e["tip"] = b->get_tooltip_text();
				e["id"] = (int64_t)b->get_instance_id();
				e["visible"] = b->is_visible_in_tree();
				icons.push_back(e);
			}
	}
	d["icons"] = icons;
	return d;
}

// Test hook: the selection, the selected structure's rally point, and every own
// unit with its template, rank, world position and screen position.
Dictionary RtsoMain::debug_selection_state() {
	Dictionary d;
	if (!sim) return d;
	d["selected"] = selected_ids;
	d["building"] = (int64_t)selected_building_id;
	for (const auto &b : sim->buildings())
		if (b.id == selected_building_id && b.has_rally_point) d["rally"] = Vector2(b.rally_x, b.rally_z);
	Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
	Array units;
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (!uview.alive[i] || uview.team[i] != local_player()) continue;
		Dictionary u;
		u["id"] = (int64_t)uview.id[i];
		u["tmpl"] = (int64_t)uview.tmpl[i];
		u["rank"] = (int64_t)uview.rank[i];
		u["pos"] = Vector2(uview.pos_x[i], uview.pos_z[i]);
		if (cam) {
			const Vector3 wp(uview.pos_x[i], sim->get_terrain_height(uview.pos_x[i], uview.pos_z[i]), uview.pos_z[i]);
			u["behind"] = cam->is_position_behind(wp);
			u["screen"] = cam->unproject_position(wp);
		}
		units.push_back(u);
	}
	d["units"] = units;
	d["viewport"] = get_viewport()->get_visible_rect().size;
	return d;
}

// Test hook: the command panel's buttons after a refresh (text, tooltip,
// disabled, instance id).
Array RtsoMain::debug_command_row() {
	Array out;
	update_command_panel();
	for (int i = 0; i < production_bar->get_child_count(); ++i)
		if (Button *btn = Object::cast_to<Button>(production_bar->get_child(i))) {
			Dictionary e;
			e["text"] = btn->has_meta("label") ? String(btn->get_meta("label")) : btn->get_text();
			e["tip"] = btn->get_tooltip_text();
			e["disabled"] = btn->is_disabled();
			e["id"] = (int64_t)btn->get_instance_id();
			out.push_back(e);
		}
	return out;
}

// Test hook: destroys the units (Delete's command) without touching the selection,
// as deaths in battle do.
void RtsoMain::debug_delete_units(const PackedInt32Array &ids) {
	if (!sim || ids.is_empty()) return;
	std::vector<uint32_t> v;
	for (int i = 0; i < ids.size(); ++i) v.push_back((uint32_t)ids[i]);
	sim->cmd_delete(local_player(), v.data(), v.size());
}

// Test hook: which session dialogs are open, the pause overlay's state, and
// the chat log; `chat` in the input line is sent first when given.
Dictionary RtsoMain::debug_dialogs() {
	Dictionary d;
	d["chat"] = chat_window_.is_open();
	d["diplomacy"] = diplo_dialog_.is_open();
	d["objectives"] = objectives_dialog_.is_open();
	d["match_settings_text"] = objectives_dialog_.text();
	d["match_settings_state"] = objectives_dialog_.player_state();
	Node *barter = get_node_or_null(NodePath("BarterDialog"));
	d["barter"] = barter ? bool(barter->call("is_open")) : false;
	d["overlay"] = pause_overlay_ && pause_overlay_->is_visible();
	d["overlay_resume"] = pause_resume_label_ && pause_resume_label_->is_visible();
	d["chat_log"] = chat_window_.log_text();
	return d;
}

// Test hook: step 0 places a barracks with a rally point and selects it; step 1
// does what a right-click on the barracks itself does; every step reports the
// rally point and whether the barracks centre / a far spot count as "itself".
Dictionary RtsoMain::debug_unset_rally_step(int step) {
	Dictionary d;
	if (!sim || net_lockstep()) return d;
	const uint8_t me = local_player();
	if (step == 0) {
		float cx = 0.0f, cz = 0.0f;
		for (const auto &b : sim->buildings())
			if (b.team == me && b.type == rtso::BLD_TOWN_CENTER) { cx = b.x; cz = b.z; break; }
		const uint32_t id = sim->spawn_building_world(cx + 24.0f, cz - 24.0f, rtso::BLD_BARRACKS, me, 1.0f);
		sim->cmd_set_rally_point(me, id, cx + 40.0f, cz - 24.0f, 0, 0, false);
		clear_selection();
		selected_building_id = id;
	}
	for (const auto &b : sim->buildings()) {
		if (b.id != selected_building_id) continue;
		if (step == 1 && over_own_rally_structure(Vector3(b.x, 0, b.z)))
			post_lockstep(14, nullptr, 0, 0, 0, selected_building_id, 1);
		d["has_rally"] = b.has_rally_point;
		d["over_self"] = over_own_rally_structure(Vector3(b.x + b.half_size * 0.5f, 0, b.z));
		d["over_far"] = over_own_rally_structure(Vector3(b.x + 30.0f, 0, b.z));
	}
	return d;
}

Dictionary RtsoMain::get_audio_state() const {
	static const char *kStates[] = {"off", "menu", "peace", "battle", "victory", "defeat"};
	Dictionary d;
	d["music_state"] = kStates[music_.state()];
	d["track"] = music_.current_track();
	d["last_sound"] = session_audio_.last_played();
	d["in_battle"] = session_audio_.in_battle();
	d["work_sounds"] = session_audio_.work_sounds_played();
	d["workers_at_work"] = session_audio_.workers_at_work();
	d["workers_in_earshot"] = session_audio_.workers_in_earshot();
	d["listener"] = session_audio_.listener();
	return d;
}

void RtsoMain::update_music_state() {
	if (!sim) return;
	const uint8_t me = local_player();
	const uint8_t civ = sim->player_civ(me);
	if (civ != music_civ_) {
		music_civ_ = civ;
		music_.store_civ_tracks(civ);
		if (music_.state() != rtso::RtsoMusic::OFF) {
			const auto keep = music_.state();
			music_.set_state(rtso::RtsoMusic::OFF);
			music_.set_state(keep);
		}
	}
	rtso::RtsoMusic::State want = session_audio_.in_battle() ? rtso::RtsoMusic::BATTLE : rtso::RtsoMusic::PEACE;
	if (sim->is_victory_for(me)) want = rtso::RtsoMusic::VICTORY;
	else if (sim->is_victory() || sim->is_game_over() || sim->is_defeated(me)) want = rtso::RtsoMusic::DEFEAT;
	const auto was = music_.state();
	if (want == was) return;
	if (want == rtso::RtsoMusic::VICTORY) session_audio_.play_victory();
	else if (want == rtso::RtsoMusic::DEFEAT) session_audio_.play_defeat();
	music_.set_state(want);
}

void RtsoMain::play_spatial_sfx(const char *path, Vector3 pos) {
	if (view) {
		view->play_spatial_sound_path(path, pos);
		return;
	}
	if (!spatial_sfx_player) return;
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(path);
		if (s.is_valid()) {
			spatial_sfx_player->set_position(pos);
			spatial_sfx_player->set_stream(s);
			spatial_sfx_player->play();
		}
	}
}

void RtsoMain::play_sfx(const char *path) {
	if (!sfx_player) return;
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(path);
		if (s.is_valid()) {
			sfx_player->set_stream(s);
			sfx_player->play();
		}
	}
}

void RtsoMain::play_alarm(const char *path) {
	if (!alarm_player) return;
	if (ResourceLoader::get_singleton()->exists(path)) {
		Ref<AudioStream> s = ResourceLoader::get_singleton()->load(path);
		if (s.is_valid()) {
			alarm_player->set_stream(s);
			alarm_player->play();
		}
	}
}

// Tactical flare: broadcast to session peers over NMT_CHAT (approximation of
// 0 A.D.'s dedicated flare message; receive side parses "[Flare] x,z" in the
// onChat callback registered in _ready()). Local ping already added by caller.
void RtsoMain::_on_flare_requested(Vector2 world_pos) {
	if (!net_lockstep()) return;
	std::string wire = "[Flare] " + std::to_string(world_pos.x) + "," + std::to_string(world_pos.y);
	net_host_.sendChat(wire);
}

// 0 A.D. gui/session/Cheats.js executeCheat with the codes of simulation/data/cheats/*.json.
// Player numbers typed after "wololo" and "exodia" are 0 A.D.'s (0 = Gaia, 1 = the first player).
bool RtsoMain::chat_try_cheat(const String &text) {
	using S = rtso::Simulation;
	struct CheatCode { const char *name; uint8_t action; int resource; const char *def; bool numeric; };
	static const CheatCode kCheats[] = {
		{"i want pizza", S::CHEAT_ADD_RESOURCE, rtso::RES_FOOD, "1000", true},
		{"bring me my axe", S::CHEAT_ADD_RESOURCE, rtso::RES_WOOD, "1000", true},
		{"i see a mountain here", S::CHEAT_ADD_RESOURCE, rtso::RES_STONE, "1000", true},
		{"your money or your life", S::CHEAT_ADD_RESOURCE, rtso::RES_GOLD, "1000", true},
		{"jame jam", S::CHEAT_REVEAL_MAP, 0, "", false},
		{"wololo", S::CHEAT_CONVERT_UNITS, 0, "-1", true},
		{"black death", S::CHEAT_KILL_UNITS, 0, "", false},
		{"exodia", S::CHEAT_DEFEAT_PLAYER, 0, "2", true},
		{"salad bowl", S::CHEAT_CREATE_UNITS, 0, "13", true},
		{"back to the future", S::CHEAT_CHANGE_PHASE, 0, "", false},
		{"brainiac", S::CHEAT_RESEARCH, 0, "", false},
		{"gift from the gods", S::CHEAT_META, 0, "100000", true},
		{"the hive master", S::CHEAT_MAX_POPULATION, 0, "", false},
		{"TARDIS", S::CHEAT_CHANGE_MAX_POPULATION, 0, "", false},
		{"i am too busy", S::CHEAT_FAST_ACTIONS, 0, "", false},
	};
	const uint8_t me = local_player();
	if (!sim || !sim->cheats_enabled() || me >= rtso::MAX_PLAYERS || sim->is_defeated(me)) return false;
	const CheatCode *cheat = nullptr;
	for (const CheatCode &c : kCheats)
		if (text.begins_with(c.name)) { cheat = &c; break; }
	if (!cheat) return false;
	String parameter = text.substr(String(cheat->name).length() + 1).strip_edges();
	if (parameter.is_empty()) parameter = cheat->def;
	if (cheat->numeric && !parameter.is_empty() && !parameter.is_valid_int()) {
		chat_window_.append_line("[color=#eebb22]This cheat requires a numeric parameter.[/color]\n");
		return false;
	}
	int64_t number = cheat->numeric && !parameter.is_empty() ? parameter.to_int() : 0;
	uint32_t extra = (uint32_t)std::max(cheat->resource, 0);
	if (cheat->action == S::CHEAT_CONVERT_UNITS || cheat->action == S::CHEAT_DEFEAT_PLAYER)
		number = number < 0 ? -1 : (number == 0 ? rtso::GAIA_PLAYER : number - 1);
	// The selection, a selected structure first (createunits and brainiac read selected[0]).
	std::vector<uint32_t> ids;
	if (selected_building_id) ids.push_back(selected_building_id);
	for (int i = 0; i < selected_ids.size(); ++i) ids.push_back((uint32_t)selected_ids[i]);
	if (cheat->action == S::CHEAT_RESEARCH) {
		// "brainiac N" researches the selected structure's Nth technology, else a tech by its 0 A.D. name.
		extra = rtso::TECH_NONE;
		int btype = -1; // the selected structure's type
		for (const auto &bb : sim->buildings())
			if (selected_building_id && bb.id == selected_building_id) btype = bb.type;
		if (btype >= 0 && parameter.is_valid_int()) {
			int nth = (int)parameter.to_int();
			for (int t = 1; t < rtso::TECH_COUNT && nth > 0; ++t) {
				const rtso::TechInfo &ti = rtso::tech_info((uint8_t)t);
				if (ti.researcher != btype || !(ti.civ_mask & rtso::civ_bit(sim->player_civ(me)))) continue;
				if (--nth == 0) extra = (uint32_t)t;
			}
		} else {
			const std::string want = parameter.utf8().get_data();
			for (int t = 1; t < rtso::TECH_COUNT; ++t)
				if (want == rtso::tech_info((uint8_t)t).template_name) extra = (uint32_t)t;
		}
	}
	if (cheat->action == S::CHEAT_CREATE_UNITS && !selected_building_id) {
		chat_window_.append_line("[color=#eebb22]You need to select a building that trains units.[/color]\n");
		return true;
	}
	if (turn_cmds()) post_lockstep(39, ids.data(), ids.size(), (float)extra, 0.0f, (uint32_t)(int32_t)number, cheat->action);
	else sim->cmd_cheat(me, cheat->action, (int32_t)number, extra, ids.data(), ids.size());
	return true;
}

void RtsoMain::chat_send_wire(const std::string &wire) {
	// 0 A.D. gui/session/chat/ChatCommands.js: the host removes a player by name.
	for (const char *cmd : {"/kick ", "/ban "}) {
		const std::string prefix = cmd;
		if (wire.compare(0, prefix.size(), prefix) != 0) continue;
		const std::string name = wire.substr(prefix.size());
		const bool ban = prefix == "/ban ";
		String msg;
		if (!net_host_.isHost()) msg = "Only the host can kick or ban players";
		else if (!net_host_.kickPeer(net_host_.peerByName(name), ban)) msg = "No player named " + String::utf8(name.c_str());
		if (!msg.is_empty() && lobby_status) lobby_status->set_text(msg);
		return;
	}
	net_host_.sendChat(wire);
}

void RtsoMain::_on_minimap_clicked(Vector2 world_pos) {
	if (rtso::hotkeys::held("session.flare")) { // MiniMap.js onWorldClick: the flare hotkey wins
		trigger_flare(world_pos.x, world_pos.y);
		return;
	}
	if (cam_rig) {
		cam_rig->set_target_pos(Vector3(world_pos.x, 0.0f, world_pos.y));
	}
}

// input.js triggerFlareAction: one flare per g_FlareCooldown (3 s), shown in the
// world and on the minimap and sent to the other players.
void RtsoMain::trigger_flare(float x, float z) {
	const double now = Time::get_singleton()->get_ticks_msec() / 1000.0;
	if (now < last_flare_time_ + 3.0) return;
	last_flare_time_ = now;
	range_overlays_.add_marker(x, z, Color(1.0f, 0.45f, 0.1f), user_opts_.flare_time);
	if (minimap_view) minimap_view->add_ping(Vector2(x, z), Color(1.0f, 0.4f, 0.2f));
	_on_flare_requested(Vector2(x, z));
	play_alarm("res://assets/audio/interface/alarm/alarmally_1.ogg");
}

// MiniMap.js onWorldClick, right button: determineAction with no entity under the
// cursor, so a structure sets its rally point and units walk there (attack-walk
// with session.attackmove), queued with session.queue.
void RtsoMain::_on_minimap_order(Vector2 world_pos) {
	if (!sim) return;
	if (rtso::hotkeys::held("session.flare")) {
		trigger_flare(world_pos.x, world_pos.y);
		return;
	}
	const bool queued = rtso::hotkeys::held("session.queue");
	const uint8_t qbit = queued ? rtso::NET_PARAM_QUEUED : 0;
	if (selected_building_id != 0) {
		if (turn_cmds()) {
			uint32_t extra[2] = {0, 0};
			post_lockstep(14, extra, 2, world_pos.x, world_pos.y, selected_building_id, qbit);
		} else {
			sim->cmd_set_rally_point(local_player(), selected_building_id, world_pos.x, world_pos.y, 0, 0, queued);
		}
		return;
	}
	if (selected_ids.is_empty()) return;
	const uint32_t *sel_ptr = (const uint32_t *)selected_ids.ptr();
	const size_t sel_n = selected_ids.size();
	range_overlays_.add_marker(world_pos.x, world_pos.y, Color(1, 1, 1, 0.9f), RtsoRangeOverlays::kMoveMarkerTime);
	if (rtso::hotkeys::held("session.attackmove")) {
		if (turn_cmds())
			post_lockstep(10, sel_ptr, sel_n, world_pos.x, world_pos.y, 0, qbit);
		else
			sim->order_attack_move(sel_ptr, sel_n, world_pos.x, world_pos.y, queued, false, true);
		play_civ_voice("attack");
		return;
	}
	if (turn_cmds())
		post_lockstep(1, sel_ptr, sel_n, world_pos.x, world_pos.y, 0, qbit);
	else
		sim->order_walk(sel_ptr, sel_n, world_pos.x, world_pos.y, queued, true);
	play_civ_voice("move");
}

void RtsoMain::_on_idle_worker_clicked() {
	cycle_idle_worker();
}

void RtsoMain::cycle_idle_worker() {
	find_idle_unit(IDLE_WORKER);
}

// 0 A.D. findIdleUnit (gui/session/input.js:1659) with the class lists from
// session.js g_WorkerTypes / g_MilitaryTypes and hotkeys/misc.xml: selects the
// next idle unit of the kind after the last one found and centres the camera
// on it. selection.add (Shift) appends; selection.offscreen (Alt) selects every
// idle unit of the kind at once.
void RtsoMain::find_idle_unit(IdleKind kind) {
	if (!sim) return;
	uint64_t want = 0;
	switch (kind) {
		case IDLE_WORKER: want = rtso::TC_FEMALECITIZEN | rtso::TC_TRADER | rtso::TC_FISHINGBOAT | rtso::TC_CITIZEN; break;
		case IDLE_WARRIOR: want = rtso::TC_MELEE | rtso::TC_RANGED; break;
		case IDLE_BUILDER: want = rtso::TC_WORKER | rtso::TC_CITIZEN; break;
		case IDLE_ANY: want = ~0ull; break;
	}
	const bool append = rtso::hotkeys::held("selection.add");
	const bool select_all = rtso::hotkeys::held("selection.offscreen");
	if (select_all || kind != last_idle_kind_) last_idle_unit_ = 0;
	last_idle_kind_ = kind;

	auto uview = sim->units();
	std::vector<size_t> idle;
	for (size_t i = 0; i < uview.count; ++i) {
		if (!uview.alive[i] || uview.team[i] != local_player() || uview.action_state[i] != rtso::ACTION_IDLE) continue;
		if (!(rtso::unit_tech_classes(uview.unit_type[i]) & want)) continue;
		if (append && selected_ids.has((int32_t)uview.id[i])) continue;
		idle.push_back(i);
	}
	if (idle.empty()) {
		last_idle_unit_ = 0;
		session_audio_.play_no_idle_unit();
		return;
	}
	if (!append) {
		selected_ids.clear();
		selected_building_id = 0;
	}
	if (select_all) {
		for (size_t i : idle) selected_ids.push_back((int32_t)uview.id[i]);
		play_civ_voice("select");
		return;
	}
	// The idle unit with the next id after the previous pick, wrapping around.
	size_t pick = idle.front(), first = idle.front();
	bool found = false;
	for (size_t i : idle) {
		if (uview.id[i] < uview.id[first]) first = i;
		if (uview.id[i] > last_idle_unit_ && (!found || uview.id[i] < uview.id[pick])) {
			pick = i;
			found = true;
		}
	}
	if (!found) pick = first;
	last_idle_unit_ = uview.id[pick];
	selected_ids.push_back((int32_t)uview.id[pick]);
	if (cam_rig) cam_rig->set_target_pos(Vector3(uview.pos_x[pick], 0, uview.pos_z[pick]));
	play_civ_voice("select");
}

// Under-attack alerts (0 A.D. AttackDetection.js AttackAlert, called for every
// Attacked message): a hit on one of the local player's entities by another
// player runs 0 A.D.'s suppression rules (attack_detection.h) and may notify.
void RtsoMain::detect_attacks(const std::vector<rtso::Simulation::AttackedEvent> &attacks) {
	if (!sim) return;
	const uint8_t me = local_player();
	if (me != attack_watch_player_) {
		// New match seat (load, net start): forget suppressed attacks.
		attack_watch_player_ = me;
		attack_detection_.clear();
	}
	for (const auto &a : attacks) {
		// Attacks against other players, or dealt by myself, are not registered.
		if (a.target_owner != me || a.attacker_owner == me) continue;
		if (attack_detection_.report(a.x, a.z, match_elapsed_time))
			notify_attack(Vector3(a.x, 0, a.z), a.attacker_owner, a.target_is_building != 0);
	}
}

// 0 A.D. messages.js "attack" notification: remember the spot for
// camera.lastattackfocus, ping the minimap, play the "attacked" sound group
// (alarm_attackplayer, or alarm_attacked_gaia for Gaia) and add a chat line.
void RtsoMain::notify_attack(Vector3 pos, uint8_t attacker, bool structure) {
	has_last_attack_ = true;
	last_attack_pos_ = pos;
	if (minimap_view) minimap_view->add_ping(Vector2(pos.x, pos.z), Color(1.0f, 0.15f, 0.1f));
	const bool gaia = attacker == rtso::GAIA_PLAYER;
	session_audio_.play_group("interface/alarm", gaia ? "alarm_attacked_gaia" : "alarmattackplayer", 0.25f);
	if (!chat_window_.is_ready() || !user_opts_.attack_notice) return;
	String who = gaia ? String("Gaia")
			: player_display_name(attacker) + " (" + String(rtso::civ_info(sim->player_civ(attacker)).name) + ")";
	(void)structure; // 0 A.D. words unit and structure attacks the same way
	const String what = "You have been attacked";
	const String key = rtso::hotkeys::table().label("camera.lastattackfocus").c_str();
	chat_window_.append_line("[color=#ff6644]" + what + " by " + who + "![/color]" +
			(key.is_empty() ? String() : " [color=#aaaaaa](" + key + " to view)[/color]") + "\n");
}

// colorizePlayernameByID: the name in the player's displayed colour.
String RtsoMain::colored_player_name(uint8_t pid) const {
	const uint32_t rgb = sim ? rtso::display_colors::rgb(*sim, pid) : 0xFFFFFFu;
	return "[color=#" + String::num_int64(rgb, 16).lpad(6, "0") + "]" + player_display_name(pid) + "[/color]";
}

// DiplomacyColors.toggle (session.diplomacycolors, the dialog and minimap buttons).
void RtsoMain::toggle_diplomacy_colors() {
	rtso::display_colors::toggle();
	if (minimap_view) minimap_view->queue_redraw();
	if (diplo_dialog_.is_open()) diplo_dialog_.refresh();
}

// 0 A.D. ChatMessageFormatSimulation: who sees which tribute, barter, phase and
// diplomacy line. Observers (and defeated players) see more, as g_IsObserver does.
void RtsoMain::show_session_notices(const std::vector<rtso::Simulation::SessionNotice> &notices) {
	if (notices.empty() || !sim || !chat_window_.is_ready() || turn_mgr_.is_replaying()) return;
	using N = rtso::Simulation::SessionNotice;
	const uint8_t me = local_player();
	const bool observer = me >= rtso::MAX_PLAYERS || sim->is_defeated(me);
	auto mutual_ally = [&](uint8_t a, uint8_t b) {
		return a < rtso::MAX_PLAYERS && b < rtso::MAX_PLAYERS && sim->get_diplomacy(a, b) == rtso::Simulation::DIPLO_ALLY &&
				sim->get_diplomacy(b, a) == rtso::Simulation::DIPLO_ALLY;
	};
	// getLocalizedResourceAmounts: "100 food, 50 wood and 200 metal".
	auto amounts_text = [](const int32_t *amounts, const uint8_t *types, int n) {
		static const char *kNames[rtso::RES_COUNT] = {"wood", "metal", "stone", "food"}; // RES_* order
		String parts[rtso::RES_COUNT];
		int count = 0;
		for (int i = 0; i < n; ++i)
			if (amounts[i] > 0 && types[i] < rtso::RES_COUNT)
				parts[count++] = String::num_int64(amounts[i]) + " " + kNames[types[i]];
		String out;
		for (int i = 0; i < count; ++i)
			out += (i == 0 ? "" : i == count - 1 ? " and " : ", ") + parts[i];
		return out;
	};
	for (const N &n : notices) {
		String line;
		if (n.kind == N::TRIBUTE) {
			// GetTributableCodes order: food, wood, stone, metal.
			static const uint8_t kOrder[4] = {rtso::RES_FOOD, rtso::RES_WOOD, rtso::RES_STONE, rtso::RES_GOLD};
			int32_t a[4];
			for (int i = 0; i < 4; ++i) a[i] = n.amounts[kOrder[i]];
			const String what = amounts_text(a, kOrder, 4);
			if (n.other == me) line = colored_player_name(n.player) + " has sent you " + what + ".";
			else if (n.player == me) line = "You have sent " + colored_player_name(n.other) + " " + what + ".";
			else if (user_opts_.tribute_notice &&
					(observer || (sim->teams_locked() && mutual_ally(n.player, me) && mutual_ally(n.other, me))))
				line = colored_player_name(n.player) + " has sent " + colored_player_name(n.other) + " " + what + ".";
		} else if (n.kind == N::BARTER) {
			if (observer && user_opts_.barter_notice) {
				const String given = amounts_text(&n.amounts[0], &n.other, 1);
				const String gained = amounts_text(&n.amounts[1], &n.tech, 1);
				line = colored_player_name(n.player) + " bartered " + given + " for " + gained + ".";
			}
		} else if (n.kind == N::PHASE_STARTED || n.kind == N::PHASE_ABORTED || n.kind == N::PHASE_COMPLETED) {
			if (user_opts_.phase_notice == 0 || (n.player != me && !observer && !mutual_ally(n.player, me))) continue;
			const String phase = String(rtso::tech_info(n.tech).name);
			if (n.kind == N::PHASE_COMPLETED) line = colored_player_name(n.player) + " has reached the " + phase + ".";
			else if (user_opts_.phase_notice == 2 && n.kind == N::PHASE_STARTED)
				line = colored_player_name(n.player) + " is advancing to the " + phase + ".";
			else if (user_opts_.phase_notice == 2)
				line = "The " + phase + " of " + colored_player_name(n.player) + " has been aborted.";
		} else if (n.kind == N::DIPLOMACY) {
			static const char *kWord[3] = {"allied with", "neutral with", "at war with"};
			const char *word = kWord[n.tech <= rtso::Simulation::DIPLO_ENEMY ? n.tech : 2];
			if (observer) line = colored_player_name(n.player) + " is now " + word + " " + colored_player_name(n.other) + ".";
			else if (n.player == me) line = "You are now " + String(word) + " " + colored_player_name(n.other) + ".";
			else if (n.other == me) line = colored_player_name(n.player) + " is now " + word + " you.";
		}
		if (!line.is_empty()) chat_window_.append_line(line + "\n");
	}
}

// Context cursors (0 A.D. gui/session/unit_actions.js "cursor" of each action
// and input.js updateCursorAndTooltip, art/textures/cursors/action-*.png): the
// cursor shows what a right-click would do with the current selection.
// unit_actions.js "unset-rallypoint" getActionInfo: the target is the selected
// structure itself and it has a rally point.
// unit_actions.js: right-clicking an enemy with a structure that shoots arrows selected
// sends Commands.js "focus-fire" (BuildingAI.AddFocusTarget; Shift queues it) instead of
// setting the rally point there. Lockstep command 50.
bool RtsoMain::try_focus_fire(const Vector3 &at) {
	const uint8_t me = local_player();
	const auto bit = std::find_if(sim->buildings().begin(), sim->buildings().end(),
			[&](const rtso::Simulation::Building &b) { return b.id == selected_building_id; });
	if (bit == sim->buildings().end() || bit->team != me || !rtso::building_ai_info(bit->type).shoots()) return false;
	uint32_t target = 0;
	auto uview = sim->units();
	float best = 2.2f * 2.2f;
	for (size_t i = 0; i < uview.count; ++i) {
		if (!uview.alive[i] || !sim->is_hostile(me, uview.team[i])) continue;
		const float dx = uview.pos_x[i] - at.x, dz = uview.pos_z[i] - at.z, d2 = dx * dx + dz * dz;
		if (d2 < best) { best = d2; target = uview.id[i]; }
	}
	for (const auto &b : sim->buildings()) {
		if (target) break;
		if (b.wrecked || b.team >= rtso::MAX_PLAYERS || !sim->is_hostile(me, b.team)) continue;
		const float dx = b.x - at.x, dz = b.z - at.z, hit_r = b.half_size + 1.2f;
		if (dx * dx + dz * dz <= hit_r * hit_r) target = b.id;
	}
	if (!target) return false;
	const bool queued = rtso::hotkeys::held("session.queue");
	const uint32_t ids[1] = {selected_building_id};
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_FOCUS_FIRE, ids, 1, 0, 0, target, queued ? rtso::NET_PARAM_QUEUED : 0);
	else if (!sim->cmd_focus_fire(me, ids, 1, target, queued))
		return false;
	return true;
}

bool RtsoMain::over_own_rally_structure(const Vector3 &at) const {
	if (!sim || selected_building_id == 0 || !selected_ids.is_empty()) return false;
	for (const auto &b : sim->buildings()) {
		if (b.id != selected_building_id) continue;
		if (b.team != local_player() || !b.has_rally_point) return false;
		// Inside the turned footprint (u axis = (ux, uz), half extents hu, hv).
		const float dx = at.x - b.x, dz = at.z - b.z;
		const float u = dx * b.ux + dz * b.uz, v = -dx * b.uz + dz * b.ux;
		const float hu = b.hu > 0.0f ? b.hu : b.half_size, hv = b.hv > 0.0f ? b.hv : b.half_size;
		return std::fabs(u) <= hu && std::fabs(v) <= hv;
	}
	return false;
}

void RtsoMain::update_cursor() {
	Viewport *vp = get_viewport();
	if (!sim || !vp) return;
	const char *want = "";
	String tip; // the action's tooltip, shown beside the cursor (0 A.D. cursor tooltips)
	Vector2 hot(1, 1);
	const Vector2 mouse = vp->get_mouse_position();
	const bool over_ui = vp->gui_get_hovered_control() != nullptr;
	if (!over_ui && !build_mode) {
		if (rtso::hotkeys::held("session.flare")) {
			want = "action-flare";
		} else if (selected_building_id != 0 && selected_ids.is_empty()) {
			if (over_own_rally_structure(get_ground_click_pos(mouse))) {
				want = "action-unset-rally";
			} else {
				want = "cursor-rally";
				hot = Vector2(5, 31);
			}
		} else if (!selected_ids.is_empty()) {
			const Vector3 at = get_ground_click_pos(mouse);
			const uint8_t me = local_player();
			auto near2 = [&](float x, float z, float r) { return (x - at.x) * (x - at.x) + (z - at.z) * (z - at.z) <= r * r; };
			if (attack_move_mode || call_to_arms_mode || rtso::hotkeys::held("session.attackmove") ||
					rtso::hotkeys::held("session.calltoarms")) want = "action-attack-move";
			else if (patrol_mode || rtso::hotkeys::held("session.patrol")) want = "action-patrol";
			else if (guard_mode || rtso::hotkeys::held("session.guard")) want = "action-guard";
			else if (garrison_mode) want = "action-garrison";
			else if (repair_mode) want = "action-repair";
			if (!*want) {
				auto uv = sim->units();
				bool healer = false, builder = false;
				for (int s = 0; s < selected_ids.size() && !(healer && builder); ++s)
					for (size_t i = 0; i < uv.count; ++i)
						if (uv.id[i] == (uint32_t)(int64_t)selected_ids[s]) {
							const uint16_t tm = uv.tmpl ? uv.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE;
							healer = healer || rtso::unit_is_healer(tm, uv.unit_type[i]);
							builder = builder || rtso::unit_can_build(uv.unit_type[i], tm);
							break;
						}
				for (size_t i = 0; i < uv.count && !*want; ++i) {
					if (!uv.alive[i] || !near2(uv.pos_x[i], uv.pos_z[i], 2.2f)) continue;
					if (sim->is_hostile(me, uv.team[i])) want = "action-attack";
					// unit_actions "repair": a builder over a damaged own or allied siege engine or ship.
					else if (builder && uv.hp[i] < uv.max_hp[i] && (uv.team[i] == me || sim->is_ally(me, uv.team[i])) &&
							sim->is_repairable_unit(uv.id[i]))
						want = "action-repair";
					// 0 A.D. unit_actions "heal": a healer over a wounded own or allied unit.
					else if (healer && uv.hp[i] < uv.max_hp[i] && (uv.team[i] == me || sim->is_ally(me, uv.team[i])))
						want = "action-heal";
				}
			}
			if (!*want) {
				for (const auto &t : sim->treasures())
					if (near2(t.x, t.z, 2.0f)) { want = "action-collect-treasure"; break; }
			}
			if (!*want) {
				auto fv = sim->fauna();
				for (size_t f = 0; f < fv.count && !*want; ++f) {
					// A penned animal is garrisoned in its corral: nothing to do with it.
					if (!fv.alive[f] || fv.behavior[f] == rtso::FAUNA_CORRALLED || !near2(fv.pos_x[f], fv.pos_z[f], 2.5f)) continue;
					// Slaughter for livestock as for game (0 A.D.); Alt herds your own to a corral.
					const bool herd = rtso::fauna_type_info(fv.fauna_type[f]).is_domestic() && fv.owner[f] == local_player() &&
					                  Input::get_singleton()->is_key_pressed(KEY_ALT);
					want = herd ? "action-herd" : "action-gather-meat";
				}
			}
			if (!*want && scenery_at(at.x, at.z) != 0) want = "action-attack";
			if (!*want) {
				auto rv = sim->resources();
				for (size_t r = 0; r < rv.count && !*want; ++r) {
					if (!rv.alive[r] || rv.amount[r] <= 0 || !near2(rv.x[r], rv.z[r], 2.2f)) continue;
					switch (rv.type[r]) {
						case rtso::RES_WOOD: want = "action-gather-tree"; break;
						case rtso::RES_GOLD: want = "action-gather-ore"; break;
						case rtso::RES_STONE: want = "action-gather-rock"; break;
						default:
							want = rv.subtype[r] == rtso::RES_SUB_MEAT ? "action-gather-meat"
									: rv.subtype[r] == rtso::RES_SUB_GRAIN ? "action-gather-grain"
									: rv.subtype[r] == rtso::RES_SUB_FISH ? "action-gather-fish" : "action-gather-fruit";
					}
				}
			}
			if (!*want) {
				for (const auto &b : sim->buildings()) {
					if (b.wrecked || !near2(b.x, b.z, b.half_size + 1.2f)) continue;
					if (trade_route_click(b, false, false, &tip)) {
						want = "action-setup-trade-route";
						break;
					}
					// 0 A.D. "capture" is the default on enemy structures; session.attack
					// (or attack-move's Ctrl) attacks instead.
					if (b.team == rtso::GAIA_PLAYER && b.type == rtso::BLD_FIELD && sim->field_resource_id(b.id))
						want = "action-gather-grain";
					else if (b.team != me && sim->is_hostile(me, b.team))
						want = (rtso::hotkeys::held("session.attack") || rtso::hotkeys::held("session.attackmove") ||
										  b.max_capture_points <= 0.0f)
								? "action-attack" : "action-capture";
					else if (b.team != me && !sim->is_ally(me, b.team)) want = ""; // neutral
					else if (b.build_progress < 1.0f || (b.type == rtso::BLD_FIELD && b.team == me)) want = "action-build";
					else if (b.hp < b.max_hp) want = "action-repair"; // own or allied (unit_actions "repair")
					// unit_actions "garrison": an own or allied holder whose List takes a selected unit.
					else if (selection_can_garrison(b.id)) want = "action-garrison";
					break;
				}
			}
		}
	}
	if (cursor_tip_) {
		cursor_tip_->set_visible(!tip.is_empty());
		if (!tip.is_empty()) {
			if (cursor_tip_->get_text() != tip) cursor_tip_->set_text(tip);
			cursor_tip_->set_position(mouse + Vector2(24, 20));
		}
	}
	if (cursor_name_ == want) return;
	cursor_name_ = want;
	Input *in = Input::get_singleton();
	if (!*want) {
		in->set_custom_mouse_cursor(Ref<Resource>());
		return;
	}
	Ref<Texture2D> tex = load_tex(String("res://assets/textures/cursors/") + want + ".png");
	in->set_custom_mouse_cursor(tex, Input::CURSOR_ARROW, hot);
}

// Game speeds offered in the session's speed menu (as 0 A.D.'s game-speed
// dropdown); a replay also offers the fast speeds for skimming through it.
static const char *const kQuicksavePath = "user://saves/quicksave.rts0sav";

static const float kGameSpeeds[] = {0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
static const float kReplaySpeeds[] = {3.0f, 5.0f, 10.0f, 20.0f};

String RtsoMain::speed_text(float speed) { return String::num(speed, 2) + "x"; }

Array RtsoMain::speed_choices(bool replay) {
	Array out;
	for (float sp : kGameSpeeds) out.push_back(sp);
	if (replay)
		for (float sp : kReplaySpeeds) out.push_back(sp);
	return out;
}

void RtsoMain::_on_speed_button_pressed() {
	if (!speed_btn) return;
	if (!speed_menu_) {
		speed_menu_ = memnew(PopupMenu);
		speed_menu_->connect("id_pressed", Callable(this, "_on_speed_selected"));
		speed_btn->add_child(speed_menu_);
	}
	speed_menu_->clear();
	int id = 0;
	for (float sp : kGameSpeeds) speed_menu_->add_radio_check_item(speed_text(sp), id++);
	if (turn_mgr_.is_replaying())
		for (float sp : kReplaySpeeds) speed_menu_->add_radio_check_item(speed_text(sp), id++);
	for (int i = 0; i < speed_menu_->get_item_count(); ++i) {
		const int k = speed_menu_->get_item_id(i);
		const float sp = k < (int)std::size(kGameSpeeds) ? kGameSpeeds[k] : kReplaySpeeds[k - std::size(kGameSpeeds)];
		speed_menu_->set_item_checked(i, sp == game_speed);
	}
	speed_menu_->set_position(Vector2i(speed_btn->get_screen_position() + Vector2(0, speed_btn->get_size().y)));
	speed_menu_->popup();
}

void RtsoMain::_on_speed_selected(int id) {
	const int n = (int)std::size(kGameSpeeds);
	if (id >= 0 && id < n) set_game_speed(kGameSpeeds[id]);
	else if (id >= n && id < n + (int)std::size(kReplaySpeeds)) set_game_speed(kReplaySpeeds[id - n]);
}

// Offline and in replays only: networked games run at the host's speed.
void RtsoMain::set_game_speed(float speed) {
	if (net_lockstep() || !(speed > 0.0f)) return;
	const float most = turn_mgr_.is_replaying() ? kReplaySpeeds[std::size(kReplaySpeeds) - 1] : kGameSpeeds[std::size(kGameSpeeds) - 1];
	game_speed = std::clamp(speed, kGameSpeeds[0], most);
	if (speed_btn) speed_btn->set_text(speed_text(game_speed));
	if (replay_speed_picker_)
		for (int i = 0; i < replay_speed_picker_->get_item_count(); ++i)
			if ((float)replay_speed_picker_->get_item_metadata(i) == game_speed) replay_speed_picker_->select(i);
}

// The pause hotkey and the replay's Pause button. Networked, the pause holds
// every peer's turns (0 A.D. NMT_CLIENT_PAUSED).
// 0 A.D. session hotkeys quicksave/quickload: one quicksave slot, not offered
// in networked games or while watching a replay; a short notice says what happened.
void RtsoMain::show_notice(const String &text) {
	if (!banner_label || !banner_panel || hotkey_paused_) return;
	banner_label->set_text(text);
	banner_panel->set_visible(true);
	notice_until_ = Time::get_singleton()->get_ticks_msec() + 2000;
}

void RtsoMain::quicksave() {
	if (net_lockstep() || turn_mgr_.is_replaying()) return;
	show_notice(save_game(kQuicksavePath, "Quicksave") ? "Quicksaved game" : "Quicksave failed");
}

void RtsoMain::quickload() {
	if (net_lockstep() || turn_mgr_.is_replaying()) return;
	if (!FileAccess::file_exists(kQuicksavePath)) {
		show_notice("No quicksave to load");
		return;
	}
	show_notice(load_game(kQuicksavePath) ? "Quickloaded game" : "Quickload failed");
}

void RtsoMain::toggle_pause() {
	// PauseControl.js canPause: an observer who is not the host cannot pause a
	// multiplayer match (resuming their own pause stays allowed).
	if (!hotkey_paused_ && net_lockstep() && !turn_mgr_.is_replaying() && local_player() >= rtso::MAX_PLAYERS &&
			!net_host_.isHost())
		return;
	hotkey_paused_ = !hotkey_paused_;
	if (net_lockstep()) turn_mgr_.set_local_paused(hotkey_paused_ || paused_via_menu);
	if (replay_pause_btn_) replay_pause_btn_->set_text(hotkey_paused_ ? "Resume" : "Pause");
	update_pause_overlay();
}

// PauseOverlay.rebuild: shown for your own pause and while another client
// pauses; the resume line only for your own; "Paused by" in multiplayer.
void RtsoMain::update_pause_overlay() {
	if (!pause_overlay_) return;
	const bool networked = net_lockstep() && !turn_mgr_.is_replaying();
	String who;
	bool others = false;
	if (networked)
		for (uint32_t c : turn_mgr_.paused_clients()) {
			others = others || c != turn_mgr_.local_client_id();
			String name;
			for (const auto &e : net_host_.playerAssignment())
				if (e.client == c && !e.name.empty()) name = String::utf8(e.name.c_str());
			if (name.is_empty()) {
				const uint8_t pl = turn_mgr_.player_for_client(c);
				name = pl < rtso::MAX_PLAYERS ? player_display_name(pl) : String("an observer");
			}
			who += (who.is_empty() ? "" : ", ") + name;
		}
	const bool show = hotkey_paused_ || others;
	if (pause_overlay_->is_visible() != show) pause_overlay_->set_visible(show);
	if (!show) return;
	pause_resume_label_->set_visible(hotkey_paused_);
	const String by = networked && !who.is_empty() ? vformat(tr("Paused by %s"), who) : String();
	if (pause_by_label_->get_text() != by) pause_by_label_->set_text(by);
}

void RtsoMain::_on_pause_overlay_pressed() {
	if (hotkey_paused_) toggle_pause();
}

// messages.js handleNetStatusMessage "disconnected": once the link to the host
// is lost the match is over for this client. Dialogs close, the overlay says
// why (network.js getDisconnectReason) and stays until Exit.
void RtsoMain::update_net_status_overlay() {
	if (net_disconnected_ || !net_status_overlay_ || !net_game_started_ || net_host_.isHost() ||
			!net_host_.connectionLost())
		return;
	net_disconnected_ = true;
	finished_asked_ = true; // no quit question over the overlay
	close_open_dialogs();
	String why;
	switch (net_host_.disconnectReason()) {
		case 2: why = tr("The host has ended the game."); break;
		case 6: why = tr("You have been kicked."); break;
		case 7: why = tr("You have been banned."); break;
		case 14: why = tr("Password is invalid."); break;
		default: why = tr("An unknown error occurred."); break;
	}
	net_status_label_->set_text(tr("Connection to the server has been lost.") + "\n" + why);
	net_status_overlay_->set_visible(true);
	if (lobby_status) lobby_status->set_text(net_status_label_->get_text().replace("\n", " "));
}

// disconnectedExitButton: endGame(true), the summary page.
void RtsoMain::_on_disconnected_exit_pressed() {
	Ref<Script> q = ResourceLoader::get_singleton()->load("res://scenes/session/quit_confirmation.gd");
	if (q.is_valid()) q->call("end_game", this, true);
}

// messages.js handlePlayerAssignmentsMessage (onClientJoin, onClientLeave) and
// the kicked message, as ChatMessageFormatNetwork words them.
void RtsoMain::post_network_chat() {
	auto who = [&](const rtso::PlayerAssignmentMsg::Entry &e) {
		String name = String::utf8(e.name.empty() ? rtso::peer_guid(e.client).c_str() : e.name.c_str()).replace("[", "[lb]");
		if (e.player >= rtso::MAX_PLAYERS || !sim) return name;
		const uint32_t rgb = rtso::display_colors::rgb(*sim, e.player);
		return "[color=#" + String::num_int64(rgb, 16).lpad(6, "0") + "]" + name + "[/color]";
	};
	const auto &now = net_host_.playerAssignment();
	if (net_host_.playerAssignments() != chat_assignments_seen_) {
		const bool first = chat_assignments_seen_ == 0 && chat_assignments_.empty();
		chat_assignments_seen_ = net_host_.playerAssignments();
		auto in = [](const std::vector<rtso::PlayerAssignmentMsg::Entry> &list, uint32_t client) {
			for (const auto &e : list)
				if (e.client == client && e.status != rtso::PlayerAssignmentMsg::LEFT) return &e;
			return (const rtso::PlayerAssignmentMsg::Entry *)nullptr;
		};
		if (!first) {
			for (const auto &e : chat_assignments_)
				if (e.status != rtso::PlayerAssignmentMsg::LEFT && !in(now, e.client))
					chat_window_.append_line(vformat(tr("%s has left the game."), who(e)) + "\n");
			for (const auto &e : now)
				if (e.status != rtso::PlayerAssignmentMsg::LEFT && !in(chat_assignments_, e.client))
					chat_window_.append_line(vformat(e.player < rtso::MAX_PLAYERS
							? tr("%s is starting to rejoin the game.") : tr("%s is starting to join the game."), who(e)) + "\n");
		}
		chat_assignments_ = now;
	}
	const auto &kicks = net_host_.kicks();
	for (; kicks_chatted_ < kicks.size(); ++kicks_chatted_) {
		const auto &k = kicks[kicks_chatted_];
		const String name = String::utf8(k.name.empty() ? k.guid.c_str() : k.name.c_str()).replace("[", "[lb]");
		chat_window_.append_line(vformat(k.ban ? tr("%s has been banned") : tr("%s has been kicked"), name) + "\n");
	}
}

void RtsoMain::_on_menu_button_pressed() {
	Node *pause = get_node_or_null(NodePath("PauseMenu"));
	if (pause) {
		pause->call("toggle");
		set_menu_paused(!paused_via_menu);
	}
}

// In a networked match the pause menu pauses every peer (0 A.D.
// NMT_CLIENT_PAUSED); offline it just stops the local sim.
void RtsoMain::set_menu_paused(bool paused) {
	paused_via_menu = paused;
	if (net_lockstep()) turn_mgr_.set_local_paused(paused || hotkey_paused_);
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
			civ = b.team == rtso::GAIA_PLAYER ? sim->player_civ(local_player()) : sim->player_civ(b.team);
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
	Ref<Script> viewer = ResourceLoader::get_singleton()->load("res://scenes/reference/template_viewer.gd");
	if (viewer.is_valid()) viewer->call("open", ui_layer, kind, id, civ);
}

// Tests: select the local player's first structure or first live unit.
// unit_actions.js "garrison" actionCheck: some selected unit may garrison in this own or allied
// structure (its GarrisonHolder List, IsOwnedByMutualAllyOfEntity). Room is not checked.
bool RtsoMain::selection_can_garrison(uint32_t building_id) const {
	if (!sim) return false;
	for (int s = 0; s < selected_ids.size(); ++s)
		if (sim->can_garrison_in((uint32_t)(int64_t)selected_ids[s], building_id)) return true;
	return false;
}

bool RtsoMain::select_first_own(bool building) {
	if (!sim) return false;
	selected_ids.clear();
	selected_building_id = 0;
	if (building) {
		for (const auto &b : sim->buildings())
			if (b.team == local_player()) { selected_building_id = b.id; return true; }
		return false;
	}
	auto uv = sim->units();
	for (size_t i = 0; i < uv.count; ++i)
		if (uv.alive[i] && uv.team[i] == local_player()) { selected_ids.push_back((int32_t)uv.id[i]); return true; }
	return false;
}

void RtsoMain::_on_ungarrison_pressed() {
	if (selected_building_id != 0) {
		if (turn_cmds())
			post_lockstep(13, nullptr, 0, 0, 0, selected_building_id, 0);
		else
			sim->cmd_unload(local_player(), selected_building_id, nullptr, 0);
	} else if (!selected_ids.is_empty()) {
		for (int s = 0; s < selected_ids.size(); ++s) {
			if (sim->is_transport((uint32_t)selected_ids[s])) {
				uint32_t tid = (uint32_t)selected_ids[s];
				if (turn_cmds())
					post_lockstep(13, &tid, 1, 0, 0, 0, 1);
				else
					sim->order_disembark_all(tid);
			}
		}
	}
}

// unit_actions.js g_EntityCommands "remove-guard": the selected guards stop guarding
// (lockstep command 37).
void RtsoMain::_on_remove_guard_pressed() {
	if (!sim || selected_ids.is_empty()) return;
	std::vector<uint32_t> ids;
	for (int s = 0; s < selected_ids.size(); ++s)
		if (sim->unit_is_guarding((uint32_t)selected_ids[s])) ids.push_back((uint32_t)selected_ids[s]);
	if (ids.empty()) return;
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_REMOVE_GUARD, ids.data(), ids.size());
	else
		sim->order_remove_guard(ids.data(), ids.size());
}

// g_EntityCommands "call-to-arms": arms ACTION_CALLTOARMS, so the next right-click
// sends the selection there (drop loads, then attack-move; lockstep command 38).
void RtsoMain::_on_call_to_arms_pressed() {
	if (selected_ids.is_empty()) return;
	call_to_arms_mode = true;
	attack_move_mode = patrol_mode = guard_mode = garrison_mode = repair_mode = false;
}

// One g_EntityCommands button (unit_actions.js): the hotkey and title on the
// tooltip's first line, the body below, the icon, and a count where it has one.
void RtsoMain::add_entity_command(const char *name, const char *label, const char *hotkey, const String &tip,
		const char *icon, bool enabled, int count) {
	Button *btn = memnew(Button);
	const String text = count > 0 ? String(label) + " (" + String::num_int64(count) + ")" : String(label);
	btn->set_meta("label", text);
	const std::string key = (hotkey && *hotkey) ? rtso::hotkeys::table().label(hotkey) : std::string();
	btn->set_tooltip_text(key.empty() ? tip : "[" + String(key.c_str()) + "] " + tip);
	Ref<Texture2D> ico = load_tex(String("res://assets/textures/ui/archive/icons/") + icon);
	if (ico.is_valid()) {
		// 0.28's command row is icons, with the count (Unload All) on the icon.
		btn->set_button_icon(ico);
		btn->set_expand_icon(true);
		btn->set_custom_minimum_size(Vector2(32, 32));
		btn->set_text(count > 0 ? String::num_int64(count) : String());
		btn->add_theme_font_size_override("font_size", 10);
	} else {
		btn->set_text(text);
	}
	btn->set_disabled(!enabled);
	btn->connect("pressed", Callable(this, "_on_entity_command").bind(String(name)));
	production_bar->add_child(btn);
}

// g_EntityCommands execute() for the command row, and the hotkeys that run them.
void RtsoMain::_on_entity_command(const String &name) {
	if (!sim) return;
	const uint32_t *ids = (const uint32_t *)selected_ids.ptr();
	const size_t n = selected_ids.size();
	auto arm = [&](bool &mode) {
		if (n == 0) return;
		attack_move_mode = patrol_mode = guard_mode = call_to_arms_mode = garrison_mode = repair_mode = false;
		mode = true;
	};
	if (name == "delete") {
		request_delete();
	} else if (name == "stop") { // stopUnits
		if (n == 0) return;
		if (turn_cmds())
			post_lockstep(2, ids, n);
		else {
			sim->cmd_stop(ids, n);
			sim->order_fauna_stop(local_player(), ids, n);
		}
	} else if (name == "back-to-work") { // backToWork
		if (n == 0) return;
		if (turn_cmds())
			post_lockstep(31, ids, n);
		else
			sim->order_back_to_work(ids, n);
		play_civ_voice("gather");
	} else if (name == "call-to-arms") {
		_on_call_to_arms_pressed();
	} else if (name == "garrison") {
		arm(garrison_mode);
	} else if (name == "repair") {
		arm(repair_mode);
	} else if (name == "add-guard") {
		arm(guard_mode);
	} else if (name == "patrol") {
		arm(patrol_mode);
	} else if (name == "remove-guard") {
		_on_remove_guard_pressed();
	} else if (name == "unload-all") {
		_on_ungarrison_pressed();
	} else if (name == "select-trading-goods") {
		_toggle_session_dialog("BarterDialog");
	} else if (name == "focus-rally") {
		// The first rally point, else the structure itself.
		for (const auto &b : sim->buildings())
			if (b.id == selected_building_id && cam_rig) {
				cam_rig->set_target_pos(b.has_rally_point ? Vector3(b.rally_x, 0, b.rally_z) : Vector3(b.x, 0, b.z));
				break;
			}
	}
}

// unit_actions.js "delete": everything selected that may be deleted (not a
// structure you hold less than half the capture points of), after
// DeleteSelectionConfirmation unless session.noconfirmation is held.
void RtsoMain::request_delete() {
	delete_pending_.clear();
	for (int s = 0; s < selected_ids.size(); ++s) delete_pending_.push_back(selected_ids[s]);
	for (const auto &b : sim->buildings())
		if (b.id == selected_building_id && b.team == local_player() && !(b.max_capture_points > 0.0f &&
				b.capture_points_by_team[b.team] < b.max_capture_points * 0.5f))
			delete_pending_.push_back((int32_t)b.id);
	if (delete_pending_.is_empty()) return;
	if (rtso::hotkeys::held("session.noconfirmation")) {
		_on_delete_confirmed();
		return;
	}
	if (!delete_dialog_) {
		delete_dialog_ = memnew(ConfirmationDialog);
		delete_dialog_->set_title(tr("Delete"));
		delete_dialog_->set_text(tr("Destroy everything currently selected?"));
		delete_dialog_->get_ok_button()->set_text(tr("Yes"));
		delete_dialog_->get_cancel_button()->set_text(tr("No"));
		delete_dialog_->connect("confirmed", Callable(this, "_on_delete_confirmed"));
		ui_layer->add_child(delete_dialog_);
	}
	delete_dialog_->popup_centered();
}

void RtsoMain::_on_delete_confirmed() {
	if (!sim || delete_pending_.is_empty()) return;
	std::vector<uint32_t> ids;
	for (int s = 0; s < delete_pending_.size(); ++s) ids.push_back((uint32_t)(int64_t)delete_pending_[s]);
	delete_pending_.clear();
	if (turn_cmds())
		post_lockstep(30, ids.data(), ids.size());
	else
		sim->cmd_delete(local_player(), ids.data(), ids.size());
	clear_selection();
}

void RtsoMain::_on_stance_selected(int stance) {
	if (!selected_ids.is_empty()) {
		if (turn_cmds())
			post_lockstep(7, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, 0, (uint8_t)stance);
		else
			sim->order_set_stance((const uint32_t *)selected_ids.ptr(), selected_ids.size(), (uint8_t)stance);
	}
}

void RtsoMain::_on_formation_selected(int formation) {
	if (!selected_ids.is_empty()) {
		if (turn_cmds())
			post_lockstep(8, (const uint32_t *)selected_ids.ptr(), selected_ids.size(), 0, 0, 0, (uint8_t)formation);
		else
			sim->order_set_formation((const uint32_t *)selected_ids.ptr(), selected_ids.size(), (uint8_t)formation);
	}
}

void RtsoMain::_on_train_pressed(int utype) {
	try_train_unit((rtso::UnitType)utype);
}

void RtsoMain::_on_roster_item_clicked(int index, Vector2 at, int mouse_button) {
	(void)at;
	if (!roster_list || index < 0 || index >= roster_list->get_item_count()) return;
	roster_list->deselect_all();
	if (mouse_button != MOUSE_BUTTON_LEFT || roster_list->is_item_disabled(index)) return;
	const int meta = (int)roster_list->get_item_metadata(index);
	if (meta & ROSTER_ANIMAL) {
		try_breed(selected_building_id, (uint8_t)(meta & 0xFF), train_batch());
		return;
	}
	if (selected_building_id == 0 && trainer_unit_id_ != 0) {
		if (meta & ROSTER_TECH) {
			const uint8_t tech = (uint8_t)(meta & 0xFF);
			if (turn_cmds())
				post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_TRAIN_FROM_UNIT, nullptr, 0, (float)tech, 0, trainer_unit_id_, 2);
			else
				sim->cmd_research_from_unit(local_player(), trainer_unit_id_, tech);
			return;
		}
		try_train_from_unit(trainer_unit_id_, (uint16_t)meta, train_batch());
		return;
	}
	try_train_template(selected_building_id, (uint16_t)meta, train_batch());
}

// The Gaia scenery whose rotated rectangle (with a little slack) holds (x, z), 0 if none.
uint32_t RtsoMain::scenery_at(float x, float z) const {
	if (!sim) return 0;
	for (const auto &sc : sim->scenery()) {
		const float c = std::cos(sc.angle), s = std::sin(sc.angle);
		const float rx = x - sc.x, rz = z - sc.z;
		const float lu = rx * c - rz * s, lv = rx * s + rz * c;
		if (std::fabs(lu) <= sc.half_w + 0.5f && std::fabs(lv) <= sc.half_d + 0.5f) return sc.id;
	}
	return 0;
}

// A unit's Trainer (triremes, Cyrus II): lockstep NET_CMD_TRAIN_FROM_UNIT.
void RtsoMain::try_train_from_unit(uint32_t unit_id, uint16_t tmpl, int count) {
	if (unit_id == 0 || tmpl >= rtso::CIV_UNIT_TEMPLATE_COUNT) return;
	if (turn_cmds()) {
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_TRAIN_FROM_UNIT, nullptr, 0, (float)tmpl, (float)count, unit_id, 0);
		play_civ_voice("select");
		return;
	}
	if (sim->cmd_train_from_unit(local_player(), unit_id, tmpl, count))
		play_civ_voice("select");
}

// Corral training (0 A.D. Trainer at the corral): the lockstep Train command with no unit
// type and the animal in target_x.
void RtsoMain::try_breed(uint32_t building_id, uint8_t ftype, int count) {
	if (building_id == 0) return;
	if (turn_cmds()) {
		post_lockstep(6, nullptr, 0, (float)ftype, (float)count, building_id, (uint8_t)rtso::UNIT_TYPE_NONE);
		return;
	}
	sim->cmd_breed_livestock(local_player(), building_id, ftype, count);
}

void RtsoMain::_on_build_btn_pressed(int btype) {
	enter_build_mode(true, (uint8_t)btype);
}

void RtsoMain::_on_tech_pressed(int tech_id) {
	if (selected_building_id != 0) {
		if (turn_cmds())
			post_lockstep(9, nullptr, 0, 0, 0, selected_building_id, (uint8_t)tech_id);
		else
			sim->cmd_research(local_player(), selected_building_id, (uint8_t)tech_id);
	}
}

// 0 A.D. Upgrade.js through lockstep command 26: `choice` is which of
// building_upgrade()'s choices (target_x), -1 cancels and refunds (param 1).
void RtsoMain::_on_upgrade_pressed(int choice) {
	if (selected_building_id == 0) return;
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_UPGRADE_BUILDING, nullptr, 0, choice < 0 ? 0.0f : (float)choice, 0,
				selected_building_id, choice < 0 ? 1 : 0);
	else if (choice < 0)
		sim->cmd_cancel_upgrade(local_player(), selected_building_id);
	else
		sim->cmd_upgrade_building(local_player(), selected_building_id, choice);
}

// 0 A.D. Gate.js LockGate / UnlockGate through lockstep command 27.
void RtsoMain::_on_gate_lock_pressed(int lock) {
	if (selected_building_id == 0) return;
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_LOCK_GATE, nullptr, 0, 0, 0, selected_building_id, lock ? 1 : 0);
	else
		sim->cmd_lock_gate(local_player(), selected_building_id, lock != 0);
}

// 0 A.D. ProductionQueue.js EnableAutoQueue / DisableAutoQueue through lockstep command 44.
void RtsoMain::_on_autoqueue_pressed(int on) {
	if (selected_building_id == 0) return;
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_AUTOQUEUE, nullptr, 0, 0, 0, selected_building_id, on ? 1 : 0);
	else
		sim->cmd_set_autoqueue(local_player(), selected_building_id, on != 0);
}

// 0 A.D. set-dropsite-sharing through lockstep command 45.
void RtsoMain::_on_dropsite_sharing_pressed(int shared) {
	if (selected_building_id == 0) return;
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_SET_DROPSITE_SHARING, nullptr, 0, 0, 0, selected_building_id,
				shared ? 1 : 0);
	else
		sim->cmd_set_dropsite_sharing(local_player(), selected_building_id, shared != 0);
}

// 0 A.D. AlertRaiser alert-raise / alert-end through lockstep command 35.
void RtsoMain::_on_alert_pressed(int end) {
	if (selected_building_id == 0) return;
	if (net_lockstep())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_ALERT, nullptr, 0, 0, 0, selected_building_id, end ? 1 : 0);
	else if (end)
		sim->cmd_end_alert(local_player(), selected_building_id);
	else
		sim->cmd_raise_alert(local_player(), selected_building_id);
}

// 0 A.D. unit Upgrade on the selection through lockstep command 43 (`to` = roster
// template, -1 = cancel and refund).
void RtsoMain::_on_unit_upgrade_pressed(int to) {
	if (selected_ids.is_empty()) return;
	const uint32_t *ids = (const uint32_t *)selected_ids.ptr();
	const size_t n = selected_ids.size();
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_UPGRADE_UNIT, ids, n, 0, 0, to < 0 ? 0 : (uint32_t)to, to < 0 ? 1 : 0);
	else if (to < 0)
		sim->cmd_cancel_unit_upgrades(local_player(), ids, n);
	else
		sim->cmd_upgrade_units(local_player(), ids, n, (uint16_t)to);
}

// 0 A.D. Pack / Unpack on the selected siege engines through lockstep command 29.
void RtsoMain::_on_pack_pressed(int pack) {
	if (selected_ids.is_empty()) return;
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_PACK, (const uint32_t *)selected_ids.ptr(),
				selected_ids.size(), 0, 0, 0, pack ? 1 : 0);
	else
		sim->order_pack((const uint32_t *)selected_ids.ptr(), selected_ids.size(), pack != 0);
}

void RtsoMain::_on_barter_requested(int sell_type, int buy_type, int amount) {
	if (!sim) return;
	if (sell_type < 0 || sell_type >= 4 || buy_type < 0 || buy_type >= 4) return;
	// Lockstep: barter mutates resources + market prices (both hashed) -> cmd_type 23
	// (turn_manager.cpp) applies on every peer at the turn boundary. 0 A.D. Barter.js.
	bool ok = false;
	if (turn_cmds()) {
		post_lockstep(23, nullptr, 0, (float)sell_type, (float)buy_type, (uint32_t)amount, 0);
		ok = true; // validated deterministically on every peer at the boundary
	} else {
		ok = sim->cmd_barter(local_player(), (rtso::ResourceType)sell_type, (rtso::ResourceType)buy_type, amount);
	}
	if (ok) {
		play_sfx("res://assets/sounds/ui/bonus.ogg");
		// Price labels will be refreshed on next update_hud / barter dialog visibility
	}
}

// 0 A.D. TradeDialog: everything the barter and trade panels show for the local
// player. Barter quotes follow BarterButton.update (sell 100, or 500 with
// session.massbarter); trader counts follow GuiInterface.GetTraderNumber, where a
// land trader counts as garrisoned while it rides a merchant ship that is trading.
Dictionary RtsoMain::get_trade_dialog_state() const {
	Dictionary d;
	if (!sim) return d;
	const uint8_t me = local_player();
	const auto &ps = sim->player(me);
	d["player"] = (int)me;
	d["can_barter"] = sim->can_barter(me);
	d["controls"] = !sim->is_defeated(me);
	// Barter.js: 100 per click, 5x with session.massbarter (the only amounts cmd_barter takes).
	const int multiplier = 5;
	const int amount = 100 * (rtso::hotkeys::held("session.massbarter") ? multiplier : 1);
	d["amount"] = amount;
	d["multiplier"] = multiplier;
	PackedInt32Array stock, goods, quotes;
	for (int r = 0; r < rtso::RES_COUNT; ++r) {
		stock.push_back((int)ps.res[r]);
		goods.push_back((int)sim->trading_goods(me)[r]);
	}
	// quotes[sell * RES_COUNT + buy]: what one click buys.
	for (int sell = 0; sell < rtso::RES_COUNT; ++sell)
		for (int buy = 0; buy < rtso::RES_COUNT; ++buy)
			quotes.push_back(sim->barter_quote((rtso::ResourceType)sell, (rtso::ResourceType)buy, amount));
	d["stock"] = stock;
	d["trading_goods"] = goods;
	d["quotes"] = quotes;

	auto uv = sim->units();
	std::unordered_set<uint32_t> trading_ships;
	int land_total = 0, land_trading = 0, ship_total = 0, ship_trading = 0, land_garrisoned = 0;
	for (size_t i = 0; i < uv.count; ++i) {
		if (uv.team[i] != me || uv.hp[i] <= 0.0f || !rtso::unit_is_trader(uv.unit_type[i])) continue;
		if (uv.action_state[i] == rtso::ACTION_DYING) continue;
		const bool trading = uv.alive[i] && uv.action_state[i] == rtso::ACTION_TRADE;
		if (uv.unit_type[i] == rtso::UNIT_MERCHANT_SHIP) {
			++ship_total;
			if (trading) { ++ship_trading; trading_ships.insert(uv.id[i]); }
		} else {
			++land_total;
			if (trading) ++land_trading;
		}
	}
	for (const auto &[ship, passengers] : sim->transport_passengers()) {
		if (!trading_ships.count(ship)) continue;
		for (uint32_t pid : passengers)
			for (size_t i = 0; i < uv.count; ++i)
				if (uv.id[i] == pid) {
					if (uv.unit_type[i] == rtso::UNIT_TRADER && uv.team[i] == me && uv.hp[i] > 0.0f) ++land_garrisoned;
					break;
				}
	}
	Dictionary land, ship;
	land["total"] = land_total;
	land["trading"] = land_trading;
	land["garrisoned"] = land_garrisoned;
	ship["total"] = ship_total;
	ship["trading"] = ship_trading;
	d["land"] = land;
	d["ship"] = ship;
	return d;
}

// TradeButtonManager.setTradingGoods: shares by ResourceType, summing to 100.
// Lockstep command 33 packs one percent byte per resource into target_id.
void RtsoMain::_on_trading_goods_requested(const PackedInt32Array &shares) {
	if (!sim || shares.size() != rtso::RES_COUNT) return;
	uint8_t pct[rtso::RES_COUNT];
	int sum = 0;
	uint32_t packed = 0;
	for (int r = 0; r < rtso::RES_COUNT; ++r) {
		if (shares[r] < 0 || shares[r] > 100) return;
		pct[r] = (uint8_t)shares[r];
		sum += pct[r];
		packed |= (uint32_t)pct[r] << (8 * r);
	}
	if (sum != 100) return;
	if (turn_cmds())
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_SET_TRADING_GOODS, nullptr, 0, 0, 0, packed, 0);
	else
		sim->cmd_set_trading_goods(local_player(), pct);
}

bool RtsoMain::save_game(String path, String description) {
	if (!sim) return false;
	String global_path = ProjectSettings::get_singleton()->globalize_path(path);
	Ref<DirAccess> dir = DirAccess::open("user://");
	if (dir.is_valid()) {
		dir->make_dir_recursive("saves");
	}
	bool res = sim->save_state(global_path.utf8().get_data());
	if (res) {
		// The save and load dialogs list saves by this metadata (0 A.D. gui/savedgames).
		write_save_metadata(path, description);
		write_save_thumbnail(path);
		print_line("[RTS0] Game saved successfully to " + path);
	}
	return res;
}

bool RtsoMain::load_game(String path) {
	if (!sim) return false;
	String global_path = ProjectSettings::get_singleton()->globalize_path(path);
	load_progress(50, "Loading saved game");
	// The replay so far stays valid up to here; what follows starts from the save.
	stop_replay_recording();
	bool success = sim->load_state(global_path.utf8().get_data());
	if (success) {
		selected_ids.clear();
		selected_building_id = 0;
		print_line("[RTS0] Game loaded successfully from " + path);
		// The game goes on from the save as a new replay that starts from it.
		if (!loading_replay_start_ && !net_lockstep()) begin_recording_from_save(path);
	}
	return success;
}

PackedByteArray RtsoMain::get_fow_grid() const {
	if (!sim) return PackedByteArray();
	const uint8_t* grid = sim->fow_grid(view_player());
	int sz = sim->fow_res() * sim->fow_res();
	PackedByteArray arr;
	arr.resize(sz);
	for (int i = 0; i < sz; ++i) arr.set(i, grid[i]);
	return arr;
}

void RtsoMain::order_guard_units(uint32_t target_id) {
	if (!sim || selected_ids.is_empty()) return;
	std::vector<uint32_t> ids;
	for (int i = 0; i < selected_ids.size(); ++i) ids.push_back((uint32_t)selected_ids[i]);
	if (turn_cmds())
		post_lockstep(21, ids.data(), ids.size(), 0, 0, target_id);
	else
		sim->order_guard(ids.data(), ids.size(), target_id);
}

void RtsoMain::spawn_large_battle(int total_units) {
	if (!sim) return;
	clear_selection();
	if (banner_panel) banner_panel->set_visible(false);
	stop_replay_recording(); // a debug spawn is not a command, so a replay could not reproduce it
	sim->spawn_war(total_units);
	if (cam_rig) {
		cam_rig->set_target_pos(Vector3(0, 0, 0));
	}
	print_line(String("[RTS0] Spawned massive war simulation: ") + String::num_int64(total_units) + " units across 128m map.");
}

void RtsoMain::update_ghost_mesh() {
	if (!ghost_mesh) return;
	String cat = "buildings";
	const char *fid = rtso::civ_building_foundation(current_build_type);
	auto bentry = rtso::ModelLibrary::get(cat, String(fid));
	if (!bentry.valid) {
		uint8_t pciv = sim ? sim->player_civ(local_player()) : (uint8_t)rtso::CIV_ATHENIANS;
		bentry = rtso::ModelLibrary::get(cat, String(rtso::civ_building_model(pciv, current_build_type)));
	}
	if (bentry.valid) {
		ghost_mesh->set_mesh(bentry.mesh);
		ghost_mesh->set_scale(Vector3(bentry.scale, bentry.scale, bentry.scale));
	} else {
		Ref<BoxMesh> box;
		box.instantiate();
		box->set_size(Vector3(1.8f, 1.8f, 1.8f));
		ghost_mesh->set_mesh(box);
	}
}

// Options page settings the session reads (rtso::user_config, 0 A.D.
// ConfigDB), refreshed whenever the options page changes one.
void RtsoMain::refresh_user_options() {
	namespace uc = rtso::user_config;
	if (user_opts_.generation == uc::generation()) return;
	user_opts_.generation = uc::generation();
	user_opts_.batch_size = std::clamp((int)uc::get_number("gui.session.batchtrainingsize"), 1, 20);
	user_opts_.wounded_fraction = (float)std::clamp(uc::get_number("gui.session.woundedunithotkeythreshold"), 0.0, 100.0) / 100.0f;
	user_opts_.flare_time = (float)std::clamp(uc::get_number("gui.session.flarelifetime"), 0.0, 60.0);
	user_opts_.drag_delta = (float)std::clamp(uc::get_number("gui.session.dragdelta"), 1.0, 200.0);
	user_opts_.attack_notice = uc::get_bool("gui.session.notifications.attack");
	user_opts_.tribute_notice = uc::get_bool("gui.session.notifications.tribute");
	user_opts_.barter_notice = uc::get_bool("gui.session.notifications.barter");
	{
		const std::string phase = uc::get("gui.session.notifications.phase");
		user_opts_.phase_notice = phase == "none" ? 0 : phase == "all" ? 2 : 1;
	}
	chat_window_.set_timestamps(uc::get_bool("chat.timestamp"));
	range_overlays_.show_attack = uc::get_bool("gui.session.attackrange");
	range_overlays_.show_auras = uc::get_bool("gui.session.aurasrange");
	range_overlays_.show_heal = uc::get_bool("gui.session.healrange");
}

void RtsoMain::_process(double delta) {
	refresh_user_options();
	if (net_host_.getRole() != rtso::NetRole::None) {
		net_host_.poll(net_arena_);
		net_arena_.reset();
		// Consume NMT_GAME_START seed-sync queued by TurnManager::onMessage.
		apply_net_game_start();
		// Rejoin of a match in progress: the host sent its world; load it and
		// play on in the seat our dropped client had.
		if ((net_host_.isHost() && !net_game_started_) || (lobby_seats && lobby_seats->get_child_count()))
			refresh_lobby_seats();
		// A host that turned us away mid-match says why (0 A.D. disconnect reasons).
		if (!net_host_.isHost() && !net_game_started_ && lobby_status) {
			const uint8_t why = turn_mgr_.rejoin_reject_reason();
			const char *text = why == 6 ? "This game does not let observers join after it started."
					: why == 7 ? "This game is full: it has as many observers as it allows."
					: why == 3 || why == 4 || why == 5 ? "Could not load the host's game."
					: nullptr;
			if (text && lobby_status->get_text() != text) lobby_status->set_text(text);
		}
		if (!net_host_.isHost() && !net_game_started_ && sim && turn_mgr_.apply_pending_rejoin(*sim)) {
			net_game_started_ = true;
			selected_ids.clear();
			selected_building_id = 0;
			if (lobby_status)
				lobby_status->set_text("Rejoined the match at turn " + String::num_int64(turn_mgr_.rejoin_turn()) +
						" as Player " + String::num_int64(turn_mgr_.local_player() + 1));
		}
	}
	if (cam_rig) {
		// Letters pick buildings while placing one, and typing never moves the camera.
		Control *focus = get_viewport() ? get_viewport()->gui_get_focus_owner() : nullptr;
		const bool typing = focus && (Object::cast_to<LineEdit>(focus) || focus->is_class("TextEdit"));
		cam_rig->set_key_scroll_mode(typing ? RtsoCameraRig::KEYS_NONE
				: build_mode ? RtsoCameraRig::KEYS_ARROWS_ONLY : RtsoCameraRig::KEYS_ALL);
		if (const uint32_t fid = sim ? cam_rig->follow_id() : 0) {
			bool alive = false;
			auto fview = sim->units();
			for (size_t i = 0; i < fview.count; ++i) {
				if (fview.id[i] == fid && fview.alive[i]) {
					cam_rig->follow_to(Vector3(fview.pos_x[i], 0, fview.pos_z[i]));
					alive = true;
					break;
				}
			}
			if (!alive) cam_rig->set_follow_id(0);
		}
	}
	// Networked: keep running TurnManager::tick, which holds turns for everyone
	// while any client is paused, so this peer still sees remote pauses end.
	music_.tick(delta);
	if (notice_until_ && Time::get_singleton()->get_ticks_msec() >= notice_until_) {
		notice_until_ = 0;
		if (!hotkey_paused_ && banner_panel) banner_panel->set_visible(false);
	}
	update_pause_overlay();
	if ((paused_via_menu || hotkey_paused_) && !net_lockstep()) return;
	double scaled_delta = delta * game_speed;
	match_elapsed_time += scaled_delta;

	rtso::PerfTracker::begin_frame();
	uint64_t start_us = Time::get_singleton()->get_ticks_usec();

	accumulator += scaled_delta;
	int ticks = 0;
	if (net_lockstep() && lobby_status) {
		// 0 A.D. NetClient notices: the host refused us, or removed someone.
		const int auth = net_host_.authResult();
		if (auth == rtso::ARC_PASSWORD_INVALID) lobby_status->set_text("Wrong password");
		else if (auth == rtso::ARC_BANNED) lobby_status->set_text("You have been banned from this game");
		const auto &kicks = net_host_.kicks();
		for (; kicks_shown_ < kicks.size(); ++kicks_shown_) {
			const auto &k = kicks[kicks_shown_];
			const String who = String::utf8(k.name.empty() ? k.guid.c_str() : k.name.c_str());
			lobby_status->set_text(net_host_.kickedSelf() && k.guid == rtso::peer_guid(net_host_.getLocalPeerId())
					? String(k.ban ? "You have been banned" : "You have been kicked")
					: who + String(k.ban ? " has been banned" : " has been kicked"));
		}
	}
	if (net_lockstep()) {
		if (net_game_started_) {
			// NetClientTurnManager: one 200ms turn batch, sim steps inside TurnManager::tick
			if (turn_mgr_.tick((float)scaled_delta, *sim))
				ticks = rtso::TurnManager::TICKS_PER_TURN;
			if (turn_mgr_.has_out_of_sync() && lobby_status) {
				// 0 A.D. session.js "Out-Of-Sync" notice: the first diverging turn and who.
				String who;
				for (const auto &n : turn_mgr_.oos_players()) who += (who.is_empty() ? "" : ", ") + String::utf8(n.c_str());
				lobby_status->set_text("Out-Of-Sync on turn " + String::num_int64(turn_mgr_.oos_turn()) +
						(who.is_empty() ? String() : ". Players: " + who) +
						(turn_mgr_.oos_dump_path().empty() ? String() : ". Dump: " + String::utf8(turn_mgr_.oos_dump_path().c_str())));
			} else if (turn_mgr_.is_paused() && lobby_status) {
				String who;
				for (uint32_t c : turn_mgr_.paused_clients()) {
					const uint8_t pl = turn_mgr_.player_for_client(c);
					who += (who.is_empty() ? "" : ", ") + (pl < 8 ? player_display_name(pl) : String("an observer"));
				}
				lobby_status->set_text("Game paused by " + who);
			} else if (lobby_status && !net_disconnected_) {
				// 0 A.D. session.js network warnings: who the turn waits on, who
				// is not responding and whose connection is bad.
				auto name = [&](uint32_t c) {
					for (const auto &e : net_host_.playerAssignment())
						if (e.client == c && !e.name.empty()) return String::utf8(e.name.c_str());
					const uint8_t pl = turn_mgr_.player_for_client(c);
					return pl < 8 ? "Player " + String::num_int64(pl + 1) : String("an observer");
				};
				String status;
				if (turn_mgr_.waiting_ms() > rtso::NET_TIMEOUT_WARN_MS) {
					String who;
					for (uint32_t c : turn_mgr_.waiting_on()) who += (who.is_empty() ? "" : ", ") + name(c);
					if (!who.is_empty()) status = "Waiting for " + who;
				}
				for (const auto &e : net_host_.clientPerformance()) {
					String line;
					if (e.silent_ms > rtso::NET_TIMEOUT_WARN_MS)
						line = name(e.client) + " is not responding (" + String::num_int64(e.silent_ms / 1000) + " s)";
					else if (e.rtt_ms > rtso::NET_LAG_WARN_RTT_MS)
						line = name(e.client) + " has a bad connection (" + String::num_int64(e.rtt_ms) + " ms)";
					if (!line.is_empty()) status += (status.is_empty() ? "" : ". ") + line;
				}
				if (!status.is_empty() || net_status_shown_) lobby_status->set_text(status);
				net_status_shown_ = !status.is_empty();
			}
		}
		if (net_game_started_) {
			post_network_chat();
			update_net_status_overlay();
		}
		// 0 A.D. player list: who plays which player, who has left, who watches.
		if (lobby_players && net_host_.playerAssignments() != players_shown_) {
			players_shown_ = net_host_.playerAssignments();
			String playing, watching;
			for (const auto &e : net_host_.playerAssignment()) {
				const String who = String::utf8(e.name.empty() ? rtso::peer_guid(e.client).c_str() : e.name.c_str());
				if (e.status == rtso::PlayerAssignmentMsg::OBSERVER) {
					watching += String(watching.is_empty() ? "" : ", ") + who;
					continue;
				}
				String line = who + String(" (Player ") + String::num_int64(e.player + 1);
				if (e.status == rtso::PlayerAssignmentMsg::LEFT) line += String(", left");
				playing += String(playing.is_empty() ? "" : ", ") + line + String(")");
			}
			String text = playing.is_empty() ? String() : "Players: " + playing;
			if (!watching.is_empty()) text += (text.is_empty() ? "" : ". ") + String("Observers: ") + watching;
			if (turn_mgr_.is_observer()) text += (text.is_empty() ? "" : ". ") + String("You are observing.");
			lobby_players->set_text(text);
		}
		// else pre-Start lobby phase: poll + GAME_START consume only (above) -
		// no lockstep turns and no hash exchange across divergent boot worlds.
	} else if (turn_cmds()) {
		// Offline turns (0 A.D. CLocalTurnManager): orders run on the next 200 ms
		// turn and every turn lands in the replay. A fast game speed runs a few
		// turns a frame; the timer keeps any remainder for the next frame.
		if (!replay_finished_) {
			float step = (float)scaled_delta;
			const int max_turns = std::max(4, (int)std::ceil(game_speed * 2.0f)); // fast replays
			for (int t = 0; t < max_turns && turn_mgr_.tick(step, *sim); ++t) {
				ticks += rtso::TurnManager::TICKS_PER_TURN;
				step = 0.0f;
			}
		}
	} else {
		while (accumulator >= rtso::Simulation::FIXED_STEP) {
			sim->tick(rtso::Simulation::FIXED_STEP);
			accumulator -= rtso::Simulation::FIXED_STEP;
			ticks++;
			if (ticks > 8) break;
		}
	}


	tick_match_archive();

	uint64_t after_sim_us = Time::get_singleton()->get_ticks_usec();
	rtso::PerfTracker::record_tick((after_sim_us - start_us) / 1000);
	rtso::PerfTracker::record_draw((int)sim->unit_count(), (int)sim->building_count());

	const uint8_t viewer = view_player();
	rtso::display_colors::refresh_config();
	rtso::display_colors::state().viewer = viewer; // DiplomacyColors: g_ViewedPlayer's stances
	view->set_local_player(viewer);
	if (viewer < rtso::MAX_PLAYERS) sim->set_fow_viewer(viewer); // an observer reads the revealed grid
	if (inspected_kind_ == INSPECT_UNIT || inspected_kind_ == INSPECT_FAUNA) {
		PackedInt32Array shown = selected_ids;
		shown.push_back((int32_t)inspected_id_);
		view->sync(sim, (float)delta, shown);
	} else {
		view->sync(sim, (float)delta, selected_ids);
	}
	if (cam_rig) {
		minimap_view->set_local_player(viewer);
		minimap_view->sync(sim, cam_rig->get_target_pos());
	}
	uint64_t after_sync_us = Time::get_singleton()->get_ticks_usec();
	rtso::PerfTracker::record_sync((after_sync_us - after_sim_us) / 1000);
	rtso::PerfTracker::sample_memory();
	rtso::PerfTracker::maybe_log();

	if (debug_overlay->is_visible()) {
		String perf_txt = rtso::PerfTracker::get_report() +
			" | Tick US: " + String::num_int64(sim->stats().tick_us) +
			" | Jobs: " + String::num_int64((int64_t)rtso::JobSystem::instance().parallel_width()) +
			" | FPS: " + String::num_int64(Engine::get_singleton()->get_frames_per_second());
		debug_overlay->update_info(perf_txt);
	}

	update_hud();
	if (sim && time_notification_label_) {
		String txt;
		for (const rtso::TimeNotification *n : sim->time_notifications_for(local_player())) {
			if (!txt.is_empty()) txt += "\n";
			// GuiInterface time notifications: %(time)s is the time left (timeToString).
			String line = String::utf8(n->text);
			if (line.contains("%(time)s")) {
				const uint32_t now = sim->trigger_state().time_ms;
				const int left_s = n->end_ms > now ? (int)((n->end_ms - now + 999) / 1000) : 0;
				line = line.replace("%(time)s", vformat("%d:%02d", left_s / 60, left_s % 60));
			}
			txt += line;
		}
		// CeasefireManager: "You can attack in %(time)s" for the last 10 s, then
		// "You can attack now!" for 5 s.
		if (sim->ceasefire_seconds() > 0.0f) {
			const float left = sim->ceasefire_seconds() - sim->game_time();
			String line;
			if (left > 0.0f && left <= 10.0f) line = "You can attack in 0:" + String(left > 9.0f ? "" : "0") + String::num_int64((int64_t)std::ceil(left));
			else if (left <= 0.0f && left > -5.0f) line = "You can attack now!";
			if (!line.is_empty()) txt += (txt.is_empty() ? "" : "\n") + line;
		}
		if (time_notification_label_->get_text() != txt) time_notification_label_->set_text(txt);
	}
	if (sim && chat_window_.is_ready()) {
		for (const auto &line : sim->pending_petra_chat()) {
			if (!sim->petra_chat_reaches(line, local_player())) continue; // addressed to someone else
			const std::string text = rtso::petra_chat_text(line, [&](uint8_t pid) {
				return std::string(player_display_name(pid).utf8().get_data());
			});
			// 0 A.D. marks "/allies" lines "(to allies)" and "/msg" lines "(privately)".
			const bool ai = line.kind != rtso::PETRA_CHAT_ATTACK_REQUESTED || sim->player(line.from).is_ai;
			const String audience = line.to == rtso::PETRA_CHAT_ALLIES ? " (to allies)" : line.to < rtso::MAX_PLAYERS ? " (privately)" : "";
			chat_window_.append_line("[color=#88ccff]" + player_display_name(line.from) + (ai ? " (AI)" : "") + audience + ":[/color] " +
			                         String::utf8(text.c_str()) + "\n");
		}
		if (!sim->pending_petra_chat().empty()) sim->clear_petra_chat();
	}
	prune_selection();
	if (!selected_ids.is_empty() || selected_building_id != 0) inspected_kind_ = INSPECT_NONE;
	update_inspector();
	update_command_panel();
	const auto attacks = sim->take_attacked_events();
	detect_attacks(attacks);
	show_session_notices(sim->take_session_notices());
	update_cursor();
	chat_window_.tick_overlay();
	if (objectives_dialog_.is_open()) objectives_dialog_.set_player_state(match_settings_player_state());
	tutorial_panel_.refresh(sim);
	if (diplo_dialog_.is_open()) diplo_dialog_.refresh();

	// Sound events and the music state (0 A.D. music.js: battle while
	// BattleDetection says so, victory and defeat playlists at the end).
	if (cam_rig) {
		const Vector3 focus = cam_rig->get_target_pos();
		session_audio_.set_listener(focus.x, focus.z);
	}
	session_audio_.tick(sim, local_player(), match_elapsed_time, true);
	update_music_state();

	// Environmental Ambience: Viewport Elevation & Coastal Water Proximity
	Vector3 cam_world_pos(0, 30, 0);
	if (cam_rig && cam_rig->get_camera()) {
		cam_world_pos = cam_rig->get_camera()->get_global_position();
	} else if (cam_rig) {
		cam_world_pos = cam_rig->get_target_pos() + Vector3(0, 30, 0);
	}

	// 1. Wind & Nature elevation crossfade
	float elev_factor = Math::clamp((cam_world_pos.y - 12.0f) / 38.0f, 0.0f, 1.0f);
	float target_wind_db = Math::lerp(-32.0f, -8.0f, elev_factor);
	float target_nature_db = Math::lerp(-12.0f, -28.0f, elev_factor);

	if (ambient_wind_player) {
		if (!ambient_wind_player->is_playing()) ambient_wind_player->play();
		float cur_db = ambient_wind_player->get_volume_db();
		ambient_wind_player->set_volume_db(Math::lerp(cur_db, target_wind_db, (float)delta * 3.5f));
	}
	if (ambient_player) {
		if (!ambient_player->is_playing()) ambient_player->play();
		float cur_db = ambient_player->get_volume_db();
		ambient_player->set_volume_db(Math::lerp(cur_db, target_nature_db, (float)delta * 3.5f));
	}

	// 2. Coastal water proximity modulation
	float target_water_db = -80.0f;
	if (sim && sim->water_height() > 0.0f) {
		float water_h = sim->water_height();
		Vector3 target_pos = cam_rig ? cam_rig->get_target_pos() : cam_world_pos;
		float terrain_h = sim->get_terrain_height(target_pos.x, target_pos.z);
		float d_water = terrain_h - water_h;
		if (d_water <= 3.5f) {
			float coast_factor = Math::clamp(1.0f - (d_water / 3.5f), 0.0f, 1.0f);
			target_water_db = Math::lerp(-40.0f, -10.0f, coast_factor);
		}
	}
	if (ambient_water_player) {
		if (!ambient_water_player->is_playing()) ambient_water_player->play();
		float cur_db = ambient_water_player->get_volume_db();
		ambient_water_player->set_volume_db(Math::lerp(cur_db, target_water_db, (float)delta * 3.0f));
	}

	// Update Building Placement Ghost (0 A.D. gui/session/building_placement.js:45-92 BuildPreview parity)
	if (build_mode) {
		Vector2 mouse_pos = get_viewport()->get_mouse_position();
		if (wall_drag_mode) {
			if (ghost_mesh && ghost_mesh->is_visible()) ghost_mesh->set_visible(false);
			// The pieces the sim will lay (helpers/Walls.js GetWallPlacement), each tinted by whether
			// it can be placed where it stands.
			const Vector3 drag_curr_pos = get_ground_click_pos(mouse_pos);
			std::vector<rtso::Simulation::WallPiece> pieces;
			uint32_t start_tower = 0, end_tower = 0;
			const uint8_t wall_kind = rtso::wallset_kind_of((uint8_t)current_build_type);
			const rtso::WallSetInfo &wall_set = rtso::wallset_info(wall_kind);
			if (sim)
				pieces = sim->wall_placement(local_player(), wall_drag_start_pos.x, wall_drag_start_pos.z,
						drag_curr_pos.x, drag_curr_pos.z, &start_tower, &end_tower, wall_kind);
			while (wall_ghosts.size() < pieces.size()) {
				MeshInstance3D *g = memnew(MeshInstance3D);
				g->set_visible(false);
				add_child(g);
				wall_ghosts.push_back(g);
			}

			uint8_t pciv = sim ? sim->player_civ(local_player()) : (uint8_t)rtso::CIV_ATHENIANS;
			auto tower_entry = rtso::ModelLibrary::get("buildings", String(rtso::civ_building_foundation(rtso::BLD_TOWER)));
			if (!tower_entry.valid) {
				tower_entry = rtso::ModelLibrary::get("buildings", String(rtso::civ_building_model(pciv, rtso::BLD_TOWER)));
			}
			// Pieces of this wall don't block each other; the towers it snaps onto don't block it.
			const uint32_t snapped[2] = {start_tower, end_tower};
			const float yaw = std::atan2(drag_curr_pos.x - wall_drag_start_pos.x, drag_curr_pos.z - wall_drag_start_pos.z);
			for (size_t i = 0; i < pieces.size(); ++i) {
				const auto &p = pieces[i];
				const bool is_tower = p.type == wall_set.tower && p.half_w == wall_set.tower_half;
				MeshInstance3D *g = wall_ghosts[i];
				// Along the wall (the box's z): the segment's length; across it: its 0 A.D. depth.
				if (is_tower && tower_entry.valid && wall_kind == rtso::WALLSET_STONE) {
					g->set_mesh(tower_entry.mesh);
					g->set_scale(Vector3(tower_entry.scale, tower_entry.scale, tower_entry.scale));
				} else {
					Ref<BoxMesh> box;
					box.instantiate();
					box->set_size(is_tower ? Vector3(2.2f * p.half_w, 4.5f, 2.2f * p.half_w)
							: Vector3(2.0f * wall_set.half_depth, 2.5f, 2.0f * p.half_w));
					g->set_mesh(box);
					g->set_scale(Vector3(1.0f, 1.0f, 1.0f));
				}
				g->set_global_position(Vector3(p.x, (sim ? sim->get_terrain_height(p.x, p.z) : 0.0f) + 0.05f, p.z));
				g->set_rotation(Vector3(0.0f, yaw, 0.0f));
				const bool ok = sim && sim->check_wall_piece(local_player(), p, snapped, 2) == rtso::Simulation::PLACE_OK;
				g->set_material_override(ok ? ghost_mat_ok : ghost_mat_bad);
				g->set_visible(true);
			}
			for (size_t i = pieces.size(); i < wall_ghosts.size(); ++i) {
				if (wall_ghosts[i] && wall_ghosts[i]->is_visible()) {
					wall_ghosts[i]->set_visible(false);
				}
			}
		} else {
			for (auto *wg : wall_ghosts) {
				if (wg && wg->is_visible()) wg->set_visible(false);
			}
			if (ghost_mesh) {
				ghost_mesh->set_visible(true);
				auto [cell_x, cell_y] = mouse_cell(mouse_pos);
				if (build_click_pending) {
					cell_x = build_click_cell_x;
					cell_y = build_click_cell_y;
					// Dragging turns the building toward the cursor; back near the
					// click it snaps to the default facing (input.js getMaxDragDelta).
					if (build_click_screen.distance_to(mouse_pos) >= 4.0f) {
						const Vector3 cur = get_ground_click_pos(mouse_pos);
						placement_angle = std::atan2(cur.x - build_click_ground.x, cur.z - build_click_ground.z);
					} else {
						placement_angle = rtso::Simulation::DEFAULT_BUILDING_ROT;
					}
				}
				int footprint = rtso::building_type_info(current_build_type).footprint;

				float gx = (cell_x + footprint * 0.5f) * rtso::Simulation::GRID;
				float gz = (cell_y + footprint * 0.5f) * rtso::Simulation::GRID;
				ghost_mesh->set_global_position(Vector3(gx, sim->get_terrain_height(gx, gz) + 0.05f, gz));
				const float ghost_angle = placement_angle_at(current_build_type, gx, gz);
				ghost_mesh->set_rotation(Vector3(0.0f, ghost_angle - rtso::Simulation::DEFAULT_BUILDING_ROT, 0.0f));

				bool can_build = sim->check_placement(local_player(), cell_x, cell_y, current_build_type, ghost_angle) ==
						rtso::Simulation::PLACE_OK;
				ghost_mesh->set_material_override(can_build ? ghost_mat_ok : ghost_mat_bad);
			}
		}
	} else {
		if (ghost_mesh && ghost_mesh->is_visible()) ghost_mesh->set_visible(false);
		for (auto *wg : wall_ghosts) {
			if (wg && wg->is_visible()) wg->set_visible(false);
		}
	}

	// Update 3D Rally Point Marker
	rtso::update_rally_marker(rally_flag, sim, selected_building_id, local_player());
	range_overlays_.tick((float)delta);
	range_overlays_.update(sim, local_player(), selected_building_id, selected_ids.ptr(), selected_ids.size());

	if ((sim->is_victory() || sim->is_game_over()) && !summary_shown_) {
		summary_shown_ = true;
		write_replay(); // the finished match, with its final statistics
	}
	tick_quit_confirmation();

	update_observer_view_picker();
	// Check Victory / Defeat Condition, from the local player's seat
	if (sim->is_victory_for(seat_player())) {
		banner_panel->set_visible(true);
		banner_label->set_text("VICTORY!\nYour side has prevailed.");
		banner_label->add_theme_color_override("font_color", Color(0.3f, 1.0f, 0.5f));
	} else if ((sim->is_victory() || sim->is_game_over()) && seat_player() >= rtso::MAX_PLAYERS) {
		// An observer has no side: name the winners instead (0 A.D. summary "Winner").
		String winners;
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p)
			if ((sim->winner_mask() >> p) & 1) winners += (winners.is_empty() ? "" : ", ") + player_display_name(p);
		banner_panel->set_visible(true);
		banner_label->set_text("MATCH OVER\n" + (winners.is_empty() ? String("No one prevailed.") : winners + String(" prevailed.")));
		banner_label->add_theme_color_override("font_color", Color(0.85f, 0.85f, 0.95f));
	} else if (sim->is_victory() || sim->is_game_over()) {
		banner_panel->set_visible(true);
		banner_label->set_text("DEFEAT!\nYour side has been defeated.");
		banner_label->add_theme_color_override("font_color", Color(1.0f, 0.3f, 0.3f));
	} else if (sim->is_defeated(seat_player())) {
		// 0 A.D.: a defeated player watches the rest of the match with the map revealed.
		// The notice shows for a few seconds, then gets out of the way.
		static double defeated_at = -1.0;
		if (defeated_at < 0.0 || match_elapsed_time < defeated_at) defeated_at = match_elapsed_time;
		banner_panel->set_visible(match_elapsed_time - defeated_at < 6.0);
		banner_label->set_text("DEFEATED\nYou are now observing. The match goes on.");
		banner_label->add_theme_color_override("font_color", Color(1.0f, 0.75f, 0.3f));
	} else if ((local_observer_ || (net_lockstep() && seat_player() >= rtso::MAX_PLAYERS)) && !turn_mgr_.is_replaying()) {
		// Every Game Setup slot was AI, or this client joined to watch: say so
		// for the first few seconds.
		banner_panel->set_visible(match_elapsed_time < 6.0);
		banner_label->set_text(local_observer_ ? "OBSERVING\nEvery player is an AI. The map is revealed."
				: "OBSERVING\nYou are watching this match. The map is revealed.");
		banner_label->add_theme_color_override("font_color", Color(0.85f, 0.85f, 0.95f));
	}
}

bool RtsoMain::spectating() const {
	if (turn_mgr_.is_replaying() || !sim) return false;
	const uint8_t seat = seat_player();
	return seat >= rtso::MAX_PLAYERS || sim->is_defeated(seat);
}

// 0 A.D. session "viewPlayer" dropdown (gui/session/top_panel/PlayerViewControl):
// an observer, or a player once defeated, watches the whole map or follows
// one player's sight and panels. Shown from the moment this client spectates.
void RtsoMain::update_observer_view_picker() {
	if (!spectating()) {
		if (observer_view_picker_) observer_view_picker_->get_parent()->call("set_visible", false);
		return;
	}
	if (!observer_view_picker_) {
		HBoxContainer *row = memnew(HBoxContainer);
		row->set_position(Vector2(16.0f, 64.0f));
		Label *caption = memnew(Label);
		caption->set_text("Player view:");
		row->add_child(caption);
		observer_view_picker_ = memnew(OptionButton);
		observer_view_picker_->add_item("Observer", 0);
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
			if (!sim->player_in_match(p) && !((sim->participant_mask() >> p) & 1)) continue;
			observer_view_picker_->add_item(player_display_name(p) + " (" + String(rtso::civ_info(sim->player(p).civ).name) + ")", p + 1);
		}
		observer_view_picker_->set_focus_mode(Control::FOCUS_NONE);
		observer_view_picker_->connect("item_selected", Callable(this, "_on_observer_view_selected"));
		row->add_child(observer_view_picker_);
		CanvasLayer *layer = memnew(CanvasLayer);
		layer->set_layer(5);
		add_child(layer);
		layer->add_child(row);
		set_observer_view(-1);
	}
	observer_view_picker_->get_parent()->call("set_visible", true);
}

void RtsoMain::_on_observer_view_selected(int index) {
	if (observer_view_picker_ && index >= 0 && index < observer_view_picker_->get_item_count())
		set_observer_view(observer_view_picker_->get_item_id(index) - 1);
}

void RtsoMain::set_observer_view(int player) {
	observer_view_ = player >= 0 && player < rtso::MAX_PLAYERS ? (uint8_t)player : rtso::OBSERVER_VIEW;
	clear_selection();
	if (observer_view_picker_)
		for (int i = 0; i < observer_view_picker_->get_item_count(); ++i)
			if (observer_view_picker_->get_item_id(i) == (observer_view_ == rtso::OBSERVER_VIEW ? 0 : observer_view_ + 1))
				observer_view_picker_->select(i);
}

Vector3 RtsoMain::get_ground_click_pos(Vector2 mouse_pos) const {
	if (!cam_rig || !cam_rig->get_camera()) return Vector3();
	Camera3D *cam = cam_rig->get_camera();
	Vector3 ray_origin = cam->project_ray_origin(mouse_pos);
	Vector3 ray_dir = cam->project_ray_normal(mouse_pos);
	if (std::abs(ray_dir.y) < 0.0001f) return Vector3();
	const float t_plane = -ray_origin.y / ray_dir.y;
	// The ground the player sees is the heightmap, not y = 0 (0 A.D.'s
	// GetTerrainCursorPosition): walk the ray down until it passes under the
	// terrain, then bisect that step. Without a heightmap, or when the ray
	// never meets it, the y = 0 plane stands in.
	if (sim && ray_dir.y < 0.0f && !sim->terrain_heights().empty()) {
		constexpr float STEP = 0.5f;
		const float t_end = std::max(t_plane, 0.0f) + 400.0f;
		float prev = 0.0f;
		for (float t = 0.0f; t <= t_end; t += STEP) {
			const Vector3 p = ray_origin + ray_dir * t;
			if (p.y > sim->get_terrain_height(p.x, p.z)) {
				prev = t;
				continue;
			}
			float lo = prev, hi = t;
			for (int i = 0; i < 12; ++i) {
				const float mid = 0.5f * (lo + hi);
				const Vector3 m = ray_origin + ray_dir * mid;
				(m.y > sim->get_terrain_height(m.x, m.z) ? lo : hi) = mid;
			}
			const Vector3 hit = ray_origin + ray_dir * hi;
			return Vector3(hit.x, sim->get_terrain_height(hit.x, hit.z), hit.z);
		}
	}
	return ray_origin + ray_dir * t_plane;
}

// The local player's structure whose box the mouse ray enters first: its
// turned footprint (hu, hv) from the ground up to a height that grows with its
// size, so a click on a roof selects it and not the ground behind it.
uint32_t RtsoMain::building_under_mouse(Vector2 mouse_pos, float *hit_t) const {
	if (!sim || !cam_rig || !cam_rig->get_camera()) return 0;
	Camera3D *cam = cam_rig->get_camera();
	const Vector3 o = cam->project_ray_origin(mouse_pos);
	const Vector3 d = cam->project_ray_normal(mouse_pos);
	uint32_t best = 0;
	float best_t = 1e30f;
	for (const auto &b : sim->buildings()) {
		if (b.team != local_player()) continue;
		const float hu = (b.hu > 0.0f ? b.hu : b.half_size) + 0.3f;
		const float hv = (b.hv > 0.0f ? b.hv : b.half_size) + 0.3f;
		const float base = sim->get_terrain_height(b.x, b.z);
		// The model's own height (ModelLibrary bounds), else one from its size.
		float height = std::clamp(std::max(hu, hv) * 1.2f, 1.5f, 8.0f);
		const auto model = rtso::ModelLibrary::get("buildings", String(rtso::civ_building_model(sim->team_civ(b.team), b.type)));
		if (model.valid && model.aabb.size.y > 0.1f)
			height = model.y_offset + model.aabb.size.y * (model.scale > 0.05f ? model.scale : 1.0f);
		const float top = base + height;
		// The ray in the structure's frame: u along (ux, uz), v across it.
		const float px = o.x - b.x, pz = o.z - b.z;
		const float po[3] = {px * b.ux + pz * b.uz, o.y, -px * b.uz + pz * b.ux};
		const float pd[3] = {d.x * b.ux + d.z * b.uz, d.y, -d.x * b.uz + d.z * b.ux};
		const float lo[3] = {-hu, base - 0.5f, -hv}, hi[3] = {hu, top, hv};
		float t0 = 0.0f, t1 = 1e30f;
		bool hit = true;
		for (int a = 0; a < 3 && hit; ++a) {
			if (std::fabs(pd[a]) < 1e-6f) {
				hit = po[a] >= lo[a] && po[a] <= hi[a];
				continue;
			}
			float ta = (lo[a] - po[a]) / pd[a], tb = (hi[a] - po[a]) / pd[a];
			if (ta > tb) std::swap(ta, tb);
			t0 = std::max(t0, ta);
			t1 = std::min(t1, tb);
			hit = t0 <= t1;
		}
		if (hit && t0 < best_t) {
			best_t = t0;
			best = b.id;
		}
	}
	if (hit_t) *hit_t = best_t;
	return best;
}

std::pair<int, int> RtsoMain::mouse_cell(Vector2 mouse_pos) const {
	Vector3 pos = get_ground_click_pos(mouse_pos);
	int cx = (int)std::floor(pos.x / rtso::Simulation::GRID);
	int cy = (int)std::floor(pos.z / rtso::Simulation::GRID);
	return {cx, cy};
}

void RtsoMain::update_hud() {
	top_panel_.update(sim, local_player(), match_elapsed_time);
	// GameSpeedButton is hidden in networked games.
	if (speed_btn) speed_btn->set_visible(!net_lockstep());
}

// Selection details (0 A.D. gui/session/selection_details.js): real values
// only. Health always; capture points for capturable structures; experience
// toward the next rank for units (Simulation::promotion_required_xp);
// construction, upgrade or research progress for structures; aura names from
// the unit's roster template; carried resources. Several units show the
// multi-selection icon grid (selection_panels.js "Selection") and a structure
// with a garrison shows the garrison grid ("Garrison").
// EntitySelection.update (gui/session/selection.js): units that died leave the
// selection, so its count and groups follow the battle.
// The player's own unpenned animal nearest (x, z) within a click's reach, or 0.
uint32_t RtsoMain::own_fauna_at(float x, float z) const {
	if (!sim) return 0;
	auto fview = sim->fauna();
	uint32_t best = 0;
	float best_d2 = 1.5f * 1.5f;
	for (size_t f = 0; f < fview.count; ++f) {
		if (!sim->fauna_selectable_by(local_player(), fview.id[f])) continue;
		const float dx = fview.pos_x[f] - x, dz = fview.pos_z[f] - z, d2 = dx * dx + dz * dz;
		if (d2 < best_d2) {
			best_d2 = d2;
			best = fview.id[f];
		}
	}
	return best;
}

void RtsoMain::prune_selection() {
	if (!sim || selected_ids.is_empty()) return;
	auto uview = sim->units();
	std::unordered_set<int32_t> wanted(selected_ids.ptr(), selected_ids.ptr() + selected_ids.size());
	std::unordered_set<int32_t> living;
	for (size_t i = 0; i < uview.count; ++i)
		if (uview.alive[i] && wanted.count((int32_t)uview.id[i])) living.insert((int32_t)uview.id[i]);
	// The player's own livestock stays selected until it dies or is penned.
	for (int32_t id : wanted)
		if (!living.count(id) && sim->fauna_selectable_by(local_player(), (uint32_t)id)) living.insert(id);
	if ((int64_t)living.size() == selected_ids.size()) return;
	PackedInt32Array kept;
	for (int s = 0; s < selected_ids.size(); ++s)
		if (living.count(selected_ids[s])) kept.push_back(selected_ids[s]);
	selected_ids = kept;
}

// What a click on (x, z) inspects when it hits nothing of the player's own: another
// player's unit, an animal, another player's structure or a resource, nearest first by
// that order, and only what the player sees (units, animals) or has explored.
bool RtsoMain::inspect_at(const Vector3 &click_pos) {
	if (!sim) return false;
	const uint8_t vp = view_player();
	const bool all = sim->reveal_map() || vp >= rtso::MAX_PLAYERS;
	auto seen = [&](float x, float z, uint8_t team) { return all || sim->is_entity_visible_to_viewer(x, z, team, vp); };
	auto explored = [&](float x, float z) { return all || sim->fow_state_at(x, z, vp) != rtso::Simulation::FOW_UNEXPLORED; };
	auto pick = [&](uint8_t kind, uint32_t id) {
		inspected_kind_ = kind;
		inspected_id_ = id;
		return true;
	};
	const float cx = click_pos.x, cz = click_pos.z;
	{
		auto uv = sim->units();
		size_t best = (size_t)-1;
		float best_d2 = 1.4f * 1.4f;
		for (size_t i = 0; i < uv.count; ++i) {
			if (!uv.alive[i] || uv.team[i] == local_player() || !seen(uv.pos_x[i], uv.pos_z[i], uv.team[i])) continue;
			const float dx = uv.pos_x[i] - cx, dz = uv.pos_z[i] - cz, d2 = dx * dx + dz * dz;
			if (d2 < best_d2) { best_d2 = d2; best = i; }
		}
		if (best != (size_t)-1) return pick(INSPECT_UNIT, uv.id[best]);
	}
	{
		auto fv = sim->fauna();
		size_t best = (size_t)-1;
		float best_d2 = 1.6f * 1.6f;
		for (size_t f = 0; f < fv.count; ++f) {
			if (!fv.alive[f] || fv.behavior[f] == rtso::FAUNA_CORRALLED) continue;
			if (!seen(fv.pos_x[f], fv.pos_z[f], fv.owner ? fv.owner[f] : rtso::GAIA_PLAYER)) continue;
			const float dx = fv.pos_x[f] - cx, dz = fv.pos_z[f] - cz, d2 = dx * dx + dz * dz;
			if (d2 < best_d2) { best_d2 = d2; best = f; }
		}
		if (best != (size_t)-1) return pick(INSPECT_FAUNA, fv.id[best]);
	}
	for (const auto &b : sim->buildings()) {
		if (b.team == local_player() || b.wrecked || !explored(b.x, b.z)) continue;
		const float hr = (b.hu > 0.0f ? std::max(b.hu, b.hv) : b.half_size) + 0.4f;
		const float dx = b.x - cx, dz = b.z - cz;
		if (dx * dx + dz * dz <= hr * hr) return pick(INSPECT_BUILDING, b.id);
	}
	{
		auto rv = sim->resources();
		size_t best = (size_t)-1;
		float best_d2 = 1.3f * 1.3f;
		for (size_t r = 0; r < rv.count; ++r) {
			if (!rv.alive[r] || rv.subtype[r] == rtso::RES_SUB_GRAIN || !explored(rv.x[r], rv.z[r])) continue;
			const float dx = rv.x[r] - cx, dz = rv.z[r] - cz, d2 = dx * dx + dz * dz;
			if (d2 < best_d2) { best_d2 = d2; best = r; }
		}
		if (best != (size_t)-1) return pick(INSPECT_RESOURCE, rv.id[best]);
	}
	inspected_kind_ = INSPECT_NONE;
	return false;
}

namespace {
String inspect_armour(float hack, float pierce, float crush) {
	return "Armor: " + String::num(hack, 0) + " hack, " + String::num(pierce, 0) + " pierce, " + String::num(crush, 0) + " crush";
}
String flora_title(uint8_t flora) {
	static const char *const kNames[] = {"Tree", "Tree", "Pine", "Date Palm", "Oak", "Baobab", "Acacia", "Bamboo", "Bush",
		"Berry Bush", "Apple Tree", "Date Palm", "Carob", "Poplar", "Fan Palm", "Beech", "Dead Tree", "Cypress",
		"Aleppo Pine", "Fir", "Tropical Bush", "Olive", "Banyan", "Tropical Palm", "Maple", "Juniper", "Teak"};
	return flora < sizeof(kNames) / sizeof(kNames[0]) ? String(kNames[flora]) : String("Tree");
}
} // namespace

bool RtsoMain::fill_inspected_details() {
	if (!sim || inspected_kind_ == INSPECT_NONE) return false;
	const uint8_t vp = view_player();
	const bool all = sim->reveal_map() || vp >= rtso::MAX_PLAYERS;
	auto bar = [](ProgressBar *pb, Label *lbl, bool on, float value = 0.0f, float max = 1.0f, const String &text = String()) {
		pb->set_visible(on);
		lbl->set_visible(on);
		if (!on) return;
		pb->set_max(max > 0.0f ? max : 1.0f);
		pb->set_value(value);
		lbl->set_text(text);
	};
	auto owner_text = [&](uint8_t owner) {
		return owner >= rtso::MAX_PLAYERS ? String("Gaia")
				: player_display_name(owner) + " (" + String(rtso::civ_info(sim->player_civ(owner)).name) + ")";
	};
	auto health = [&](float hp, float max) {
		bar(inspector_hp_bar, inspector_hp_label, max > 0.0f, hp, max,
				"Health: " + String::num((double)hp, 0) + " / " + String::num((double)max, 0));
	};
	bar(inspector_capture_bar, inspector_capture_label, false);
	bar(inspector_xp_bar, inspector_xp_label, false);
	bar(inspector_aura_bar, inspector_aura_label, false);
	inspector_extra->set_text("");

	if (inspected_kind_ == INSPECT_UNIT) {
		auto uv = sim->units();
		size_t u = 0;
		for (; u < uv.count; ++u)
			if (uv.id[u] == inspected_id_) break;
		if (u == uv.count || !uv.alive[u] || (!all && !sim->is_entity_visible_to_viewer(uv.pos_x[u], uv.pos_z[u], uv.team[u], vp)))
			return false;
		const uint8_t utype = uv.unit_type[u];
		const uint16_t tmpl = uv.tmpl ? uv.tmpl[u] : rtso::CIV_UNIT_TEMPLATE_NONE;
		const auto &uinfo = tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? rtso::civ_template_unit_info(tmpl, utype) : rtso::unit_type_info(utype);
		inspector_portrait->set_texture(load_tex("res://assets/textures/ui/portraits/" + String(rtso::unit_type_portrait(utype)) + ".png"));
		static const char *kRank[] = {"", "", "Advanced ", "Elite "};
		inspector_title->set_text(String(kRank[std::min(std::max(1, (int)uv.rank[u]), 3)]) + String::utf8(uinfo.name));
		inspector_class->set_text(owner_text(uv.team[u]));
		health(uv.hp[u], uv.max_hp[u]);
		String stats;
		if (uinfo.attack_damage > 0.0f) stats = "Attack: " + String::num(uinfo.attack_damage, 1) + ", interval " + String::num(uinfo.attack_cooldown, 2) + " s\n";
		inspector_stats->set_text(stats + inspect_armour(uinfo.hack_armor, uinfo.pierce_armor, uinfo.crush_armor));
		return true;
	}
	if (inspected_kind_ == INSPECT_FAUNA) {
		auto fv = sim->fauna();
		size_t f = 0;
		for (; f < fv.count; ++f)
			if (fv.id[f] == inspected_id_) break;
		const uint8_t owner = f < fv.count && fv.owner ? fv.owner[f] : rtso::GAIA_PLAYER;
		if (f == fv.count || !fv.alive[f] || fv.behavior[f] == rtso::FAUNA_CORRALLED ||
				(!all && !sim->is_entity_visible_to_viewer(fv.pos_x[f], fv.pos_z[f], owner, vp)))
			return false;
		const auto &fi = rtso::fauna_type_info(fv.fauna_type[f]);
		inspector_portrait->set_texture(Ref<Texture2D>());
		inspector_title->set_text(String::utf8(fi.name));
		inspector_class->set_text(owner_text(owner));
		health(fv.hp[f], fv.max_hp[f]);
		String stats;
		if (fi.attack_damage() > 0.0f) stats = "Attack: " + String::num(fi.attack_damage(), 1) + ", interval " + String::num(fi.attack_cooldown, 2) + " s\n";
		inspector_stats->set_text(stats + inspect_armour(fi.hack_armor, fi.pierce_armor, fi.crush_armor));
		if (fv.food_amount[f] > 0) inspector_extra->set_text("Food: " + String::num_int64(fv.food_amount[f]));
		return true;
	}
	if (inspected_kind_ == INSPECT_BUILDING) {
		const rtso::Simulation::Building *found = nullptr;
		for (const auto &b : sim->buildings())
			if (b.id == inspected_id_) { found = &b; break; }
		if (!found || found->wrecked || found->hp <= 0.0f) return false;
		const auto &binfo = rtso::building_type_info(found->type);
		inspector_portrait->set_texture(load_tex("res://assets/textures/ui/portraits/" + String(binfo.portrait_id) + ".png"));
		inspector_title->set_text(String(binfo.name));
		inspector_class->set_text(owner_text(found->team));
		health(found->hp, found->max_hp);
		if (found->max_capture_points > 0.0f)
			bar(inspector_capture_bar, inspector_capture_label, true, found->capture_points, found->max_capture_points,
					"Capture points: " + String::num((double)found->capture_points, 0) + " / " + String::num((double)found->max_capture_points, 0));
		inspector_stats->set_text(found->build_progress < 1.0f
				? "Construction: " + String::num((double)(found->build_progress * 100.0f), 0) + "%" : String());
		return true;
	}
	// A resource: its kind and what is left (selection_details.js resourceSupply).
	auto rv = sim->resources();
	size_t r = 0;
	for (; r < rv.count; ++r)
		if (rv.id[r] == inspected_id_) break;
	if (r == rv.count || !rv.alive[r] || rv.amount[r] <= 0) return false;
	String title, kind;
	switch (rv.type[r]) {
	case rtso::RES_WOOD: title = flora_title(rv.flora ? rv.flora[r] : 0); kind = "Wood"; break;
	case rtso::RES_GOLD: title = rv.subtype[r] == rtso::RES_SUB_RUINS ? "Ruins" : "Metal Mine"; kind = "Metal"; break;
	case rtso::RES_STONE: title = rv.subtype[r] == rtso::RES_SUB_RUINS ? "Ruins" : "Stone Quarry"; kind = "Stone"; break;
	default:
		title = rv.subtype[r] == rtso::RES_SUB_MEAT ? String("Carcass") : rv.subtype[r] == rtso::RES_SUB_FISH ? String("Fish")
				: flora_title(rv.flora ? rv.flora[r] : rtso::FLORA_BERRY);
		kind = "Food";
	}
	inspector_portrait->set_texture(Ref<Texture2D>());
	inspector_title->set_text(title);
	inspector_class->set_text("Gaia");
	bar(inspector_hp_bar, inspector_hp_label, false);
	inspector_stats->set_text(kind + ": " + String::num_int64(rv.amount[r]) + "\nGatherers: up to " +
			String::num_int64(sim->resource_max_gatherers(rv.id[r])));
	return true;
}

// The player's own selected livestock (0 A.D. livestock are units): one animal's details,
// or a count for several. False when the selection holds anything else.
bool RtsoMain::fill_livestock_details() {
	if (!sim || selected_ids.is_empty()) return false;
	for (int s = 0; s < selected_ids.size(); ++s)
		if (!sim->fauna_selectable_by(local_player(), (uint32_t)selected_ids[s])) return false;
	const uint8_t keep_kind = inspected_kind_;
	const uint32_t keep_id = inspected_id_;
	inspected_kind_ = INSPECT_FAUNA;
	inspected_id_ = (uint32_t)selected_ids[0];
	const bool ok = fill_inspected_details();
	inspected_kind_ = keep_kind;
	inspected_id_ = keep_id;
	if (ok && selected_ids.size() > 1) inspector_title->set_text(String::num_int64(selected_ids.size()) + " animals selected");
	return ok;
}

void RtsoMain::update_inspector() {
	if (!inspector_panel) return;
	auto show_bar = [](ProgressBar *bar, Label *lbl, bool v) {
		bar->set_visible(v);
		lbl->set_visible(v);
	};
	auto set_bar = [&](ProgressBar *bar, Label *lbl, float value, float max, const String &text) {
		show_bar(bar, lbl, true);
		bar->set_max(max > 0.0f ? max : 1.0f);
		bar->set_value(value);
		lbl->set_text(text);
	};
	auto armour_text = [](float hack, float pierce, float crush) {
		return "Armor: " + String::num(hack, 0) + " hack, " + String::num(pierce, 0) + " pierce, " + String::num(crush, 0) + " crush";
	};
	// The stamina bar has no 0 A.D. counterpart.
	show_bar(inspector_stamina_bar, inspector_stamina_label, false);
	// Each grid shows if a branch below asks for it; the guard applies the
	// result once, whichever way update_inspector returns.
	bool show_selection_grid = false, show_garrison_grid = false;
	struct GridVisibility {
		RtsoSelectionGrid &sel, &gar;
		bool &show_sel, &show_gar;
		~GridVisibility() {
			sel.set_visible(show_sel);
			gar.set_visible(show_gar);
		}
	} grid_visibility{selection_grid_, garrison_grid_, show_selection_grid, show_garrison_grid};

	if (selected_building_id != 0) {
		const rtso::Simulation::Building *found_b = nullptr;
		for (const auto &b : sim->buildings()) {
			if (b.id == selected_building_id) { found_b = &b; break; }
		}

		if (found_b) {
			const auto &binfo = rtso::building_type_info(found_b->type);
			inspector_portrait->set_texture(load_tex("res://assets/textures/ui/portraits/" + String(binfo.portrait_id) + ".png"));
			inspector_title->set_text(String(binfo.name));
			const uint8_t owner = found_b->team;
			String owner_name = owner == rtso::GAIA_PLAYER ? String("Gaia")
					: player_display_name(owner) + " (" + String(rtso::civ_info(sim->player_civ(owner)).name) + ")";
			inspector_class->set_text(owner_name + (found_b->garrison_cap > 0
					? " - Garrison: " + String::num_int64(found_b->garrisoned_units.size()) + " / " + String::num_int64(found_b->garrison_cap)
					: String()));

			set_bar(inspector_hp_bar, inspector_hp_label, found_b->hp, found_b->max_hp,
					"Health: " + String::num((double)found_b->hp, 0) + " / " + String::num((double)found_b->max_hp, 0));

			if (found_b->max_capture_points > 0.0f)
				set_bar(inspector_capture_bar, inspector_capture_label, found_b->capture_points, found_b->max_capture_points,
						"Capture points: " + String::num((double)found_b->capture_points, 0) + " / " + String::num((double)found_b->max_capture_points, 0));
			else
				show_bar(inspector_capture_bar, inspector_capture_label, false);

			if (found_b->build_progress < 1.0f) {
				set_bar(inspector_xp_bar, inspector_xp_label, found_b->build_progress, 1.0f,
						"Construction: " + String::num((double)(found_b->build_progress * 100.0f), 0) + "%");
			} else if (found_b->upgrade_to != rtso::BLD_TYPE_COUNT && found_b->upgrade_duration > 0.0f) {
				set_bar(inspector_xp_bar, inspector_xp_label, found_b->upgrade_timer, found_b->upgrade_duration,
						"Upgrading to " + String(rtso::building_type_name(found_b->upgrade_to)) + " (" +
						String::num(found_b->upgrade_duration - found_b->upgrade_timer, 0) + " s left)");
			} else if (const uint8_t rt = rtso::Simulation::researching(*found_b); rt != rtso::TECH_NONE && found_b->queue.front().duration > 0.0f) {
				const rtso::ProductionOrder &po = found_b->queue.front();
				set_bar(inspector_xp_bar, inspector_xp_label, po.timer, po.duration,
						"Researching " + String(rtso::tech_info(rt).name) + " (" +
						String::num(po.duration - po.timer, 0) + " s left)");
			} else {
				show_bar(inspector_xp_bar, inspector_xp_label, false);
			}
			show_bar(inspector_aura_bar, inspector_aura_label, false);

			String status;
			if (rtso::is_gate_type(found_b->type))
				status = found_b->gate_locked ? "Gate: locked" : (found_b->gate_open ? "Gate: open" : "Gate: closed");
			else if (!found_b->queue.empty())
				status = "Production: " + String::num_int64(found_b->queue.size()) + " / " + String::num_int64((int64_t)rtso::PRODUCTION_QUEUE_MAX) +
						" in queue" + (found_b->autoqueue ? " (autoqueue on)" : "") + " (right-click a slot to cancel)";
			if (const rtso::BuildingAIInfo &ai = rtso::building_ai_info(found_b->type); ai.shoots())
				status += String(status.is_empty() ? "" : "\n") + "Attack: " + String::num_int64(sim->building_arrow_count(*found_b)) + " arrows of " +
						String::num(ai.pierce, 0) + " pierce every " + String::num(ai.repeat_time, 1) + " s, range " +
						String::num(sim->building_arrow_range(*found_b) * rtso::AttackDetection::kMetresPerUnit, 0) + " m";
			inspector_stats->set_text(status);
			// Animals penned in a corral (GarrisonHolder List Animal) unload with the same button.
			const int penned = found_b->type == rtso::BLD_CORRAL ? sim->corral_count(found_b->id) : 0;
			String extra = found_b->has_rally_point ? "Rally point set" : "";
			if (penned > 0)
				extra += String(extra.is_empty() ? "" : "\n") + "Penned animals: " + String::num_int64(penned) + " / " +
						String::num_int64(rtso::CORRAL_GARRISON_MAX);
			inspector_extra->set_text(extra);

			// Garrison panel: one icon per unit type inside, click to unload one,
			// Shift+click (session.unloadtype) to unload that type, U unloads all. On an ally's
			// structure only our own units inside (Commands.js unload / unload-all-by-owner).
			const std::vector<uint32_t> mine = sim->unloadable_by(local_player(), found_b->id);
			if (!mine.empty()) {
				auto uview = sim->units();
				garrison_grid_.update(RtsoSelectionGrid::group_units(uview, mine.data(), mine.size()),
						"Click to unload one, Shift+click to unload all of this type.");
				show_garrison_grid = true;
			}
			ungarrison_btn->set_text("Unload all (" + String(rtso::hotkeys::table().label("session.unload").c_str()) + ")");
			ungarrison_btn->set_visible(!mine.empty() || (penned > 0 && owner == local_player()));
			inspector_panel->set_visible(true);
			return;
		}
	}

	if (selected_ids.is_empty()) {
		ungarrison_btn->set_visible(false);
		if (inspected_kind_ != INSPECT_NONE && !fill_inspected_details()) inspected_kind_ = INSPECT_NONE;
		inspector_panel->set_visible(inspected_kind_ != INSPECT_NONE);
		return;
	}
	if (fill_livestock_details()) {
		ungarrison_btn->set_visible(false);
		inspector_panel->set_visible(true);
		return;
	}
	auto uview = sim->units();
	const uint32_t *ids = (const uint32_t *)selected_ids.ptr();
	const auto groups = RtsoSelectionGrid::group_units(uview, ids, selected_ids.size());
	float hp_sum = 0, hp_max_sum = 0;
	for (const auto &g : groups) {
		hp_sum += g.hp;
		hp_max_sum += g.max_hp;
	}
	set_bar(inspector_hp_bar, inspector_hp_label, hp_sum, hp_max_sum,
			"Health: " + String::num((double)hp_sum, 0) + " / " + String::num((double)hp_max_sum, 0));
	show_bar(inspector_capture_bar, inspector_capture_label, false);

	if (selected_ids.size() == 1 && !groups.empty()) {
		size_t u = 0;
		for (; u < uview.count; ++u)
			if (uview.id[u] == (uint32_t)selected_ids[0]) break;
		if (u == uview.count) {
			inspector_panel->set_visible(false);
			return;
		}
		const uint8_t utype = uview.unit_type[u];
		const uint16_t tmpl = uview.tmpl ? uview.tmpl[u] : rtso::CIV_UNIT_TEMPLATE_NONE;
		const auto &uinfo = tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? rtso::civ_template_unit_info(tmpl, utype) : rtso::unit_type_info(utype);
		inspector_portrait->set_texture(load_tex("res://assets/textures/ui/portraits/" + String(rtso::unit_type_portrait(utype)) + ".png"));

		// 0 A.D. ranks: Basic, Advanced, Elite (Promotion.js).
		const int rank = std::max(1, (int)uview.rank[u]);
		static const char *kRank[] = {"", "", "Advanced ", "Elite "};
		String name = String(kRank[std::min(rank, 3)]) + String::utf8(uinfo.name);
		if (tmpl != rtso::CIV_UNIT_TEMPLATE_NONE) {
			const char *specific = rtso::civ_unit_template(tmpl).specific_name;
			if (specific && specific[0]) name += String::utf8(" (") + String::utf8(specific) + ")";
		}
		inspector_title->set_text(name);
		const uint8_t owner = uview.team[u];
		inspector_class->set_text(player_display_name(owner) + " (" + String(rtso::civ_info(sim->player_civ(owner)).name) +
				") - Kills: " + String::num_int64(uview.kills[u]));

		// Experience toward the next rank (Promotion.js GetRequiredXp after technologies).
		if (const float need = sim->promotion_required_xp(u); need > 0.0f) {
			set_bar(inspector_xp_bar, inspector_xp_label, (float)uview.xp[u], need,
					"Experience: " + String::num_int64(uview.xp[u]) + " / " + String::num(need, 0));
		} else {
			show_bar(inspector_xp_bar, inspector_xp_label, false);
		}

		String auras;
		if (tmpl != rtso::CIV_UNIT_TEMPLATE_NONE) {
			for (int16_t a : rtso::civ_unit_template(tmpl).auras) {
				if (a < 0) continue;
				if (!auras.is_empty()) auras += ", ";
				auras += String::utf8(rtso::civ_hero_aura(a).name);
			}
		}
		if (!auras.is_empty()) set_bar(inspector_aura_bar, inspector_aura_label, 1.0f, 1.0f, "Auras: " + auras);
		else show_bar(inspector_aura_bar, inspector_aura_label, false);

		// Attack and armour (gui/common/tooltips.js getAttackTooltip / getArmorTooltip).
		String stats;
		if (uinfo.attack_damage > 0.0f) {
			String dmg;
			auto add = [&](float v, const char *kind) {
				if (v <= 0.0f) return;
				if (!dmg.is_empty()) dmg += ", ";
				dmg += String::num(v, 1) + " " + kind;
			};
			add(uinfo.hack_damage, "hack");
			add(uinfo.pierce_damage, "pierce");
			add(uinfo.crush_damage, "crush");
			if (dmg.is_empty()) dmg = String::num(uinfo.attack_damage, 1);
			stats = "Attack: " + dmg + ", interval " + String::num(uinfo.attack_cooldown, 2) + " s";
			if (uinfo.attack_range > 2.5f) stats += ", range " + String::num(uinfo.attack_range * rtso::AttackDetection::kMetresPerUnit, 0) + " m";
			stats += "\n";
		}
		stats += armour_text(uinfo.hack_armor, uinfo.pierce_armor, uinfo.crush_armor);
		inspector_stats->set_text(stats);

		static const char *kRes[] = {"wood", "metal", "stone", "food"};
		String extra;
		uint32_t route_a = 0, route_b = 0;
		int route_leg = 0;
		if (rtso::unit_is_trader(uview.unit_type[u]) &&
				sim->trader_route(uview.id[u], route_a, route_b, route_leg)) {
			// selection_details.js: a trader shows its goods and their total gain.
			const rtso::Simulation::Building *ma = nullptr, *mb = nullptr;
			for (const auto &bb : sim->buildings()) {
				if (bb.id == route_a) ma = &bb;
				if (bb.id == route_b) mb = &bb;
			}
			if (ma && mb) {
				const auto g = sim->calculate_trade_gain(*ma, *mb, uview.team[u], uview.unit_type[u]);
				const uint8_t goods = uview.res_type_carried[u];
				extra = "Trading " + String(goods < rtso::RES_COUNT ? kRes[goods] : "goods") + ", gain " +
						String::num_int64(g.trader + g.market1 + g.market2) + " (" + String::num_int64(g.trader) +
						" to you)";
			} else {
				extra = "Trade route lost";
			}
		} else if (uview.carry[u] > 0 && uview.res_type_carried[u] < rtso::RES_COUNT)
			extra = "Carrying " + String::num_int64(uview.carry[u]) + " " + kRes[uview.res_type_carried[u]];
		if (sim->is_transport((uint32_t)selected_ids[0])) {
			const size_t p_count = sim->get_transport_passenger_count((uint32_t)selected_ids[0]);
			extra = "Passengers: " + String::num_int64(p_count) + " / " + String::num_int64(sim->get_transport_capacity((uint32_t)selected_ids[0]));
			ungarrison_btn->set_text("Unload all (" + String(rtso::hotkeys::table().label("session.unload").c_str()) + ")");
			ungarrison_btn->set_visible(p_count > 0);
		} else {
			ungarrison_btn->set_visible(false);
		}
		inspector_extra->set_text(extra);
	} else {
		inspector_portrait->set_texture(groups.empty() ? Ref<Texture2D>()
				: load_tex("res://assets/textures/ui/portraits/" + String(rtso::unit_type_portrait(groups.front().unit_type)) + ".png"));
		inspector_title->set_text(String::num_int64(selected_ids.size()) + " units selected");
		inspector_class->set_text(String::num_int64(groups.size()) + (groups.size() == 1 ? " type" : " types"));
		show_bar(inspector_xp_bar, inspector_xp_label, false);
		show_bar(inspector_aura_bar, inspector_aura_label, false);
		inspector_stats->set_text("");
		inspector_extra->set_text("");
		ungarrison_btn->set_visible(false);
		selection_grid_.update(groups, "Click to select only these, Ctrl+click or right-click to deselect them.");
		show_selection_grid = true;
	}
	inspector_panel->set_visible(true);
}

// Multi-selection icon click (0 A.D. selection_panels.js Selection.execute):
// select only that group, or drop it with session.deselectgroup (Ctrl).
// selection_panels.js Selection onPressRight: a right-click drops the group.
void RtsoMain::_on_selection_group_gui_input(const Ref<InputEvent> &event, int key) {
	Ref<InputEventMouseButton> mb = event;
	if (mb.is_null() || mb->get_button_index() != MOUSE_BUTTON_RIGHT || mb->is_pressed()) return;
	select_group(key, true);
}

void RtsoMain::_on_selection_group_clicked(int key) {
	select_group(key, rtso::hotkeys::held("session.deselectgroup"));
}

// makePrimarySelectionGroup (keep only the group) or removeFromSelectionGroup (drop it).
void RtsoMain::select_group(int key, bool drop) {
	if (!sim) return;
	auto uview = sim->units();
	PackedInt32Array kept;
	for (int s = 0; s < selected_ids.size(); ++s) {
		for (size_t i = 0; i < uview.count; ++i) {
			if (uview.id[i] != (uint32_t)selected_ids[s]) continue;
			const uint16_t tmpl = uview.tmpl ? uview.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE;
			const bool in_group = RtsoSelectionGrid::group_key(uview.unit_type[i], tmpl) == key;
			if (in_group != drop) kept.push_back(selected_ids[s]);
			break;
		}
	}
	selected_ids = kept;
}

// Garrison icon click (selection_panels.js Garrison.execute -> unloadTemplate):
// unload one unit of the group, or all of them with session.unloadtype (Shift).
void RtsoMain::_on_garrison_group_clicked(int key) {
	if (!sim || selected_building_id == 0) return;
	const std::vector<uint32_t> mine = sim->unloadable_by(local_player(), selected_building_id);
	const auto *inside = &mine;
	if (inside->empty()) return;
	const bool all = rtso::hotkeys::held("session.unloadtype");
	auto uview = sim->units();
	std::vector<uint32_t> out;
	for (uint32_t gid : *inside) {
		for (size_t i = 0; i < uview.count; ++i) {
			if (uview.id[i] != gid) continue;
			const uint16_t tmpl = uview.tmpl ? uview.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE;
			if (RtsoSelectionGrid::group_key(uview.unit_type[i], tmpl) == key) out.push_back(gid);
			break;
		}
		if (!all && !out.empty()) break;
	}
	if (out.empty()) return;
	if (turn_cmds())
		post_lockstep(13, out.data(), out.size(), 0, 0, selected_building_id, 0);
	else
		sim->cmd_unload(local_player(), selected_building_id, out.data(), out.size());
}

void RtsoMain::update_command_panel() {
	if (!command_panel) return;

	// g_SelectionPanels.Stance: shown when only units are selected, the stances
	// some selected unit holds highlighted (GuiInterface IsStanceSelected).
	if (stances_bar && sim) {
		bool held[5] = {};
		bool any = false;
		if (selected_building_id == 0 && !selected_ids.is_empty()) {
			auto uv = sim->units();
			for (int s = 0; s < selected_ids.size(); ++s)
				for (size_t i = 0; i < uv.count; ++i)
					if (uv.id[i] == (uint32_t)selected_ids[s]) {
						if (uv.stance[i] < 5) held[uv.stance[i]] = true;
						any = true;
						break;
					}
		}
		stances_bar->set_visible(any);
		for (int st = 0; st < 5; ++st)
			if (stance_buttons_[st] && stance_buttons_[st]->is_pressed() != held[st])
				stance_buttons_[st]->set_pressed_no_signal(held[st]);
	}

	// The buttons are built after the current ones each frame, then
	// keep_command_buttons() keeps whichever set is older when both look and
	// act the same. Replacing them every frame meant a click's press landed on
	// one Button and its release on the next, so pressed never fired.
	const int old_actions = actions_bar->get_child_count();
	const int old_production = production_bar->get_child_count();

	if (selected_building_id != 0) {
		const auto &bldgs = sim->buildings();
		const rtso::Simulation::Building *found_b = nullptr;
		for (const auto &b : bldgs) {
			if (b.id == selected_building_id) { found_b = &b; break; }
		}

		// Update 8-slot Visual Production Queue (ProductionQueue.js parity)
		production_queue_.update(found_b);
		update_roster_list(found_b);

		if (found_b && found_b->build_progress >= 1.0f) {
			uint8_t pciv = sim->player_civ(local_player());
			// Units train from the civ's own list only (update_roster_list, 0 A.D. ProductionQueue's
			// per-template Trainer/Entities), never from generic class buttons.

			// Technologies researched here (0 A.D. ProductionQueue "technologies"):
			// one button per tech whose researcher is this building type, greyed
			// out with the reason research_check() gives.
			for (int t = 1; t < rtso::TECH_COUNT; ++t) {
				const rtso::TechInfo &ti = rtso::tech_info((uint8_t)t);
				if (!rtso::building_researches_tech(found_b->type, (uint8_t)t, pciv) || !rtso::tech_available_to_civ((uint8_t)t, pciv)) continue;
				rtso::Simulation::ResearchCheck rc = sim->research_check(local_player(), (uint8_t)t, found_b->id);
				if (rc == rtso::Simulation::TECH_ERR_RESEARCHED || rc == rtso::Simulation::TECH_ERR_CIV
				    || rc == rtso::Simulation::TECH_ERR_PAIRED || rc == rtso::Simulation::TECH_ERR_DISABLED) continue;
				Button *btn = memnew(Button);
				String cost;
				auto add_cost = [&](int v, const char *suffix) {
					if (v <= 0) return;
					if (!cost.is_empty()) cost += ",";
					cost += String::num_int64(v) + suffix;
				};
				add_cost(ti.food_cost, "f");
				add_cost(ti.wood_cost, "w");
				add_cost(ti.gold_cost, "g");
				add_cost(ti.stone_cost, "s");
				btn->set_text(String(ti.name) + (cost.is_empty() ? String() : " (" + cost + ")"));
				if (t == rtso::TECH_PHASE_TOWN || t == rtso::TECH_PHASE_CITY) {
					btn->set_button_icon(load_tex(t == rtso::TECH_PHASE_TOWN
							? "res://assets/textures/ui/phases/town.png" : "res://assets/textures/ui/phases/city.png"));
					btn->set_icon_alignment(HORIZONTAL_ALIGNMENT_CENTER);
					btn->add_theme_constant_override("icon_max_width", 20);
				}
				String tip = String(ti.desc ? ti.desc : "");
				const char *why = nullptr;
				switch (rc) {
					case rtso::Simulation::TECH_ERR_IN_PROGRESS: why = "Already being researched"; break;
					case rtso::Simulation::TECH_ERR_PHASE:
						why = ti.required_phase >= 3 ? "Requires City Phase" : "Requires Town Phase"; break;
					case rtso::Simulation::TECH_ERR_PREREQUISITE: {
						tip += String("\nRequires ") + rtso::tech_info(ti.prerequisite).name;
						btn->set_disabled(true);
						break;
					}
					case rtso::Simulation::TECH_ERR_STRUCTURES:
						why = t == rtso::TECH_PHASE_CITY ? "Requires 3 Town Phase structures"
								: "Requires 5 Village Phase structures"; break;
					case rtso::Simulation::TECH_ERR_COST: why = "Not enough resources"; break;
					default: break;
				}
				if (why) {
					tip += String("\n") + why;
					if (rc != rtso::Simulation::TECH_ERR_COST) btn->set_disabled(true);
				}
				btn->set_tooltip_text(tip);
				btn->connect("pressed", Callable(this, "_on_tech_pressed").bind(t));
				production_bar->add_child(btn);
			}

			// Gate lock (0 A.D. Gate.js): a locked gate stays shut for everyone.
			if (found_b->team == local_player() && rtso::is_gate_type(found_b->type)) {
				Button *btn = memnew(Button);
				btn->set_text(found_b->gate_locked ? "Unlock gate" : "Lock gate");
				btn->set_tooltip_text(found_b->gate_locked ? "Let your and your allies' units through again"
						: "Keep the gate shut, even for your own units");
				btn->connect("pressed", Callable(this, "_on_gate_lock_pressed").bind(found_b->gate_locked ? 0 : 1));
				production_bar->add_child(btn);
			}

			// Dropsite sharing (0 A.D. ResourceDropsite.js): shown once the owner has Diaspora.
			if (found_b->team == local_player() && rtso::Simulation::is_dropsite_type(found_b->type) &&
					sim->player(local_player()).has_tech(rtso::TECH_UNLOCK_SHARED_DROPSITES)) {
				Button *btn = memnew(Button);
				btn->set_text(found_b->dropsite_shared ? "Stop sharing dropsite" : "Share dropsite");
				btn->set_tooltip_text(found_b->dropsite_shared ? "Allies can no longer drop resources here"
						: "Let allies with Diaspora drop resources here");
				btn->connect("pressed", Callable(this, "_on_dropsite_sharing_pressed").bind(found_b->dropsite_shared ? 0 : 1));
				production_bar->add_child(btn);
			}

			// Alert (0 A.D. AlertRaiser.js): civilians take shelter, then go back to work.
			rtso::AlertRaiserInfo alert;
			if (found_b->team == local_player() && found_b->build_progress >= 1.0f &&
					rtso::building_alert_raiser(found_b->type, alert)) {
				const char *who = alert.traders ? "traders" : "women";
				for (int end = 0; end < 2; ++end) {
					Button *btn = memnew(Button);
					btn->set_text(end ? "End alert" : "Raise alert");
					btn->set_tooltip_text(end ? String("Send nearby ") + who + " back to work"
							: String("Send nearby ") + who + " into the nearest structures with room");
					btn->connect("pressed", Callable(this, "_on_alert_pressed").bind(end));
					production_bar->add_child(btn);
				}
			}

			// Autoqueue (0 A.D. ProductionQueue.js): each finished batch is queued again.
			if (found_b->team == local_player() && found_b->build_progress >= 1.0f && roster_list &&
					roster_list->get_item_count() > 0) {
				Button *btn = memnew(Button);
				btn->set_text(found_b->autoqueue ? "Autoqueue: on" : "Autoqueue: off");
				btn->set_tooltip_text(found_b->autoqueue ? "Stop repeating the batches this structure trains"
						: "Queue each batch again when it finishes, paying for it again");
				btn->connect("pressed", Callable(this, "_on_autoqueue_pressed").bind(found_b->autoqueue ? 0 : 1));
				production_bar->add_child(btn);
			}

			// Structure upgrade (0 A.D. Upgrade.js), or cancel one in progress.
			if (found_b->team == local_player()) {
				if (found_b->upgrade_to != rtso::BLD_TYPE_COUNT) {
					Button *btn = memnew(Button);
					btn->set_text(String("Cancel upgrade to ") + rtso::building_type_name(found_b->upgrade_to));
					btn->set_tooltip_text("Stop upgrading and get the resources back");
					btn->connect("pressed", Callable(this, "_on_upgrade_pressed").bind(-1));
					production_bar->add_child(btn);
				} else {
					for (int choice = 0; choice < rtso::building_upgrade_count(found_b->type, pciv); ++choice) {
						const rtso::BuildingUpgradeInfo up = rtso::building_upgrade(found_b->type, pciv, choice);
						const rtso::Simulation::UpgradeCheck check = sim->upgrade_check(local_player(), found_b->id, choice);
						// Not this piece's (only wall_long and palisades_long become gates).
						if (check == rtso::Simulation::UPGRADE_ERR_NONE) continue;
						Button *btn = memnew(Button);
						String cost;
						auto add_cost = [&](int v, const char *suffix) {
							if (v <= 0) return;
							if (!cost.is_empty()) cost += ",";
							cost += String::num_int64(v) + suffix;
						};
						add_cost(up.food_cost, "f");
						add_cost(up.wood_cost, "w");
						add_cost(up.gold_cost, "g");
						add_cost(up.stone_cost, "s");
						btn->set_text(String("Upgrade to ") + rtso::building_type_name(up.to) + (cost.is_empty() ? String() : " (" + cost + ")"));
						String tip = String(up.tooltip ? up.tooltip : "") + "\n" + String::num((double)up.time, 0) + "s";
						const char *why = nullptr;
						switch (check) {
							case rtso::Simulation::UPGRADE_ERR_BUSY: why = "Busy: finish or cancel production first"; break;
							case rtso::Simulation::UPGRADE_ERR_PHASE: why = up.required_phase >= 3 ? "Requires City Phase" : "Requires Town Phase"; break;
							case rtso::Simulation::UPGRADE_ERR_COST: why = "Not enough resources"; break;
							case rtso::Simulation::UPGRADE_ERR_LIMIT: why = "Limit reached"; break;
							default: break;
						}
						if (why) {
							tip += String("\n") + why;
							btn->set_disabled(true);
						}
						btn->set_tooltip_text(tip);
						btn->connect("pressed", Callable(this, "_on_upgrade_pressed").bind(choice));
						production_bar->add_child(btn);
					}
				}
			}
		}
		// The structure's command row (unit_actions.js g_EntityCommands).
		if (found_b && found_b->team == local_player()) {
			const int inside = (int)found_b->garrisoned_units.size();
			if (inside > 0)
				add_entity_command("unload-all", "Unload All", "session.unload",
						"Unload All\nOrder all units to leave the selected entities.", "garrison-out.png", true, inside);
			// isUndeletable: you must hold half the capture points.
			if (found_b->max_capture_points > 0.0f &&
					found_b->capture_points_by_team[found_b->team] < found_b->max_capture_points * 0.5f)
				add_entity_command("delete", "Delete", "",
						"You cannot destroy this entity as you own less than half the capture points", "kill_small_disabled.png", false);
			else
				add_entity_command("delete", "Delete", "session.kill",
						String("Self-Destruct\nDestroy the selected entities.\nUse ") +
								rtso::hotkeys::table().label("session.noconfirmation").c_str() + " to avoid the confirmation dialog.",
						"kill_small.png");
			if (found_b->has_rally_point)
				add_entity_command("focus-rally", "Focus on Rally Point", "camera.rallypointfocus",
						"Focus on Rally Point\nCenter the view on the selected rally point.", "focus-rally.png");
			if (found_b->type == rtso::BLD_MARKET && found_b->build_progress >= 1.0f)
				add_entity_command("select-trading-goods", "Barter & Trade", "session.gui.barter.toggle",
						"Barter & Trade\nOpen the dialog for managing resource trading and bartering.", "economics.png");
		}
	} else {
		// Non-building selected: clear visual queue slots, or show a training unit's (one selected).
		trainer_unit_id_ = 0;
		if (selected_ids.size() != 1 || !update_unit_roster((uint32_t)selected_ids[0])) {
			production_queue_.update(nullptr);
			update_roster_list(nullptr);
		}

		if (!selected_ids.is_empty()) {
			auto uview = sim->units();

			// 0 A.D. gui/session/selection_panels.js g_SelectionPanels.Formation: one button per
			// formation the player has (Player.js GetFormations), disabled with the template's
			// DisabledTooltip unless the selection can form it (Commands.js CanMoveEntsIntoFormation),
			// highlighted when a selected unit is in it (GuiInterface IsFormationSelected).
			static const char *const kFormationIcons[rtso::FORMATION_COUNT] = {
				"null", "line_closed", "box", "column", "phalanx", "wedge", "skirmish", "line_open",
				"column_open", "flank", "battle_line", "ship_line", "ship_column", "flank", "syntagma",
				"testudo", "testudo"};
			const uint32_t *sel_ids = (const uint32_t *)selected_ids.ptr();
			const size_t sel_n = (size_t)selected_ids.size();
			uint32_t in_use = 0;       // formations a selected unit is in (bit 0: a unit in none)
			bool any_can_form = false; // the panel only shows for units that take formations
			for (size_t i = 0; i < sel_n; ++i) {
				for (size_t u = 0; u < uview.count; ++u) {
					if (uview.id[u] != sel_ids[i]) continue;
					in_use |= 1u << (uview.formation[u] < rtso::FORMATION_COUNT ? uview.formation[u] : 0);
					break;
				}
				for (uint8_t f = 1; f < rtso::FORMATION_COUNT && !any_can_form; ++f)
					any_can_form = sim->unit_can_use_formation(sel_ids[i], f);
			}
			const uint32_t available = any_can_form ? sim->player_formation_mask(local_player()) : 0u;
			for (uint8_t f = 0; f < rtso::FORMATION_COUNT; ++f) {
				if (!((available >> f) & 1u)) continue;
				const rtso::FormationTemplate &ft = rtso::formation_template(f);
				const bool enabled = f == rtso::FORMATION_NONE || sim->can_move_into_formation(sel_ids, sel_n, f);
				// The Formation panel's buttons are icons; the name is on the tooltip.
				Button *fb = memnew(Button);
				fb->set_custom_minimum_size(Vector2(26, 26));
				fb->set_clip_text(true);
				String tip = String::utf8(ft.name);
				const char *why = enabled ? ft.tooltip : ft.disabled_tooltip;
				if (why && *why) tip += String("\n") + String::utf8(why);
				fb->set_tooltip_text(tip);
				Ref<Texture2D> f_ico = load_tex(String("res://assets/textures/ui/formations/") + kFormationIcons[f] + ".png");
				if (f_ico.is_valid()) {
					fb->set_button_icon(f_ico);
					fb->set_expand_icon(true);
					fb->set_icon_alignment(HORIZONTAL_ALIGNMENT_CENTER);
				} else {
					fb->set_text(String::utf8(ft.name));
					fb->set_custom_minimum_size(Vector2(44, 26));
				}
				fb->add_theme_font_size_override("font_size", 8);
				if ((in_use >> f) & 1u) {
					// IsFormationSelected: the formation a selected unit is in is lit.
					fb->add_theme_color_override("font_color", Color(1.0f, 0.85f, 0.2f));
					fb->add_theme_color_override("icon_normal_color", Color(1.0f, 0.85f, 0.2f));
					fb->add_theme_color_override("icon_hover_color", Color(1.0f, 0.9f, 0.4f));
				}
				fb->set_disabled(!enabled);
				fb->connect("pressed", Callable(this, "_on_formation_selected").bind((int)f));
				actions_bar->add_child(fb);
			}

			// Villager Build Menu
			bool has_worker = false;
			for (int i = 0; i < selected_ids.size(); ++i) {
				for (size_t u = 0; u < uview.count; ++u) {
					if (uview.id[u] == (uint32_t)selected_ids[i]) {
						uint8_t ut = uview.unit_type[u];
						if (rtso::unit_can_build(ut, uview.tmpl[u])) {
							has_worker = true;
							break;
						}
					}
				}
			}

			// 0 A.D. Pack / Unpack buttons (gui/session unit_actions.js "pack"): shown when the
			// selection has siege engines with a Pack component, enabled for the state they can go to.
			bool can_pack = false, can_unpack = false;
			for (int i = 0; i < selected_ids.size(); ++i) {
				for (size_t u = 0; u < uview.count; ++u) {
					if (uview.id[u] != (uint32_t)selected_ids[i] || !uview.alive[u]) continue;
					const uint8_t ps = uview.pack_state[u];
					if (ps == rtso::PACK_UNPACKED || ps == rtso::PACK_UNPACKING) can_pack = true;
					if (ps == rtso::PACK_PACKED || ps == rtso::PACK_PACKING) can_unpack = true;
					break;
				}
			}
			if (can_pack || can_unpack) {
				static const struct { const char *label; const char *tip; const char *icon; int pack; } kPackButtons[2] = {
					{"Pack", "Pack the siege engine so it can move (10 s)", "res://assets/textures/ui/archive/icons/pack_fg.png", 1},
					{"Unpack", "Unpack the siege engine so it can attack (10 s)", "res://assets/textures/ui/archive/icons/unpack.png", 0},
				};
				for (const auto &pb : kPackButtons) {
					Button *btn = memnew(Button);
					btn->set_text(pb.label);
					btn->set_tooltip_text(pb.tip);
					Ref<Texture2D> p_ico = load_tex(pb.icon);
					if (p_ico.is_valid()) {
						btn->set_button_icon(p_ico);
						btn->add_theme_constant_override("icon_max_width", 20);
					}
					btn->set_disabled(pb.pack ? !can_pack : !can_unpack);
					btn->connect("pressed", Callable(this, "_on_pack_pressed").bind(pb.pack));
					production_bar->add_child(btn);
				}
			}

			// 0 A.D. unit upgrades (selection_panels.js "Upgrade"): one button per target the
			// selected units can upgrade to, enabled while any of them can start it, and
			// a cancel button while any of them is upgrading.
			{
				uint16_t targets[8];
				int target_n = 0;
				bool any_upgrading = false;
				for (int i = 0; i < selected_ids.size(); ++i) {
					const uint32_t id = (uint32_t)selected_ids[i];
					if (sim->unit_upgrade_of(id)) any_upgrading = true;
					for (size_t u = 0; u < uview.count; ++u) {
						if (uview.id[u] != id || uview.team[u] != local_player()) continue;
						for (const rtso::CivUnitUpgrade *up = rtso::civ_unit_upgrade_table(); up->from != 0xFFFF; ++up) {
							if (up->from != uview.tmpl[u] || (up->from_rank && uview.rank[u] < up->from_rank)) continue;
							bool seen = false;
							for (int t = 0; t < target_n; ++t) seen = seen || targets[t] == up->to;
							if (!seen && target_n < 8) targets[target_n++] = up->to;
						}
						break;
					}
				}
				for (int t = 0; t < target_n; ++t) {
					const uint16_t to = targets[t];
					const rtso::CivUnitUpgrade *row = nullptr;
					const char *why = "Already upgrading";
					for (int i = 0; i < selected_ids.size(); ++i) {
						const rtso::Simulation::UpgradeCheck c = sim->unit_upgrade_check(local_player(), (uint32_t)selected_ids[i], to);
						if (c == rtso::Simulation::UPGRADE_OK) { why = nullptr; break; }
						if (c == rtso::Simulation::UPGRADE_ERR_COST) why = "Not enough resources";
						else if (c == rtso::Simulation::UPGRADE_ERR_PHASE) why = "Requires City Phase";
						else if (c == rtso::Simulation::UPGRADE_ERR_LIMIT) why = "Limit reached";
					}
					for (const rtso::CivUnitUpgrade *up = rtso::civ_unit_upgrade_table(); up->from != 0xFFFF && !row; ++up)
						if (up->to == to) row = up;
					String cost;
					auto add_cost = [&](int v, const char *suffix) {
						if (v <= 0) return;
						if (!cost.is_empty()) cost += ",";
						cost += String::num_int64(v) + suffix;
					};
					add_cost(row->food_cost, "f");
					add_cost(row->wood_cost, "w");
					add_cost(row->gold_cost, "g");
					add_cost(row->stone_cost, "s");
					Button *btn = memnew(Button);
					btn->set_text(String("Upgrade to ") + rtso::civ_unit_template(to).name + (cost.is_empty() ? String() : " (" + cost + ")"));
					String tip = String(rtso::civ_unit_template(to).specific_name) + "\n" + String::num((double)row->time, 0) + "s";
					if (why) {
						tip += String("\n") + why;
						btn->set_disabled(true);
					}
					btn->set_tooltip_text(tip);
					btn->connect("pressed", Callable(this, "_on_unit_upgrade_pressed").bind((int)to));
					production_bar->add_child(btn);
				}
				if (any_upgrading) {
					Button *btn = memnew(Button);
					btn->set_text("Cancel upgrade");
					btn->set_tooltip_text("Stop upgrading and get the resources back");
					btn->connect("pressed", Callable(this, "_on_unit_upgrade_pressed").bind(-1));
					production_bar->add_child(btn);
				}
			}

			// The command row, g_EntityCommands in unit_actions.js order: each shows when
			// its getInfo would (any selected unit it applies to).
			{
				bool any_guarding = false, any_busy = false, any_work = false, any_fighter = false;
				bool any_guard = false, any_can_add_guard = false, any_garrison = false;
				int passengers = 0;
				for (int s = 0; s < selected_ids.size(); ++s) {
					const uint32_t id = (uint32_t)selected_ids[s];
					for (size_t u = 0; u < uview.count; ++u) {
						if (uview.id[u] != id) continue;
						const uint8_t ut = uview.unit_type[u];
						const uint16_t tm = uview.tmpl[u];
						const uint64_t cls = tm < rtso::CIV_UNIT_TEMPLATE_COUNT ? rtso::civ_unit_template(tm).tech_classes : rtso::unit_tech_classes(ut);
						const bool guarding = sim->unit_is_guarding(id);
						any_guarding = any_guarding || guarding;
						any_busy = any_busy || (sim->unit_has_order(id) && !guarding); // "stop": not idle, not guarding
						any_work = any_work || sim->unit_has_work_order(id);
						// "call-to-arms": Soldier, Warship, Siege or Healer.
						any_fighter = any_fighter || (cls & (rtso::TC_SOLDIER | rtso::TC_WARSHIP | rtso::TC_SIEGE | rtso::TC_HEALER)) != 0;
						// UnitAI CanGuard / CanPatrol: soldiers, warships and siege.
						const bool can_guard = (cls & (rtso::TC_SOLDIER | rtso::TC_WARSHIP | rtso::TC_SIEGE)) != 0;
						any_guard = any_guard || can_guard;
						any_can_add_guard = any_can_add_guard || (can_guard && !guarding);
						any_garrison = any_garrison || !rtso::unit_is_ship(ut); // Garrisonable
						if (sim->is_transport(id)) passengers += sim->get_transport_passenger_count(id);
						break;
					}
				}
				if (passengers > 0)
					add_entity_command("unload-all", "Unload All", "session.unload",
							"Unload All\nOrder all units to leave the selected entities.", "garrison-out.png", true, passengers);
				add_entity_command("delete", "Delete", "session.kill",
						String("Self-Destruct\nDestroy the selected entities.\nUse ") +
								rtso::hotkeys::table().label("session.noconfirmation").c_str() + " to avoid the confirmation dialog.",
						"kill_small.png");
				if (any_busy)
					add_entity_command("stop", "Abort", "session.stop",
							"Abort\nCancel the current orders for the selected units.", "stop.png");
				if (any_fighter)
					add_entity_command("call-to-arms", "Call to Arms", "session.calltoarms",
							"Attack\nSend the selected units on attack move to the specified location after dropping resources.",
							"call-to-arms.png");
				if (any_garrison)
					add_entity_command("garrison", "Garrison", "session.garrison",
							"Garrison\nOrder the selected units to garrison in a structure or another unit.", "garrison.png");
				if (has_worker)
					add_entity_command("repair", "Repair", "session.repair",
							"Repair\nOrder the selected units to repair a structure, ship, or siege engine.", "repair.png");
				if (any_work)
					add_entity_command("back-to-work", "Back to Work", "session.backtowork",
							"Back to Work\nOrder the selected units to resume their work.", "back-to-work.png");
				if (any_can_add_guard)
					add_entity_command("add-guard", "Guard", "session.guard",
							"Guard\nOrder the selected units to guard a structure or unit.", "add-guard.png");
				if (any_guarding)
					add_entity_command("remove-guard", "Abort Guard", "",
							"Abort Guard\nOrder the selected units to stop guarding.", "remove-guard.png");
				if (any_guard)
					add_entity_command("patrol", "Patrol", "session.patrol",
							"Patrol\nOrder to repeatedly go to a point and come back, attacking all enemies along the way.",
							"patrol.png");
			}

			if (has_worker) {
				// Build menu in 0 A.D. order; cost and phase come from the building table.
				static const int b_types[] = {
					rtso::BLD_HOUSE, rtso::BLD_STOREHOUSE, rtso::BLD_FARMSTEAD, rtso::BLD_FIELD,
					rtso::BLD_CORRAL, rtso::BLD_DOCK, rtso::BLD_BARRACKS, rtso::BLD_STABLE,
					rtso::BLD_RANGE, rtso::BLD_SENTRY_TOWER, rtso::BLD_OUTPOST, rtso::BLD_PALISADE,
					rtso::BLD_FORGE, rtso::BLD_MARKET, rtso::BLD_TEMPLE, rtso::BLD_TOWER,
					rtso::BLD_EMBASSY, rtso::BLD_WALL_SEGMENT, rtso::BLD_WALL_TOWER, rtso::BLD_GATE,
					rtso::BLD_FORTRESS, rtso::BLD_ARSENAL, rtso::BLD_ELEPHANT_STABLE, rtso::BLD_THEATER,
					rtso::BLD_WONDER
				};
				static const char *kPhaseNames[4] = {"", "Village", "Town", "City"};
				// Generic structures the civ's 0 A.D. Builder lists name, then the civ's
				// own (0 A.D. structures/<civ>/).
				const uint8_t my_civ = sim->player_civ(local_player());
				std::vector<int> menu;
				for (int bt : b_types)
					if (rtso::civ_can_build(my_civ, (uint8_t)bt)) menu.push_back(bt);
				for (int bt = rtso::CIV_BUILDING_FIRST; bt < rtso::BLD_TYPE_COUNT; ++bt)
					if (rtso::civ_can_build(my_civ, (uint8_t)bt)) menu.push_back(bt);
				// The map script's SetDisabledTemplates (Survival of the Fittest) hides those structures.
				menu.erase(std::remove_if(menu.begin(), menu.end(), [&](int bt) {
					return sim->building_disabled_by_trigger(local_player(), (uint8_t)bt);
				}), menu.end());
				for (int bt : menu) {
					const rtso::BuildingTypeInfo bi = rtso::civ_building_type_info(my_civ, (uint8_t)bt); // the big house
					String cost;
					auto add_cost = [&](int amount, const char *suffix) {
						if (amount <= 0) return;
						if (!cost.is_empty()) cost += ",";
						cost += String::num_int64(amount) + suffix;
					};
					add_cost(bi.food_cost, "f");
					add_cost(bi.wood_cost, "w");
					add_cost(bi.gold_cost, "g");
					add_cost(bi.stone_cost, "s");
					uint8_t need = rtso::building_required_phase((uint8_t)bt);
					// 0.28's Construction panel: an icon grid, name and cost on the tooltip.
					Button *bb = memnew(Button);
					const String label = String::utf8(bi.name) + (cost.is_empty() ? String() : " (" + cost + ")");
					bb->set_meta("label", label);
					String tip = String::utf8(bi.name) + (cost.is_empty() ? String() : "\nCost: " + cost);
					Ref<Texture2D> b_ico = structure_icon(bi.portrait_id);
					if (b_ico.is_valid()) {
						bb->set_button_icon(b_ico);
						bb->set_expand_icon(true);
						bb->set_custom_minimum_size(Vector2(38, 38));
					} else {
						bb->set_text(label);
					}
					bb->add_theme_font_size_override("font_size", 9);
					if (need > sim->player_phase(local_player())) {
						bb->set_disabled(true);
						tip += String("\nRequires ") + kPhaseNames[need] + " Phase";
					}
					bb->set_tooltip_text(tip);
					bb->connect("pressed", Callable(this, "_on_build_btn_pressed").bind(bt));
					production_bar->add_child(bb);
				}
			}
		}
	}
	// Compact production buttons, so a wrapped build menu fits a few rows.
	for (int i = old_production; i < production_bar->get_child_count(); ++i)
		if (Button *btn = Object::cast_to<Button>(production_bar->get_child(i)))
			if (!btn->has_theme_font_size_override("font_size")) btn->add_theme_font_size_override("font_size", 11);
	keep_command_buttons(actions_bar, old_actions);
	keep_command_buttons(production_bar, old_production);
	// A structure shows its queue under the buttons; a unit selection has no
	// queue, so the buttons get that room.
	const bool queue_shown = selected_building_id != 0;
	if (queue_bar->is_visible() != queue_shown) queue_bar->set_visible(queue_shown);
	// A builder's construction grid can outgrow the two rows a unit selection
	// has: the panel grows upward to show all of it (0.28 shows the whole
	// Construction panel), up to kMaxGrow, past which it scrolls.
	constexpr float kMaxGrow = 120.0f;
	const float h = queue_shown ? 40.0f : std::clamp(production_bar->get_combined_minimum_size().y, 96.0f, 96.0f + kMaxGrow);
	const Vector2 area(552, h);
	if (production_scroll->get_size() != area) production_scroll->set_size(area);
	const float top = -198.0f - (queue_shown ? 0.0f : h - 96.0f);
	if (command_panel->get_offset(SIDE_TOP) != top) command_panel->set_offset(SIDE_TOP, top);
}

// Everything that makes a command button look or act differently: its text,
// tooltip, enabled state, icon, size, overrides and what pressed calls.
static String command_button_signature(Node *n) {
	Button *b = Object::cast_to<Button>(n);
	if (!b) return n ? n->get_class() : String();
	String s = b->get_text() + String("\x1f") + b->get_tooltip_text() + (b->is_disabled() ? "\x1fD" : "\x1fE");
	Ref<Texture2D> ico = b->get_button_icon();
	if (ico.is_valid()) s += String("\x1fi") + (ico->get_path().is_empty() ? String::num_int64(ico->get_instance_id()) : ico->get_path());
	s += String("\x1fm") + String(Variant(b->get_custom_minimum_size()));
	if (b->has_theme_color_override("font_color")) s += String("\x1fc") + String(Variant(b->get_theme_color("font_color")));
	if (b->has_theme_font_size_override("font_size")) s += String("\x1ff") + String::num_int64(b->get_theme_font_size("font_size"));
	TypedArray<Dictionary> conns = b->get_signal_connection_list("pressed");
	for (int i = 0; i < conns.size(); ++i) {
		Callable c = Dictionary(conns[i])["callable"];
		s += String("\x1fp") + String(c.get_method()) + String(Variant(c.get_bound_arguments()));
	}
	return s;
}

// bar holds the previous frame's buttons (the first `old_count`) followed by
// this frame's. Keep the old ones when the two sets match, so a press and its
// release reach the same Button; otherwise swap in the new ones.
void RtsoMain::keep_command_buttons(Control *bar, int old_count) {
	const int total = bar->get_child_count();
	const int fresh = total - old_count;
	bool same = fresh == old_count;
	for (int i = 0; same && i < old_count; ++i)
		same = command_button_signature(bar->get_child(i)) == command_button_signature(bar->get_child(old_count + i));
	const int from = same ? old_count : 0, to = same ? total : old_count;
	for (int i = to - 1; i >= from; --i) {
		Node *n = bar->get_child(i);
		bar->remove_child(n);
		memdelete(n);
	}
}

// Drops one unit from the selection after an orderone command
// (0 A.D. gui/session/input.js popOneFromSelection -> g_Selection.removeList).
static void pop_from_selection(PackedInt32Array &sel, uint32_t id) {
	const int64_t at = sel.find((int32_t)id);
	if (at >= 0) sel.remove_at(at);
}

void RtsoMain::enter_build_mode(bool enable, uint8_t btype) {
	build_mode = enable;
	current_build_type = btype;
	build_click_pending = false;
	placement_angle = rtso::Simulation::DEFAULT_BUILDING_ROT; // placementSupport.Reset()
	if (!enable) {
		wall_drag_mode = false;
		for (auto *wg : wall_ghosts) {
			if (wg) wg->set_visible(false);
		}
	}
	update_ghost_mesh();
	if (ghost_mesh) ghost_mesh->set_visible(build_mode && !wall_drag_mode);
}

float RtsoMain::placement_angle_at(uint8_t btype, float x, float z) const {
	if (btype == rtso::BLD_DOCK && sim) {
		const float a = sim->dock_angle(x, z, rtso::building_type_info(btype).footprint * rtso::Simulation::GRID * 0.5f);
		if (!std::isnan(a)) return a;
	}
	return placement_angle;
}

void RtsoMain::try_train_unit(rtso::UnitType utype) {
	if (!sim || !rtso::civ_can_train(sim->player_civ(local_player()), (uint8_t)utype)) return;
	// The selected structure if it trains the class, else the first finished one
	// that does (0 A.D. trains a unit only where its template's Trainer lists it).
	const uint8_t civ = sim->player_civ(local_player());
	uint32_t bid = 0;
	for (const auto &b : sim->buildings()) {
		if (b.team != local_player() || b.wrecked || b.build_progress < 1.0f) continue;
		if (!building_trains_class(civ, (uint8_t)utype, b.type)) continue;
		if (b.id == selected_building_id) { bid = b.id; break; }
		if (bid == 0) bid = b.id;
	}
	if (bid == 0) return;
	const int count = train_batch();
	if (turn_cmds()) {
		post_lockstep(6, nullptr, 0, 0, (float)count, bid, (uint8_t)utype);
		play_civ_voice("select");
		return;
	}
	if (sim->cmd_train(local_player(), bid, utype, count))
		play_civ_voice("select");
}

int RtsoMain::train_batch() const {
	return rtso::hotkeys::held("session.batchtrain") ? user_opts_.batch_size : 1;
}

// One queue item of `count` units (0 A.D. batch training: one cost x count, one shorter timer).
void RtsoMain::try_train_template(uint32_t building_id, uint16_t tmpl, int count) {
	if (building_id == 0 || tmpl >= rtso::CIV_UNIT_TEMPLATE_COUNT) return;
	if (turn_cmds()) {
		post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_TRAIN_TEMPLATE, nullptr, 0, (float)tmpl, (float)count, building_id, 0);
		play_civ_voice("select");
		return;
	}
	if (sim->cmd_train_template(local_player(), building_id, tmpl, count))
		play_civ_voice("select");
}

// The selected building's trainable roster for the local player's civ: every
// civ_unit_template_table() row whose trainers include this building type, with
// 0 A.D. names and costs after team bonuses; rows above the player's phase are
// listed but disabled with the phase they need.
void RtsoMain::update_roster_list(const rtso::Simulation::Building *b) {
	if (!roster_list) return;
	const uint8_t pid = local_player();
	if (!b || b->team != pid || b->build_progress < 1.0f) {
		roster_list->set_visible(false);
		roster_list_key_ = ~0ull;
		return;
	}
	const uint8_t civ = sim->player_civ(pid);
	const uint8_t phase = sim->player_phase(pid);
	// Rebuild when the building, civ, phase, champion unlocks or allies (team bonus prices) change.
	const auto &ps = sim->player(pid);
	const uint64_t unlocks = (ps.has_tech(rtso::TECH_UNLOCK_CHAMPION_INFANTRY) ? 1u : 0u) |
		(ps.has_tech(rtso::TECH_UNLOCK_CHAMPION_CAVALRY) ? 2u : 0u) |
		(ps.has_tech(rtso::TECH_UNLOCK_CHAMPION_CHARIOTS) ? 4u : 0u);
	const uint64_t key = ((uint64_t)b->id << 32) | ((uint64_t)sim->ally_civ_mask(pid) << 12) |
		(unlocks << 8) | ((uint64_t)(civ & 0x3F) << 2) | (uint64_t)(phase & 0x3);
	if (key == roster_list_key_) return;
	roster_list_key_ = key;
	roster_list->clear();
	// A corral trains its civ's animals (structures/<civ>/corral.xml Trainer/Entities);
	// metadata ROSTER_ANIMAL | FaunaType tells the click handler to breed one.
	if (b->type == rtso::BLD_CORRAL) {
		const rtso::CorralTrainable &ct = rtso::CORRAL_TRAINABLE[civ < rtso::CIV_COUNT ? civ : 0];
		for (uint8_t k = 0; k < ct.count; ++k) {
			const uint8_t f = ct.fauna[k];
			const rtso::FaunaTrainCost &c = rtso::fauna_train_cost(f);
			String label = String::utf8(rtso::fauna_type_info(f).name) + " (" + String::num_int64(c.res[0]) + "f)";
			int idx = roster_list->add_item(label);
			roster_list->set_item_metadata(idx, ROSTER_ANIMAL | (int)f);
			roster_list->set_item_tooltip(idx, String::utf8(rtso::fauna_type_info(f).name) + "\nTrain " +
					String::num(sim->livestock_train_time(pid, f), 0) + " s, " +
					String::num_int64(rtso::fauna_type_info(f).food_amount) + " food when slaughtered, " +
					String::num(rtso::fauna_corral_food(f), 0) + " food every " +
					String::num(rtso::CORRAL_TRICKLE_INTERVAL, 0) + " s while penned");
		}
		roster_list->set_visible(roster_list->get_item_count() > 0);
		return;
	}
	for (uint16_t i : rtso::civ_roster_ids(civ)) {
		const rtso::CivUnitTemplate &u = rtso::civ_unit_template(i);
		if (!(u.trainers & rtso::bld_bit(b->type))) continue;
		// RTS0-only rows (<civ>/rts0_villager) back class training for the AI and old saves;
		// 0 A.D. has no such unit, so players don't see it.
		if (std::strstr(u.id, "/rts0_")) continue;
		String tip;
		const int idx = add_roster_item(pid, i, tip);
		if (u.phase > phase) {
			roster_list->set_item_disabled(idx, true);
			tip += u.phase >= 3 ? "\nRequires City Phase" : "\nRequires Town Phase";
		} else if (!sim->can_train_at(pid, u.unit_type, b->type, i)) {
			roster_list->set_item_disabled(idx, true);
			uint8_t gate = sim->train_unlock_tech(pid, u.unit_type, b->type, i);
			if (gate != rtso::TECH_NONE) tip += String("\nRequires ") + rtso::tech_info(gate).name;
		}
		roster_list->set_item_tooltip(idx, tip);
	}
	roster_list->set_visible(roster_list->get_item_count() > 0);
}

// A selected unit that trains (0 A.D. Trainer on triremes and Cyrus II): its Trainer list,
// the ones whose requirements aren't met yet disabled, and its queue. False when the
// selection is not one of the local player's training units.
bool RtsoMain::update_unit_roster(uint32_t unit_id) {
	if (!roster_list || unit_id == 0) return false;
	const uint8_t pid = local_player();
	const uint16_t tmpl = sim->unit_template(unit_id);
	if (!rtso::Simulation::unit_is_trainer(tmpl)) return false;
	auto uview = sim->units();
	size_t i = 0;
	while (i < uview.count && uview.id[i] != unit_id) ++i;
	if (i == uview.count || uview.team[i] != pid) return false;
	trainer_unit_id_ = unit_id;
	production_queue_.update_unit(unit_id, sim->unit_queue(unit_id));
	const std::vector<uint16_t> can = sim->unit_trainables(pid, tmpl);
	uint64_t can_mask = 0;
	const std::vector<uint16_t> all = sim->unit_trainables(pid, tmpl, true);
	for (size_t k = 0; k < all.size() && k < 64; ++k)
		if (std::find(can.begin(), can.end(), all[k]) != can.end()) can_mask |= 1ull << k;
	// Researcher on a unit: each technology's research_check result (researched ones drop out).
	const std::vector<uint8_t> techs = rtso::Simulation::unit_researchables(tmpl);
	uint64_t tech_key = 0;
	for (uint8_t t : techs) tech_key = tech_key * 31 + (uint64_t)sim->research_check(pid, t) + 1;
	const uint64_t key = ((uint64_t)unit_id << 32) ^ (can_mask << 8) ^ (tech_key << 20) ^ 0x80ull ^ sim->ally_civ_mask(pid);
	if (key == roster_list_key_) return true;
	roster_list_key_ = key;
	roster_list->clear();
	for (size_t k = 0; k < all.size(); ++k) {
		String tip;
		const int idx = add_roster_item(pid, all[k], tip);
		if (!(can_mask & (1ull << k))) {
			roster_list->set_item_disabled(idx, true);
			tip += "\nRequirements not met";
		}
		roster_list->set_item_tooltip(idx, tip);
	}
	for (uint8_t t : techs) {
		const auto check = sim->research_check(pid, t);
		if (check == rtso::Simulation::TECH_ERR_RESEARCHED) continue;
		const rtso::TechInfo &ti = rtso::tech_info(t);
		String costs;
		auto add_cost = [&](int v, const char *suffix) {
			if (v <= 0) return;
			if (!costs.is_empty()) costs += " ";
			costs += String::num_int64(v) + suffix;
		};
		add_cost(ti.food_cost, "f");
		add_cost(ti.wood_cost, "w");
		add_cost(ti.stone_cost, "s");
		add_cost(ti.gold_cost, "m");
		const int idx = roster_list->add_item(String::utf8(ti.name) + " (" + costs + ")");
		roster_list->set_item_metadata(idx, ROSTER_TECH | (int)t);
		String tip = String::utf8(ti.name) + "\n" + String::utf8(ti.desc) + "\nResearch " + String::num(ti.research_time, 0) + " s";
		if (check != rtso::Simulation::TECH_OK && check != rtso::Simulation::TECH_ERR_COST) {
			roster_list->set_item_disabled(idx, true);
			tip += "\nRequirements not met";
		}
		roster_list->set_item_tooltip(idx, tip);
	}
	roster_list->set_visible(roster_list->get_item_count() > 0);
	return true;
}

// One roster row: the 0 A.D. name and cost after team bonuses; `tip` gets its tooltip.
int RtsoMain::add_roster_item(uint8_t pid, uint16_t i, String &tip) {
	const rtso::CivUnitTemplate &u = rtso::civ_unit_template(i);
	float cost[rtso::RES_COUNT];
	sim->unit_train_cost(pid, i, rtso::civ_template_unit_info(i, u.unit_type), cost);
	String costs;
	auto add_cost = [&](float v, const char *suffix) {
		if (v <= 0.0f) return;
		if (!costs.is_empty()) costs += " ";
		costs += String::num_int64((int64_t)v) + suffix;
	};
	add_cost(cost[rtso::RES_FOOD], "f");
	add_cost(cost[rtso::RES_WOOD], "w");
	add_cost(cost[rtso::RES_STONE], "s");
	add_cost(cost[rtso::RES_GOLD], "m");
	String label = String::utf8(u.name);
	if (u.flags & rtso::ROSTER_HERO) label = String::utf8("\u2605 ") + label;
	label += costs.is_empty() ? String(" (free)") : " (" + costs + ")";
	int idx = roster_list->add_item(label);
	roster_list->set_item_metadata(idx, (int)i);
	tip = String::utf8(u.name);
	if (u.specific_name && u.specific_name[0]) tip += String::utf8(" \u2014 ") + String::utf8(u.specific_name);
	tip += "\nHP " + String::num_int64((int64_t)u.hp) + ", attack " + String::num(u.attack_damage, 1) +
		", train " + String::num(u.train_time, 0) + " s, pop " + String::num_int64(u.pop_cost);
	return idx;
}

void RtsoMain::clear_selection() {
	selected_ids.clear();
	selected_building_id = 0;
}

void RtsoMain::_unhandled_input(const Ref<InputEvent> &event) {
	// Keys go through named 0 A.D. hotkeys (hotkeys.h, config/default.cfg
	// [hotkey.*]; user overrides in user://config/user.cfg). Same-specificity
	// hotkeys can share a key (B = build menu or fifth trainee), so the context
	// below picks between them, like 0 A.D.'s gui/session/hotkeys/*.xml.
	Ref<InputEventKey> key = event;
	const std::vector<std::string> hk = rtso::hotkeys::fired(key);
	if (!hk.empty()) {
		auto fired = [&](const char *name) { return rtso::hotkeys::has(hk, name); };
		auto ids_ptr = [&]() { return (const uint32_t *)selected_ids.ptr(); };

		if (fired("cancel") || fired("selection.cancel")) {
			// 0 A.D. closeOpenDialogs() closes every dialog at once, then
			// clearSelection() (input.js:1709).
			if (close_open_dialogs()) return;
			if (build_mode) enter_build_mode(false);
			else if (attack_move_mode || patrol_mode || guard_mode || call_to_arms_mode || garrison_mode || repair_mode)
				attack_move_mode = patrol_mode = guard_mode = call_to_arms_mode = garrison_mode = repair_mode = false;
			else clear_selection();
			return;
		}

		// While placing a building, letters pick the building to place
		// (RTS0 [hotkey.rts0.build]); the camera scrolls with the arrows only.
		if (build_mode) {
			static const struct { const char *name; uint8_t type; } kBuildKeys[] = {
				{"rts0.build.civilcentre", rtso::BLD_TOWN_CENTER}, {"rts0.build.house", rtso::BLD_HOUSE},
				{"rts0.build.barracks", rtso::BLD_BARRACKS}, {"rts0.build.storehouse", rtso::BLD_STOREHOUSE},
				{"rts0.build.defensetower", rtso::BLD_TOWER}, {"rts0.build.farmstead", rtso::BLD_FARMSTEAD},
				{"rts0.build.market", rtso::BLD_MARKET}, {"rts0.build.fortress", rtso::BLD_FORTRESS},
				{"rts0.build.gate", rtso::BLD_GATE}, {"rts0.build.temple", rtso::BLD_TEMPLE},
				{"rts0.build.wonder", rtso::BLD_WONDER}, {"rts0.build.dock", rtso::BLD_DOCK},
				{"rts0.build.wall", rtso::BLD_WALL_SEGMENT}, {"rts0.build.corral", rtso::BLD_CORRAL},
			};
			for (const auto &bk : kBuildKeys) {
				if (fired(bk.name)) {
					enter_build_mode(true, bk.type);
					return;
				}
			}
			if (build_mode && !wall_drag_mode) {
			constexpr float ROTATION_STEP = 3.14159265f / 12.0f; // 24 presses make a full turn
			if (fired("session.rotate.cw")) { placement_angle += ROTATION_STEP; return; }
			if (fired("session.rotate.ccw")) { placement_angle -= ROTATION_STEP; return; }
		}

		if (fired("rts0.build.menu")) {
				enter_build_mode(false);
				return;
			}
		}

		if (fired("chat")) {
			toggle_chat();
			return;
		}
		if (fired("teamchat") || fired("privatechat")) {
			if (!chat_window_.is_open()) toggle_chat();
			chat_window_.select_channel(fired("teamchat") ? 1 : 0);
			return;
		}

		// Control groups (0 A.D. input.js:852-873 and performGroup, input.js:1606):
		// N selects group N, Ctrl+N saves the selection as group N, Shift+N adds
		// group N to the selection (performGroup "add"), and pressing N twice within
		// 500 ms centres the camera on the group. Groups are disjoint
		// (gui.session.disjointcontrolgroups).
		for (int g = 1; g <= 10; ++g) {
			const int grp = g % 10;
			const std::string n = std::to_string(g);
			const bool sel = fired(("selection.group.select." + n).c_str());
			const bool save = fired(("selection.group.save." + n).c_str());
			const bool add = fired(("selection.group.add." + n).c_str());
			if (!sel && !save && !add) continue;
			if (save) {
				selected_groups[grp].clear();
				for (int s = 0; s < selected_ids.size(); ++s) {
					for (int o = 0; o < 10; ++o) {
						if (o == grp) continue;
						const int64_t at = selected_groups[o].find(selected_ids[s]);
						if (at >= 0) selected_groups[o].remove_at(at);
					}
					if (!selected_groups[grp].has(selected_ids[s])) selected_groups[grp].push_back(selected_ids[s]);
				}
				return;
			}
			PackedInt32Array recall;
			Vector3 first_pos;
			if (sim) {
				auto gview = sim->units();
				for (int m = 0; m < selected_groups[grp].size(); ++m) {
					const int32_t gid = selected_groups[grp][m];
					for (size_t i = 0; i < gview.count; ++i) {
						if (gview.alive[i] && gview.id[i] == (uint32_t)gid && gview.team[i] == local_player()) {
							if (recall.is_empty()) first_pos = Vector3(gview.pos_x[i], 0, gview.pos_z[i]);
							recall.push_back(gid);
							break;
						}
					}
				}
			}
			selected_groups[grp] = recall;
			if (recall.is_empty()) return;
			if (add) {
				for (int m = 0; m < recall.size(); ++m)
					if (!selected_ids.has(recall[m])) selected_ids.push_back(recall[m]);
				selected_building_id = 0;
				play_civ_voice("select");
				return;
			}
			const uint64_t now_ms = Time::get_singleton()->get_ticks_msec();
			if (grp == last_group_pressed_ && now_ms - last_group_press_ms_ < 500 && cam_rig)
				cam_rig->set_target_pos(first_pos);
			last_group_pressed_ = grp;
			last_group_press_ms_ = now_ms;
			selected_ids = recall;
			selected_building_id = 0;
			play_civ_voice("select");
			return;
		}

		// Idle unit finders (0 A.D. findIdleUnit, input.js:1659, classes from
		// gui/session/hotkeys/misc.xml and MiniMapIdleWorkerButton.js).
		if (fired("selection.idleworker")) { find_idle_unit(IDLE_WORKER); return; }
		if (fired("selection.idlewarrior")) { find_idle_unit(IDLE_WARRIOR); return; }
		if (fired("selection.idleunit")) { find_idle_unit(IDLE_ANY); return; }
		if (fired("selection.idlebuilder")) { find_idle_unit(IDLE_BUILDER); return; }

		if (fired("session.gui.diplomacy.toggle")) { toggle_diplomacy(); return; }
		if (fired("session.diplomacycolors")) { toggle_diplomacy_colors(); return; }
		if (fired("session.gui.matchsettings.toggle") || fired("session.gui.objectives.toggle")) { toggle_objectives(); return; }
		if (fired("session.gui.tutorial.toggle")) { tutorial_panel_.toggle(); return; }
		if (fired("session.gui.barter.toggle")) {
			_toggle_session_dialog("BarterDialog");
			return;
		}
		if (fired("structree")) {
			if (Node *tree = get_node_or_null(NodePath("TechTreeDialog"))) tree->call("toggle");
			return;
		}
		if (fired("civinfo")) {
			if (Node *tree = get_node_or_null(NodePath("TechTreeDialog"))) tree->call("toggle_civinfo");
			return;
		}
		if (fired("session.gui.toggle")) {
			if (ui_layer) ui_layer->set_visible(!ui_layer->is_visible());
			return;
		}
		if (fired("summary")) {
			close_open_dialogs(); // MenuButtons Summary.onPress: closeOpenDialogs first
			toggle_summary();
			return;
		}
		if (fired("session.gui.menu.toggle")) {
			_on_menu_button_pressed();
			return;
		}
		if (fired("pause")) {
			toggle_pause();
			return;
		}
		// 0 A.D. misc.xml: quicksave/quickload are disabled in networked games.
		if (fired("quicksave")) {
			quicksave();
			return;
		}
		if (fired("quickload")) {
			quickload();
			return;
		}

		// Camera (0 A.D. [hotkey.camera], gui/session/hotkeys/camera.xml).
		for (int j = 1; j <= 4; ++j) {
			const std::string n = std::to_string(j);
			if (fired(("camera.jump.set." + n).c_str())) {
				if (cam_rig) camera_bookmarks_[j - 1] = {true, cam_rig->get_target_pos()};
				return;
			}
			if (fired(("camera.jump." + n).c_str())) {
				if (cam_rig && camera_bookmarks_[j - 1].set) cam_rig->set_target_pos(camera_bookmarks_[j - 1].pos);
				return;
			}
		}
		if (fired("camera.reset")) {
			if (cam_rig) cam_rig->reset_camera();
			return;
		}
		if (fired("camera.follow")) {
			if (cam_rig) cam_rig->set_follow_id(selected_ids.is_empty() ? 0 : (uint32_t)selected_ids[0]);
			return;
		}
		if (fired("camera.rallypointfocus")) { // unit_actions.js "focus-rally"
			_on_entity_command("focus-rally");
			return;
		}
		if (fired("camera.lastattackfocus")) {
			if (cam_rig && has_last_attack_) cam_rig->set_target_pos(last_attack_pos_);
			return;
		}

		// Unit commands (0 A.D. [hotkey.session], misc.xml).
		if (fired("session.stop")) {
			_on_entity_command("stop");
			return;
		}
		if (fired("session.unload")) {
			_on_ungarrison_pressed();
			return;
		}
		// Delete (0 A.D. session.kill, unit_actions "delete"): DeleteSelectionConfirmation
		// asks first unless session.noconfirmation (Shift) is held.
		if (fired("session.kill")) {
			_on_entity_command("delete");
			return;
		}
		// Back to work (session.backtowork): resume the last gather or build.
		if (fired("session.backtowork")) {
			_on_entity_command("back-to-work");
			return;
		}
		auto apply_stance = [&](uint8_t st) {
			if (selected_ids.is_empty()) return;
			if (turn_cmds())
				post_lockstep(7, ids_ptr(), selected_ids.size(), 0, 0, 0, st);
			else
				sim->order_set_stance(ids_ptr(), selected_ids.size(), st);
		};
		if (fired("rts0.stance.aggressive")) { apply_stance(rtso::STANCE_AGGRESSIVE); return; }
		if (fired("rts0.stance.defensive")) { apply_stance(rtso::STANCE_DEFENSIVE); return; }
		if (fired("rts0.stance.standground")) { apply_stance(rtso::STANCE_STAND_GROUND); return; }
		if (fired("rts0.stance.violent")) { apply_stance(rtso::STANCE_VIOLENT); return; }
		if (fired("rts0.stance.passive")) { apply_stance(rtso::STANCE_PASSIVE); return; }
		if (fired("session.attackmoveunit")) {
			if (!selected_ids.is_empty()) {
				attack_move_mode = true;
				patrol_mode = guard_mode = call_to_arms_mode = garrison_mode = repair_mode = false;
			}
			return;
		}

		// Training: Z X C V B N M , queue the selected building's 1st-8th
		// trainee; Shift trains a batch of 5 (gui.session.batchtrainingsize).
		if (selected_building_id != 0 && roster_list) {
			for (int q = 1; q <= 8; ++q) {
				if (!fired(("session.queueunit." + std::to_string(q)).c_str())) continue;
				if (q - 1 < roster_list->get_item_count() && !roster_list->is_item_disabled(q - 1)) {
					const uint16_t tmpl = (uint16_t)(int)roster_list->get_item_metadata(q - 1);
					try_train_template(selected_building_id, tmpl, train_batch());
				}
				return;
			}
		}

		if (fired("rts0.build.menu")) {
			enter_build_mode(!build_mode, rtso::BLD_HOUSE);
			return;
		}

		if (fired("rts0.debug.battle500")) { spawn_large_battle(500); return; }
		if (fired("rts0.debug.battle1000")) { spawn_large_battle(1000); return; }
		if (fired("rts0.debug.battle2000")) { spawn_large_battle(2000); return; }

		// gui/session/hotkeys/misc.xml session.showstatusbars -> toggleStatusBars().
		if (fired("session.showstatusbars")) {
			if (view) view->set_show_all_status_bars(!view->show_all_status_bars());
			return;
		}
		// 0 A.D. hotkeys/misc.xml: the toggles save the choice (ConfigDB_CreateAndSaveValue).
		auto toggle_overlay = [this](const char *key) {
			rtso::user_config::set(key, rtso::user_config::get_bool(key) ? "false" : "true");
			rtso::user_config::save();
			refresh_user_options();
		};
		if (fired("session.toggleattackrange")) { toggle_overlay("gui.session.attackrange"); return; }
		if (fired("session.toggleaurasrange")) { toggle_overlay("gui.session.aurasrange"); return; }
		if (fired("session.togglehealrange")) { toggle_overlay("gui.session.healrange"); return; }

		if (fired("fps.toggle")) {
			if (debug_overlay) debug_overlay->set_visible(!debug_overlay->is_visible());
			return;
		}

		if (fired("screenshot")) {
			Viewport *vp = get_viewport();
			if (vp) {
				Ref<ViewportTexture> tex = vp->get_texture();
				if (tex.is_valid()) {
					Ref<Image> img = tex->get_image();
					if (img.is_valid()) {
						DirAccess::make_dir_recursive_absolute("user://screenshots");
						const String path = "user://screenshots/screenshot" +
								String::num_int64((int64_t)Time::get_singleton()->get_unix_time_from_system()) + ".png";
						img->save_png(path);
						print_line("[RTS0] Saved screenshot to " + path);
					}
				}
			}
			return;
		}
	}

	Ref<InputEventMouseButton> mb = event;
	if (mb.is_valid()) {
		Vector2 mouse_pos = mb->get_position();

		if (mb->get_button_index() == MOUSE_BUTTON_LEFT) {
			if (mb->is_pressed()) {
				// Hold K and click to send a flare (0 A.D. session.flare).
				if (rtso::hotkeys::held("session.flare")) {
					const Vector3 click_pos = get_ground_click_pos(mouse_pos);
					trigger_flare(click_pos.x, click_pos.z);
					return;
				}

				if (build_mode) {
					if (current_build_type == rtso::BLD_WALL_SEGMENT || current_build_type == rtso::BLD_PALISADE ||
							current_build_type == rtso::BLD_SIEGE_WALL) {
						wall_drag_mode = true;
						wall_drag_start_pos = get_ground_click_pos(mouse_pos);
						return;
					}
					// The site is fixed at the press; the release places it (input.js INPUT_BUILDING_CLICK).
					auto [cell_x, cell_y] = mouse_cell(mouse_pos);
					build_click_pending = true;
					build_click_screen = mouse_pos;
					build_click_ground = get_ground_click_pos(mouse_pos);
					build_click_cell_x = cell_x;
					build_click_cell_y = cell_y;
					return;
				}
				// Godot flags the second press of a double-click (0 A.D. ev.clicks == 2,
				// input.js:893); consumed on release below.
				// A third press soon after a double-click is 0 A.D.'s triple-click
				// (ev.clicks == 3: the exact template and rank).
				{
					const uint64_t now = Time::get_singleton()->get_ticks_msec();
					if (mb->is_double_click()) {
						click_count_ = 2;
						last_double_click_ms_ = now;
						last_double_click_pos_ = mouse_pos;
					} else if (last_double_click_ms_ != 0 && now - last_double_click_ms_ < 500 &&
							mouse_pos.distance_to(last_double_click_pos_) < 8.0f) {
						click_count_ = 3;
						last_double_click_ms_ = 0;
					} else {
						click_count_ = 1;
					}
				}
				double_click_pending = click_count_ >= 2;
				dragging = true;
				drag_start = mouse_pos;
				sel_rect->set_rect(drag_start, mouse_pos, true);
			} else {
				const bool dbl_click = double_click_pending;
				double_click_pending = false;
				if (build_click_pending) {
					build_click_pending = false;
					const int cell_x = build_click_cell_x, cell_y = build_click_cell_y;
					const float fp = (float)rtso::building_type_info(current_build_type).footprint;
					const float angle = placement_angle_at(current_build_type, (cell_x + fp * 0.5f) * rtso::Simulation::GRID,
							(cell_y + fp * 0.5f) * rtso::Simulation::GRID);
					// Shift or Alt keeps placing; Alt sends one builder, which leaves the
					// selection (input.js:895 and popOneFromSelection).
					const bool order_one = rtso::hotkeys::held("session.orderone") && !selected_ids.is_empty();
					const bool keep_placing = order_one || rtso::hotkeys::held("session.queue");
					const uint32_t one_builder = order_one ? (uint32_t)selected_ids[0] : 0;
					const uint32_t *builders = order_one ? &one_builder : (const uint32_t *)selected_ids.ptr();
					const size_t builder_n = order_one ? 1 : (size_t)selected_ids.size();
					if (turn_cmds()) {
						post_lockstep(5, builders, builder_n,
								(float)cell_x, (float)cell_y, rtso::Simulation::pack_angle(angle), current_build_type);
						play_civ_voice("build");
						if (order_one) pop_from_selection(selected_ids, one_builder);
						if (!keep_placing) enter_build_mode(false);
					} else if (uint32_t bid = sim->cmd_build(local_player(), cell_x, cell_y, current_build_type, angle)) {
						if (builder_n > 0)
							sim->order_build_or_repair(builders, builder_n, bid);
						play_civ_voice("build");
						if (order_one) pop_from_selection(selected_ids, one_builder);
						if (!keep_placing) enter_build_mode(false);
					} else {
						session_audio_.play_invalid_placement();
					}
					return;
				}
				if (wall_drag_mode) {
					wall_drag_mode = false;
					for (auto *wg : wall_ghosts) {
						if (wg) wg->set_visible(false);
					}
					const Vector3 drag_end_pos = get_ground_click_pos(mouse_pos);
					// Commands.js "construct-wall": the sim lays the pieces (helpers/Walls.js) and
					// snaps the ends onto the player's wall towers, the same on every peer.
					// Alt hands the whole wall to one builder (popOneFromSelection).
					const bool order_one = rtso::hotkeys::held("session.orderone") && !selected_ids.is_empty();
					const uint32_t one_builder = order_one ? (uint32_t)selected_ids[0] : 0;
					const uint32_t *builders = order_one ? &one_builder : (const uint32_t *)selected_ids.ptr();
					const size_t builder_n = order_one ? 1 : (size_t)selected_ids.size();
					const bool queued = rtso::hotkeys::held("session.queue");
					const uint8_t wall_kind = rtso::wallset_kind_of((uint8_t)current_build_type);
					const bool any = sim && !sim->wall_placement(local_player(), wall_drag_start_pos.x, wall_drag_start_pos.z,
							drag_end_pos.x, drag_end_pos.z, nullptr, nullptr, wall_kind).empty();
					if (any) {
						if (turn_cmds()) {
							post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_BUILD_WALL, builders, builder_n,
									drag_end_pos.x, drag_end_pos.z,
									rtso::Simulation::pack_wall_point(wall_drag_start_pos.x, wall_drag_start_pos.z),
									(uint8_t)(wall_kind | (queued ? rtso::NET_PARAM_QUEUED : 0)));
						} else {
							sim->cmd_build_wall(local_player(), wall_drag_start_pos.x, wall_drag_start_pos.z,
									drag_end_pos.x, drag_end_pos.z, builders, builder_n, queued, wall_kind);
						}
						play_civ_voice("build");
						if (order_one) pop_from_selection(selected_ids, one_builder);
					}
					enter_build_mode(false);
					return;
				}
				if (dragging) {
					dragging = false;
					sel_rect->set_rect(Vector2(), Vector2(), false);
					Vector2 drag_end = mouse_pos;
					// 0 A.D. [hotkey.selection] modifiers (input.js handleInputBeforeGui):
					// Shift adds, Ctrl removes, Alt keeps military only (Alt+Y non-military),
					// I idle only, O wounded only, Alt on a double-click reaches offscreen.
					const bool is_shift = rtso::hotkeys::held("selection.add");
					const bool is_ctrl = rtso::hotkeys::held("selection.remove");
					const bool nonmilitary_only = rtso::hotkeys::held("selection.nonmilitaryonly");
					const bool military_only = !nonmilitary_only && rtso::hotkeys::held("selection.militaryonly");
					const bool idle_only = rtso::hotkeys::held("selection.idleonly");
					const bool wounded_only = rtso::hotkeys::held("selection.woundedonly");
					const bool offscreen = rtso::hotkeys::held("selection.offscreen");

					if (drag_start.distance_to(drag_end) < user_opts_.drag_delta) {
						// Single click selection
						Vector3 click_pos = get_ground_click_pos(mouse_pos);
						auto uview = sim->units();

						// A structure the ray reaches first hides the units behind it.
						float bld_t = 1e30f;
						building_under_mouse(mouse_pos, &bld_t);
						Camera3D *pick_cam = cam_rig ? cam_rig->get_camera() : nullptr;
						const Vector3 ray_o = pick_cam ? pick_cam->project_ray_origin(mouse_pos) : Vector3();
						const Vector3 ray_d = pick_cam ? pick_cam->project_ray_normal(mouse_pos) : Vector3();

						// Check unit hit
						size_t best_uidx = (size_t)-1;
						float best_ud2 = 2.0f * 2.0f;
						for (size_t i = 0; i < uview.count; ++i) {
							if (!uview.alive[i] || uview.team[i] != local_player()) continue;
							if (pick_cam && bld_t < 1e29f) {
								const Vector3 up(uview.pos_x[i], sim->get_terrain_height(uview.pos_x[i], uview.pos_z[i]), uview.pos_z[i]);
								if ((up - ray_o).dot(ray_d) > bld_t + 0.5f) continue;
							}
							float dx = uview.pos_x[i] - click_pos.x;
							float dz = uview.pos_z[i] - click_pos.z;
							float d2 = dx * dx + dz * dz;
							if (d2 < best_ud2) {
								best_ud2 = d2;
								best_uidx = i;
							}
						}

						if (best_uidx != (size_t)-1 && is_ctrl && !dbl_click) {
							// Ctrl+click drops the unit from the selection (selection.remove).
							const int64_t at = selected_ids.find((int32_t)uview.id[best_uidx]);
							if (at >= 0) selected_ids.remove_at(at);
						} else if (best_uidx != (size_t)-1) {
							if (dbl_click) {
								// Double-click selects the player's units of the clicked one's template
								// (any rank), triple-click its exact template and rank, on screen or on
								// the whole map with selection.offscreen; Ctrl removes them instead
								// (0 A.D. gui/session/input.js:1009-1042, selectionGroupName).
								const uint8_t same_type = uview.unit_type[best_uidx];
								const uint16_t same_tmpl = uview.tmpl[best_uidx];
								const uint8_t same_rank = uview.rank[best_uidx];
								const bool exact = click_count_ >= 3;
								auto matches = [&](size_t i) {
									if (same_tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? uview.tmpl[i] != same_tmpl : uview.unit_type[i] != same_type)
										return false;
									return !exact || uview.rank[i] == same_rank;
								};
								Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
								if (!is_shift && !is_ctrl) {
									selected_ids.clear();
								}
								selected_building_id = 0;
								if (cam) {
									const Vector2 vp_size = get_viewport()->get_visible_rect().size;
									for (size_t i = 0; i < uview.count; ++i) {
										if (!uview.alive[i] || uview.team[i] != local_player() || !matches(i))
											continue;
										Vector3 world_pos(uview.pos_x[i], 0, uview.pos_z[i]);
										if (!offscreen && cam->is_position_behind(world_pos)) continue;
										Vector2 screen_pos = cam->unproject_position(world_pos);
										if (offscreen || (screen_pos.x >= 0.0f && screen_pos.x <= vp_size.x &&
											screen_pos.y >= 0.0f && screen_pos.y <= vp_size.y)) {
											int32_t uid = (int32_t)uview.id[i];
											const int64_t at = selected_ids.find(uid);
											if (is_ctrl) {
												if (at >= 0) selected_ids.remove_at(at);
											} else if (at < 0) {
												selected_ids.push_back(uid);
											}
										}
									}
								}
								if (selected_ids.is_empty() && !is_ctrl)
									selected_ids.push_back((int32_t)uview.id[best_uidx]);
							} else if (is_shift) {
								// Shift+click adds the unit (selection.add -> addList keeps it if
								// already selected; Ctrl+click is the removal).
								const int32_t uid = (int32_t)uview.id[best_uidx];
								if (!selected_ids.has(uid)) selected_ids.push_back(uid);
								selected_building_id = 0;
							} else {
								selected_ids.clear();
								selected_building_id = 0;
								selected_ids.push_back((int32_t)uview.id[best_uidx]);
							}
							play_civ_voice("select");
						} else if (uint32_t animal = own_fauna_at(click_pos.x, click_pos.z)) {
							// The player's own livestock (0 A.D. livestock are units).
							const int64_t at = selected_ids.find((int32_t)animal);
							if (is_ctrl) {
								if (at >= 0) selected_ids.remove_at(at);
							} else {
								if (!is_shift) selected_ids.clear();
								selected_building_id = 0;
								if (at < 0) selected_ids.push_back((int32_t)animal);
							}
						} else {
							// Check building hit: any visible part (0 A.D. picks the
							// rendered entity), then the old ground test round its base.
							uint32_t hit_bld = building_under_mouse(mouse_pos, nullptr);
							for (const auto &b : sim->buildings()) {
								if (hit_bld != 0) break;
								if (b.team != local_player()) continue;
								float dx = b.x - click_pos.x;
								float dz = b.z - click_pos.z;
								float hit_r = b.half_size + 0.6f;
								if (dx * dx + dz * dz <= hit_r * hit_r) {
									hit_bld = b.id;
									break;
								}
							}
							if (hit_bld != 0) {
								selected_building_id = hit_bld;
								if (!is_shift) selected_ids.clear();
							} else if (!is_ctrl && inspect_at(click_pos)) {
								// Another player's unit or structure, an animal or a resource:
								// selected for its details only (0 A.D. EntitySelection).
								selected_ids.clear();
								selected_building_id = 0;
							} else {
								// Clicked empty ground: deselect if shift not pressed
								if (!is_shift) {
									selected_ids.clear();
									selected_building_id = 0;
								}
							}
						}
					} else {
						// Box selection
						Vector2 min_p(std::min(drag_start.x, drag_end.x), std::min(drag_start.y, drag_end.y));
						Vector2 max_p(std::max(drag_start.x, drag_end.x), std::max(drag_start.y, drag_end.y));

						auto uview = sim->units();
						if (!is_shift && !is_ctrl) {
							selected_ids.clear();
						}
						selected_building_id = 0;
						Camera3D *cam = cam_rig ? cam_rig->get_camera() : nullptr;
						if (cam) {
							for (size_t i = 0; i < uview.count; ++i) {
								if (!uview.alive[i] || uview.team[i] != local_player()) continue;
								const uint64_t cls = rtso::unit_tech_classes(uview.unit_type[i]);
								const bool military = (cls & (rtso::TC_MELEE | rtso::TC_RANGED)) != 0;
								if ((military_only && !military) || (nonmilitary_only && military)) continue;
								if (idle_only && uview.action_state[i] != rtso::ACTION_IDLE) continue;
								// gui.session.woundedunithotkeythreshold = 33 %.
								if (wounded_only && uview.hp[i] >= uview.max_hp[i] * user_opts_.wounded_fraction) continue;
								Vector3 world_pos(uview.pos_x[i], 0, uview.pos_z[i]);
								if (cam->is_position_behind(world_pos)) continue;
								Vector2 screen_pos = cam->unproject_position(world_pos);
								if (screen_pos.x >= min_p.x && screen_pos.x <= max_p.x &&
									screen_pos.y >= min_p.y && screen_pos.y <= max_p.y) {
									int32_t uid = (int32_t)uview.id[i];
									if (is_ctrl) {
										const int64_t at = selected_ids.find(uid);
										if (at >= 0) selected_ids.remove_at(at);
									} else if (!selected_ids.has(uid)) {
										selected_ids.push_back(uid);
									}
								}
							}
							// The player's own livestock in the box (not with the military,
							// idle or wounded filters, which pick soldiers and workers).
							auto fview = sim->fauna();
							for (size_t f = 0; f < fview.count && !military_only && !idle_only && !wounded_only; ++f) {
								if (!sim->fauna_selectable_by(local_player(), fview.id[f])) continue;
								Vector3 world_pos(fview.pos_x[f], 0, fview.pos_z[f]);
								if (cam->is_position_behind(world_pos)) continue;
								Vector2 screen_pos = cam->unproject_position(world_pos);
								if (screen_pos.x < min_p.x || screen_pos.x > max_p.x || screen_pos.y < min_p.y || screen_pos.y > max_p.y)
									continue;
								const int32_t fid = (int32_t)fview.id[f];
								const int64_t at = selected_ids.find(fid);
								if (is_ctrl) {
									if (at >= 0) selected_ids.remove_at(at);
								} else if (at < 0) {
									selected_ids.push_back(fid);
								}
							}
							if (selected_ids.size() > 0) {
								play_civ_voice("select");
							}
						}
					}
				}
			}
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT && mb->is_pressed()) {
			// A right-click while band-boxing cancels the box and gives no order
			// (input.js INPUT_BANDBOXING: right button resets to INPUT_NORMAL).
			if (dragging && drag_start.distance_to(mouse_pos) > 4.0f) {
				dragging = false;
				double_click_pending = false;
				sel_rect->set_rect(Vector2(), Vector2(), false);
				return;
			}
			if (build_mode) {
				enter_build_mode(false);
				return;
			}
			Vector3 click_pos = get_ground_click_pos(mouse_pos);

			// Right-clicking the selected structure itself drops its rally point
			// (unit_actions.js "unset-rallypoint", lockstep command 14 param 1).
			if (over_own_rally_structure(click_pos)) {
				if (turn_cmds())
					post_lockstep(14, nullptr, 0, 0, 0, selected_building_id, 1);
				else
					sim->cmd_unset_rally_point(local_player(), selected_building_id);
				return;
			}
			// If building selected, right click sets Rally Point!
			if (selected_building_id != 0) {
				if (try_focus_fire(click_pos)) return;
				uint32_t target_res = 0, target_bld = 0;
				auto rview = sim->resources();
				for (size_t r = 0; r < rview.count; ++r) {
					if (!rview.alive[r]) continue;
					float dx = rview.x[r] - click_pos.x;
					float dz = rview.z[r] - click_pos.z;
					if (dx * dx + dz * dz < 2.0f * 2.0f) { target_res = rview.id[r]; break; }
				}
				for (const auto &b : sim->buildings()) {
					if (b.id == selected_building_id) continue;
					float dx = b.x - click_pos.x;
					float dz = b.z - click_pos.z;
					if (dx * dx + dz * dz < 3.0f * 3.0f) { target_bld = b.id; break; }
				}
				// session.queue (Shift) adds a position after the ones set (RallyPoint.AddPosition).
				const bool rally_queued = rtso::hotkeys::held("session.queue");
				if (turn_cmds()) {
					uint32_t extra[2] = {target_res, target_bld};
					post_lockstep(14, extra, 2, click_pos.x, click_pos.z, selected_building_id,
							rally_queued ? rtso::NET_PARAM_QUEUED : 0);
				} else {
					sim->cmd_set_rally_point(local_player(), selected_building_id, click_pos.x, click_pos.z, target_res, target_bld,
							rally_queued);
				}
				play_civ_voice("select");
				return;
			}

			if (selected_ids.is_empty()) return;

			// Shift queues the order behind the units' current ones (0 A.D.
			// gui/session/input.js:1345 session.queue); without it the order replaces them.
			const bool queue_shift = rtso::hotkeys::held("session.queue");
			const uint8_t qbit = queue_shift ? rtso::NET_PARAM_QUEUED : 0;
			const uint32_t *sel_ptr = (const uint32_t *)selected_ids.ptr();
			size_t sel_n = selected_ids.size();
			// Alt orders only one unit, which then leaves the selection (0 A.D.
			// gui/session/input.js popOneFromSelection, session.orderone).
			uint32_t one_id = 0;
			struct PopOne {
				PackedInt32Array &sel;
				const uint32_t &id;
				~PopOne() {
					if (id != 0) pop_from_selection(sel, id);
				}
			} pop_one{ selected_ids, one_id };
			if (rtso::hotkeys::held("session.orderone")) {
				one_id = sel_ptr[0];
				sel_ptr = &one_id;
				sel_n = 1;
			}

			// Owned livestock alone (0 A.D. livestock are units): a right-click walks it, or
			// sends it into the player's own corral under the cursor (UnitAI "garrison").
			bool fauna_only = sel_n > 0;
			for (size_t k = 0; k < sel_n && fauna_only; ++k) fauna_only = sim->fauna_selectable_by(local_player(), sel_ptr[k]);
			if (fauna_only) {
				uint32_t corral = 0;
				for (const auto &b : sim->buildings()) {
					if (b.team != local_player() || b.type != rtso::BLD_CORRAL) continue;
					const float dx = b.x - click_pos.x, dz = b.z - click_pos.z, hit_r = b.half_size + 1.2f;
					if (dx * dx + dz * dz <= hit_r * hit_r) corral = b.id;
				}
				if (!corral)
					range_overlays_.add_marker(click_pos.x, click_pos.z, Color(1, 1, 1, 0.9f), RtsoRangeOverlays::kMoveMarkerTime);
				if (turn_cmds()) {
					if (corral) post_lockstep(12, sel_ptr, sel_n, 0, 0, corral, qbit);
					else post_lockstep(1, sel_ptr, sel_n, click_pos.x, click_pos.z, 0, qbit);
				} else {
					sim->order_fauna_walk(local_player(), sel_ptr, sel_n, click_pos.x, click_pos.z, corral);
				}
				return;
			}

			// Ctrl over an enemy structure attacks it instead of capturing it (0 A.D.
			// session.attack); Ctrl over anything else is attack-move (session.attackmove).
			if ((rtso::hotkeys::held("session.attack") || rtso::hotkeys::held("session.attackmove")) && !attack_move_mode) {
				for (const auto &b : sim->buildings()) {
					if (b.wrecked || b.team == local_player()) continue;
					const float dx = b.x - click_pos.x, dz = b.z - click_pos.z;
					const float hit_r = b.half_size + 1.2f;
					if (dx * dx + dz * dz > hit_r * hit_r) continue;
					if (turn_cmds())
						post_lockstep(15, sel_ptr, sel_n, 0, 0, b.id, (uint8_t)(qbit | rtso::NET_PARAM_NO_CAPTURE));
					else
						sim->order_attack_building(sel_ptr, sel_n, b.id, queue_shift, false);
					play_civ_voice("attack");
					return;
				}
			}

			// Attack-move: session.attackmove (Ctrl) + right-click, or attack_move_mode
			// toggled. Ctrl on one of our own buildings garrisons instead (session.garrison).
			// The Garrison and Repair buttons arm one click, like holding the modifier.
			const bool force_garrison = garrison_mode || rtso::hotkeys::held("session.garrison");
			const bool force_repair = repair_mode || rtso::hotkeys::held("session.repair");
			garrison_mode = repair_mode = false;
			bool garrison_click = false;
			if (force_garrison) {
				for (const auto &b : sim->buildings()) {
					if (b.team != local_player() && !sim->is_ally(local_player(), b.team)) continue; // own or allied
					const float dx = b.x - click_pos.x, dz = b.z - click_pos.z, hit_r = b.half_size + 1.2f;
					if (dx * dx + dz * dz <= hit_r * hit_r) {
						garrison_click = true;
						break;
					}
				}
			}
			// session.calltoarms + right-click: Commands.js call-to-arms (drop loads, then attack-walk).
			if (call_to_arms_mode || (!garrison_click && rtso::hotkeys::held("session.calltoarms"))) {
				call_to_arms_mode = false;
				range_overlays_.add_marker(click_pos.x, click_pos.z, Color(1, 1, 1, 0.9f), RtsoRangeOverlays::kMoveMarkerTime);
				if (turn_cmds())
					post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_CALL_TO_ARMS, sel_ptr, sel_n, click_pos.x, click_pos.z, 0, qbit);
				else
					sim->order_call_to_arms(sel_ptr, sel_n, click_pos.x, click_pos.z, queue_shift);
				play_civ_voice("attack");
				return;
			}
			if (attack_move_mode || (!garrison_click && rtso::hotkeys::held("session.attackmove"))) {
				range_overlays_.add_marker(click_pos.x, click_pos.z, Color(1, 1, 1, 0.9f), RtsoRangeOverlays::kMoveMarkerTime);
				if (turn_cmds())
					post_lockstep(10, sel_ptr, sel_n, click_pos.x, click_pos.z, 0, qbit);
				else
					sim->order_attack_move(sel_ptr, sel_n, click_pos.x, click_pos.z, queue_shift, false, true);
				play_civ_voice("attack");
				attack_move_mode = false;
				return;
			}
			// Patrol and guard: the command button arms them, or hold P / G while
			// right-clicking (0 A.D. session.patrol / session.guard modifiers).
			if (patrol_mode || rtso::hotkeys::held("session.patrol")) {
				if (turn_cmds())
					post_lockstep(11, sel_ptr, sel_n, click_pos.x, click_pos.z, 0, qbit);
				else
					sim->order_patrol(sel_ptr, sel_n, click_pos.x, click_pos.z, queue_shift);
				play_civ_voice("select");
				patrol_mode = false;
				return;
			}
			if (guard_mode || rtso::hotkeys::held("session.guard")) {
				auto uview = sim->units();
				uint32_t target_ally_id = 0;
				float best_ad2 = 3.0f * 3.0f;
				for (size_t i = 0; i < uview.count; ++i) {
					if (!uview.alive[i] || uview.team[i] != local_player()) continue;
					float dx = uview.pos_x[i] - click_pos.x;
					float dz = uview.pos_z[i] - click_pos.z;
					float d2 = dx * dx + dz * dz;
					if (d2 < best_ad2) {
						best_ad2 = d2;
						target_ally_id = uview.id[i];
					}
				}
				// Guard.js guards structures too: one's own or an ally's under the cursor.
				if (target_ally_id == 0) {
					for (const auto &b : sim->buildings()) {
						if (b.wrecked || (b.team != local_player() && !sim->is_ally(local_player(), b.team))) continue;
						if (std::fabs(b.x - click_pos.x) <= b.half_size && std::fabs(b.z - click_pos.z) <= b.half_size) {
							target_ally_id = b.id;
							break;
						}
					}
				}
				if (target_ally_id != 0) {
					if (turn_cmds())
						post_lockstep(21, sel_ptr, sel_n, 0, 0, target_ally_id, qbit);
					else
						sim->order_guard(sel_ptr, sel_n, target_ally_id, queue_shift);
					play_civ_voice("select");
				}
				guard_mode = false;
				return;
			}

			auto uview = sim->units();

			// 0. Right click with Loaded Transport on Shore/Land (Disembark)
			bool has_loaded_transport = false;
			uint32_t transport_sel_id = 0;
			for (int s = 0; s < selected_ids.size(); ++s) {
				if (sim->is_transport((uint32_t)selected_ids[s]) && sim->get_transport_passenger_count((uint32_t)selected_ids[s]) > 0) {
					has_loaded_transport = true;
					transport_sel_id = (uint32_t)selected_ids[s];
					break;
				}
			}
			if (has_loaded_transport && sim->get_terrain_height(click_pos.x, click_pos.z) >= sim->water_height() + 0.1f) {
				if (one_id != 0) one_id = transport_sel_id;
				if (turn_cmds())
					post_lockstep(19, &transport_sel_id, 1, click_pos.x, click_pos.z);
				else
					sim->order_disembark(transport_sel_id, click_pos.x, click_pos.z);
				play_civ_voice("move");
				return;
			}

			// 1. Right click on Allied War Trireme / Transport (Board Naval Transport)
			uint32_t clicked_transport_id = 0;
			for (size_t i = 0; i < uview.count; ++i) {
				if (!uview.alive[i] || uview.team[i] != local_player() || uview.unit_type[i] != rtso::UNIT_TRIREME) continue;
				float dx = uview.pos_x[i] - click_pos.x;
				float dz = uview.pos_z[i] - click_pos.z;
				if (dx * dx + dz * dz <= 3.2f * 3.2f) {
					clicked_transport_id = uview.id[i];
					break;
				}
			}
			if (clicked_transport_id != 0) {
				if (turn_cmds())
					post_lockstep(18, sel_ptr, sel_n, 0, 0, clicked_transport_id);
				else
					sim->order_board_transport(sel_ptr, sel_n, clicked_transport_id);
				play_civ_voice("move");
				return;
			}

			// 1b. Builders right-clicking a damaged own or allied siege engine or ship repair it
			// (0 A.D. unit_actions "repair": Repairable units, Builder.js CanRepair).
			{
				bool any_builder = false;
				for (int k = 0; k < selected_ids.size() && !any_builder; ++k)
					for (size_t i = 0; i < uview.count; ++i)
						if (uview.id[i] == (uint32_t)selected_ids[k]) {
							any_builder = rtso::unit_can_build(uview.unit_type[i], uview.tmpl ? uview.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE);
							break;
						}
				uint32_t repair_id = 0;
				float best_rd2 = 2.2f * 2.2f;
				for (size_t i = 0; any_builder && i < uview.count; ++i) {
					if (!uview.alive[i] || uview.hp[i] >= uview.max_hp[i] || !sim->is_repairable_unit(uview.id[i])) continue;
					if (uview.team[i] != local_player() && !sim->is_ally(local_player(), uview.team[i])) continue;
					const float dx = uview.pos_x[i] - click_pos.x, dz = uview.pos_z[i] - click_pos.z;
					const float d2 = dx * dx + dz * dz;
					if (d2 < best_rd2) { best_rd2 = d2; repair_id = uview.id[i]; }
				}
				if (repair_id != 0) {
					if (turn_cmds())
						post_lockstep(16, sel_ptr, sel_n, 0, 0, repair_id, qbit);
					else
						sim->order_build_or_repair(sel_ptr, sel_n, repair_id, queue_shift);
					play_civ_voice("build");
					return;
				}
			}

			// 1c. Healers right-clicking an injured own or allied unit heal it (0 A.D. "heal" cursor).
			{
				// With orderone the first healer of the selection is the one ordered
				// (input.js firstAbleEntity).
				uint32_t first_healer = 0;
				for (int k = 0; k < selected_ids.size() && !first_healer; ++k) {
					for (size_t i = 0; i < uview.count; ++i)
						if (uview.id[i] == (uint32_t)selected_ids[k]) {
							if (rtso::unit_is_healer(uview.tmpl ? uview.tmpl[i] : rtso::CIV_UNIT_TEMPLATE_NONE, uview.unit_type[i])) first_healer = uview.id[i];
							break;
						}
				}
				const bool any_healer = first_healer != 0;
				uint32_t heal_id = 0;
				float best_hd2 = 2.2f * 2.2f;
				for (size_t i = 0; any_healer && i < uview.count; ++i) {
					if (!uview.alive[i] || uview.hp[i] >= uview.max_hp[i]) continue;
					if (uview.team[i] != local_player() && !sim->is_ally(local_player(), uview.team[i])) continue;
					const float dx = uview.pos_x[i] - click_pos.x, dz = uview.pos_z[i] - click_pos.z;
					const float d2 = dx * dx + dz * dz;
					if (d2 < best_hd2) { best_hd2 = d2; heal_id = uview.id[i]; }
				}
				if (heal_id != 0) {
					if (one_id != 0) one_id = first_healer;
					if (turn_cmds())
						post_lockstep(32, sel_ptr, sel_n, 0, 0, heal_id, qbit);
					else
						sim->order_heal(sel_ptr, sel_n, heal_id, queue_shift);
					play_civ_voice("heal");
					return;
				}
			}

			// 1d. A Gaia treasure: collect it (0 A.D. unit_actions "collect-treasure").
			{
				uint32_t treasure_id = 0;
				float best_td2 = 2.0f * 2.0f;
				for (const auto &t : sim->treasures()) {
					const float dx = t.x - click_pos.x, dz = t.z - click_pos.z;
					if (dx * dx + dz * dz < best_td2) { best_td2 = dx * dx + dz * dz; treasure_id = t.id; }
				}
				if (treasure_id != 0) {
					if (turn_cmds())
						post_lockstep(17, sel_ptr, sel_n, 0, 0, treasure_id, (uint8_t)(2 | qbit));
					else
						sim->order_collect_treasure(sel_ptr, sel_n, treasure_id, queue_shift);
					play_civ_voice("gather");
					return;
				}
			}

			// 2. Right click on Enemy Unit (Attack)
			uint32_t clicked_enemy_id = 0;
			float best_ed2 = 2.2f * 2.2f;
			for (size_t i = 0; i < uview.count; ++i) {
				if (!uview.alive[i] || uview.team[i] == local_player()) continue;
				float dx = uview.pos_x[i] - click_pos.x;
				float dz = uview.pos_z[i] - click_pos.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best_ed2) {
					best_ed2 = d2;
					clicked_enemy_id = uview.id[i];
				}
			}
			if (clicked_enemy_id != 0) {
				if (turn_cmds())
					post_lockstep(3, sel_ptr, sel_n, 0, 0, clicked_enemy_id, qbit);
				else
					sim->order_attack(sel_ptr, sel_n, clicked_enemy_id, queue_shift);
				play_civ_voice("attack");
				return;
			}

			// 3. Right click on Wildlife Fauna (Hunting vs Herding into Corral)
			auto fview = sim->fauna();
			uint32_t clicked_fauna_id = 0;
			uint8_t clicked_fauna_type = 0;
			float best_fd2 = 2.5f * 2.5f;
			size_t best_f = 0;
			for (size_t f = 0; f < fview.count; ++f) {
				if (!fview.alive[f]) continue;
				float dx = fview.pos_x[f] - click_pos.x;
				float dz = fview.pos_z[f] - click_pos.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best_fd2) {
					best_fd2 = d2;
					clicked_fauna_id = fview.id[f];
					clicked_fauna_type = fview.fauna_type[f];
					best_f = f;
				}
			}
			if (clicked_fauna_id != 0) {
				const auto &finfo = rtso::fauna_type_info(clicked_fauna_type);
				// 0 A.D.: a right-click on livestock slaughters it, whoever owns it. RTS0 can't select
				// animals, so Alt+right-click on your own livestock herds it to your corral instead.
				uint8_t herd = finfo.is_domestic() && fview.behavior[best_f] != rtso::FAUNA_CORRALLED &&
				               fview.owner[best_f] == local_player() && Input::get_singleton()->is_key_pressed(KEY_ALT) ? 1 : 0;
				if (turn_cmds())
					post_lockstep(17, sel_ptr, sel_n, 0, 0, clicked_fauna_id, herd);
				else if (herd)
					sim->order_herd(sel_ptr, sel_n, clicked_fauna_id);
				else
					sim->order_hunt(sel_ptr, sel_n, clicked_fauna_id);
				play_civ_voice(herd ? "gather" : "attack");
				return;
			}

			// Right click on Gaia scenery (a fence, column, obelisk): attack it (template_structure
			// Health; capturing scenery is not modelled).
			if (const uint32_t sid = scenery_at(click_pos.x, click_pos.z); sid != 0) {
				if (turn_cmds())
					post_lockstep(17, sel_ptr, sel_n, 0, 0, sid, 0);
				else
					sim->order_attack_scenery(sel_ptr, sel_n, sid);
				play_civ_voice("attack");
				return;
			}

			// 3. Right click on Resource Node (Gather)
			auto rview = sim->resources();
			uint32_t clicked_res_id = 0;
			float best_rd2 = 2.2f * 2.2f;
			for (size_t r = 0; r < rview.count; ++r) {
				if (!rview.alive[r] || rview.amount[r] <= 0) continue;
				float dx = rview.x[r] - click_pos.x;
				float dz = rview.z[r] - click_pos.z;
				float d2 = dx * dx + dz * dz;
				if (d2 < best_rd2) {
					best_rd2 = d2;
					clicked_res_id = rview.id[r];
				}
			}
			if (clicked_res_id != 0) {
				if (turn_cmds())
					post_lockstep(4, sel_ptr, sel_n, 0, 0, clicked_res_id, qbit);
				else
					sim->order_gather(sel_ptr, sel_n, clicked_res_id, queue_shift);
				play_civ_voice("gather");
				return;
			}

			// 4. Right click on Building (Attack Enemy vs Build/Repair/Garrison Allied)
			for (const auto &b : sim->buildings()) {
				float dx = b.x - click_pos.x;
				float dz = b.z - click_pos.z;
				float hit_r = b.half_size + 1.2f;
				if (dx * dx + dz * dz <= hit_r * hit_r) {
					const uint32_t gaia_grain = b.team == rtso::GAIA_PLAYER && b.type == rtso::BLD_FIELD ? sim->field_resource_id(b.id) : 0;
					const bool friendly = b.team == local_player() || sim->is_ally(local_player(), b.team);
					if (force_garrison && friendly && selection_can_garrison(b.id)) {
						// session.garrison is a hotkeyActionCheck: it wins over returning
						// resources and repairing (unit_actions.js "garrison").
						if (turn_cmds())
							post_lockstep(12, sel_ptr, sel_n, 0, 0, b.id, qbit);
						else
							sim->order_garrison(sel_ptr, sel_n, b.id, queue_shift);
						clear_selection();
					} else if (force_repair && friendly && (b.build_progress < 1.0f || b.hp < b.max_hp)) {
						// session.repair likewise wins over returning resources (unit_actions.js "repair").
						if (turn_cmds())
							post_lockstep(16, sel_ptr, sel_n, 0, 0, b.id, qbit);
						else
							sim->order_build_or_repair(sel_ptr, sel_n, b.id, queue_shift);
						play_civ_voice("build");
					} else if (trade_route_click(b, true, queue_shift, nullptr)) {
						// setup-trade-route has specificness 0: it wins over garrison and capture.
					} else if (gaia_grain) { // a Gaia field: anyone may farm it (0 A.D. "gather" comes before "capture")
						if (turn_cmds())
							post_lockstep(4, sel_ptr, sel_n, 0, 0, gaia_grain, qbit);
						else
							sim->order_gather(sel_ptr, sel_n, gaia_grain, queue_shift);
						play_civ_voice("gather");
					} else if (b.team != local_player() && sim->is_hostile(local_player(), b.team)) {
						if (turn_cmds())
							post_lockstep(15, sel_ptr, sel_n, 0, 0, b.id, qbit); // capture (0 A.D. default)
						else
							sim->order_attack_building(sel_ptr, sel_n, b.id, queue_shift, true);
						play_civ_voice("attack");
					} else if (b.team != local_player() && !sim->is_ally(local_player(), b.team)) {
						break; // neutral: walk there
					} else if (b.team != local_player()) {
						// An ally's structure: repair it, else garrison (Garrisonable / IsOwnedByMutualAllyOfEntity).
						if (b.build_progress < 1.0f || b.hp < b.max_hp) {
							if (turn_cmds())
								post_lockstep(16, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_build_or_repair(sel_ptr, sel_n, b.id, queue_shift);
							play_civ_voice("build");
						} else if (selection_can_garrison(b.id)) {
							if (turn_cmds())
								post_lockstep(12, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_garrison(sel_ptr, sel_n, b.id, queue_shift);
							clear_selection();
						} else {
							break; // nothing to do there: walk
						}
					} else {
						// 0 A.D. "returnresource" comes first: carriers take their load to this dropsite.
						bool carriers = false;
						for (size_t k = 0; k < sel_n && !carriers; ++k) carriers = sim->can_return_resource(sel_ptr[k], b.id);
						if (carriers) {
							if (turn_cmds())
								post_lockstep(34, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_return_resource(sel_ptr, sel_n, b.id, queue_shift);
							play_civ_voice("gather");
						} else if (b.build_progress < 1.0f || b.hp < b.max_hp || b.type == rtso::BLD_FIELD) { // finished fields: farm
							if (turn_cmds())
								post_lockstep(16, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_build_or_repair(sel_ptr, sel_n, b.id, queue_shift);
							play_civ_voice("build");
						} else if (b.type == rtso::BLD_WALL_SEGMENT && Input::get_singleton()->is_key_pressed(KEY_CTRL)) {
							// 0 A.D. Upgrade.js wall -> gate (lockstep cmd 26), so replays and peers see it.
							if (turn_cmds())
								post_lockstep(26, nullptr, 0, 0, 0, b.id, 0);
							else
								sim->cmd_upgrade_building(local_player(), b.id);
							play_civ_voice("build");
						} else if (selection_can_garrison(b.id)) {
							if (turn_cmds())
								post_lockstep(12, sel_ptr, sel_n, 0, 0, b.id, qbit);
							else
								sim->order_garrison(sel_ptr, sel_n, b.id, queue_shift);
							clear_selection();
						} else {
							break; // no selected unit is on its GarrisonHolder List: walk
						}
					}
					return;
				}
			}

			// 5. Default: Move Order (a player's walk ignores enemies until it arrives)
			range_overlays_.add_marker(click_pos.x, click_pos.z, Color(1, 1, 1, 0.9f), RtsoRangeOverlays::kMoveMarkerTime);
			if (turn_cmds())
				post_lockstep(1, sel_ptr, sel_n, click_pos.x, click_pos.z, 0, qbit);
			else {
				sim->order_walk(sel_ptr, sel_n, click_pos.x, click_pos.z, queue_shift, true);
				sim->order_fauna_walk(local_player(), sel_ptr, sel_n, click_pos.x, click_pos.z);
			}
			play_civ_voice("move");
		}
	}

	Ref<InputEventMouseMotion> mm = event;
	if (mm.is_valid() && dragging) {
		sel_rect->set_rect(drag_start, mm->get_position(), true);
	}
}

// Full GameSetup hand-off (0 A.D. page_gamesetup -> match launch).
// Runs at the end of _ready (view + cam_rig exist) and mirrors
// apply_game_setup (NMT_GAME_START path): map first via load_map_seeded,
// then victory / Petra / starting resources / slot civs.
void RtsoMain::apply_match_settings() {
	if (!sim) return;
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	if (!ms) {
		sim->spawn_initial();
		return;
	}
	String map_id = (String)ms->get("map_id");
	uint32_t seed = (uint32_t)(int64_t)(int)ms->get("seed");
	if (seed == 0) seed = (uint32_t)Time::get_singleton()->get_ticks_msec();
	match_seed_ = seed; // replays and saves rebuild the map from it
	if (map_id.is_empty()) map_id = "random_continental";
	int victory = (int)ms->get("victory");
	int diff = (int)ms->get("ai_difficulty");
	int behavior = (int)ms->get("ai_behavior");
	int start_res = (int)ms->get("start_res");
	// 0 A.D. map sizes; absent or out of range keeps the 128 default.
	int map_size = (int)ms->get("map_size");
	random_map_size = (map_size >= 128 && map_size <= 512) ? map_size : 128;
	Array slot_civs = ms->get("slot_civs");
	// Normalize legacy fields: if the setup screen never ran, slot_civs
	// may be empty - fall back to player_civ/ai_civ.
	int civ = (int)ms->get("player_civ");
	int ai = (int)ms->get("ai_civ");
	if (slot_civs.size() >= 2) {
		civ = (int)slot_civs[0];
		ai = (int)slot_civs[1];
	}
	// Seats from the lobby (0 human, 1 AI, 2 closed); a random map gets a base for each.
	Array slot_types = ms->get("slot_types");
	int seats = 0;
	for (int i = 0; i < slot_types.size() && i < rtso::MAX_PLAYERS; ++i)
		if ((int)slot_types[i] != 2) seats = i + 1;
	random_map_players = std::max(2, seats);
	local_observer_ = seats > 0;
	for (int i = 0; i < seats; ++i)
		if ((int)slot_types[i] == 0) local_observer_ = false;
	{
		Array slot_teams_arr = ms->get("slot_teams");
		for (int i = 0; i < rtso::MAX_PLAYERS; ++i) {
			const int tm = i < slot_teams_arr.size() ? (int)slot_teams_arr[i] : 0;
			random_map_teams[i] = (uint8_t)((tm >= 1 && tm <= 8) ? tm : 0);
		}
	}
	sim->set_biome_override((int)ms->get("biome") - 1); // 0 = the map's own biome
	sim->set_team_placement((int)ms->get("team_placement"));
	sim->set_map_landscape((int)ms->get("landscape"));
	sim->set_trigger_difficulty((int)ms->get("trigger_difficulty"));
	sim->set_map_daytime_choice((int)ms->get("map_daytime"));
	// Civs before the map: spawn_unit gives each starting unit its owner's
	// roster template (Simulation::reset keeps civs), as 0 A.D. spawns
	// StartEntities from the player's own civ.
	if (rtso::is_playable_civ(civ)) sim->set_player_civ((uint8_t)civ);
	if (rtso::is_playable_civ(ai)) sim->set_team_civ(1, (uint8_t)ai);
	for (int t = 0; t < rtso::MAX_PLAYERS && t < slot_civs.size(); ++t) {
		const int sc = (int)slot_civs[t];
		if (rtso::is_playable_civ(sc)) sim->set_team_civ((uint8_t)t, (uint8_t)sc);
	}
	sim->set_nomad((bool)ms->get("nomad")); // random maps only (generate_random_map)
	if (!load_map_seeded(map_id, seed)) {
		UtilityFunctions::printerr("[RtsoMain] GameSetup map failed: ", map_id, " - default arena.");
		if (rtso::is_playable_civ(civ)) sim->set_player_civ((uint8_t)civ);
		if (rtso::is_playable_civ(ai)) sim->set_team_civ(1, (uint8_t)ai);
		sim->spawn_initial();
		return;
	}
	if (victory >= 0 && victory < 256 && rtso::valid_victory_code((uint8_t)victory)) sim->set_victory_condition((uint8_t)victory);
	sim->set_waves_enabled((bool)ms->get("waves_enabled"));
	sim->set_last_man_standing((bool)ms->get("last_man_standing"));
	{
		// negative: World Population (0 A.D. WorldPopulationCap); team_pop: Team Population
		const int pc = (int)ms->get("pop_cap");
		if (pc > 0 && (bool)ms->get("team_pop")) sim->set_team_pop_cap(pc);
		else sim->set_pop_cap_setting(pc);
	}
	sim->set_ceasefire_seconds(60.0f * (float)std::clamp((int)ms->get("ceasefire_min"), 0, 60));
	sim->set_reveal_map((bool)ms->get("reveal_map"));
	sim->set_explore_map((bool)ms->get("explore_map"));
	sim->set_wonder_duration(60.0f * (float)std::clamp((int)ms->get("wonder_min"), 0, 60));
	sim->set_relic_duration(60.0f * (float)std::clamp((int)ms->get("relic_min"), 0, 60));
	sim->set_relic_count((int)ms->get("relic_count"));
	sim->set_regicide_garrison((bool)ms->get("regicide_garrison"));
	sim->set_lock_teams((bool)ms->get("lock_teams"));
	sim->set_cheats_enabled((bool)ms->get("cheats"));
	sim->set_disable_spies((bool)ms->get("disable_spies"));
	sim->set_disable_treasures((bool)ms->get("disable_treasures"));
	{
		Array scolors = ms->get("slot_colors");
		for (int p = 0; p < rtso::MAX_PLAYERS; ++p)
			sim->set_player_color((uint8_t)p, (uint8_t)(p < scolors.size() ? std::clamp((int)scolors[p], 0, rtso::PLAYER_COLOR_COUNT - 1) : p));
	}
	sim->set_water_level_mode(std::clamp((int)ms->get("water_level"), 0, 2));
	sim->set_sea_level_rise_minutes(std::clamp((int)ms->get("sea_level_rise_min"), 0, 60));
	// Game-setup teams (0 = None, 1-4 = T1..T4) decide allied victory, shared vision and territory.
	{
		Array slot_teams = ms->get("slot_teams");
		uint8_t teams[rtso::MAX_PLAYERS] = {};
		for (int p = 0; p < rtso::MAX_PLAYERS && p < slot_teams.size(); ++p)
			teams[p] = (uint8_t)std::clamp((int)slot_teams[p], 0, 8);
		sim->set_setup_teams(teams);
	}
	sim->apply_team_diplomacy(); // stances follow the lobby's teams
	sync_victory_selector();
	if (diff >= 0 && diff <= 5) sim->set_petra_difficulty((uint8_t)diff);
	if (behavior >= 0 && behavior <= 3) sim->set_petra_behavior((uint8_t)behavior);
	// Each AI slot's own difficulty and behaviour (slot index == player id; after the shared setters).
	{
		Array sdiffs = ms->get("slot_diffs");
		Array sbehs = ms->get("slot_behs");
		for (int p = 0; p < rtso::MAX_PLAYERS; ++p) {
			if (p < sdiffs.size()) sim->set_petra_difficulty((uint8_t)p, (uint8_t)std::clamp((int)sdiffs[p], 0, 5));
			if (p < sbehs.size()) sim->set_petra_behavior((uint8_t)p, (uint8_t)std::clamp((int)sbehs[p], 0, 3));
		}
	}
	// Game Setup speed (0 A.D. gameSpeed); the in-game speed button still cycles from here.
	{
		const float sp = (float)(double)ms->get("game_speed");
		game_speed = (sp >= 0.25f && sp <= 3.0f) ? sp : 1.0f;
	}
	sim->set_starting_resources_preset(start_res); // 0 A.D. Very Low .. Deathmatch
	// Per-slot civs (slot 0 = human; extra slots honor team civs the
	// RMS/JSON spawns actually created - Petra only drives team 1).
	for (int t = 0; t < rtso::MAX_PLAYERS && t < slot_civs.size(); ++t) {
		int sc = (int)slot_civs[t];
		if (rtso::is_playable_civ(sc)) sim->set_team_civ((uint8_t)t, (uint8_t)sc);
	}
	apply_map_player_overrides();
	// Seat the lobby's slots so every AI slot runs its own Petra (as apply_game_setup does).
	if (seats > 0) {
		for (int t = 0; t < rtso::MAX_PLAYERS; ++t) {
			const int ty = t < slot_types.size() ? (int)slot_types[t] : 2;
			// Slot 1 is you unless every slot is AI (local_observer_); then Petra plays it too.
			const bool seated = t < seats && ty != 2 && (t > 0 || ty == 0 || local_observer_);
			sim->set_player_active((uint8_t)t, seated || t == 0, (t > 0 || local_observer_) && seated && ty == 1);
		}
	}
	apply_player_names(ms->get("slot_names")); // after seating: the diplomacy labels read civ and AI
	UtilityFunctions::print("[RtsoMain] GameSetup ", map_id, " seed ", (int64_t)seed,
			" civ ", rtso::civ_info(sim->player_civ()).name,
			" vs ", rtso::civ_info(sim->team_civ(1)).name);
	// Map just (re)spawned - select the human player's units like the old
	// _ready tail did (it ran before any map existed, so it selected nothing).
	clear_selection();
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (uview.team[i] == local_player()) selected_ids.push_back((int32_t)uview.id[i]);
	}
}

// Match summary for the setup E2E + future summary screen: what the sim
// actually holds after apply_match_settings / apply_game_setup.
Dictionary RtsoMain::get_match_summary() const {
	Dictionary d;
	if (!sim) return d;
	d["map_id"] = String(sim->current_map_id().c_str());
	d["map_name"] = String(sim->current_map_name().c_str());
	d["seed"] = (int64_t)sim->sim_seed();
	d["victory"] = (int)sim->victory_condition();
	d["petra_diff"] = (int)sim->petra_difficulty();
	d["petra_behavior"] = (int)sim->petra_behavior();
	d["food"] = sim->food(local_player());
	d["wood"] = sim->wood(local_player());
	d["gold"] = sim->gold(local_player());
	d["stone"] = sim->stone(local_player());
	d["player_civ"] = (int)sim->player_civ(local_player());
	d["ai_civ"] = (int)sim->team_civ(1);
	d["units"] = (int64_t)sim->unit_count();
	d["buildings"] = (int64_t)sim->building_count();
	return d;
}

void RtsoMain::_on_map_selected(int index) {
	PackedStringArray maps = get_available_maps();
	if (index >= 0 && index < maps.size()) {
		load_map(maps[index]);
	}
}

PackedStringArray RtsoMain::get_available_maps() const {
	PackedStringArray maps;
	Ref<DirAccess> dir = DirAccess::open("res://data/maps");
	if (dir.is_valid()) {
		dir->list_dir_begin();
		String fname = dir->get_next();
		PackedStringArray json_maps;
		while (!fname.is_empty()) {
			if (!dir->current_is_dir() && fname.ends_with(".json")) {
				json_maps.push_back(fname.get_basename());
			}
			fname = dir->get_next();
		}
		json_maps.sort();
		for (int i = 0; i < json_maps.size(); ++i) {
			maps.push_back(json_maps[i]);
		}
	}
	if (maps.is_empty()) {
		maps.push_back("acropolis_bay");
		maps.push_back("corinthian_isthmus");
		maps.push_back("atlas_valleys");
		maps.push_back("bactria");
		maps.push_back("battle_for_the_tiber");
	}
	maps.push_back("default");
	maps.push_back("random_continental");
	maps.push_back("random_archipelago");
	maps.push_back("random_oasis");
	maps.push_back("random_highlands");
	maps.push_back("random_valley");
	maps.push_back("random_mediterranean");
	maps.push_back("random_acropolis");
	maps.push_back("random_spartan_valley");
	maps.push_back("random_danubius");
	maps.push_back("random_saharan_oases");
	maps.push_back("random_anatolian_plateau");
	maps.push_back("random_jebel_barkal");
	for (int t = rtso::MAP_MAINLAND; t < rtso::MAP_TYPE_COUNT; ++t)
		maps.push_back(String("random_") + rtso::map_type_info((uint8_t)t).id);
	return maps;
}

bool RtsoMain::load_map(String map_id) {
	uint32_t seed = (uint32_t)Time::get_singleton()->get_ticks_msec();
	if (sim) {
		sim->set_biome_override(-1); // a map picked in game keeps its own biome
		sim->set_nomad(false);
	}
	local_observer_ = false; // a map picked in game is played from slot 1
	return load_map_seeded(map_id, seed);
}

void RtsoMain::apply_map_player_overrides() {
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
		if (map_civ_[p] >= 0) sim->set_team_civ(p, (uint8_t)map_civ_[p]);
		if (map_has_res_[p]) sim->set_player_resources(p, map_res_[p][0], map_res_[p][1], map_res_[p][3], map_res_[p][2]);
		sim->set_player_pop_cap(p, map_player_pop_limit_[p]);
	}
	if (map_pop_limit_ > 0) sim->set_pop_cap(map_pop_limit_);
	if (map_has_teams_) {
		// A map with LockTeams sets the teams itself (0 A.D. gamesetup locks them).
		sim->set_setup_teams(map_team_);
		sim->apply_team_diplomacy();
	}
}

bool RtsoMain::load_map_seeded(String map_id, uint32_t seed) {
	if (!sim || !view) return false;
	clear_selection();
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
		map_civ_[p] = -1;
		map_has_res_[p] = false;
		map_player_pop_limit_[p] = 0;
		map_team_[p] = 0;
	}
	map_has_teams_ = false;
	map_pop_limit_ = -1;

	// 1. Procedural Random Map Generation (RMS Engine)
	if (map_id.begins_with("random_") || map_id.begins_with("procedural_") ||
		map_id == "continental" || map_id == "archipelago" || map_id == "oasis" ||
		map_id == "highlands" || map_id == "valley" || map_id == "mediterranean" ||
		map_id == "random_acropolis" || map_id == "random_spartan_valley" ||
		map_id == "danubius" || map_id == "random_danubius" ||
		map_id == "random_saharan_oases" ||
		map_id == "anatolian_plateau" || map_id == "random_anatolian_plateau" ||
		map_id == "jebel_barkal" || map_id == "random_jebel_barkal") {
		
		rtso::MapType mtype = rtso::MAP_RANDOM_CONTINENTAL;
		int num_p = std::clamp(random_map_players, 2, (int)rtso::MAX_PLAYERS);
		if (map_id.contains("archipelago")) mtype = rtso::MAP_RANDOM_ARCHIPELAGO;
		else if (map_id.contains("saharan_oases")) mtype = rtso::MAP_SAHARAN_OASES;
		else if (map_id.contains("oasis")) mtype = rtso::MAP_RANDOM_OASIS;
		else if (map_id.contains("highlands")) mtype = rtso::MAP_RANDOM_HIGHLANDS;
		else if (map_id.contains("valley") && !map_id.contains("spartan")) mtype = rtso::MAP_RANDOM_VALLEY;
		else if (map_id.contains("spartan")) mtype = rtso::MAP_SPARTAN_VALLEY;
		else if (map_id.contains("acropolis")) mtype = rtso::MAP_ACROPOLIS;
		else if (map_id.contains("danubius")) mtype = rtso::MAP_DANUBIUS;
		else if (map_id.contains("anatolian")) mtype = rtso::MAP_ANATOLIAN_PLATEAU;
		else if (map_id.contains("jebel") || map_id.contains("barkal")) mtype = rtso::MAP_JEBEL_BARKAL;
		else if (map_id.contains("mediterranean")) mtype = rtso::MAP_RANDOM_MEDITERRANEAN;
		// Newer maps resolve by id ("random_<id>" or "<id>"), see map_type_info.
		for (int t = rtso::MAP_MAINLAND; t < rtso::MAP_TYPE_COUNT; ++t) {
			String id = rtso::map_type_info((uint8_t)t).id;
			if (map_id == id || map_id == "random_" + id) { mtype = (rtso::MapType)t; break; }
		}

		load_progress(40, "Generating map");
		sim->generate_random_map(mtype, seed, num_p, (float)random_map_size * 0.5f, random_map_teams);
		load_progress(70, "Building terrain");
		view->rebuild_terrain(sim);

		if (cam_rig) {
			cam_rig->set_map_bounds_half(sim->map_half());
			for (const auto &b : sim->buildings()) {
				if (b.team == local_player() && rtso::is_civic_centre(b.type)) {
					cam_rig->set_target_pos(Vector3(b.x, 0, b.z));
					break;
				}
			}
		}

		UtilityFunctions::print("[RtsoMain] Generated procedural RMS map '", rtso::map_type_info(mtype).name, "' (seed=", (int64_t)seed, ", players=", num_p, ")");
		return true;
	}

	if (map_id == "default" || map_id == "arena") {
		sim->set_sim_seed(seed);
		sim->spawn_initial();
		view->rebuild_terrain(sim);
		if (cam_rig) {
			cam_rig->set_map_bounds_half(sim->map_half());
			cam_rig->set_target_pos(Vector3(0, 0, 0));
		}
		UtilityFunctions::print("[RtsoMain] Loaded default arena map.");
		return true;
	}

	String path = "res://data/maps/" + map_id + ".json";
	if (!FileAccess::file_exists(path)) {
		path = "res://data/maps/" + map_id;
		if (!FileAccess::file_exists(path)) {
			UtilityFunctions::printerr("[RtsoMain] Map file not found: ", map_id);
			return false;
		}
	}

	load_progress(40, "Loading map");
	Ref<FileAccess> fa = FileAccess::open(path, FileAccess::READ);
	if (!fa.is_valid()) return false;
	String content = fa->get_as_text();
	fa->close();

	Variant parsed = JSON::parse_string(content);
	if (parsed.get_type() != Variant::DICTIONARY) return false;
	Dictionary d = parsed;

	sim->set_sim_seed(seed);
	sim->reset();
	map_pop_limit_ = (int)d.get("pop_limit", -1);
	{
		Array pl = d.get("players", Array());
		for (int i = 0; i < pl.size(); ++i) {
			Dictionary p = pl[i];
			const int id = (int)p.get("id", 1);
			const uint8_t team = id <= 1 ? 0 : (uint8_t)(id - 1);
			if (team >= rtso::MAX_PLAYERS) continue;
			const int c = (int)p.get("civ", -1);
			if (rtso::is_playable_civ(c)) map_civ_[team] = (int8_t)c;
			if (p.has("resources")) {
				Dictionary r = p["resources"];
				const char *keys[4] = {"food", "wood", "stone", "metal"};
				for (int k = 0; k < 4; ++k) map_res_[team][k] = (int)r.get(keys[k], 300); // Player.js: 300 of each unless set
				map_has_res_[team] = true;
			}
			// 0.28 PlayerData PopulationLimit (InitGame.js SetPerPlayerPopulationCaps).
			if (p.has("pop_limit")) map_player_pop_limit_[team] = std::max(0, (int)p["pop_limit"]);
			if (p.has("team")) {
				const int t = (int)p["team"];
				map_team_[team] = (uint8_t)(t >= 0 && t < rtso::MAX_PLAYERS ? t + 1 : 0);
				map_has_teams_ = true;
			}
		}
		// Civs before the units: spawn_unit takes its owner's roster.
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p)
			if (map_civ_[p] >= 0) sim->set_team_civ(p, (uint8_t)map_civ_[p]);
	}

	String name = d.get("name", map_id);
	String biome = d.get("biome", "mediterranean");
	float world_size = (float)d.get("world_size", 256.0f);
	float water_h = (float)d.get("water_height", -1.0f);
	int h_res = (int)d.get("heightmap_res", 64);

	sim->set_map_half(world_size * 0.5f);
	sim->set_map_info(map_id.utf8().get_data(), name.utf8().get_data(), biome.utf8().get_data());

	Array h_arr = d.get("heightmap", Array());
	std::vector<float> heights;
	heights.resize(h_arr.size());
	for (int i = 0; i < h_arr.size(); ++i) {
		heights[i] = (float)h_arr[i];
	}
	sim->set_terrain_data(heights, h_res, water_h);
	// 0 A.D. per-tile terrain textures (port_0ad_maps.py extract_pmp_terrain).
	if (d.has("terrain") && d["terrain"].get_type() == Variant::DICTIONARY) {
		Dictionary t = d["terrain"];
		Array tn = t.get("names", Array());
		std::vector<std::string> names;
		for (int i = 0; i < tn.size(); ++i) names.push_back(String(tn[i]).utf8().get_data());
		sim->set_terrain_tiles(rtso::Simulation::terrain_tiles_from_rle((int)t.get("res", 0), std::move(names),
				String(t.get("rle", "")).utf8().get_data()));
	}

	// Spawn Resources
	Array res_arr = d.get("resources", Array());
	for (int i = 0; i < res_arr.size(); ++i) {
		Dictionary r = res_arr[i];
		String rtype_str = r.get("type", "tree");
		float rx = (float)r.get("x", 0.0);
		float rz = (float)r.get("z", 0.0);
		int amt = (int)r.get("amount", 600);

		uint8_t rtype = rtso::RES_WOOD;
		uint8_t rsub = rtso::RES_SUB_NONE;
		// A plant name ("pine", "berry", ...) is a tree or fruit supply of that FloraKind.
		const uint8_t flora = rtso::flora_kind_by_name(rtype_str.utf8().get_data());
		if (flora < rtso::FLORA_COUNT && rtso::flora_is_fruit(flora)) rtype = rtso::RES_FOOD;
		else if (rtype_str == "gold" || rtype_str == "metal") rtype = rtso::RES_GOLD;
		else if (rtype_str == "stone" || rtype_str == "rock") rtype = rtso::RES_STONE;
		else if (rtype_str == "food" || rtype_str == "fruit") rtype = rtso::RES_FOOD;
		else if (rtype_str == "fish") { // 0 A.D. gaia/fish/*: only fishing boats gather it
			rtype = rtso::RES_FOOD;
			rsub = rtso::RES_SUB_FISH;
			if (!r.has("amount")) amt = rtso::FISH_FOOD_SUPPLY;
		}

		// 0 A.D. ResourceSupply/MaxGatherers when the template differs from its class (banyan 12, baobab 9).
		const int maxg = std::clamp((int)r.get("max_gatherers", 0), 0, 255);
		sim->spawn_resource_world(rx, rz, rtype, amt, rsub, (uint8_t)maxg, flora < rtso::FLORA_COUNT ? flora : rtso::FLORA_DEFAULT);
	}

	// Spawn Wildlife Fauna
	Array fauna_arr = d.get("fauna", Array());
	for (int i = 0; i < fauna_arr.size(); ++i) {
		Dictionary f = fauna_arr[i];
		String ftype_str = f.get("type", "deer");
		float fx = (float)f.get("x", 0.0);
		float fz = (float)f.get("z", 0.0);
		// 0 A.D. template stem ("bear_brown" / "fauna_bear_brown"); older maps use deer/boar/sheep/wolf.
		uint8_t ftype = rtso::fauna_type_by_name(ftype_str.utf8().get_data());
		if (ftype >= rtso::FAUNA_COUNT) ftype = rtso::FAUNA_DEER;
		sim->spawn_fauna(fx, fz, ftype);
	}
	// 0 A.D. gaia/treasure/* and gaia/ruins/* by template stem (tools/port_0ad_maps.py).
	Array treasure_arr = d.get("treasures", Array());
	for (int i = 0; i < treasure_arr.size(); ++i) {
		Dictionary t = treasure_arr[i];
		const uint8_t tt = rtso::treasure_type_by_name(String(t.get("type", "")).utf8().get_data());
		if (tt < rtso::TREASURE_TYPE_COUNT) sim->spawn_treasure((float)t.get("x", 0.0), (float)t.get("z", 0.0), tt);
	}
	Array ruins_arr = d.get("ruins", Array());
	for (int i = 0; i < ruins_arr.size(); ++i) {
		Dictionary r = ruins_arr[i];
		const uint8_t rt = rtso::ruins_type_by_name(String(r.get("type", "")).utf8().get_data());
		if (rt < rtso::RUINS_TYPE_COUNT) sim->spawn_ruins((float)r.get("x", 0.0), (float)r.get("z", 0.0), rt);
	}

	// Spawn Structures. Each finished building would otherwise rebuild the
	// whole nav grid and territory map; do that once after the last one.
	sim->begin_bulk_spawn();
	Array str_arr = d.get("structures", Array());
	for (int i = 0; i < str_arr.size(); ++i) {
		Dictionary b = str_arr[i];
		String stype_str = b.get("type", "house");
		int player = (int)b.get("player", 1);
		float bx = (float)b.get("x", 0.0);
		float bz = (float)b.get("z", 0.0);
		uint8_t team = (player <= 1) ? 0 : (uint8_t)(player - 1);
		if (team >= rtso::MAX_PLAYERS) team = 1;

		if (b.has("wallset")) {
			spawn_map_wall(*sim, b, team);
			continue;
		}
		uint8_t btype = b.has("template") ? map_building_template(b["template"]) : (uint8_t)rtso::BLD_TYPE_COUNT;
		if (btype >= rtso::BLD_TYPE_COUNT) btype = map_building_type(stype_str);
		if (btype >= rtso::BLD_TYPE_COUNT) {
			if (stype_str.is_empty()) continue; // a civ structure RTS0 doesn't have
			btype = rtso::BLD_HOUSE;
		}
		sim->spawn_building_world(bx, bz, btype, team, 1.0f);
	}
	// Gaia-owned structures (Player 0 in 0 A.D.'s map: towers, palisades, houses, docks,
	// structures/merc_camp_egyptian): hostile to everyone, captured or destroyed like 0 A.D.'s.
	Array gaia_str_arr = d.get("gaia_structures", Array());
	std::vector<uint32_t> gaia_structure_ids(gaia_str_arr.size(), 0);
	for (int i = 0; i < gaia_str_arr.size(); ++i) {
		Dictionary b = gaia_str_arr[i];
		if (b.has("wallset")) {
			gaia_structure_ids[i] = spawn_map_wall(*sim, b, rtso::GAIA_PLAYER);
			continue;
		}
		const uint8_t btype = map_building_type(b.get("type", ""));
		if (btype >= rtso::BLD_TYPE_COUNT) continue;
		gaia_structure_ids[i] = sim->spawn_building_world((float)b.get("x", 0.0), (float)b.get("z", 0.0), btype, rtso::GAIA_PLAYER, 1.0f);
	}
	// Gaia scenery (fences, columns, obelisks): oriented obstructions.
	Array scenery_arr = d.get("scenery", Array());
	for (int i = 0; i < scenery_arr.size(); ++i) {
		Dictionary sc = scenery_arr[i];
		const uint8_t st = rtso::scenery_type_by_name(String(sc.get("type", "")).utf8().get_data());
		if (st < rtso::SCENERY_TYPE_COUNT)
			sim->spawn_scenery((float)sc.get("x", 0.0), (float)sc.get("z", 0.0), st, (float)sc.get("angle", 0.0),
					(float)sc.get("scale", 0.25));
	}
	sim->end_bulk_spawn();

	// Spawn Starting Units
	Array units_arr = d.get("units", Array());
	std::vector<uint32_t> unit_ids(units_arr.size(), 0);
	for (int i = 0; i < units_arr.size(); ++i) {
		Dictionary u = units_arr[i];
		String utype_str = u.get("type", "villager");
		int player = (int)u.get("player", 1);
		float ux = (float)u.get("x", 0.0);
		float uz = (float)u.get("z", 0.0);
		uint8_t team = (player <= 1) ? 0 : (uint8_t)(player - 1);
		if (team >= rtso::MAX_PLAYERS) team = 1;

		// The map's own roster template when RTS0 has it ("spart/infantry_javelineer_b").
		const uint16_t tmpl = u.has("template")
				? rtso::civ_unit_template_by_id(String(u["template"]).utf8().get_data()) : rtso::CIV_UNIT_TEMPLATE_NONE;
		if (tmpl != rtso::CIV_UNIT_TEMPLATE_NONE) {
			unit_ids[i] = sim->spawn_unit(ux, uz, team, (rtso::UnitType)rtso::civ_unit_template(tmpl).unit_type, 0.0f, tmpl);
			continue;
		}
		const rtso::UnitType utype = map_unit_type(utype_str);
		const auto &uinfo = rtso::unit_type_info(utype);
		unit_ids[i] = sim->spawn_unit(ux, uz, team, utype, uinfo.hp);
	}
	// "garrison_in": index of the ship the unit starts aboard (0 A.D. <Garrison>,
	// e.g. sicilia_nomad_2p). Passengers sit on the ship, so boarding is instant.
	for (int i = 0; i < units_arr.size(); ++i) {
		Dictionary u = units_arr[i];
		int holder = (int)u.get("garrison_in", -1);
		if (holder < 0 || holder >= (int)unit_ids.size() || !unit_ids[i] || !unit_ids[holder]) continue;
		sim->order_board_transport(&unit_ids[i], 1, unit_ids[holder]);
	}
	// Gaia-owned units: 0 A.D. template ("gaul/infantry_javelineer_a"), its rank, and the
	// Gaia ship or structure it starts in.
	Array gaia_units_arr = d.get("gaia_units", Array());
	std::vector<uint32_t> gaia_unit_ids(gaia_units_arr.size(), 0);
	for (int i = 0; i < gaia_units_arr.size(); ++i) {
		Dictionary u = gaia_units_arr[i];
		gaia_unit_ids[i] = sim->spawn_gaia_unit((float)u.get("x", 0.0), (float)u.get("z", 0.0),
				map_unit_type(u.get("type", "")), String(u.get("template", "")).utf8().get_data(), (int)u.get("rank", 1));
	}
	for (int i = 0; i < gaia_units_arr.size(); ++i) {
		Dictionary u = gaia_units_arr[i];
		if (!gaia_unit_ids[i]) continue;
		const int ship = (int)u.get("garrison_in", -1), str = (int)u.get("garrison_in_structure", -1);
		if (ship >= 0 && ship < (int)gaia_unit_ids.size() && gaia_unit_ids[ship])
			sim->order_board_transport(&gaia_unit_ids[i], 1, gaia_unit_ids[ship]);
		else if (str >= 0 && str < (int)gaia_structure_ids.size() && gaia_structure_ids[str])
			sim->garrison_unit_now(gaia_unit_ids[i], gaia_structure_ids[str]);
	}

	// TriggerPoint.js markers the scripts spawn at or aim for (owner 0 is Gaia).
	Array tp_arr = d.get("trigger_points", Array());
	for (int i = 0; i < tp_arr.size(); ++i) {
		Dictionary tp = tp_arr[i];
		const String ref = tp.get("ref", "A");
		const int owner = (int)tp.get("owner", 0);
		if (ref.is_empty()) continue;
		sim->add_trigger_point((uint8_t)ref.unicode_at(0), (float)tp.get("x", 0.0), (float)tp.get("z", 0.0),
				owner <= 0 ? rtso::GAIA_PLAYER : (uint8_t)std::min(owner - 1, rtso::MAX_PLAYERS - 1));
	}
	// The map's TriggerScripts (0 A.D. ScriptSettings), ported to C++ (triggers.h).
	Array trig_arr = d.get("trigger_scripts", Array());
	for (int i = 0; i < trig_arr.size(); ++i) {
		const String script = trig_arr[i];
		if (!sim->register_trigger_script(script.utf8().get_data()))
			UtilityFunctions::printerr("[RtsoMain] Map trigger script not ported: ", script);
	}

	// Rebuild World View 3D terrain and bounds
	load_progress(70, "Building terrain");
	view->rebuild_terrain(sim);
	if (cam_rig) {
		cam_rig->set_map_bounds_half(sim->map_half());
		// 0 A.D. opens on the player's civic centre (session.js initGUIObjects
		// jumps to it); scenarios without one fall back to Player 1's start.
		bool framed = false;
		for (const auto &b : sim->buildings()) {
			if (b.team == local_player() && rtso::is_civic_centre(b.type) && !b.wrecked) {
				cam_rig->set_target_pos(Vector3(b.x, 0, b.z));
				framed = true;
				break;
			}
		}
		Array players_arr = d.get("players", Array());
		if (!framed && !players_arr.is_empty()) {
			Dictionary p1 = players_arr[0];
			float start_x = (float)p1.get("start_x", 0.0);
			float start_z = (float)p1.get("start_z", 0.0);
			cam_rig->set_target_pos(Vector3(start_x, 0, start_z));
		}
	}

	apply_map_player_overrides();
	UtilityFunctions::print("[RtsoMain] Loaded 0 A.D. map '", name, "' (", world_size, "m, ", sim->resource_count(), " resources, ", sim->building_count(), " structures, ", sim->unit_count(), " units)");
	return true;
}

void RtsoMain::_on_civ_selected(int index) {
	set_civ(index);
}

void RtsoMain::_on_ai_diff_selected(int index) {
	if (!sim || !ai_diff_selector) return;
	sim->set_petra_difficulty((uint8_t)ai_diff_selector->get_item_id(index));
}

void RtsoMain::_on_ai_behavior_selected(int index) {
	if (!sim || !ai_behavior_selector) return;
	sim->set_petra_behavior((uint8_t)ai_behavior_selector->get_item_id(index));
}

String RtsoMain::player_display_name(uint8_t pid) const {
	if (pid >= rtso::MAX_PLAYERS) return pid == rtso::GAIA_PLAYER ? String("Gaia") : String("an observer");
	// 0 A.D. onFinalizeAttributes: a seated client's own name replaces the slot's.
	if (net_lockstep())
		for (const auto &e : net_host_.playerAssignment())
			if (e.player == pid && !e.name.empty()) return String::utf8(e.name.c_str());
	if (!player_names_[pid].is_empty()) return player_names_[pid];
	return "Player " + String::num_int64(pid + 1);
}

void RtsoMain::apply_player_names(const Array &names) {
	for (int p = 0; p < rtso::MAX_PLAYERS; ++p)
		player_names_[p] = p < names.size() ? String(names[p]).strip_edges().left(48) : String();
}

void RtsoMain::sync_victory_selector() {
	if (!sim || !victory_selector) return;
	for (int i = 0; i < victory_selector->get_item_count(); ++i) {
		if (victory_selector->get_item_id(i) == (int)sim->victory_condition()) victory_selector->select(i);
	}
}

void RtsoMain::_on_victory_selected(int index) {
	if (!sim || !victory_selector) return;
	sim->set_victory_condition((uint8_t)victory_selector->get_item_id(index));
}

void RtsoMain::_on_voice_button_pressed() {
	use_english_voice_orders_ = !use_english_voice_orders_;
	if (voice_btn)
		voice_btn->set_text(use_english_voice_orders_ ? "Voice: EN" : "Voice: Civ");
}

void RtsoMain::_on_lobby_button_pressed() {
	if (!lobby_panel) return;
	lobby_panel->set_visible(!lobby_panel->is_visible());
	if (lobby_panel->is_visible())
		refresh_lobby_rooms();
}

void RtsoMain::refresh_lobby_rooms() {
	if (!lobby_rooms) return;
	lobby_rooms->clear();
	lobby_room_cache_.clear();
	std::string body;
	if (!rtso::lobby_http_get(lobby_http_host_, lobby_http_port_, "/rooms", body)) {
		if (lobby_status)
			lobby_status->set_text("Lobby unreachable - start: python3 tools/lobby_server.py");
		return;
	}
	lobby_room_cache_ = rtso::parse_lobby_rooms(body);
	for (const auto &r : lobby_room_cache_) {
		String line = String(r.id.c_str()) + "  " + String(r.host_ip.c_str()) + ":" +
		              String::num_int64(r.host_port) + "  " + String::num_int64(r.players) +
		              "/" + String::num_int64(r.max_players) + "  " + String(r.map_id.c_str());
		lobby_rooms->add_item(line);
	}
	if (lobby_status)
		lobby_status->set_text(String::num_int64((int64_t)lobby_room_cache_.size()) + " rooms");
}

// A joiner still in game setup changes its mind: tell the host it now wants
// to watch (or play), as its request at connect did.
void RtsoMain::_on_lobby_observe_toggled(bool on) {
	if (net_lockstep() && !net_host_.isHost() && !net_game_started_) turn_mgr_.request_rejoin(on);
}

void RtsoMain::_start_menu_net() {
	Node *ms = get_node_or_null(NodePath("/root/MatchSettings"));
	if (!ms) return;
	const String mode = ms->get("net_mode");
	if (mode != "host" && mode != "join") return;
	ms->set("net_mode", ""); // once: a later match starts offline unless the lobby says otherwise
	const String lobby = ms->get("net_lobby");
	if (lobby.contains(":")) {
		lobby_http_host_ = lobby.get_slice(":", 0).utf8().get_data();
		lobby_http_port_ = (uint16_t)lobby.get_slice(":", 1).to_int();
	}
	if (lobby_name) lobby_name->set_text(ms->get("net_player_name"));
	if (lobby_host && mode == "join") lobby_host->set_text(ms->get("net_ip"));
	if (lobby_port) lobby_port->set_text(String::num_int64((int64_t)ms->get("net_port")));
	if (lobby_password) lobby_password->set_text(ms->get("net_password"));
	if (lobby_observe) lobby_observe->set_pressed_no_signal((bool)ms->get("net_observe"));
	if (lobby_panel) lobby_panel->set_visible(true);
	if (lobby_btn) lobby_btn->set_visible(true);
	if (mode == "host") _on_lobby_host_pressed();
	else _on_lobby_join_pressed();
}

Dictionary RtsoMain::get_lobby_state() const {
	Dictionary d;
	const bool hosting = net_host_.isHost();
	int players = 0;
	if (hosting) {
		players = 1;
		for (const auto &p : net_host_.getPeers())
			if (p.id != 0 && p.connected && p.is_authenticated && !p.is_observer) ++players;
	}
	d["hosting"] = hosting;
	d["players"] = players;
	d["started"] = net_game_started_;
	// For tools/playtest_godot_mp.py: where the match is and whether it agrees.
	d["turn"] = (int64_t)turn_mgr_.current_turn();
	d["seat"] = (int)seat_player();
	d["oos"] = turn_mgr_.has_out_of_sync();
	d["oos_turn"] = (int64_t)turn_mgr_.oos_turn();
	d["disconnected"] = net_disconnected_;
	d["disconnect_reason"] = net_host_.disconnectReason();
	d["net_status"] = net_status_overlay_ && net_status_overlay_->is_visible() ? net_status_label_->get_text() : String();
	d["chat"] = chat_window_.log_text();
	return d;
}

int64_t RtsoMain::get_turn_hash(int64_t turn) const {
	const auto &history = turn_mgr_.turn_history();
	for (auto it = history.rbegin(); it != history.rend(); ++it)
		if ((int64_t)it->turn_index == turn) return (int64_t)it->sim_state_hash;
	return 0;
}

// 0 A.D. gamesetup player assignment (PlayerAssignmentsController.js): the host
// gives each client a slot, or makes it an observer; a client given an AI slot
// replaces the AI, and one given a slot another client plays swaps with it.
// Unassigned clients take the free human slots in order. Shown only on the
// host until Start.
void RtsoMain::refresh_lobby_seats() {
	if (!lobby_seats) return;
	struct Client { uint32_t id; std::string name; bool watches; };
	std::vector<Client> clients;
	struct Slot { char type; String name; };
	std::vector<Slot> slots;
	if (net_host_.isHost() && !net_game_started_) {
		clients.push_back({0, net_host_.localPlayerName(), lobby_observe && lobby_observe->is_pressed()});
		for (const auto &p : net_host_.getPeers())
			if (p.connected && p.is_authenticated && !p.kick_disconnect_ms && p.id != 0 && p.id < 8)
				clients.push_back({p.id, p.player_name, p.is_observer});
		if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
			Array types = ms->get("slot_types");
			Array names = ms->get("slot_names");
			for (int i = 0; i < 8 && i < types.size(); ++i) {
				const int ty = (int)types[i];
				slots.push_back({ty == 0 ? 'h' : ty == 1 ? 'a' : 'x', i < names.size() ? String(names[i]) : String()});
			}
		}
	}
	if (clients.size() < 2) clients.clear(); // nobody to assign but the host
	for (auto it = seat_choice_.begin(); it != seat_choice_.end();) {
		bool here = false;
		for (const auto &c : clients) here |= c.id == it->first;
		it = here ? std::next(it) : seat_choice_.erase(it);
	}
	for (auto it = seat_seen_.begin(); it != seat_seen_.end();) {
		bool here = false;
		for (const auto &c : clients) here |= c.id == *it;
		it = here ? std::next(it) : seat_seen_.erase(it);
	}
	uint8_t eff[8];
	lobby_effective_seats(eff);
	// 0 A.D. onClientJoin: gui.gamesetup.assignplayers decides whether a joiner
	// gets a slot (everyone, only the host's buddies, or nobody until the host
	// assigns one), and a name seen before gets its last slot back when free.
	for (const auto &c : clients) {
		if (c.id == 0 || !seat_seen_.insert(c.id).second || seat_choice_.count(c.id)) continue;
		const std::string opt = rtso::user_config::get("gui.gamesetup.assignplayers");
		bool buddy = false;
		{
			const std::string list = "," + rtso::user_config::get("lobby.buddies") + ",";
			buddy = !c.name.empty() && list.find("," + c.name + ",") != std::string::npos;
		}
		if (opt == "disabled" || (opt == "buddies" && !buddy)) { seat_choice_[c.id] = SEAT_OBSERVER; continue; }
		auto last = seat_last_.find(c.name);
		if (c.watches || last == seat_last_.end() || last->second >= slots.size() || slots[last->second].type == 'x') continue;
		bool held = false;
		for (const auto &o : clients) held |= o.id != c.id && eff[o.id] == last->second;
		if (!held) seat_choice_[c.id] = last->second;
	}
	lobby_effective_seats(eff);
	for (const auto &c : clients) seat_last_[c.name] = eff[c.id];
	std::string key;
	for (const auto &c : clients) {
		auto ch = seat_choice_.find(c.id);
		key += std::to_string(c.id) + ":" + c.name + ":" + (c.watches ? "o" : "p") + ":" +
				std::to_string(ch == seat_choice_.end() ? -1 : ch->second) + ":" + std::to_string(eff[c.id]) + ";";
	}
	for (const auto &s : slots) key += std::string(1, s.type) + s.name.utf8().get_data() + ";";
	if (key == lobby_seats_key_) return;
	lobby_seats_key_ = key;
	for (int i = lobby_seats->get_child_count() - 1; i >= 0; --i) {
		Node *n = lobby_seats->get_child(i);
		lobby_seats->remove_child(n);
		n->queue_free();
	}
	if (clients.empty()) return;
	Label *title = memnew(Label);
	title->set_text("Player assignment");
	lobby_seats->add_child(title);
	for (const auto &c : clients) {
		HBoxContainer *row = memnew(HBoxContainer);
		Label *who = memnew(Label);
		who->set_text(c.name.empty() ? String("Player") : String::utf8(c.name.c_str()));
		who->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		row->add_child(who);
		OptionButton *pick = memnew(OptionButton);
		pick->add_item(eff[c.id] < 8 ? "Next free slot (Player " + String::num_int64(eff[c.id] + 1) + ")" : String("Next free slot"), SEAT_AUTO);
		pick->add_item("Observer", SEAT_OBSERVER);
		for (int i = 0; i < (int)slots.size(); ++i) {
			if (slots[i].type == 'x') continue;
			String label = "Player " + String::num_int64(i + 1);
			if (slots[i].type == 'a') label += String(" (AI") + (slots[i].name.is_empty() ? String() : ": " + slots[i].name) + String(")");
			pick->add_item(label, i);
		}
		auto ch = seat_choice_.find(c.id);
		const int chosen = ch != seat_choice_.end() ? ch->second : (c.watches ? SEAT_OBSERVER : SEAT_AUTO);
		const int idx = pick->get_item_index(chosen);
		pick->select(idx >= 0 ? idx : 0);
		pick->set_meta("client", (int)c.id);
		pick->connect("item_selected", Callable(this, "_on_lobby_seat_selected").bind((int)c.id));
		row->add_child(pick);
		lobby_seats->add_child(row);
	}
}

void RtsoMain::_on_lobby_seat_selected(int index, int client) {
	OptionButton *pick = nullptr;
	for (int i = 0; lobby_seats && i < lobby_seats->get_child_count() && !pick; ++i)
		if (Node *row = lobby_seats->get_child(i))
			for (int j = 0; j < row->get_child_count(); ++j)
				if (OptionButton *ob = Object::cast_to<OptionButton>(row->get_child(j)))
					if ((int)ob->get_meta("client", -1) == client) pick = ob;
	if (!pick) return;
	const int choice = pick->get_item_id(index);
	if (client < 0 || client >= 8) return;
	// 0 A.D. assignPlayer: a slot another client plays is swapped with this
	// client's own slot, or leaves that client unassigned (an observer).
	uint8_t eff[8];
	lobby_effective_seats(eff);
	if (choice < 8)
		for (uint32_t other = 0; other < 8; ++other) {
			if (other == (uint32_t)client || eff[other] != choice) continue;
			bool connected = other == 0;
			for (const auto &p : net_host_.getPeers()) connected |= p.id == other && p.connected;
			if (connected) seat_choice_[other] = eff[client] < 8 ? (int)eff[client] : SEAT_OBSERVER;
		}
	seat_choice_[(uint32_t)client] = choice;
	lobby_seats_key_.clear();
}

// Start: the host's player assignment into NMT_GAME_START (seats= and obs=).
// 0 A.D. game setup assigns observers no player: the host when Observe is
// ticked, a joiner that asked to watch (its rejoin request at connect), and
// whoever the host's player assignment made an observer; a client given an AI
// slot replaces the AI, and if too few human slots are left for the joiners
// the first AI slots become human ones. `commit` (Start) also tells the net
// layer who watches.
void RtsoMain::prepare_lobby_seats(rtso::GameSetupParams &setup, bool commit) {
	const bool host_watches = lobby_observe && lobby_observe->is_pressed();
	setup.observers = host_watches ? 1 : 0;
	for (const auto &p : net_host_.getPeers())
		if (p.connected && p.id != 0 && p.id < 8 && p.is_observer) setup.observers |= (uint8_t)(1u << p.id);
	for (const auto &e : seat_choice_) {
		if (e.first >= 8) continue;
		const uint8_t bit = (uint8_t)(1u << e.first);
		if (e.second == SEAT_OBSERVER) { setup.observers |= bit; continue; }
		setup.observers &= (uint8_t)~bit;
		if (e.second < 0 || e.second >= setup.slot_count || setup.slot_types[e.second] == 'x') continue;
		setup.seats[e.first] = (uint8_t)e.second;
		setup.slot_types[e.second] = 'h';
	}
	int need = (setup.observers & 1) ? 0 : 1;
	for (const auto &p : net_host_.getPeers()) {
		if (!p.connected || p.id == 0) continue;
		const bool watches = p.id < 8 && ((setup.observers >> p.id) & 1);
		if (commit && p.id < 8) net_host_.setPeerObserver(p.id, watches);
		if (!watches) ++need;
	}
	for (uint8_t i = 0; i < setup.slot_count; ++i)
		if (setup.slot_types[i] == 'h') --need;
	for (uint8_t i = 0; i < setup.slot_count && need > 0; ++i)
		if (setup.slot_types[i] == 'a') { setup.slot_types[i] = 'h'; --need; }
}

// The slot each client would play if the host pressed Start now (0xFF: watches).
void RtsoMain::lobby_effective_seats(uint8_t out[8]) {
	rtso::GameSetupParams setup;
	setup.slot_count = 0;
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		Array types = ms->get("slot_types");
		for (int i = 0; i < 8 && i < types.size(); ++i) {
			const int ty = (int)types[i];
			setup.slot_types[i] = ty == 0 ? 'h' : ty == 1 ? 'a' : 'x';
			setup.slot_count++;
		}
	}
	prepare_lobby_seats(setup, false);
	rtso::TurnManager::seat_clients(setup, out);
}

void RtsoMain::_on_lobby_refresh_pressed() {
	refresh_lobby_rooms();
}

void RtsoMain::post_lockstep(uint8_t cmd_type, const uint32_t *ids, size_t n,
                             float x, float z, uint32_t tid, uint8_t param) {
	rtso::NetCommand c;
	c.cmd_type = cmd_type;
	if (ids && n) c.entity_ids.assign(ids, ids + n);
	c.target_x = x;
	c.target_z = z;
	c.target_id = tid;
	c.param = param;
	if (spectating()) return; // observers and defeated players order nothing
	turn_mgr_.post_local_command(c);
}

void RtsoMain::_on_lobby_host_pressed() {
	rtso::NetConfig cfg;
	cfg.port = (uint16_t)lobby_port->get_text().to_int();
	if (cfg.port == 0) cfg.port = rtso::PS_DEFAULT_PORT;
	net_host_.setLocalPlayerName(lobby_name->get_text().utf8().get_data());
	net_host_.setPassword(lobby_password ? lobby_password->get_text().utf8().get_data() : "");
	kicks_shown_ = 0;
	if (!net_host_.startHost(cfg)) {
		if (lobby_status) lobby_status->set_text("Host bind failed");
		return;
	}
	turn_mgr_.reset();
	turn_mgr_.set_net_host(&net_host_, 0);
	turn_mgr_.clear_game_started();
	net_game_started_ = false; // wait for Start (NMT_GAME_START) before any lockstep turn
	std::ostringstream js;
	// No host_ip: the lobby records the address this request comes from.
	js << "{\"id\":\"room-" << cfg.port << "\",\"host_port\":"
	   << cfg.port << ",\"players\":1,\"max_players\":" << (int)cfg.maxPlayers
	   << ",\"map_id\":\"default\"}";
	std::string body;
	rtso::lobby_http_post_json(lobby_http_host_, lobby_http_port_, "/rooms", js.str(), body);
	if (lobby_status) lobby_status->set_text("Hosting on " + String::num_int64(cfg.port));
	refresh_lobby_rooms();
}

void RtsoMain::_on_lobby_start_pressed() {
	if (!sim) return;
	if (!net_host_.isHost()) {
		if (lobby_status) lobby_status->set_text("Start is host-only - Join waits for host Start");
		return;
	}
	// Seed-sync + full setup broadcast: the host's CURRENT match settings
	// (map+seed+victory+petra+slots from the GameSetup screen) are packed
	// into NMT_GAME_START so the join path applies the identical setup via
	// apply_game_setup. generate_random_map funnels the seed into
	// set_sim_seed, so both RNG streams (RMS + sim ticks) match.
	rtso::GameSetupParams setup;
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		String mid = (String)ms->get("map_id");
		setup.map_id = mid.is_empty() ? sim->current_map_id() : mid.utf8().get_data();
		setup.seed = (uint32_t)(int64_t)(int)ms->get("seed");
		setup.victory = (uint8_t)((int)ms->get("victory"));
		setup.waves = (bool)ms->get("waves_enabled") ? 1 : 0;
		setup.lms = (bool)ms->get("last_man_standing") ? 1 : 0;
		setup.pop_cap = (uint16_t)std::clamp((int)ms->get("pop_cap"), 0, 1000);
		// A negative pop_cap is Game Setup's World Population.
		setup.world_pop = (uint16_t)std::clamp(-(int)ms->get("pop_cap"), 0, rtso::Simulation::MAX_WORLD_POP);
		// team_pop: the positive pop_cap is 0 A.D. 0.28's Team Population.
		if (setup.pop_cap > 0 && (bool)ms->get("team_pop")) {
			setup.team_pop = (uint16_t)std::min((int)ms->get("pop_cap"), rtso::Simulation::MAX_TEAM_POP);
			setup.pop_cap = 0;
		}
		setup.ceasefire = (uint8_t)std::clamp((int)ms->get("ceasefire_min"), 0, 60);
		setup.reveal = (bool)ms->get("reveal_map") ? 1 : 0;
		setup.explore = (bool)ms->get("explore_map") ? 1 : 0;
		setup.wonder_min = (uint8_t)std::clamp((int)ms->get("wonder_min"), 0, 60);
		setup.relic_min = (uint8_t)std::clamp((int)ms->get("relic_min"), 0, 60);
		setup.relics = (uint8_t)std::clamp((int)ms->get("relic_count"), 1, 16);
		setup.biome = (uint8_t)std::clamp((int)ms->get("biome"), 0, (int)rtso::BIOME_COUNT);
		Array sdiffs = ms->get("slot_diffs");
		Array sbehs = ms->get("slot_behs");
		for (int i = 0; i < 8; ++i) {
			if (i < sdiffs.size()) setup.diffs[i] = (uint8_t)std::clamp((int)sdiffs[i], 0, 5);
			if (i < sbehs.size()) setup.behs[i] = (uint8_t)std::clamp((int)sbehs[i], 0, 3);
		}
		Array scolors = ms->get("slot_colors");
		for (int i = 0; i < 8 && i < scolors.size(); ++i)
			setup.colors[i] = (uint8_t)std::clamp((int)scolors[i], 0, rtso::PLAYER_COLOR_COUNT - 1);
		setup.nomad = (bool)ms->get("nomad") ? 1 : 0;
		setup.regicide_garrison = (bool)ms->get("regicide_garrison") ? 1 : 0;
		setup.lock_teams = (bool)ms->get("lock_teams") ? 1 : 0;
		setup.cheats = (bool)ms->get("cheats") ? 1 : 0;
		setup.no_spies = (bool)ms->get("disable_spies") ? 1 : 0;
		setup.no_treasures = (bool)ms->get("disable_treasures") ? 1 : 0;
		Array snames = ms->get("slot_names");
		for (int i = 0; i < 8 && i < snames.size(); ++i) setup.names[i] = String(snames[i]).utf8().get_data();
		setup.water_level = (uint8_t)std::clamp((int)ms->get("water_level"), 0, 2);
		setup.team_placement = (uint8_t)std::clamp((int)ms->get("team_placement"), 0, (int)rtso::TEAM_PLACEMENT_COUNT - 1);
		setup.landscape = (uint8_t)std::clamp((int)ms->get("landscape"), 0, 15);
		setup.trigger_difficulty = (uint8_t)std::clamp((int)ms->get("trigger_difficulty"), 1, 5);
		setup.daytime = (uint8_t)std::clamp((int)ms->get("map_daytime"), 0, 2);
		setup.sea_rise_min = (uint8_t)std::clamp((int)ms->get("sea_level_rise_min"), 0, 60);
		int msz = (int)ms->get("map_size");
		setup.map_size = (uint16_t)((msz >= 128 && msz <= 512) ? msz : 128);
		setup.difficulty = (uint8_t)((int)ms->get("ai_difficulty"));
		setup.behavior = (uint8_t)((int)ms->get("ai_behavior"));
		setup.start_res = (uint8_t)((int)ms->get("start_res"));
		Array civs = ms->get("slot_civs");
		Array types = ms->get("slot_types");
		Array teams = ms->get("slot_teams");
		setup.slot_count = 0;
		for (int i = 0; i < 8 && i < civs.size(); ++i) {
			int c = (int)civs[i];
			setup.civs[i] = rtso::is_playable_civ(c) ? (uint8_t)c : 0;
			int ty = (i < types.size()) ? (int)types[i] : 2;
			setup.slot_types[i] = (ty == 0) ? 'h' : ((ty == 1) ? 'a' : 'x');
			int tm = (i < teams.size()) ? (int)teams[i] : 0;
			setup.teams[i] = (tm >= 0 && tm < 10) ? (uint8_t)tm : 0;
			setup.slot_count++;
		}
		if (setup.slot_count == 0) {
			setup.slot_count = 2;
			setup.civs[0] = sim->player_civ();
			setup.civs[1] = sim->team_civ(1);
		}
	} else {
		setup.map_id = sim->current_map_id();
		setup.civs[0] = sim->player_civ();
		setup.civs[1] = sim->team_civ(1);
	}
	if (setup.map_id.empty()) setup.map_id = "random_continental";
	if (setup.seed == 0) setup.seed = (uint32_t)Time::get_singleton()->get_ticks_msec();
	if (!rtso::valid_victory_code(setup.victory)) setup.victory = sim->victory_condition();
	if (setup.difficulty > 5) setup.difficulty = sim->petra_difficulty();
	if (setup.behavior > 3) setup.behavior = sim->petra_behavior();
	if (setup.start_res >= rtso::Simulation::STARTING_RESOURCE_PRESET_COUNT) setup.start_res = 1;
	// Every connected joiner needs a human slot to play: if the lobby left too
	// few, turn the first AI slots into human ones (before broadcast, so both
	// peers seat players identically in assign_slot_players).
	{
		prepare_lobby_seats(setup, true);
	}
	turn_mgr_.reset();
	turn_mgr_.set_net_host(&net_host_, 0);
	turn_mgr_.assign_slot_players(setup);
	apply_game_setup(setup);
	turn_mgr_.set_game_start_applied_sim(sim);
	turn_mgr_.mark_game_started(sim);
	net_host_.note_game_start_sent();
	net_host_.sendGameStart(rtso::format_game_start_full(setup));
	net_game_started_ = true;
	begin_match_recording(&setup);
	print_line("[RTS0][GameStart] HOST broadcast setup: map=" + String(setup.map_id.c_str()) +
	           " seed=" + String::num_int64((int64_t)setup.seed) +
	           " slots=" + String::num_int64(setup.slot_count) +
	           " civ0=" + String::num_int64((int64_t)setup.civs[0]) +
	           " civ1=" + String::num_int64((int64_t)setup.civs[1]) +
	           " types=" + String::num_int64((int64_t)setup.slot_types[0]) +
	           "/" + String::num_int64((int64_t)setup.slot_types[1]) +
	           " res=" + String::num_int64((int64_t)setup.start_res));
	if (lobby_status)
		lobby_status->set_text(String("Started ") + String(setup.map_id.c_str()) +
		                         " seed " + String::num_int64((int64_t)setup.seed));
}

// Apply the full GameSetup staged by NMT_GAME_START (map+seed+options).
// Order mirrors _ready: map first (reset inside), options after.
void RtsoMain::apply_game_setup(const rtso::GameSetupParams &setup) {
	if (!sim) return;
	// Every peer generates the same map: the size rides in NMT_GAME_START.
	random_map_size = (setup.map_size >= 128 && setup.map_size <= 512) ? setup.map_size : 128;
	match_seed_ = setup.seed;
	random_map_players = 2;
	for (uint8_t i = 0; i < setup.slot_count && i < rtso::MAX_PLAYERS; ++i)
		if (setup.slot_types[i] != 'x') random_map_players = std::max(random_map_players, (int)i + 1);
	for (uint8_t i = 0; i < rtso::MAX_PLAYERS; ++i)
		random_map_teams[i] = (i < setup.slot_count && setup.teams[i] >= 1 && setup.teams[i] <= 8) ? setup.teams[i] : 0;
	sim->set_biome_override((int)setup.biome - 1);
	sim->set_team_placement(setup.team_placement);
	sim->set_map_landscape(setup.landscape);
	sim->set_trigger_difficulty(setup.trigger_difficulty);
	sim->set_map_daytime_choice(setup.daytime);
	// Civs before the map, so starting units take their owner's roster templates.
	for (uint8_t t = 0; t < rtso::MAX_PLAYERS && t < setup.slot_count; ++t) {
		if (rtso::is_playable_civ(setup.civs[t])) sim->set_team_civ(t, setup.civs[t]);
	}
	sim->set_nomad(setup.nomad != 0);
	if (!load_map_seeded(String(setup.map_id.c_str()), setup.seed)) {
		if (lobby_status) lobby_status->set_text("GameStart failed: map regen failed");
		return;
	}
	sim->set_victory_condition(rtso::valid_victory_code(setup.victory) ? setup.victory : 0);
	sim->set_waves_enabled(setup.waves != 0);
	sim->set_last_man_standing(setup.lms != 0);
	if (setup.world_pop) sim->set_world_pop_cap(setup.world_pop);
	else if (setup.team_pop) sim->set_team_pop_cap(setup.team_pop);
	else sim->set_pop_cap(setup.pop_cap);
	sim->set_ceasefire_seconds(60.0f * (float)setup.ceasefire);
	sim->set_reveal_map(setup.reveal != 0);
	sim->set_explore_map(setup.explore != 0);
	sim->set_wonder_duration(60.0f * (float)setup.wonder_min);
	sim->set_relic_duration(60.0f * (float)setup.relic_min);
	sim->set_relic_count(setup.relics);
	sim->set_regicide_garrison(setup.regicide_garrison != 0);
	sim->set_lock_teams(setup.lock_teams != 0);
	sim->set_cheats_enabled(setup.cheats != 0);
	sim->set_disable_spies(setup.no_spies != 0);
	sim->set_disable_treasures(setup.no_treasures != 0);
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) sim->set_player_color(p, setup.colors[p]);
	sim->set_water_level_mode(setup.water_level);
	sim->set_sea_level_rise_minutes(setup.sea_rise_min);
	{
		uint8_t teams[rtso::MAX_PLAYERS] = {};
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS && p < setup.slot_count; ++p) teams[p] = setup.teams[p];
		sim->set_setup_teams(teams);
	}
	sim->apply_team_diplomacy();
	sync_victory_selector();
	sim->set_petra_difficulty(setup.difficulty > 5 ? 3 : setup.difficulty);
	sim->set_petra_behavior(setup.behavior > 3 ? 2 : setup.behavior);
	for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
		sim->set_petra_difficulty(p, setup.diffs[p]);
		sim->set_petra_behavior(p, setup.behs[p]);
	}
	const uint8_t sr = setup.start_res < rtso::Simulation::STARTING_RESOURCE_PRESET_COUNT ? setup.start_res : 1;
	sim->set_starting_resources_preset(sr);
	for (uint8_t t = 0; t < rtso::MAX_PLAYERS && t < setup.slot_count; ++t) {
		if (rtso::is_playable_civ(setup.civs[t])) sim->set_team_civ(t, setup.civs[t]);
	}
	apply_map_player_overrides();
	// Seat every slot: Petra runs only for 'a' slots, so a second human in
	// slot 1 plays their own units instead of sharing them with the AI.
	for (uint8_t t = 0; t < rtso::MAX_PLAYERS; ++t) {
		const bool seated = t < setup.slot_count && setup.slot_types[t] != 'x';
		sim->set_player_active(t, seated, seated && setup.slot_types[t] == 'a');
	}
	clear_selection();
	auto uview = sim->units();
	for (size_t i = 0; i < uview.count; ++i) {
		if (uview.team[i] == local_player()) selected_ids.push_back((int32_t)uview.id[i]);
	}
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		ms->set("map_id", String(setup.map_id.c_str()));
		ms->set("seed", (int64_t)setup.seed);
		ms->set("victory", (int)sim->victory_condition());
		ms->set("waves_enabled", sim->waves_enabled());
		ms->set("last_man_standing", sim->last_man_standing());
		ms->set("pop_cap", sim->team_pop_cap() > 0 ? sim->team_pop_cap() : sim->pop_cap_setting()); // negative: World Population
		ms->set("team_pop", sim->team_pop_cap() > 0);
		ms->set("ceasefire_min", (int)(sim->ceasefire_seconds() / 60.0f));
		ms->set("reveal_map", sim->reveal_map());
		ms->set("explore_map", sim->explore_map());
		ms->set("wonder_min", (int)(sim->wonder_duration() / 60.0f));
		ms->set("relic_min", (int)(sim->relic_duration() / 60.0f));
		ms->set("relic_count", sim->relic_count());
		ms->set("biome", (int)setup.biome);
		Array sdiffs, sbehs;
		for (uint8_t i = 0; i < setup.slot_count && i < 8; ++i) {
			sdiffs.push_back((int)setup.diffs[i]);
			sbehs.push_back((int)setup.behs[i]);
		}
		ms->set("slot_diffs", sdiffs);
		ms->set("slot_behs", sbehs);
		Array scolors;
		for (uint8_t i = 0; i < setup.slot_count && i < 8; ++i) scolors.push_back((int)setup.colors[i]);
		ms->set("slot_colors", scolors);
		ms->set("nomad", setup.nomad != 0);
		ms->set("regicide_garrison", setup.regicide_garrison != 0);
		ms->set("lock_teams", setup.lock_teams != 0);
		ms->set("cheats", setup.cheats != 0);
		ms->set("disable_spies", setup.no_spies != 0);
		ms->set("disable_treasures", setup.no_treasures != 0);
		Array snames;
		for (uint8_t i = 0; i < setup.slot_count && i < 8; ++i) snames.push_back(String::utf8(setup.names[i].c_str()));
		ms->set("slot_names", snames);
		apply_player_names(snames);
		ms->set("water_level", sim->water_level_mode());
		ms->set("team_placement", (int)setup.team_placement);
		ms->set("landscape", (int)setup.landscape);
		ms->set("trigger_difficulty", (int)setup.trigger_difficulty);
		ms->set("map_daytime", (int)setup.daytime);
		ms->set("sea_level_rise_min", sim->sea_level_rise_minutes());
		ms->set("map_size", random_map_size);
		ms->set("ai_difficulty", (int)sim->petra_difficulty());
		ms->set("ai_behavior", (int)sim->petra_behavior());
		ms->set("start_res", (int)sr);
		Array civs;
		for (uint8_t t = 0; t < 4 && t < setup.slot_count; ++t) civs.push_back((int)setup.civs[t]);
		ms->set("slot_civs", civs);
		ms->set("player_civ", (int)sim->player_civ());
		ms->set("ai_civ", (int)sim->team_civ(1));
	}
}
void RtsoMain::apply_net_game_start() {
	if (!sim) return;
	turn_mgr_.set_game_start_applied_sim(sim);
	rtso::GameSetupParams setup;
	if (!turn_mgr_.take_pending_game_start_full(setup)) return;
	turn_mgr_.reset_keep_inbound(); // host batches can arrive in the same poll as GAME_START
	// The host numbers joiners 1, 2, ... in connect order; that id is our client id.
	uint32_t my_client = 0;
	if (!net_host_.isHost()) my_client = net_host_.getLocalPeerId() ? net_host_.getLocalPeerId() : 1;
	turn_mgr_.set_net_host(&net_host_, my_client);
	turn_mgr_.assign_slot_players(setup);
	apply_game_setup(setup); // after seating, so it selects the local player's units
	turn_mgr_.set_game_start_applied_sim(sim);
	turn_mgr_.note_game_start_applied(sim);
	net_game_started_ = true;
	begin_match_recording(&setup);
	print_line("[RTS0][GameStart] JOINER consumed NMT_GAME_START: map=" + String(setup.map_id.c_str()) +
	           " seed=" + String::num_int64((int64_t)setup.seed) +
	           " slots=" + String::num_int64(setup.slot_count) +
	           " civ0=" + String::num_int64((int64_t)setup.civs[0]) +
	           " civ1=" + String::num_int64((int64_t)setup.civs[1]) +
	           " types=" + String::num_int64((int64_t)setup.slot_types[0]) +
	           "/" + String::num_int64((int64_t)setup.slot_types[1]) +
	           " res=" + String::num_int64((int64_t)setup.start_res));
	if (lobby_status)
		lobby_status->set_text(String("GameStart ") + String(setup.map_id.c_str()) +
		                         " seed " + String::num_int64((int64_t)setup.seed));
}

void RtsoMain::_on_lobby_join_pressed() {
	std::string ip = lobby_host->get_text().utf8().get_data();
	uint16_t port = (uint16_t)lobby_port->get_text().to_int();
	PackedInt32Array sel = lobby_rooms->get_selected_items();
	if (sel.size() > 0) {
		int idx = sel[0];
		if (idx >= 0 && (size_t)idx < lobby_room_cache_.size()) {
			ip = lobby_room_cache_[(size_t)idx].host_ip;
			port = lobby_room_cache_[(size_t)idx].host_port;
		}
	}
	if (port == 0) port = rtso::PS_DEFAULT_PORT;
	if (ip.empty()) ip = "127.0.0.1";
	net_host_.setLocalPlayerName(lobby_name->get_text().utf8().get_data());
	net_host_.setPassword(lobby_password ? lobby_password->get_text().utf8().get_data() : "");
	kicks_shown_ = 0;
	if (!net_host_.startClient(ip, port)) {
		if (lobby_status) lobby_status->set_text("Join failed");
		return;
	}
	turn_mgr_.reset();
	turn_mgr_.set_net_host(&net_host_, 1);
	turn_mgr_.clear_game_started();
	net_game_started_ = false; // wait for host's NMT_GAME_START before lockstep turns
	// Once connected, ask to rejoin: a host still in its lobby declines and its
	// NMT_GAME_START starts us as usual; a host mid-match sends its world.
	net_host_.onPeerConnected([this](uint32_t) {
		if (!net_host_.isHost() && !net_game_started_)
			turn_mgr_.request_rejoin(lobby_observe && lobby_observe->is_pressed());
	});
	if (lobby_status)
		lobby_status->set_text("Connecting " + String(ip.c_str()) + ":" + String::num_int64(port));
}

void RtsoMain::set_civ(int civ_idx) {
	if (!sim) return;
	if (!rtso::is_playable_civ(civ_idx)) civ_idx = rtso::CIV_ATHENIANS;
	sim->set_player_civ((uint8_t)civ_idx);

	const auto &info = rtso::civ_info(civ_idx);
	top_panel_.set_civ(civ_idx);
	if (Node *ms = get_node_or_null(NodePath("/root/MatchSettings"))) {
		ms->set("player_civ", civ_idx);
	}
	UtilityFunctions::print("[RtsoMain] Switched Civilization to: ", info.name, " (", info.description, ")");
}

Dictionary RtsoMain::get_civ_info(int civ_idx) const {
	Dictionary d;
	const auto &info = rtso::civ_info(civ_idx);
	d["civ"] = (int)info.civ;
	d["code"] = String(info.code);
	d["name"] = String(info.name);
	d["emblem"] = String(info.emblem_path);
	d["desc"] = String(info.description);
	d["metal_mult"] = info.metal_gather_mult;
	d["trade_gain_mult"] = info.trade_gain_mult;
	d["ship_trade_gain_mult"] = info.ship_trade_gain_mult;
	return d;
}

// Structure tree for the local player's civ: buildings and technologies by
// phase, read from the simulation tables (tech_data.h, extracted from 0 A.D.).
Dictionary RtsoMain::get_tech_tree() const {
	Dictionary d;
	Array blds, techs;
	if (!sim) { d["buildings"] = blds; d["techs"] = techs; return d; }
	uint8_t civ = sim->player_civ(local_player());
	d["civ"] = (int)civ;
	d["phase"] = (int)sim->player_phase(local_player());
	for (uint8_t t = 0; t < rtso::BLD_TYPE_COUNT; ++t) {
		if (!rtso::civ_can_build(civ, t)) continue; // other civs' own structures
		const auto &bi = rtso::building_type_info(t);
		Dictionary b;
		b["id"] = (int)t;
		b["name"] = String::utf8(bi.name);
		b["phase"] = (int)rtso::building_required_phase(t);
		blds.push_back(b);
	}
	for (int t = 1; t < rtso::TECH_COUNT; ++t) {
		if (!rtso::tech_available_to_civ((uint8_t)t, civ)) continue;
		const auto &ti = rtso::tech_info((uint8_t)t);
		Dictionary e;
		e["id"] = t;
		e["name"] = String(ti.name);
		e["desc"] = String(ti.desc ? ti.desc : "");
		e["phase"] = (int)ti.required_phase;
		e["researcher"] = String(rtso::building_type_info(ti.researcher).name);
		e["food"] = ti.food_cost;
		e["wood"] = ti.wood_cost;
		e["metal"] = ti.gold_cost;
		e["stone"] = ti.stone_cost;
		e["time"] = ti.research_time;
		e["prerequisite"] = ti.prerequisite ? String(rtso::tech_info(ti.prerequisite).name) : String();
		e["researched"] = sim->player(local_player()).has_tech((uint8_t)t);
		techs.push_back(e);
	}
	d["buildings"] = blds;
	d["techs"] = techs;
	return d;
}

void RtsoMain::_on_queue_slot_gui_input(const Ref<InputEvent> &event, int slot_idx) {
	production_queue_.handle_gui_input(event, slot_idx, get_viewport());
}

void RtsoMain::_on_queue_slot_cancel(int slot_idx) {
	production_queue_.cancel(slot_idx);
}

void RtsoMain::toggle_chat() {
	if (!chat_window_.is_open()) close_open_dialogs();
	chat_window_.toggle();
}

void RtsoMain::_on_chat_channel_btn_pressed(int channel_idx) {
	chat_window_.select_channel(channel_idx);
}

// colorizePlayernameByGUID: the client's name in its player's colour (grey for an
// observer); "peer-<id>" is the sender's client, empty the local player.
String RtsoMain::chat_user(const String &sender_guid) {
	uint32_t client = turn_mgr_.local_client_id();
	if (sender_guid.begins_with("peer-")) client = (uint32_t)sender_guid.substr(5).to_int();
	String name;
	uint8_t pl = 0xFF;
	if (net_lockstep()) {
		for (const auto &e : net_host_.playerAssignment())
			if (e.client == client) {
				name = String::utf8(e.name.c_str());
				pl = e.player;
			}
		if (pl == 0xFF) pl = turn_mgr_.player_for_client(client);
	} else {
		pl = local_player();
	}
	if (name.is_empty()) name = pl < rtso::MAX_PLAYERS ? player_display_name(pl) : sender_guid;
	char hex[8] = "b3b3b3";
	if (sim && pl < rtso::MAX_PLAYERS) std::snprintf(hex, sizeof(hex), "%06x", (unsigned)(sim->player_color(pl) & 0xFFFFFF));
	return "[color=#" + String(hex) + "]" + name.replace("[", "[lb]") + "[/color]";
}

void RtsoMain::_on_chat_send_pressed() {
	chat_window_.send_pressed();
}

void RtsoMain::_on_chat_text_submitted(const String &text) {
	(void)text;
	chat_window_.send_pressed();
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
		for (const char *k : {"map_size", "start_res", "team_placement", "trigger_difficulty"}) d[k] = ms->get(k);
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
	d["reveal_map"] = sim->reveal_map();
	d["cheats"] = sim->cheats_enabled();
	return d;
}

String RtsoMain::match_settings_description() const {
	Ref<Script> desc = ResourceLoader::get_singleton()->load("res://scenes/session/game_description.gd");
	return desc.is_valid() ? (String)desc->call("describe", get_match_settings_info()) : String();
}

// MatchSettingsDialog.rebuild: g_PlayerStateMessages for a player who has won or lost.
String RtsoMain::match_settings_player_state() const {
	const uint8_t me = local_player();
	if (!sim || me >= rtso::MAX_PLAYERS) return String();
	if (sim->is_victory_for(me)) return "You have won!";
	if (sim->is_defeated(me)) return "You have been defeated!";
	return String();
}

// session.js closeOpenDialogs: the menu, chat, diplomacy, trade and match
// settings close together; opening one of them closes the others first.
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

// 0 A.D. messages.js onTutorialReady: "dialog-answer" to the trigger script; the
// last goal's Leave opens the session menu (0 A.D. ends the game there).
void RtsoMain::_on_tutorial_ready_pressed() {
	if (!sim || !sim->tutorial_ready_button()) return;
	if (tutorial_panel_.on_last_goal(*sim)) {
		_on_menu_button_pressed();
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
