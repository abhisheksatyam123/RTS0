#ifndef RTSO_TOP_PANEL_H
#define RTSO_TOP_PANEL_H

// P4 stage-2 leaf widget: Top HUD Bar (0 A.D. gui/session/top_panel parity).
// Owns civ & phase emblem badges; CounterResource counters in 0 A.D. order (food,
// wood, stone, metal) with the stock abbreviated like abbreviateLargeNumbers and
// the current gatherers beside it; CounterPopulation's "count/limit (max)" with
// the total gatherers; the idle worker button; the IconButtons (Diplomacy,
// Barter & Trade, Match Settings); the match clock and the wave / wonder banner.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>

#include "../simulation/simulation.h"
#include "l10n.h"
#include "user_config.h"

#ifndef RTSO_UI_LOAD_TEXTURE
#define RTSO_UI_LOAD_TEXTURE
namespace rtso {
inline godot::Ref<godot::Texture2D> load_ui_texture(const godot::String &path) {
	if (godot::ResourceLoader::get_singleton()->exists(path)) {
		godot::Ref<godot::Texture2D> tex = godot::ResourceLoader::get_singleton()->load(path);
		if (tex.is_valid()) return tex;
	}
	return godot::Ref<godot::Texture2D>();
}
} // namespace rtso
#endif

class RtsoTopPanel {
public:
	// owner: signal target object (RtsoMain, for _on_idle_worker_clicked).
	// top_hbox: parent HBoxContainer inside top_hud_panel.
	// initial_civ: current player civ ID.
	void build(godot::Object *owner, godot::HBoxContainer *top_hbox, uint8_t initial_civ) {
		using namespace godot;

		// 1. Civ Emblem Badge
		civ_emblem_ = memnew(TextureRect);
		civ_emblem_->set_custom_minimum_size(Vector2(38, 38));
		civ_emblem_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		civ_emblem_->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		civ_emblem_->set_texture(rtso::load_ui_texture(rtso::civ_info(initial_civ).emblem_path));
		// CivIcon's civIconOverlay: a click reopens the last reference page viewed.
		civ_emblem_->set_name("CivIcon");
		civ_emblem_->set_mouse_filter(Control::MOUSE_FILTER_STOP);
		civ_emblem_->connect("gui_input", Callable(owner, "_on_civ_icon_gui_input"));
		top_hbox->add_child(civ_emblem_);

		// 2. Phase Emblem Badge (Village, Town, City)
		phase_emblem_ = memnew(TextureRect);
		phase_emblem_->set_custom_minimum_size(Vector2(38, 38));
		phase_emblem_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		phase_emblem_->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		phase_emblem_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/phases/village.png"));
		top_hbox->add_child(phase_emblem_);

		// 3. Resource counters (CounterResource.js), 0 A.D. order: food, wood, stone, metal.
		for (int i = 0; i < 4; ++i) {
			HBoxContainer *rc = memnew(HBoxContainer);
			rc->add_theme_constant_override("separation", 4);
			rc->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			res_panels_[i] = rc;
			rc->connect("gui_input", Callable(owner, "_on_top_counter_gui_input"));

			res_icons_[i] = memnew(TextureRect);
			res_icons_[i]->set_custom_minimum_size(Vector2(26, 26));
			res_icons_[i]->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			res_icons_[i]->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
			res_icons_[i]->set_texture(rtso::load_ui_texture(String("res://assets/textures/ui/icons/") + kResIcon[i] + ".png"));
			res_icons_[i]->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			rc->add_child(res_icons_[i]);

			res_labels_[i] = memnew(Label);
			res_labels_[i]->set_custom_minimum_size(Vector2(48, 26));
			res_labels_[i]->add_theme_font_size_override("font_size", 14);
			res_labels_[i]->add_theme_color_override("font_color", Color(0.95f, 0.95f, 0.98f));
			res_labels_[i]->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			rc->add_child(res_labels_[i]);

			res_stats_[i] = memnew(Label);
			res_stats_[i]->set_custom_minimum_size(Vector2(24, 26));
			res_stats_[i]->add_theme_font_size_override("font_size", 11);
			res_stats_[i]->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			rc->add_child(res_stats_[i]);

			top_hbox->add_child(rc);
		}

		// 4. Population counter (CounterPopulation.js)
		HBoxContainer *pop_box = memnew(HBoxContainer);
		pop_box->add_theme_constant_override("separation", 4);
		pop_box->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		pop_box->set_tooltip_text("Population: current/limit (max)");
		pop_box->connect("gui_input", Callable(owner, "_on_top_counter_gui_input"));
		pop_panel_ = pop_box;
		pop_icon_ = memnew(TextureRect);
		pop_icon_->set_custom_minimum_size(Vector2(26, 26));
		pop_icon_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		pop_icon_->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		pop_icon_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/icons/population.png"));
		pop_icon_->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		pop_box->add_child(pop_icon_);

		pop_label_ = memnew(Label);
		pop_label_->set_custom_minimum_size(Vector2(86, 26));
		pop_label_->add_theme_font_size_override("font_size", 14);
		pop_label_->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		pop_box->add_child(pop_label_);

		pop_stats_ = memnew(Label);
		pop_stats_->set_name("PopGatherers");
		pop_stats_->set_custom_minimum_size(Vector2(24, 26));
		pop_stats_->add_theme_font_size_override("font_size", 11);
		pop_stats_->set_mouse_filter(Control::MOUSE_FILTER_PASS);
		pop_box->add_child(pop_stats_);
		top_hbox->add_child(pop_box);

		// 5. (The idle worker button is on the minimap, as 0.28's MiniMapIdleWorkerButton.)

		// 6. IconButtons: DiplomacyDialogButton, TradeDialogButton, MatchSettingsDialogButton.
		// Tooltips lead with the hotkey like colorizeHotkey; owner->_toggle_session_dialog opens them.
		const char *dialog_names[3] = {"DiplomacyDialog", "BarterDialog", "Objectives"};
		const char *dialog_icons[3] = {"res://assets/textures/ui/archive/icons/diplomacy.png",
				"res://assets/textures/ui/archive/icons/economics.png", "res://assets/textures/ui/archive/icons/match-settings.png"};
		const char *dialog_text[3] = {"Diplo", "Trade", "Obj"};
		for (int i = 0; i < 3; ++i) {
			Button *b = memnew(Button);
			b->set_custom_minimum_size(Vector2(32, 30));
			b->add_theme_font_size_override("font_size", 11);
			Ref<Texture2D> tex = dialog_icons[i][0] ? rtso::load_ui_texture(dialog_icons[i]) : Ref<Texture2D>();
			if (tex.is_valid()) {
				b->set_button_icon(tex);
				b->set_expand_icon(true);
			} else {
				b->set_text(dialog_text[i]);
			}
			b->connect("pressed", Callable(owner, "_toggle_session_dialog").bind(String(dialog_names[i])));
			dialog_btns_[i] = b;
			top_hbox->add_child(b);
		}

		// 7. BuildLabel: "Release XXVIII: Boiorix" over getBuildString, in the space between
		// the counters and the right-hand buttons (0.28 has no clock or phase here: the
		// elapsed time and ceasefire are the optional overlay counters). RTS0's wave mode
		// shows its wave timer in the same place.
		clock_label_ = memnew(Label);
		clock_label_->set_visible(false);
		top_hbox->add_child(clock_label_);
		wave_label_ = memnew(Label);
		wave_label_->set_name("BuildLabel");
		wave_label_->set_custom_minimum_size(Vector2(100, 26));
		wave_label_->set_clip_text(true);
		wave_label_->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		wave_label_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		wave_label_->set_vertical_alignment(VERTICAL_ALIGNMENT_TOP);
		wave_label_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		wave_label_->add_theme_font_size_override("font_size", 12);
		wave_label_->add_theme_constant_override("line_spacing", -2);
		wave_label_->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		wave_label_->add_theme_constant_override("outline_size", 3);
		top_hbox->add_child(wave_label_);
	}

	bool is_ready() const { return res_labels_[0] != nullptr; }

	void set_civ(uint8_t civ_id) {
		if (civ_emblem_) {
			const auto &info = rtso::civ_info(civ_id);
			civ_emblem_->set_texture(rtso::load_ui_texture(info.emblem_path));
		}
	}

	// `me` is the local player whose economy the panel shows.
	// observer: g_IsObserver (an observer, a replay viewer, or a player who won or lost),
	// whose tooltips list every player even while following one.
	// The next update() refreshes everything, not only the clock (a click changed the order).
	void invalidate() { last_refresh_ms_ = 0; }

	void update(rtso::Simulation *sim, uint8_t me, double match_elapsed_time, bool observer = false) {
		observer_ = observer;
		using namespace godot;
		if (!sim || !res_labels_[0]) return;

		// CounterManager.rebuild and CivIcon: with no player viewed (an observer,
		// g_ViewedPlayer <= 0) the resource and population counters and the civ
		// icon are hidden.
		const bool hidden = me >= rtso::MAX_PLAYERS;
		if (civ_emblem_) civ_emblem_->set_visible(!hidden);
		if (phase_emblem_) phase_emblem_->set_visible(!hidden);
		for (int i = 0; i < 4; ++i)
			if (res_panels_[i]) res_panels_[i]->set_visible(!hidden);
		if (pop_panel_) pop_panel_->set_visible(!hidden);

		// CounterPopulation.onTick: orange for half of each second while a batch waits for
		// population room, white otherwise; the rest refreshes once a turn (200 ms), as
		// 0.28's counters do on each simulation update, not every frame.
		const uint64_t now_ms = godot::Time::get_singleton()->get_ticks_msec();
		if (!hidden && pop_label_) {
			const bool flash = pop_blocked_ && now_ms % 1000 < 500;
			set_font_color(pop_label_, flash ? Color(1.0f, 165 / 255.0f, 0.0f) : Color(1, 1, 1));
		}
		if (me == last_me_ && hidden == last_hidden_ && now_ms - last_refresh_ms_ < kRefreshMs && last_refresh_ms_) {
			update_clock(sim, me, match_elapsed_time);
			return;
		}
		last_me_ = me;
		last_hidden_ = hidden;
		last_refresh_ms_ = now_ms;

		if (!hidden) {
			// Dynamic Civ Crest & Phase Badge Emblems
			const auto &cinfo = rtso::civ_info(sim->player_civ(me));
			if (civ_emblem_) {
				civ_emblem_->set_texture(rtso::load_ui_texture(cinfo.emblem_path));
				if (tooltip_civ_ != (int)sim->player_civ(me)) {
					tooltip_civ_ = (int)sim->player_civ(me);
					civ_emblem_->set_tooltip_text(civ_icon_tooltip(cinfo.name));
				}
			}
			if (phase_emblem_) {
				uint8_t pphase = sim->player_phase(me);
				if (pphase == 1) phase_emblem_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/phases/village.png"));
				else if (pphase == 2) phase_emblem_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/phases/town.png"));
				else phase_emblem_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/phases/city.png"));
			}

			// CounterResource / CounterPopulation. Gatherers are units in UnitAI's GATHER
			// state (Player.AddResourceGatherer on enter, RemoveResourceGatherer on leave):
			// walking to the resource, gathering and taking it back, counted under the
			// type of the resource they were sent to.
			int gatherers[4] = {0, 0, 0, 0};
			auto uv = sim->units();
			std::unordered_map<uint32_t, uint8_t> target_type;
			for (size_t i = 0; i < uv.count; ++i)
				if (uv.alive[i] && uv.team[i] == me && uv.target_resource_id[i] &&
						(uv.action_state[i] == rtso::ACTION_GATHER || uv.action_state[i] == rtso::ACTION_RETURN_DROPSITE))
					target_type.emplace(uv.target_resource_id[i], rtso::RES_COUNT);
			if (!target_type.empty()) {
				const auto rv = sim->resources();
				for (size_t k = 0; k < rv.count; ++k) {
					auto it = target_type.find(rv.id[k]);
					if (it != target_type.end()) it->second = rv.type[k];
				}
			}
			for (size_t i = 0; i < uv.count; ++i) {
				if (!uv.alive[i] || uv.team[i] != me) continue;
				const uint8_t st = uv.action_state[i];
				if (st != rtso::ACTION_GATHER && st != rtso::ACTION_RETURN_DROPSITE) continue;
				auto it = uv.target_resource_id[i] ? target_type.find(uv.target_resource_id[i]) : target_type.end();
				const uint8_t type = it != target_type.end() && it->second < rtso::RES_COUNT ? it->second : uv.res_type_carried[i];
				if (type < rtso::RES_COUNT && (st == rtso::ACTION_GATHER || it != target_type.end())) ++gatherers[type];
			}
			const auto &ps = sim->player(me);
			int total = 0;
			for (int i = 0; i < 4; ++i) {
				const int r = kResType[i];
				res_labels_[i]->set_text(abbreviate((int64_t)std::floor(ps.res[r])));
				set_gatherers(res_stats_[i], gatherers[r]);
				total += gatherers[r];
				// CounterResource.rebuild: the name, then the allies' stocks.
				res_panels_[i]->set_tooltip_text(rtso::tr0(kResTitle[i]) + ally_stats(sim, me, [sim, r](uint8_t p) {
					const int64_t v = (int64_t)std::llround(sim->player(p).res[r]);
					return std::make_pair(godot::String::num_int64(v), v);
				}));
			}

			pop_label_->set_text(pop_caption(sim, me));
			pop_blocked_ = sim->training_blocked(me);
			set_gatherers(pop_stats_, total);
			pop_panel_->set_tooltip_text(rtso::tr0("Population: current/limit (max)") + ally_stats(sim, me, [this, sim](uint8_t p) {
				return std::make_pair(pop_caption(sim, p), (int64_t)sim->pop_current(p));
			}));
		}

		update_clock(sim, me, match_elapsed_time);
	}

	void update_clock(rtso::Simulation *sim, uint8_t me, double match_elapsed_time) {
		using namespace godot;
		// Clock & Wave
		int mins = (int)match_elapsed_time / 60;
		int secs = (int)match_elapsed_time % 60;
		String min_str = (mins < 10 ? "0" : "") + String::num_int64(mins);
		String sec_str = (secs < 10 ? "0" : "") + String::num_int64(secs);
		clock_label_->set_text(min_str + ":" + sec_str);

		(void)me;
		String text;
		if (sim->waves_enabled()) {
			text = "Wave " + String::num_int64(sim->wave_number()) + " (" + String::num((double)sim->wave_time_remaining(), 0) + "s)";
		} else {
			// getBuildString: "Build: %(buildDate)s (%(version)s)", Engine.GetBuildDate being
			// the compile date.
			text = String(rtso::tr0("Release XXVIII: Boiorix")) + "\n" +
					rtso::sprintf0(rtso::tr0("Build: %(buildDate)s (%(version)s)"), {{"buildDate", String(__DATE__)}, {"version", String("0.1")}});
		}
		if (wave_label_->get_text() != text) wave_label_->set_text(text);
	}

	// The player name before each ally stat (colorizePlayernameHelper "■" and the
	// name), set by the owner.
	void set_ally_name(std::function<godot::String(uint8_t)> f) { ally_name_ = std::move(f); }

	// CounterManager.onPress: a click on a counter cycles the allies' order
	// (gui.session.respoptooltipsort: 0 unordered, 1 descending, -1 ascending).
	static void cycle_order() {
		const int order = sort_order();
		rtso::user_config::set("gui.session.respoptooltipsort", std::to_string((order + 2) % 3 - 1));
		rtso::user_config::save();
	}

	// The IconButtons' tooltips, built by the owner (colorizeHotkey + translate(Tooltip)).
	void set_dialog_tooltips(const godot::String &diplomacy, const godot::String &trade, const godot::String &objectives) {
		const godot::String tips[3] = {diplomacy, trade, objectives};
		for (int i = 0; i < 3; ++i)
			if (dialog_btns_[i]) dialog_btns_[i]->set_tooltip_text(tips[i]);
	}
	godot::Button *dialog_button(int i) const { return i >= 0 && i < 3 ? dialog_btns_[i] : nullptr; }
	// DiplomacyDialogButton / TradeDialogButton onViewedPlayerChange: hidden while
	// viewing no player (g_ViewedPlayer < 1).
	void set_player_buttons_visible(bool on) {
		for (int i = 0; i < 2; ++i)
			if (dialog_btns_[i] && dialog_btns_[i]->is_visible() != on) dialog_btns_[i]->set_visible(on);
	}

	// CivIcon.rebuild's tooltip keys (colorizeHotkey "civinfo" and "structree").
	void set_civ_icon_keys(const godot::String &civinfo, const godot::String &structree) {
		civ_keys_[0] = civinfo;
		civ_keys_[1] = structree;
		tooltip_civ_ = -1;
	}
	godot::String civ_icon_tooltip(const char *civ_name) const {
		return rtso::sprintf0(rtso::tr0("%(civ)s\n%(hotkey_civinfo)s: View Civilization Overview\n%(hotkey_structree)s: View Structure Tree\nLast opened view will be reopened on click."),
				{{"civ", rtso::tr0(civ_name)}, {"hotkey_civinfo", civ_keys_[0]}, {"hotkey_structree", civ_keys_[1]}});
	}

	// tooltips.js abbreviateLargeNumbers.
	static godot::String abbreviate(int64_t n) {
		using godot::String;
		if (n >= 1000000) return String::num_int64(n / 1000000) + "M";
		if (n >= 100000) return String::num_int64(n / 1000) + "k";
		if (n >= 10000) {
			const int64_t tenths = (n + 50) / 100; // toFixed(1) after /1e3 rounds: 12960 is "13k"
			String s = String::num_int64(tenths / 10);
			if (tenths % 10) s += "." + String::num_int64(tenths % 10);
			return s + "k";
		}
		return String::num_int64(n);
	}

private:
	godot::String civ_keys_[2];
	int tooltip_civ_ = -1;
	bool observer_ = false;
	bool pop_blocked_ = false;
	static int sort_order() {
		const std::string v = rtso::user_config::get("gui.session.respoptooltipsort");
		return v == "1" ? 1 : v == "-1" ? -1 : 0;
	}

	// CounterPopulation.CounterCaption.
	static godot::String pop_caption(rtso::Simulation *sim, uint8_t p) {
		using godot::String;
		return rtso::sprintf0(rtso::tr0("%(popCount)s/%(popLimit)s (%(popMax)s)"), {{"popCount", String::num_int64(sim->pop_current(p))},
				{"popLimit", String::num_int64(sim->pop_max(p))}, {"popMax", String::num_int64(sim->pop_cap(p))}});
	}

	// CounterManager.getAllyStatTooltip: for observers (and defeated players), and
	// for a player who shares its mutual allies' vision (Cartography), each other
	// undefeated player's stat, "%(playername)s: %(statValue)s", in the chosen order.
	godot::String ally_stats(rtso::Simulation *sim, uint8_t me,
			const std::function<std::pair<godot::String, int64_t>(uint8_t)> &stat) const {
		using godot::String;
		const bool observer = observer_ || me >= rtso::MAX_PLAYERS || sim->is_defeated(me);
		struct Row { String text; int64_t order; };
		std::vector<Row> rows;
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
			if (!sim->player(p).active) continue;
			if (p == me || sim->is_defeated(p)) continue;
			if (!observer && !sim->shares_los(me, p)) continue;
			const auto v = stat(p);
			rows.push_back({rtso::sprintf0(rtso::tr0("%(playername)s: %(statValue)s"),
					{{"playername", ally_name_ ? ally_name_(p) : String::num_int64(p + 1)}, {"statValue", v.first}}), v.second});
		}
		if (rows.empty()) return String();
		const int order = sort_order();
		if (order) std::stable_sort(rows.begin(), rows.end(), [order](const Row &a, const Row &b) { return order * (b.order - a.order) < 0; });
		static const char *kOrder[3] = {"Ascending", "Unordered", "Descending"};
		String out = "\n" + rtso::sprintf0(rtso::tr0("%(order)s: %(hotkey)s to change order."),
				{{"order", rtso::tr0(kOrder[order + 1])}, {"hotkey", "[Click]"}});
		for (const Row &r : rows) out += "\n" + r.text;
		return out;
	}

	std::function<godot::String(uint8_t)> ally_name_;
	static constexpr int kResType[4] = {rtso::RES_FOOD, rtso::RES_WOOD, rtso::RES_STONE, rtso::RES_GOLD};
	static constexpr const char *kResIcon[4] = {"food", "wood", "stone", "metal"};
	static constexpr const char *kResTitle[4] = {"Food", "Wood", "Stone", "Metal"};

	// DefaultResourceGatherersColor gold, "200 200 200" at zero.
	static void set_gatherers(godot::Label *l, int n) {
		l->set_text(godot::String::num_int64(n));
		set_font_color(l, n ? godot::Color(1.0f, 0.84f, 0.0f) : godot::Color(200 / 255.0f, 200 / 255.0f, 200 / 255.0f));
	}
	// A theme override notifies the control's whole subtree: only when the colour changes.
	static void set_font_color(godot::Label *l, const godot::Color &c) {
		if (l->has_theme_color_override("font_color") && l->get_theme_color("font_color") == c) return;
		l->add_theme_color_override("font_color", c);
	}
	static constexpr uint64_t kRefreshMs = 200;
	uint64_t last_refresh_ms_ = 0;
	uint8_t last_me_ = 0xFF;
	bool last_hidden_ = false;

	godot::Control *res_panels_[4] = {nullptr};
	godot::Label *res_stats_[4] = {nullptr};
	godot::Control *pop_panel_ = nullptr;
	godot::Label *pop_stats_ = nullptr;
	godot::Button *dialog_btns_[3] = {nullptr};
	godot::TextureRect *civ_emblem_ = nullptr;
	godot::TextureRect *phase_emblem_ = nullptr;
	godot::TextureRect *res_icons_[4] = {nullptr};
	godot::Label *res_labels_[4] = {nullptr};
	godot::TextureRect *pop_icon_ = nullptr;
	godot::Label *pop_label_ = nullptr;
	godot::Label *clock_label_ = nullptr;
	godot::Label *wave_label_ = nullptr;
};

#endif
