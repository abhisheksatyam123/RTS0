// RtsoMain's Godot lifecycle hooks (0 A.D. gui/session/session.js init and onTick): _ready builds
// the 3D scene and the session HUD, _process runs the frame (sim ticks, lockstep, HUD refresh).
// Split out of main_node.cpp (PLAN P4.6); the state they set up is RtsoMain's.

#include "network/chat_routing.h"
#include "main_node.h"
#include "l10n.h"
#include <godot_cpp/classes/style_box_flat.hpp>
#include <cmath>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/font_variation.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#include <godot_cpp/classes/crypto.hpp>
#include <godot_cpp/classes/hashing_context.hpp>
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
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/base_button.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/script.hpp>
#include "user_config.h"
#include "hotkey_config.h"
#include "display_colors.h"
#include <godot_cpp/classes/audio_stream.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include "../core/diagnostics/logger.h"
#include "../core/diagnostics/perf_tracker.h"
#include "../core/job_system.h"
#include "../graphics/assets/model_library.h"
#include <sstream>
using namespace godot;
using rtso::sprintf0;

// 0 A.D. HashCryptographically, as lobby_session.gd encrypt_password: PBKDF2-
// HMAC-SHA256, 1337 rounds, salted with SHA-256(fixed base + lower-case name),
// as upper-case hex. The game password travels only as this.
static std::string hash_game_password(const std::string &password, const std::string &name) {
	static const uint8_t kSaltBase[32] = {244, 243, 249, 244, 32, 33, 34, 35, 10, 11, 12, 13, 14, 15, 16, 17,
			18, 19, 20, 32, 33, 244, 224, 127, 129, 130, 140, 153, 133, 123, 234, 123};
	PackedByteArray base;
	base.resize(32);
	std::memcpy(base.ptrw(), kSaltBase, 32);
	Ref<HashingContext> ctx;
	ctx.instantiate();
	ctx->start(HashingContext::HASH_SHA256);
	ctx->update(base);
	ctx->update(String::utf8(name.c_str()).to_lower().to_utf8_buffer());
	PackedByteArray block = ctx->finish();
	for (uint8_t b : {0, 0, 0, 1}) block.push_back(b);
	Ref<Crypto> crypto;
	crypto.instantiate();
	const PackedByteArray key = String::utf8(password.c_str()).to_utf8_buffer();
	PackedByteArray u = crypto->hmac_digest(HashingContext::HASH_SHA256, key, block);
	PackedByteArray out = u.duplicate();
	for (int i = 1; i < 1337; ++i) {
		u = crypto->hmac_digest(HashingContext::HASH_SHA256, key, u);
		for (int64_t j = 0; j < out.size(); ++j) out.set(j, out[j] ^ u[j]);
	}
	return std::string(out.hex_encode().to_upper().utf8().get_data());
}

void RtsoMain::_ready() {
	rtso::Logger::info("Main", "RTS0 0 A.D. Reborn Ready - Full 0 A.D. UI & Simulation");
	// 0 A.D. writes oos_dump.txt/.dat to its log dir; ours is user://.
	turn_mgr_.set_oos_dump_dir(OS::get_singleton()->get_user_data_dir().utf8().get_data());
	{
		// 0.28's -ooslog, -serializationtest, -rejointest=N and -unique-logs (main.cpp).
		Dictionary tests;
		PackedStringArray args = OS::get_singleton()->get_cmdline_args();
		args.append_array(OS::get_singleton()->get_cmdline_user_args());
		for (const String &a : args) {
			const String arg = a.trim_prefix("-").trim_prefix("-");
			if (arg == "ooslog") tests["ooslog"] = true;
			else if (arg == "serializationtest") tests["serializationtest"] = true;
			else if (arg == "unique-logs") tests["unique_logs"] = true;
			else if (arg.begins_with("rejointest=")) tests["rejointest"] = arg.get_slice("=", 1).to_int();
		}
		set_sim_tests(tests);
	}

	// Incoming chat: 0 A.D. NetMessages.h NMT_CHAT parity. Fires on the main
	// thread from net_host_.poll() (_process), so appending to the chat
	// widget (chat_window.h) is safe.
	// net_host.cpp:540 dispatch -> onChat (:395) -> display here.
	// 0 A.D. NetServer OnChat: as host, an addressed line goes only to the
	// clients it is for (the receivers keep their own check too).
	net_host_.setPasswordHasher(hash_game_password);
	net_host_.setChatRouter([this](uint32_t from, uint32_t to, const std::string &message) {
		const std::string tag = rtso::chat_wire_tag(message);
		return tag.empty() || chat_reaches(chat_player_of_client(from), chat_player_of_client(to), tag, to);
	});
	net_host_.onChat([this](const rtso::ChatMsg &cm) {
		String msg = String::utf8(cm.message.c_str());
		// Tactical flare piggybacks NMT_CHAT (approximation of 0 A.D. minimap
		// flare): "[Flare] x,z" -> local map ping, not chat text.
		if (msg.begins_with("[Flare]")) {
			// 0 A.D. sends a flare to the sender's mutual allies and the observers.
			const String guid = String::utf8(cm.sender_guid.c_str());
			if (!chat_local_observer() && !chat_is_addressee(guid, "[Allies]")) return;
			// messages.js g_FlareRateLimitScope / g_FlareRateLimitMaximumFlares: at
			// most 16 flares from others in any 6 s; more are dropped.
			const uint64_t now = Time::get_singleton()->get_ticks_msec();
			flare_received_ms_.erase(std::remove_if(flare_received_ms_.begin(), flare_received_ms_.end(),
					[now](uint64_t t) { return now - t >= 6000; }), flare_received_ms_.end());
			if (flare_received_ms_.size() >= 16) {
				const std::string line = "Received too many flares. Dropping a flare request by '" +
						std::string(guid.utf8().get_data()) + "'.";
				rtso::Logger::warn("Chat", line.c_str());
				return;
			}
			flare_received_ms_.push_back(now);
			String coords = msg.substr(7).strip_edges();
			int32_t comma = coords.find(",");
			if (comma > 0) {
				float fx = coords.substr(0, comma).to_float();
				float fz = coords.substr(comma + 1).to_float();
				const uint8_t sender = chat_player_of(guid);
				const Color c = player_flare_color(sender);
				if (minimap_view) minimap_view->add_ping(Vector2(fx, fz), c);
				range_overlays_.add_flare(fx, fz, c, user_opts_.flare_time);
				play_alarm("res://assets/audio/interface/alarm/alarmally_1.ogg");
				// ChatMessageFormatSimulation.flare: gui.session.notifications.flare
				// "never", "observer" (only while observing) or "always".
				const std::string when = rtso::user_config::get("gui.session.notifications.flare");
				const uint8_t from = chat_player_of(guid);
				// colorizePlayernameByGUID: an observer's name in white.
				const String name = from < rtso::MAX_PLAYERS ? colored_player_name(from) : chat_user(guid).replace("[color=#b3b3b3]", "[color=#ffffff]");
				if (when == "always" || (when == "observer" && chat_local_observer()))
					chat_window_.append_line(sprintf0(tr("%(icon)s%(player)s has sent a flare."),
							{{"icon", "[img=16x16]res://assets/textures/ui/archive/icons/focus-flare.png[/img]"},
									{"player", name}}) + "\n",
							"flare:" + String::num(fx) + "," + String::num(fz), tr("Click to focus on the flare's location."));
			}
			return;
		}
		// Channel-tag parse + RichTextLabel append live in the chat leaf
		// widget (chat_window.h append_incoming()); no-op until built.
		const String sender = String::utf8(cm.sender_guid.c_str());
		// 0.28 sound.notify.nick: a chime when someone else's message names you.
		if (chat_window_.append_incoming(sender, msg)) {
			// ChatMessageFormatPlayer: the plain nick (splitRatingFromNick drops a lobby
			// rating) in someone else's message text, past its wire tag.
			const String self = "peer-" + String::num_int64(turn_mgr_.local_client_id());
			String nick = RtsoChatWindow::plain(chat_user(self));
			const int paren = nick.rfind(" (");
			if (paren > 0 && nick.ends_with(")") && nick.substr(paren + 2, nick.length() - paren - 3).is_valid_int())
				nick = nick.substr(0, paren);
			String text = msg;
			if (text.begins_with("[") && text.find("]") > 0) text = text.substr(text.find("]") + 1);
			if (!nick.is_empty() && sender != self && RtsoChatWindow::plain(chat_user(sender)) != RtsoChatWindow::plain(chat_user(self)) &&
					text.to_lower().contains(nick.to_lower()))
				sound_notification_nick();
		}
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
	debug_overlay->set_visible(false); // shown with overlay.fps (refresh_user_options)
	ui_layer->add_child(debug_overlay);

	minimap_panel_.build_background(ui_layer, [](const String &path) { return rtso::load_ui_texture(path); });
	minimap_view = memnew(RtsoMiniMapView);
	minimap_view->connect("minimap_clicked", Callable(this, "_on_minimap_clicked"));
	minimap_view->connect("minimap_order", Callable(this, "_on_minimap_order"));
	ui_layer->add_child(minimap_view);
	minimap_panel_.build(ui_layer, this, [](const String &path) { return rtso::load_ui_texture(path); });

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
	// Tooltips are plain text: the name without colorizePlayernameHelper's coloured square.
	top_panel_.set_ally_name([this](uint8_t p) { return player_display_name(p); });
	{
		auto first_key = [](const char *name) {
			const auto *combos = rtso::hotkeys::table().get(name);
			return (combos && !combos->empty()) ? String::utf8(rtso::hotkeys::combo_to_string(combos->front()).c_str()) : String();
		};
		top_panel_.set_civ_icon_keys(first_key("civinfo"), first_key("structree"));
		minimap_panel_.set_hotkeys(first_key("selection.idleworker"), first_key("session.diplomacycolors"), first_key("session.flare"));
	}

	time_notification_label_ = memnew(RichTextLabel);
	time_notification_label_->set_name("TimeNotificationText");
	time_notification_label_->set_use_bbcode(true);
	time_notification_label_->set_fit_content(true);
	time_notification_label_->set_scroll_active(false);
	// TimeNotificationOverlay.xml: 50%-300 60 50%+300 120, white sans-bold-stroke-14.
	time_notification_label_->set_anchor(SIDE_LEFT, 0.5f);
	time_notification_label_->set_anchor(SIDE_RIGHT, 0.5f);
	time_notification_label_->set_anchor(SIDE_TOP, 0.0f);
	time_notification_label_->set_anchor(SIDE_BOTTOM, 0.0f);
	time_notification_label_->set_offset(SIDE_LEFT, -300.0f);
	time_notification_label_->set_offset(SIDE_RIGHT, 300.0f);
	time_notification_label_->set_offset(SIDE_TOP, 60.0f);
	time_notification_label_->set_offset(SIDE_BOTTOM, 120.0f);
	time_notification_label_->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	time_notification_label_->add_theme_font_size_override("normal_font_size", 14);
	time_notification_label_->add_theme_font_size_override("bold_font_size", 14);
	time_notification_label_->add_theme_color_override("default_color", Color(1, 1, 1));
	time_notification_label_->add_theme_color_override("font_outline_color", Color(0.0f, 0.0f, 0.0f));
	time_notification_label_->add_theme_constant_override("outline_size", 4);
	time_notification_label_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	ui_layer->add_child(time_notification_label_);

	// NetworkDelayOverlay.xml: size="100%-300 60 100%-110 80", mono-10, white,
	// right-aligned, on "color: 0 0 0 100".
	net_delay_label_ = memnew(Label);
	net_delay_label_->set_name("NetDelayOverlay");
	net_delay_label_->set_anchor(SIDE_LEFT, 1.0f);
	net_delay_label_->set_anchor(SIDE_RIGHT, 1.0f);
	net_delay_label_->set_offset(SIDE_LEFT, -300.0f);
	net_delay_label_->set_offset(SIDE_RIGHT, -110.0f);
	net_delay_label_->set_offset(SIDE_TOP, 60.0f);
	net_delay_label_->set_offset(SIDE_BOTTOM, 80.0f);
	net_delay_label_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	net_delay_label_->set_vertical_alignment(VERTICAL_ALIGNMENT_TOP);
	net_delay_label_->add_theme_font_size_override("font_size", 10);
	net_delay_label_->add_theme_color_override("font_color", Color(1.0f, 1.0f, 1.0f));
	{
		Ref<StyleBoxFlat> bg;
		bg.instantiate();
		bg->set_bg_color(Color(0.0f, 0.0f, 0.0f, 100.0f / 255.0f));
		net_delay_label_->add_theme_stylebox_override("normal", bg);
	}
	net_delay_label_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	net_delay_label_->set_visible(false);
	ui_layer->add_child(net_delay_label_);

	placement_tip_ = memnew(Label);
	placement_tip_->set_name("placementTooltip");
	placement_tip_->add_theme_font_size_override("font_size", 13);
	placement_tip_->add_theme_color_override("font_color", Color(1.0f, 1.0f, 1.0f));
	placement_tip_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	placement_tip_->set_visible(false);
	ui_layer->add_child(placement_tip_);

	cursor_tip_ = memnew(Label);
	cursor_tip_->add_theme_font_size_override("font_size", 13);
	cursor_tip_->add_theme_color_override("font_color", Color(1.0f, 1.0f, 1.0f));
	cursor_tip_->add_theme_color_override("font_outline_color", Color(0.0f, 0.0f, 0.0f));
	cursor_tip_->add_theme_constant_override("outline_size", 4);
	cursor_tip_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	cursor_tip_->set_visible(false);
	ui_layer->add_child(cursor_tip_);

	// GameSpeedButton: a 28 px time_small icon, "Game Speed", first of the right-hand
	// buttons (Game Speed, Diplomacy, Barter & Trade, Match Settings, Menu).
	speed_btn = memnew(Button);
	speed_btn->set_name("GameSpeedButton");
	speed_btn->set_custom_minimum_size(Vector2(28, 28));
	{
		Ref<Texture2D> tex = rtso::load_ui_texture("res://assets/textures/ui/archive/icons/resources/time_small.png");
		if (tex.is_valid()) {
			speed_btn->set_button_icon(tex);
			speed_btn->set_expand_icon(true);
		} else {
			speed_btn->set_text(speed_text(1.0f));
		}
	}
	speed_btn->connect("pressed", Callable(this, "_on_speed_button_pressed"));
	top_hbox->add_child(speed_btn);
	if (Button *diplo = top_panel_.dialog_button(0)) top_hbox->move_child(speed_btn, diplo->get_index());
	for (int i = 0; i < 3; ++i)
		if (Button *b = top_panel_.dialog_button(i)) b->connect("mouse_entered", Callable(this, "_refresh_menu_tooltip"));

	// MenuButton: 156 x 28.
	menu_btn = memnew(Button);
	menu_btn->set_custom_minimum_size(Vector2(156, 28));
	menu_btn->set_text("Menu");
	// Menu.js rebuild, on every hover so a rebind or a language change shows.
	_refresh_menu_tooltip();
	menu_btn->connect("mouse_entered", Callable(this, "_refresh_menu_tooltip"));
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
	lobby_start_btn_ = start_b;
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
	inspector_.build(this, ui_layer, zeroad_theme, this);
	command_panel_.set_barter_host(inspector_.barter_host());

	// 0 A.D. selection_details.js: the "i" button opens the Template Viewer
	// (gui/reference/viewer) for the selected unit or structure.
	template_info_btn_ = memnew(Button);
	template_info_btn_->set_position(Vector2(74, 74));
	template_info_btn_->set_size(Vector2(20, 20));
	template_info_btn_->set_text("i");
	template_info_btn_->add_theme_font_size_override("font_size", 12);
	template_info_btn_->set_focus_mode(Control::FOCUS_NONE);
	template_info_btn_->connect("pressed", Callable(this, "_on_template_info_pressed"));
	inspector_.portrait()->add_child(template_info_btn_); // hides with the portrait


	// =========================================================================
	// 3. COMMAND & ACTION PANEL (Stances, Formations, Training, 8-Slot Queue)
	// =========================================================================
	command_panel_.build(this, ui_layer, zeroad_theme, this, this);
	inspector_.move_to_left_panel(command_panel_.left_panel());

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
		dim->set_color(Color(0, 0, 0, 185.0f / 255.0f)); // sprites.xml: 0 0 0 185
		dim->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		pause_overlay_->add_child(dim);
		Ref<FontVariation> bold;
		bold.instantiate();
		bold->set_base_font(ThemeDB::get_singleton()->get_fallback_font());
		bold->set_variation_embolden(0.7f);
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
			l->add_theme_font_override("font", bold); // styles.xml: sans-bold-*
			l->add_theme_color_override("font_outline_color", Color(0, 0, 0));
			l->add_theme_constant_override("outline_size", 4);
			l->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			pause_overlay_->add_child(l);
			return l;
		};
		// PauseOverlay.xml: "Game Paused" sans-bold-24, "Click to Resume Game" sans-bold-12.
		caption(tr("Game Paused"), -20.0f, 24);
		pause_resume_label_ = caption(tr("Click to Resume Game"), 20.0f, 12);
		// "Paused by" names each client in its player's colour, so it is bbcode.
		pause_by_label_ = memnew(RichTextLabel);
		pause_by_label_->set_use_bbcode(true);
		pause_by_label_->set_fit_content(true);
		pause_by_label_->set_scroll_active(false);
		pause_by_label_->set_anchor(SIDE_LEFT, 0.3f);
		pause_by_label_->set_anchor(SIDE_RIGHT, 0.7f);
		pause_by_label_->set_anchor(SIDE_TOP, 0.4f);
		pause_by_label_->set_anchor(SIDE_BOTTOM, 0.4f);
		pause_by_label_->set_offset(SIDE_TOP, 50.0f);
		pause_by_label_->set_offset(SIDE_BOTTOM, 72.0f);
		pause_by_label_->add_theme_font_size_override("normal_font_size", 16); // netStatusPlayersText: sans-bold-16
		pause_by_label_->add_theme_font_override("normal_font", bold);
		pause_by_label_->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		pause_by_label_->add_theme_constant_override("outline_size", 4);
		pause_by_label_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		pause_overlay_->add_child(pause_by_label_);
	}
	ui_layer->add_child(pause_overlay_);
	// session.xml includes PauseOverlay before the top panel, menus and HUD panels:
	// it dims the world, and the interface above it stays usable.
	ui_layer->move_child(pause_overlay_, 0);

	// gameStateNotifications: top right, under the data counter.
	net_warnings_label_ = memnew(RichTextLabel);
	net_warnings_label_->set_anchors_preset(Control::PRESET_TOP_RIGHT);
	net_warnings_label_->set_offset(SIDE_LEFT, -420.0f);
	net_warnings_label_->set_offset(SIDE_RIGHT, -15.0f);
	net_warnings_label_->set_offset(SIDE_TOP, 40.0f);
	net_warnings_label_->set_offset(SIDE_BOTTOM, 140.0f);
	net_warnings_label_->set_use_bbcode(true);
	net_warnings_label_->set_fit_content(true);
	net_warnings_label_->set_scroll_active(false);
	net_warnings_label_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	net_warnings_label_->add_theme_color_override("font_outline_color", Color(0, 0, 0));
	net_warnings_label_->add_theme_constant_override("outline_size", 3);
	net_warnings_label_->add_theme_font_size_override("normal_font_size", 13);
	net_warnings_label_->set_visible(false);
	ui_layer->add_child(net_warnings_label_);

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
		// reportConnectionRequestTimeOut's messageBox title.
		net_status_title_ = memnew(Label);
		net_status_title_->set_name("NetStatusTitle");
		net_status_title_->set_anchors_preset(Control::PRESET_CENTER);
		net_status_title_->set_offset(SIDE_LEFT, -300);
		net_status_title_->set_offset(SIDE_RIGHT, 300);
		net_status_title_->set_offset(SIDE_TOP, -150);
		net_status_title_->set_offset(SIDE_BOTTOM, -120);
		net_status_title_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		net_status_title_->add_theme_font_size_override("font_size", 20);
		net_status_title_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		net_status_title_->set_visible(false);
		net_status_overlay_->add_child(net_status_title_);
		// loadingClientsText: 50%-300 50%+60 50%+300 50%+110, the names in their colours.
		net_loading_clients_ = memnew(RichTextLabel);
		net_loading_clients_->set_use_bbcode(true);
		net_loading_clients_->set_fit_content(true);
		net_loading_clients_->set_anchors_preset(Control::PRESET_CENTER);
		net_loading_clients_->set_offset(SIDE_LEFT, -300);
		net_loading_clients_->set_offset(SIDE_RIGHT, 300);
		net_loading_clients_->set_offset(SIDE_TOP, 60);
		net_loading_clients_->set_offset(SIDE_BOTTOM, 110);
		net_loading_clients_->add_theme_font_size_override("normal_font_size", 16);
		net_loading_clients_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		net_loading_clients_->set_visible(false);
		net_status_overlay_->add_child(net_loading_clients_);
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
		net_status_exit_ = exit;
		// reportConnectionRequestTimeOut: Close and Open FAQ.
		Button *faq = memnew(Button);
		faq->set_name("OpenFaqButton");
		faq->set_text(tr("Open FAQ"));
		faq->set_anchors_preset(Control::PRESET_CENTER);
		faq->set_offset(SIDE_LEFT, 8);
		faq->set_offset(SIDE_RIGHT, 176);
		faq->set_offset(SIDE_TOP, 128);
		faq->set_offset(SIDE_BOTTOM, 160);
		faq->set_visible(false);
		faq->connect("pressed", Callable(this, "_on_open_faq_pressed"));
		net_status_overlay_->add_child(faq);
		net_status_faq_ = faq;
	}
	ui_layer->add_child(net_status_overlay_);

	// CLogger's on-screen messages (LOGMESSAGERENDER, LOGERROR): top left, mono-stroke-10,
	// at most 20 lines, each for 10 s; errors red.
	log_label_ = memnew(RichTextLabel);
	log_label_->set_name("LogMessages");
	log_label_->set_use_bbcode(true);
	log_label_->set_fit_content(true);
	log_label_->set_scroll_active(false);
	log_label_->set_position(Vector2(4, 4));
	log_label_->set_size(Vector2(900, 10));
	log_label_->add_theme_font_size_override("normal_font_size", 11);
	log_label_->add_theme_color_override("default_color", Color(1, 1, 1));
	log_label_->add_theme_color_override("font_outline_color", Color(0, 0, 0));
	log_label_->add_theme_constant_override("outline_size", 3);
	log_label_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	log_label_->set_visible(false);
	ui_layer->add_child(log_label_);

	// 5/6. Chat overlay + diplomacy/tribute matrix: P4.1 leaf widgets
	// (chat_window.h / diplomacy_dialog.h own the panel trees; the bound
	// handler methods below delegate into them).
	chat_window_.build(this, ui_layer, this);
	refresh_chat_tooltips();
	diplo_dialog_.build(this, ui_layer, this, &chat_window_);
	objectives_dialog_.build(ui_layer, this, "toggle_objectives");
	tutorial_panel_.build(ui_layer, this, "_on_tutorial_ready_pressed");
	panel_entities_.build(ui_layer, this, "_on_panel_entity_gui_input", 58.0f); // PanelEntities.xml, under the top panel
	research_progress_.build(ui_layer, this, "_on_research_progress_pressed", 58.0f);
	dev_overlay_.build(ui_layer, this, "_on_dev_command", "_on_render_debug_mode");
	control_groups_.build(ui_layer, this, "_on_control_group_gui_input");
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
	// 0.28's button styles (iconButton, StoneButton, BrownButton, CivIconOverlay, the dropdowns)
	// play ui_button_click.ogg on a press: every button of the session UI does here, through its
	// own player so it never cuts off another sound.
	click_player_ = memnew(AudioStreamPlayer);
	click_player_->set_bus("UI");
	click_player_->set_stream(rtso::load_audio("res://assets/sounds/ui/ui_button_click.ogg"));
	add_child(click_player_);
	hook_ui_click_sounds(this);
	get_tree()->connect("node_added", Callable(this, "_on_tree_node_added"));

	// 0 A.D. music.js playlists and in-game sound events (music_manager.h,
	// session_audio.h).
	music_.build(this);
	if (Node *ma = get_tree() ? get_tree()->get_root()->get_node_or_null(NodePath("MenuAudio")) : nullptr)
		ma->set("session_music_tick", Callable(this, "tick_session_music"));
	session_audio_.build(this);
	session_audio_.set_spatial([this](const Ref<AudioStream> &s, Vector3 pos, float db, float pitch) {
		if (view) view->play_spatial_sound(s, pos, db, 75.0f, pitch);
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

	range_overlays_.build(this);
	rally_points_.build(this);
	debug_overlays_.build(this);

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
	if (start_pending_replay()) {
	} else if (load_pending_save()) {
		if (!net_saved_game_.is_empty()) call_deferred("_start_menu_net"); // Host Saved Game
	} else {
		apply_match_settings();
		begin_match_recording(nullptr);
		call_deferred("_start_menu_net");
	}
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
		if (net_host_.isHost()) host_net_setup_tick(false);
		else client_net_setup_tick();
		update_net_setup_overlay();
		update_net_connect_overlay();
		// A host that turned us away mid-match says why (0 A.D. disconnect reasons).
		if (!net_host_.isHost() && !net_game_started_ && lobby_status) {
			const uint8_t why = turn_mgr_.rejoin_reject_reason();
			const char *text = why == 6 ? "This game does not let observers join after it started."
					: why == 7 ? "This game is full: it has as many observers as it allows."
					: why == 3 || why == 4 || why == 5 ? "Could not load the host's game."
					// gamesetup_mp.js handleAuthenticated: the host is sending its running game.
					: turn_mgr_.rejoin_syncing() ? "Game has already started, rejoining..."
					: nullptr;
			if (text && lobby_status->get_text() != text) lobby_status->set_text(text);
		}
		if (!net_host_.isHost() && !net_game_started_ && sim && turn_mgr_.apply_pending_rejoin(*sim)) {
			net_game_started_ = true;
			// VisualReplay skips a rejoined client's replay ("Don't process files of rejoined
			// clients"): its commands start mid-match, so it is not recorded at all.
			replay_recording_ = false;
			start_net_speed(net_setup_.seq ? net_setup_.setup.speed : 100);
			selected_ids.clear();
			selected_building_id = 0;
			if (lobby_status && net_setup_.seq && net_setup_.setup.saved_game)
				lobby_status->set_text(turn_mgr_.local_player() < rtso::MAX_PLAYERS
						? "Loaded the host's saved game as Player " + String::num_int64(turn_mgr_.local_player() + 1)
						: String("Loaded the host's saved game as an observer"));
			else if (lobby_status)
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
	// (The music's own tick runs in the MenuAudio autoload, paused or not.)
	if (notice_until_ && Time::get_singleton()->get_ticks_msec() >= notice_until_) {
		notice_until_ = 0;
		if (!hotkey_paused_ && banner_panel) banner_panel->set_visible(false);
	}
	update_log_messages();
	update_pause_overlay();
	if (hotkey_paused_ && !net_lockstep()) return;
	if (app_unfocused_ && !net_lockstep() && rtso::user_config::get_bool("pauseonfocusloss")) return;
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
		else if (net_host_.disconnectReason() == rtso::NDR_SERVER_FULL) lobby_status->set_text(tr("Server full."));
		else if (net_host_.disconnectReason() == rtso::NDR_LOBBY_AUTH_FAILED)
			lobby_status->set_text(tr("Secure lobby authentication failed. Join via lobby."));
		else if (net_host_.disconnectReason() == rtso::NDR_PLAYERNAME_IN_USE)
			lobby_status->set_text(tr("Player name in use. If you were disconnected, retry in few seconds."));
		else if (net_host_.disconnectReason() == rtso::NDR_INCORRECT_PROTOCOL_VERSION)
			lobby_status->set_text(tr("Incorrect network protocol version."));
		else if (net_host_.disconnectReason() == rtso::NDR_INCORRECT_SOFTWARE_VERSION)
			lobby_status->set_text(mismatch_detail());
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
			if (turn_mgr_.tick((float)scaled_delta, *sim)) {
				ticks = rtso::TurnManager::TICKS_PER_TURN;
				sim_tests_turn();
				rejoin_test_turn();
			}
			// OutOfSyncNetwork: the box opens once the dump is written, and again
			// if the host later names an earlier diverging turn.
			if (turn_mgr_.has_out_of_sync() && !turn_mgr_.oos_dump_pending() &&
					net_oos_shown_turn_ != turn_mgr_.oos_turn()) {
				net_oos_shown_turn_ = turn_mgr_.oos_turn();
				write_replay(false, true); // "Replay saved to", however early
				quit_confirmation("out_of_sync_network");
			}
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
		if (net_game_started_) post_network_chat();
		update_net_status_overlay();
		update_net_warnings();
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
				sim_tests_turn();
				rejoin_test_turn();
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
	{
		view->set_selected_buildings(sim->cinema_playing() ? std::vector<uint32_t>() : selected_structures());
	}
	if (sim->cinema_playing()) {
		// ICmpSelectable::SetOverrideVisibility(false): no selection rings while a path plays.
		view->sync(sim, (float)delta, PackedInt32Array());
	} else if (inspector_.kind() == RtsoInspectorView::INSPECT_UNIT || inspector_.kind() == RtsoInspectorView::INSPECT_FAUNA) {
		PackedInt32Array shown = selected_ids;
		shown.push_back((int32_t)inspector_.id());
		view->sync(sim, (float)delta, shown);
	} else {
		view->sync(sim, (float)delta, selected_ids);
	}
	cinema_view_.update(*sim, cam_rig, ui_layer, this, delta);
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
	update_net_delay_overlay(delta);
	if (sim && time_notification_label_) {
		// l10n.js timeToString: "mm:ss", or "HH:mm:ss" from an hour on (whole seconds dropped down).
		auto time_to_string = [](int64_t ms) {
			const int64_t t = std::max<int64_t>(0, ms) / 1000;
			return t < 3600 ? vformat("%02d:%02d", t / 60, t % 60) : vformat("%02d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
		};
		String txt;
		for (const rtso::TimeNotification *n : sim->time_notifications_for(local_player())) {
			if (!txt.is_empty()) txt += "\n";
			// TimeNotificationOverlay: the message translated, then its parameters
			// ("\x1fname=value" after the format) and %(time)s, the time left (timeToString).
			const PackedStringArray parts = String::utf8(n->text).split(String::chr(0x1f));
			String line = rtso::tr0(parts[0]);
			for (int k = 1; k < parts.size(); ++k) {
				const int eq = parts[k].find("=");
				if (eq > 0) line = line.replace("%(" + parts[k].substr(0, eq) + ")s", parts[k].substr(eq + 1));
			}
			if (line.contains("%(time)s")) {
				const uint32_t now = sim->trigger_state().time_ms;
				line = line.replace("%(time)s", time_to_string(n->end_ms > now ? (int64_t)(n->end_ms - now) : 0));
			}
			txt += line.replace("[", "[lb]"); // a map's text, not markup
		}
		{
			// GetTimeNotifications sorts by end time, latest first.
			const uint32_t now = (uint32_t)std::lround(sim->game_time() * 1000.0f);
			text_notices_.erase(std::remove_if(text_notices_.begin(), text_notices_.end(),
					[now](const TextNotice &t) { return t.end_ms <= now || t.end_ms > now + 10000; }), text_notices_.end());
			for (auto it = text_notices_.rbegin(); it != text_notices_.rend(); ++it) {
				if (!txt.is_empty()) txt += "\n";
				txt += it->text;
			}
			if (spawn_notified_.size() > 64) spawn_notified_.clear();
		}
		// WonderVictory.js and CaptureTheRelic.js time notifications, worded for the local
		// player (the wonder's owner, its allies or everyone else).
		{
			const uint8_t me = local_player();
			auto clock = [&](float s) { return time_to_string((int64_t)(s * 1000.0f)); };
			auto add = [&](const String &l) { if (!txt.is_empty()) txt += "\n"; txt += l; };
			// One set of lines per wonder, for the allies it started with (defeated ones too).
			if (sim->has_victory(rtso::VK_WONDER) && !sim->is_game_over())
				for (const auto &w : sim->buildings()) {
					const uint8_t p = w.wonder_owner;
					if (w.type != rtso::BLD_WONDER || p >= rtso::MAX_PLAYERS || w.wonder_hold <= 0.0f || sim->is_defeated(p)) continue;
					const String left = clock(sim->wonder_duration() - w.wonder_hold);
					const uint8_t allies = w.wonder_allies;
					const String who = colored_player_name(p);
					if (p == me) add(rtso::sprintf0(rtso::tr0(allies ? "You own a Wonder and you and your allies will win in %(time)s."
							: "You own a Wonder and will win in %(time)s."), {{"time", left}}));
					else if (me < rtso::MAX_PLAYERS && ((allies >> me) & 1))
						add(rtso::sprintf0(rtso::tr0("%(_player_)s owns a Wonder and you will win in %(time)s."), {{"_player_", who}, {"time", left}}));
					else add(rtso::sprintf0(rtso::tr0(allies ? "%(_player_)s owns a Wonder and %(_player_)s and their allies will win in %(time)s."
							: "%(_player_)s owns a Wonder and will win in %(time)s."), {{"_player_", who}, {"time", left}}));
				}
			// EndGameManager.AlliedVictoryCheck: under Last Man Standing, once only mutual
			// allies are left, "Last remaining player wins." (a 12-hour time notification).
			if (sim->last_man_standing() && sim->victory_conditions() && !sim->is_game_over()) {
				int left = 0;
				bool allied = true;
				for (uint8_t a = 0; a < rtso::MAX_PLAYERS; ++a) {
					if (!((sim->participant_mask() >> a) & 1) || sim->is_defeated(a)) continue;
					++left;
					for (uint8_t b = a + 1; b < rtso::MAX_PLAYERS; ++b)
						if (((sim->participant_mask() >> b) & 1) && !sim->is_defeated(b) && !(sim->is_ally(a, b) && sim->is_ally(b, a))) allied = false;
				}
				if (left > 1 && allied) add(rtso::tr0("Last remaining player wins."));
			}
			if (sim->has_victory(rtso::VK_RELIC) && !sim->is_game_over() && sim->relic_victory_timer() > 0.0f && !sim->relics().empty()) {
				// StartCaptureTheRelicCountdown: named after the first relic's owner, "and their
				// allies" when more than one player would win.
				const uint8_t owner = sim->relics()[0].owner_team;
				const uint8_t side = sim->relic_countdown_players();
				if (owner < rtso::MAX_PLAYERS && side) {
					const bool team = (side & (side - 1)) != 0;
					const String left = clock(sim->relic_duration() - sim->relic_victory_timer());
					if (me < rtso::MAX_PLAYERS && ((side >> me) & 1))
						add(rtso::sprintf0(rtso::tr0(team ? "You and your allies have captured all relics and will win in %(time)s."
								: "You have captured all relics and will win in %(time)s."), {{"time", left}}));
					else
						add(rtso::sprintf0(rtso::tr0(team ? "%(_player_)s and their allies have captured all relics and will win in %(time)s."
								: "%(_player_)s has captured all relics and will win in %(time)s."), {{"_player_", colored_player_name(owner)}, {"time", left}}));
				}
			}
		}
		// CeasefireManager: "You can attack in %(time)s" for the last 10 s, then
		// "You can attack now!" for 5 s.
		if (sim->ceasefire_seconds() > 0.0f) {
			const float left = sim->ceasefire_seconds() - sim->game_time();
			String line;
			if (left > 0.0f && left <= 10.0f) line = rtso::sprintf0(rtso::tr0("You can attack in %(time)s"), {{"time", time_to_string((int64_t)(left * 1000.0f))}});
			else if (left <= 0.0f && left > -5.0f) line = rtso::tr0("You can attack now!");
			if (!line.is_empty()) txt += (txt.is_empty() ? "" : "\n") + line;
		}
		if (!txt.is_empty()) txt = "[center][b]" + txt + "[/b][/center]";
		if (time_notification_label_->get_text() != txt) time_notification_label_->set_text(txt);
	}
	if (sim && chat_window_.is_ready()) {
		for (const auto &line : sim->pending_petra_chat()) {
			if (!sim->petra_chat_reaches(line, local_player())) continue; // addressed to someone else
			// messages.js: the line translated, its player names in their colours.
			const std::string text = rtso::petra_chat_text(line, [&](uint8_t pid) {
				return std::string(colored_player_name(pid).utf8().get_data());
			}, [](const char *msgid) {
				return std::string(rtso::tr0(String::utf8(msgid)).replace("[", "[lb]").utf8().get_data());
			});
			// messages.js aichat: a "message" from the player, formatted as a player's chat
			// (ChatMessageFormatPlayer): "(Ally) <name> text" for /allies, "(Private)" for /msg.
			const bool allies = line.to == rtso::PETRA_CHAT_ALLIES, priv = !allies && line.to < rtso::MAX_PLAYERS;
			const String ctx = allies ? "(" + tr("Ally") + ") " : priv ? "(" + tr("Private") + ") " : String();
			chat_window_.append_kind(ctx + String("<") + colored_player_name(line.from) + String("> ") + String::utf8(text.c_str()) + "\n",
					RtsoChatWindow::message_bits(allies ? "Ally" : priv ? "Private" : "", true));
		}
		if (!sim->pending_petra_chat().empty()) sim->clear_petra_chat();
	}
	prune_selection();
	prune_structure_selection();
	if (!selected_ids.is_empty() || selected_building_id != 0) inspector_.clear();
	update_inspector();
	update_command_panel();
	follow_player_tick();
	time_warp_tick();
	{
		// OverlayCounterManager resize handlers: the research column moves down by the data
		// counter's height and the network warnings sit left of it (displayGamestateNotifications).
		float counter_h = 0.0f;
		if (Node *us = get_node_or_null(NodePath("/root/UserSettings"))) counter_h = (float)(double)us->call("overlay_height");
		research_progress_.set_top(58.0f + counter_h);
		if (net_warnings_label_) net_warnings_label_->set_offset(SIDE_RIGHT, counter_h > 0.0f ? -110.0f : -15.0f);
	}
	const auto attacks = sim->take_attacked_events();
	detect_attacks(attacks);
	show_session_notices(sim->take_session_notices());
	show_trigger_dialogs(sim->take_trigger_dialogs());
	update_cursor();
	update_highlight();
	// input.js INPUT_BATCHTRAINING: releasing session.batchtrain posts the batch.
	if (batch_training_.active && !rtso::hotkeys::held("session.batchtrain")) flush_training_batch();
	chat_window_.tick_overlay();
	if (objectives_dialog_.is_open()) objectives_dialog_.set_player_state(match_settings_player_state());
	tutorial_panel_.refresh(sim);
	panel_entities_.refresh(*sim, view_player(), (float)Time::get_singleton()->get_ticks_msec() / 1000.0f);
	research_progress_.refresh(*sim, view_player());
	refresh_dev_overlay();
	control_groups_.refresh(*sim, local_player(), selected_groups);
	diplo_dialog_.poll_spies();
	if (diplo_dialog_.is_open()) diplo_dialog_.refresh();

	// Sound events and the music state (0 A.D. music.js: battle while
	// BattleDetection says so, victory and defeat playlists at the end).
	if (cam_rig) {
		const Vector3 focus = cam_rig->get_target_pos();
		session_audio_.set_listener(focus.x, focus.z);
	}
	session_audio_.tick(sim, local_player(), match_elapsed_time, true);
	session_audio_.tick_combat(sim, local_player(), attacks, sim->take_fauna_strikes(), match_elapsed_time);
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
			const rtso::WallSetInfo &wall_set = rtso::wallset_info(wall_kind, sim ? sim->player_civ(local_player()) : (uint8_t)rtso::CIV_COUNT);
			if (sim)
				pieces = sim->wall_placement(local_player(), wall_drag_start_pos.x, wall_drag_start_pos.z,
						drag_curr_pos.x, drag_curr_pos.z, &start_tower, &end_tower, wall_kind);
			// Walls.js returns no segment for a wall no longer than a tower, but the preview still
			// shows its end towers (GuiInterface.js SetWallPlacementPreview); a click then lays nothing.
			if (sim && pieces.empty()) {
				const uint8_t civ = sim->player_civ(local_player());
				if (!start_tower)
					pieces.push_back({wall_set.tower, wall_drag_start_pos.x, wall_drag_start_pos.z, 0.0f, wall_set.tower_half, civ});
				if (!end_tower && (drag_curr_pos - wall_drag_start_pos).length() > 0.1f)
					pieces.push_back({wall_set.tower, drag_curr_pos.x, drag_curr_pos.z, 0.0f, wall_set.tower_half, civ});
			}
			while (wall_ghosts.size() < pieces.size()) {
				MeshInstance3D *g = memnew(MeshInstance3D);
				g->set_visible(false);
				add_child(g);
				wall_ghosts.push_back(g);
			}

			uint8_t pciv = sim ? sim->player_civ(local_player()) : (uint8_t)rtso::CIV_ATHENIANS;
			// Pieces of this wall don't block each other; the towers it snaps onto don't block it.
			const uint32_t snapped[2] = {start_tower, end_tower};
			// GuiInterface.js SetWallPlacementPreview: from the first piece that can't go down (or
			// can't be paid for, as cmd_build_wall stops there too) on, every piece is invalid.
			// The tooltip's cost is that of the pieces up to the first one that can't stand there
			// (allPiecesValid); being short of resources only tints them.
			bool chain_ok = true, place_ok = true;
			float spent[rtso::RES_COUNT] = {};
			float wall_cost[rtso::RES_COUNT] = {};
			for (size_t i = 0; i < pieces.size(); ++i) {
				const auto &p = pieces[i];
				MeshInstance3D *g = wall_ghosts[i];
				if (sim && place_ok) {
					place_ok = sim->check_wall_piece(local_player(), p, snapped, 2) == rtso::Simulation::PLACE_OK;
					float c[rtso::RES_COUNT];
					sim->wall_piece_cost(local_player(), p, c);
					if (place_ok)
						for (int r = 0; r < rtso::RES_COUNT; ++r) wall_cost[r] += c[r];
				}
				// 0 A.D. previews each piece with its own actor (wall_long/medium/short, the
				// tower): the model the world view will draw for it, turned the same way.
				const auto entry = rtso::ModelLibrary::get("buildings", RtsoWorldView::building_visual_id(pciv, p.type, p.half_w));
				const float h = sim ? sim->get_terrain_height(p.x, p.z) : 0.0f;
				if (entry.valid && entry.mesh.is_valid()) {
					const float sc = entry.scale > 0.05f ? entry.scale : 1.0f;
					g->set_mesh(entry.mesh);
					g->set_scale(Vector3(sc, sc, sc));
					g->set_global_position(Vector3(p.x, h + entry.y_offset, p.z));
				} else {
					const bool is_tower = p.type == wall_set.tower && p.half_w == wall_set.tower_half;
					Ref<BoxMesh> box;
					box.instantiate();
					// Along the box's x: the piece's length (its u axis); across it: its 0 A.D. depth.
					box->set_size(is_tower ? Vector3(2.2f * p.half_w, 4.5f, 2.2f * p.half_w)
							: Vector3(2.0f * p.half_w, 2.5f, 2.0f * wall_set.half_depth));
					g->set_mesh(box);
					g->set_scale(Vector3(1.0f, 1.0f, 1.0f));
					g->set_global_position(Vector3(p.x, h + 0.05f, p.z));
				}
				g->set_rotation(Vector3(0.0f, p.rot - rtso::Simulation::DEFAULT_BUILDING_ROT, 0.0f));
				if (chain_ok && sim) {
					float c[rtso::RES_COUNT];
					sim->wall_piece_cost(local_player(), p, c);
					const auto &ps = sim->player(local_player());
					for (int r = 0; r < rtso::RES_COUNT; ++r) {
						spent[r] += c[r];
						chain_ok = chain_ok && spent[r] <= ps.res[r] + 1e-3f;
					}
					chain_ok = chain_ok && place_ok;
				}
				const bool ok = sim && chain_ok;
				g->set_material_override(ok ? ghost_mat_ok : ghost_mat_bad);
				g->set_visible(true);
			}
			for (size_t i = pieces.size(); i < wall_ghosts.size(); ++i) {
				if (wall_ghosts[i] && wall_ghosts[i]->is_visible()) {
					wall_ghosts[i]->set_visible(false);
				}
			}
			// INPUT_BUILDING_WALL_PATHING: the wall's cost and what the stock lacks for it;
			// after a refused click "Cannot build wall here!" until the cursor moves.
			if (!wall_fail_tip_.is_empty() && mouse_pos.distance_to(wall_fail_mouse_) > 2.0f) wall_fail_tip_ = String();
			if (!wall_fail_tip_.is_empty()) {
				set_placement_tip(wall_fail_tip_, true);
			} else if (sim && !pieces.empty()) {
				rtso::CostTip cost;
				cost.food = (int64_t)std::lround(wall_cost[rtso::RES_FOOD]);
				cost.wood = (int64_t)std::lround(wall_cost[rtso::RES_WOOD]);
				cost.stone = (int64_t)std::lround(wall_cost[rtso::RES_STONE]);
				cost.metal = (int64_t)std::lround(wall_cost[rtso::RES_GOLD]);
				const auto &ps = sim->player(local_player());
				String tip = rtso::cost_tooltip(cost);
				const String need = rtso::needed_resources_tooltip(cost, ps.res[rtso::RES_FOOD], ps.res[rtso::RES_WOOD],
						ps.res[rtso::RES_STONE], ps.res[rtso::RES_GOLD]);
				if (!need.is_empty()) tip += (tip.is_empty() ? String() : String("\n")) + need;
				set_placement_tip(tip, !chain_ok);
			} else {
				set_placement_tip(String(), false);
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
				snap_placement_to_edges(current_build_type, cell_x, cell_y, !build_click_pending);
				int footprint = rtso::building_type_info(current_build_type).footprint;

				float gx = (cell_x + footprint * 0.5f) * rtso::Simulation::GRID;
				float gz = (cell_y + footprint * 0.5f) * rtso::Simulation::GRID;
				ghost_mesh->set_global_position(Vector3(gx, sim->get_terrain_height(gx, gz) + 0.05f, gz));
				const float ghost_angle = placement_angle_at(current_build_type, gx, gz);
				ghost_mesh->set_rotation(Vector3(0.0f, ghost_angle - rtso::Simulation::DEFAULT_BUILDING_ROT, 0.0f));

				const auto placed = sim->check_placement(local_player(), cell_x, cell_y, current_build_type, ghost_angle);
				const bool can_build = placed == rtso::Simulation::PLACE_OK;
				ghost_mesh->set_material_override(can_build ? ghost_mat_ok : ghost_mat_bad);
				String range_tip;
				if (can_build) {
					// input.js updateBuildingPlacementPreview: a structure with a Ranged attack shows
					// its basic range and the average bonus the terrain gives it there.
					const float range = sim->structure_arrow_range(local_player(), (uint8_t)current_build_type);
					if (range > 0.0f) {
						constexpr float kM = rtso::AttackDetection::kMetresPerUnit;
						const float origin = rtso::civ_building_ai_info(sim->team_civ(local_player()), (uint8_t)current_build_type).origin_y;
						const int64_t basic = (int64_t)std::lround(range * kM);
						const int64_t bonus = (int64_t)std::lround((sim->average_elevation_range(gx, gz, range, origin) - range) * kM);
						range_tip = rtso::sprintf0(rtso::tr0(basic == 1 ? "Basic range: %(range)s meter" : "Basic range: %(range)s meters"),
											{{"range", String::num_int64(basic)}}) + "\n" +
								rtso::sprintf0(rtso::tr0(bonus == 1 || bonus == -1 ? "Average bonus range: %(range)s meter" : "Average bonus range: %(range)s meters"),
										{{"range", String::num_int64(bonus)}});
					}
				}
				set_placement_tip(can_build ? range_tip : placement_failure_text(placed, (uint8_t)current_build_type, gx, gz), !can_build);
			}
		}
	} else {
		set_placement_tip(String(), false);
		wall_fail_tip_ = String();
		if (ghost_mesh && ghost_mesh->is_visible()) ghost_mesh->set_visible(false);
		for (auto *wg : wall_ghosts) {
			if (wg && wg->is_visible()) wg->set_visible(false);
		}
	}

	// GuiInterface.DisplayRallyPoint: the rally points of the whole selection.
	rally_points_.update(sim, local_player(), sim->cinema_playing() || spectating() ? std::vector<uint32_t>() : selected_structures(),
			player_flare_color(local_player()));
	range_overlays_.tick((float)delta);
	{
		std::vector<uint32_t> ranged = selected_structures();
		for (int i = 0; i < selected_ids.size(); ++i) ranged.push_back((uint32_t)selected_ids[i]);
		for (int i = 0; i < highlight_ids_.size(); ++i) ranged.push_back((uint32_t)highlight_ids_[i]);
		range_overlays_.update(sim, spectating() ? (uint8_t)0xFF : local_player(), ranged); // observers: everyone's
	}
	{
		using O = rtso::RtsoDebugOverlays;
		std::vector<uint32_t> units;
		if (debug_overlays_.on(O::UNIT_MOTION))
			for (int i = 0; i < selected_ids.size(); ++i) units.push_back((uint32_t)selected_ids[i]);
		debug_overlays_.set_motion_units(units);
		// Selectable's debug boxes show while its highlight does: selected or hovered.
		auto &boxes = debug_overlays_.boxes();
		boxes.clear();
		if (debug_overlays_.on(O::BOUNDING_BOX) && view) {
			std::vector<uint32_t> ids = selected_structures();
			for (int i = 0; i < selected_ids.size(); ++i) ids.push_back((uint32_t)selected_ids[i]);
			for (int i = 0; i < highlight_ids_.size(); ++i) ids.push_back((uint32_t)highlight_ids_[i]);
			std::vector<RtsoWorldView::ModelBoxes> mb;
			view->model_boxes(ids, mb);
			for (const auto &b : mb) boxes.push_back({ b.world, b.xf, b.local });
		}
	}
	debug_overlays_.update(sim, view);

	if ((sim->is_victory() || sim->is_game_over()) && !summary_shown_) {
		summary_shown_ = true;
		write_replay(); // the finished match, with its final statistics
	}
	tick_quit_confirmation();

	update_observer_view_picker();
	// session.js handlePlayerState: the end of the match is the playerstate chat line and
	// the "You have won!" / "You have been defeated!" question (quit_confirmation.gd);
	// nothing is drawn across the screen.
	if (sim->is_victory_for(seat_player()) && !campaign_win_noted_ && !turn_mgr_.is_replaying()) {
		// CampaignSession.onFinish: a campaign level is completed as soon as it is won.
		campaign_win_noted_ = true;
		Ref<Script> summary = ResourceLoader::get_singleton()->load("res://scenes/session/summary_screen.gd");
		if (summary.is_valid()) summary->call("mark_campaign_won");
	}
	// LobbyRatingReporter.onPlayersFinished: the report goes out the moment this seat has won
	// or lost, not on LobbySession's next poll, so leaving at once still rates the game.
	if (!lobby_report_noted_ && !turn_mgr_.is_replaying() &&
			(sim->is_victory_for(seat_player()) || sim->is_defeated(seat_player()))) {
		lobby_report_noted_ = true;
		if (Node *lobby = get_tree() ? get_tree()->get_root()->get_node_or_null(NodePath("LobbySession")) : nullptr)
			lobby->call("_check_report");
	}
}

// 0.28 NetworkDelayOverlay.CheckDelay, once a second: the turns this client
// has in hand but hasn't run (Engine.GetPendingTurns); from MAX_LIVE_DELAY (6)
// on it shows how far behind the live game that is, at 5 turns a second, to
// one decimal under 5 s. Observer catch-up itself is the turn manager's
// (network.autocatchup).
void RtsoMain::update_net_delay_overlay(double delta) {
	if (!net_delay_label_) return;
	net_delay_check_s_ += delta;
	if (net_delay_check_s_ < 1.0) return;
	net_delay_check_s_ = 0.0;
	constexpr uint32_t kMaxLiveDelay = 6, kTurnsPerSecond = 5;
	const uint32_t delay = debug_net_delay_turns_ >= 0 ? (uint32_t)debug_net_delay_turns_
		: net_lockstep() ? turn_mgr_.turns_behind() : 0;
	if (delay < kMaxLiveDelay) {
		net_delay_label_->set_visible(false);
		return;
	}
	const double s = (double)delay / kTurnsPerSecond;
	const String secs = s < 5.0 ? String::num(s, 1).pad_decimals(1) : String::num_int64((int64_t)std::llround(s));
	net_delay_label_->set_text(rtso::sprintf0(tr("Delay to live stream: %(delay)ss"), {{"delay", secs}}));
	net_delay_label_->set_visible(true);
}

void RtsoMain::sound_notification_nick() {
	if (!rtso::user_config::get_bool("sound.notify.nick")) return;
	const uint64_t now = Time::get_singleton()->get_ticks_msec();
	if (nick_notify_ms_ == 0 || now > nick_notify_ms_ + 3000) play_alarm("res://assets/audio/interface/ui/chat_alert.ogg");
	nick_notify_ms_ = now; // lastInteractionTime moves on even when the chime is held back
}

bool RtsoMain::chat_open_page() {
	// Chat.openPage: nothing to say to a game that is gone (g_Disconnected).
	if (net_disconnected_) return false;
	close_open_dialogs();
	refresh_chat_tooltips();
	chat_window_.open_page();
	return true;
}

void RtsoMain::_on_chat_filter_selected(int index) { chat_window_.set_filter(index); }

void RtsoMain::_on_chat_history_toggled(bool on) {
	chat_window_.set_extended(on);
	rtso::user_config::set("chat.session.extended", on ? "true" : "false");
	rtso::user_config::save();
}

// ChatInput.getOpenHotkeyTooltip: the chat, teamchat (observer chat for an observer) and
// privatechat hotkeys; an unbound one reads "Unassigned hotkey: <name>" (colorizeHotkey).
String RtsoMain::chat_open_hotkey_tooltip() const {
	auto line = [](const char *text, const char *hotkey) {
		return String("\n") + RtsoHotkeyConfig::colorize_hotkey(rtso::tr0(text), hotkey);
	};
	return (line("Press %(hotkey)s to open the public chat.", "chat") +
			line(spectating() ? "Press %(hotkey)s to open the observer chat." : "Press %(hotkey)s to open the ally chat.", "teamchat") +
			line("Press %(hotkey)s to open the previously selected private chat.", "privatechat"))
			.strip_edges();
}

// Menu.js rebuild: "Press %(hotkey)s to toggle this menu." with colorizeHotkey.
// The IconButtons' and GameSpeedButton's tooltips too: colorizeHotkey("%(hotkey)s ",
// hotkey) + translate(Tooltip), rebuilt on hover for a rebind (onHotkeyChange).
void RtsoMain::_refresh_menu_tooltip() {
	if (menu_btn)
		menu_btn->set_tooltip_text(RtsoHotkeyConfig::colorize_hotkey(rtso::tr0("Press %(hotkey)s to toggle this menu."),
				"session.gui.menu.toggle"));
	if (speed_btn) speed_btn->set_tooltip_text(rtso::tr0("Game Speed"));
	auto tip = [](const char *hotkey, const char *text) {
		return RtsoHotkeyConfig::colorize_hotkey("%(hotkey)s ", hotkey) + String(rtso::tr0(text));
	};
	top_panel_.set_dialog_tooltips(tip("session.gui.diplomacy.toggle", "Diplomacy"),
			tip("session.gui.barter.toggle", "Barter & Trade"),
			tip(rtso::hotkeys::table().get("session.gui.matchsettings.toggle") ? "session.gui.matchsettings.toggle" : "session.gui.objectives.toggle",
					"Match Settings"));
}

void RtsoMain::refresh_chat_tooltips() {
	chat_window_.set_input_tooltip(String(rtso::tr0("Type the message to send.")) + "\n" +
			rtso::sprintf0(rtso::tr0("Press %(hotkey)s to autocomplete player names."), {{"hotkey", rtso::hotkey_tag(rtso::tr0("Tab"))}}) + "\n" +
			chat_open_hotkey_tooltip());
}

// gui/common/styles.xml sound_pressed, and the dropdowns' sound_opened / sound_selected.
void RtsoMain::hook_ui_click_sounds(Node *n) {
	if (BaseButton *b = Object::cast_to<BaseButton>(n)) {
		const Callable click(this, "_on_ui_click");
		if (!b->is_connected("pressed", click)) b->connect("pressed", click);
		if (OptionButton *o = Object::cast_to<OptionButton>(n)) {
			const Callable picked = Callable(this, "_on_ui_click_item");
			if (!o->is_connected("item_selected", picked)) o->connect("item_selected", picked);
		}
	}
	for (int i = 0; i < n->get_child_count(); ++i) hook_ui_click_sounds(n->get_child(i));
}

void RtsoMain::_on_tree_node_added(Node *n) {
	if (n && Object::cast_to<BaseButton>(n) && is_ancestor_of(n)) hook_ui_click_sounds(n);
}

void RtsoMain::_on_ui_click() {
	++ui_clicks_;
	if (click_player_ && click_player_->get_stream().is_valid()) click_player_->play();
}
