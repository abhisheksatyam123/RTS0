#ifndef RTSO_DIPLOMACY_DIALOG_H
#define RTSO_DIPLOMACY_DIALOG_H

// Diplomacy and tribute dialog: 0 A.D. gui/session/diplomacy (DiplomacyDialog.js,
// DiplomacyPlayerText.js, StanceButton.js, TributeButton.js, SpyRequestButton.js,
// DiplomacyDialogCeasefireCounter.js). One row per player of the match, seen from
// the local player. Stance changes ride lockstep cmd 22, tribute cmd 27 and bribes
// cmd 42 through RtsoDiplomacyHost.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/font_variation.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/h_separator.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "../simulation/simulation.h"
#include "chat_window.h"
#include "display_colors.h"
#include "hotkeys.h"
#include "l10n.h"
#include "production_queue.h"
#include "session_skin.h"

using namespace godot;

// Hooks the dialog needs from the RtsoMain coordinator.
class RtsoDiplomacyHost {
public:
	virtual ~RtsoDiplomacyHost() = default;
	virtual bool net_lockstep() const = 0;
	// Orders go through the TurnManager (always in a match, so replays record them).
	virtual bool turn_cmds() const = 0;
	virtual void post_lockstep(uint8_t cmd_type, const uint32_t *ids, size_t n,
			float x, float z, uint32_t tid, uint8_t param) = 0;
	virtual void play_sfx(const char *path) = 0;
	virtual void play_alarm(const char *path) = 0;
	virtual void update_hud() = 0;
	// The player this client plays as (0xFF for an observer).
	virtual uint8_t local_player() const = 0;
	// 0 A.D. controlsPlayer: an observer, a defeated player or a replay viewer who follows
	// a player sees that player's diplomacy, but its buttons stay disabled.
	virtual bool controls_player(uint8_t pid) const { return pid == local_player(); }
	// Whose sight the world shows: a player, or past MAX_PLAYERS for an observer watching
	// the whole map (0 A.D. g_ViewedPlayer < 1).
	virtual uint8_t view_player() const { return local_player(); }
	// translateAISettings: "Medium Balanced Petra Bot" for an AI, "" for a human.
	virtual String ai_settings_text(uint8_t pid) const { (void)pid; return String(); }
	virtual rtso::Simulation *ui_sim() = 0;
	// setCameraFollow: follow a unit (messages.js "spy-response" shows the bribed one).
	virtual void follow_unit(uint32_t id) { (void)id; }
	// 0 A.D. colorizePlayernameByID's name: lobby, Options or AI name.
	virtual String player_display_name(uint8_t pid) const = 0;
	// The session.masstribute hotkey (Shift) is held.
	virtual bool mass_tribute_held() const = 0;
};

class RtsoDiplomacyDialog {
public:
	// TributeButton.js DefaultAmount and MassAmount.
	static constexpr int TRIBUTE_DEFAULT = 100, TRIBUTE_MASS = 500;
	// 0 A.D. GetTributableCodes order: food, wood, stone, metal.
	static constexpr uint8_t TRIBUTE_RES[4] = {rtso::RES_FOOD, rtso::RES_WOOD, rtso::RES_STONE, rtso::RES_GOLD};
	static const char *res_name(uint8_t res) {
		switch (res) {
			case rtso::RES_FOOD: return "food";
			case rtso::RES_WOOD: return "wood";
			case rtso::RES_STONE: return "stone";
			default: return "metal";
		}
	}
	// TributeButton.nextAmount: the next multiple of the mass amount.
	static int next_tribute_amount(int amount) { return TRIBUTE_MASS * (amount / TRIBUTE_MASS + 1); }
	// DiplomacyPlayerText "Theirs": `them`'s stance towards `me`, as 0 A.D.'s isAlly /
	// isNeutral read it. A ceasefire makes every enemy neutral (CeasefireManager.StartCeasefire).
	static const char *stance_text(const rtso::Simulation &sim, uint8_t them, uint8_t me) {
		if (them == me) return "";
		const uint8_t s = effective_stance(sim, them, me);
		return s == rtso::Simulation::DIPLO_ALLY ? "Ally" : s == rtso::Simulation::DIPLO_NEUTRAL ? "Neutral" : "Enemy";
	}
	static uint8_t effective_stance(const rtso::Simulation &sim, uint8_t from, uint8_t to) {
		uint8_t s = sim.get_diplomacy(from, to);
		if (s == rtso::Simulation::DIPLO_ENEMY && sim.ceasefire_active()) s = rtso::Simulation::DIPLO_NEUTRAL;
		return s;
	}
	// DiplomacyPlayerText team caption: 1-based, or "None" for a player alone on its team.
	// RTS0 keeps a team as its lowest member's id, so teams are numbered in member order.
	static String team_text(const rtso::Simulation &sim, uint8_t pid) {
		int number = 0;
		bool shared = false;
		uint8_t seen[rtso::MAX_PLAYERS] = {};
		for (uint8_t q = 0; q < rtso::MAX_PLAYERS; ++q) {
			if (!sim.player(q).active && q != pid) continue;
			const uint8_t t = sim.player_team(q);
			bool has_mate = false;
			for (uint8_t r = 0; r < rtso::MAX_PLAYERS; ++r)
				has_mate |= r != q && (sim.player(r).active || r == pid) && sim.player_team(r) == t;
			if (!has_mate || seen[t & 7]) continue;
			seen[t & 7] = 1;
			++number;
			if (t == sim.player_team(pid)) { shared = true; break; }
		}
		return shared ? String::num_int64(number) : String("None");
	}

	void build(Object *owner, Node *parent, RtsoDiplomacyHost *host, RtsoChatWindow *chat) {
		host_ = host;
		chat_ = chat;
		panel_ = memnew(Panel);
		panel_->set_meta("tooltip_style", "session_bold"); // diplomacy rows: sessionToolTipBold
		// diplomacyDialogPanel "50%-280 50%-155 50%+280 50%+15" on ModernDialog, widened by the
		// tribute buttons and heightened by a 20 px row per player (resize).
		panel_->set_anchor(SIDE_LEFT, 0.5f);
		panel_->set_anchor(SIDE_RIGHT, 0.5f);
		panel_->set_anchor(SIDE_TOP, 0.5f);
		panel_->set_anchor(SIDE_BOTTOM, 0.5f);
		panel_->set_theme(rtso::session_skin());
		panel_->set_theme_type_variation("ModernDialog");
		bold_.instantiate();
		bold_->set_variation_embolden(0.6f);
		resize(2);

		// Header at 32 32 100%-32 64, rows from 64, the ceasefire line at 100%-90 and the
		// buttons at 100%-50.
		VBoxContainer *vbox = memnew(VBoxContainer);
		vbox->set_anchors_preset(Control::PRESET_FULL_RECT);
		vbox->set_offset(SIDE_LEFT, 32.0f);
		vbox->set_offset(SIDE_RIGHT, -32.0f);
		vbox->set_offset(SIDE_TOP, 32.0f);
		vbox->set_offset(SIDE_BOTTOM, -22.0f);
		vbox->add_theme_constant_override("separation", 0);
		panel_->add_child(vbox);

		// TitleText "50%-96 -16 50%+96 16": white sans-bold-stroke-14 over the top edge.
		Label *title = memnew(Label);
		title->set_name("DiplomacyTitle");
		title->set_text(rtso::tr0("Diplomacy"));
		title->set_anchor(SIDE_LEFT, 0.5f);
		title->set_anchor(SIDE_RIGHT, 0.5f);
		title->set_offset(SIDE_LEFT, -96.0f);
		title->set_offset(SIDE_RIGHT, 96.0f);
		title->set_offset(SIDE_TOP, -16.0f);
		title->set_offset(SIDE_BOTTOM, 16.0f);
		title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		title->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		diplomacy_text(title);
		panel_->add_child(title);

		// DiplomacyDialog.xml header: Name, Civilization, Team, Theirs, A, N, E, Tribute.
		HBoxContainer *head = memnew(HBoxContainer);
		head->set_custom_minimum_size(Vector2(0, 32));
		head->add_theme_constant_override("separation", 0);
		const char *heads[] = {"", "Name", "Civilization", "Team", "Theirs", "A", "N", "E", "Tribute", "", ""};
		const float widths[] = {OUTCOME_W, NAME_W, CIV_W, TEAM_W, THEIRS_W, STANCE_W, STANCE_W, STANCE_W, TRIBUTE_W * 4 + 18.0f, ATTACK_W, SPY_W};
		for (int i = 0; i < 11; ++i) {
			Label *l = memnew(Label);
			l->set_text(heads[i]);
			// diplomacyHeaderAlly/Neutral/Enemy: sessionToolTipBold "Ally", "Neutral", "Enemy".
			if (i >= 5 && i <= 7) {
				l->set_tooltip_text(rtso::tr0(i == 5 ? "Ally" : i == 6 ? "Neutral" : "Enemy"));
				l->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			}
			l->set_custom_minimum_size(Vector2(widths[i], 20));
			l->set_vertical_alignment(VERTICAL_ALIGNMENT_TOP);
			diplomacy_text(l); // DiplomacyText; Name and Tribute centred
			if (i == 1 || (i >= 5 && i <= 8)) l->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			head->add_child(l);
		}
		vbox->add_child(head);

		for (int p = 0; p < rtso::MAX_PLAYERS; ++p) {
			Row &r = rows_[p];
			// diplomacyPlayer[n]: the row on the player's colour at alpha 32.
			r.frame = memnew(PanelContainer);
			r.tint.instantiate();
			r.tint->set_bg_color(Color(0, 0, 0, 0));
			r.frame->add_theme_stylebox_override("panel", r.tint);
			r.box = memnew(HBoxContainer);
			r.box->set_custom_minimum_size(Vector2(0, 20)); // diplomacyPlayer[n] "0 0 100% 20"
			r.box->add_theme_constant_override("separation", 0);
			r.frame->add_child(r.box);
			// diplomacyPlayerOutcome: victory.png "Victorious" or defeat.png "Defeated" (setOutcomeIcon).
			r.outcome = memnew(TextureRect);
			r.outcome->set_custom_minimum_size(Vector2(OUTCOME_W, 20));
			r.outcome->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			r.outcome->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
			r.outcome->set_meta("tooltip_style", "session_bold");
			r.box->add_child(r.outcome);
			r.name = add_label(r.box, NAME_W);
			r.name->set_mouse_filter(Control::MOUSE_FILTER_PASS); // the AI settings tooltip
			r.name->set_meta("tooltip_style", "session"); // diplomacyPlayerName has no tooltip_style
			r.civ = add_label(r.box, CIV_W);
			r.team = add_label(r.box, TEAM_W);
			r.theirs = add_label(r.box, THEIRS_W);
			for (int s = 0; s < 3; ++s) {
				r.stance[s] = memnew(Button);
				r.stance[s]->set_custom_minimum_size(Vector2(STANCE_W, 20));
				r.stance[s]->add_theme_font_size_override("font_size", 11);
				r.stance[s]->connect("pressed", Callable(owner, "_on_diplo_stance_changed").bind(s, p));
				r.box->add_child(r.stance[s]);
			}
			for (int t = 0; t < 4; ++t) {
				// diplomacyPlayer[n]_tribute[r]: a 20 px resource icon (session/icons/resources/).
				r.tribute[t] = icon_button(String("res://assets/textures/ui/archive/icons/resources/") + res_name(TRIBUTE_RES[t]) + "_small.png");
				r.tribute[t]->connect("pressed", Callable(owner, "_on_diplo_tribute_pressed").bind(p, (int)TRIBUTE_RES[t]));
				r.box->add_child(r.tribute[t]);
				r.amount[t] = TRIBUTE_DEFAULT;
			}
			// diplomacyAttackRequest (attack-request.png), then diplomacySpyRequest (bribes.png).
			r.attack = icon_button("res://assets/textures/ui/archive/icons/attack-request.png");
			r.attack->set_tooltip_text(rtso::tr0("Request your allies to attack this enemy"));
			r.attack->connect("pressed", Callable(owner, "_on_diplo_attack_request_pressed").bind(p));
			r.box->add_child(r.attack);
			r.spy = icon_button("res://assets/textures/ui/archive/icons/bribes.png");
			r.spy->connect("pressed", Callable(owner, "_on_diplo_spy_pressed").bind(p));
			r.box->add_child(r.spy);
			vbox->add_child(r.frame);
		}

		Control *gap = memnew(Control);
		gap->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		vbox->add_child(gap);
		// diplomacyCeasefireCounter "32 100%-90 100%-32 100%-62": DiplomacyText.
		ceasefire_ = memnew(Label);
		ceasefire_->set_custom_minimum_size(Vector2(0, 40));
		ceasefire_->set_vertical_alignment(VERTICAL_ALIGNMENT_TOP);
		diplomacy_text(ceasefire_);
		vbox->add_child(ceasefire_);

		// diplomacyColorsWindowButton: a 28 px diplomacy.png / diplomacy-on.png icon with "Toggle
		// Diplomacy Colors" beside it, then the Close button (DiplomacyDialog.xml).
		HBoxContainer *bottom = memnew(HBoxContainer);
		bottom->add_theme_constant_override("separation", 4);
		bottom->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
		colors_btn_ = icon_button("res://assets/textures/ui/archive/icons/diplomacy.png", 28.0f);
		colors_btn_->set_meta("tooltip_style", "session");
		colors_btn_->connect("pressed", Callable(owner, "toggle_diplomacy_colors"));
		bottom->add_child(colors_btn_);
		Label *colors_label = memnew(Label);
		colors_label->set_text(rtso::tr0("Toggle Diplomacy Colors"));
		colors_label->set_custom_minimum_size(Vector2(296, 28));
		colors_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		diplomacy_text(colors_label);
		bottom->add_child(colors_label);
		close_btn_ = memnew(Button);
		close_btn_->set_custom_minimum_size(Vector2(128, 28));
		close_btn_->set_text(rtso::tr0("Close"));
		close_btn_->add_theme_font_size_override("font_size", 12);
		close_btn_->connect("pressed", Callable(owner, "toggle_diplomacy"));
		bottom->add_child(close_btn_);
		vbox->add_child(bottom);

		panel_->set_visible(false);
		parent->add_child(panel_);
	}

	bool is_open() const { return panel_ != nullptr && panel_->is_visible(); }
	// Tests: each shown row's name, its tooltip and whether its buttons can be pressed.
	godot::Dictionary debug_state() const {
		godot::Dictionary d;
		d["open"] = is_open();
		godot::Array rows;
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
			const Row &r = rows_[p];
			if (!r.box || !r.box->is_visible()) continue;
			godot::Dictionary row;
			row["player"] = (int)p;
			row["name"] = r.name->get_text();
			row["name_tooltip"] = r.name->get_tooltip_text();
			bool any_enabled = false, any_shown = false;
			for (int t = 0; t < 4; ++t)
				if (r.tribute[t]->is_visible()) { any_shown = true; any_enabled |= !r.tribute[t]->is_disabled(); }
			row["tribute_shown"] = any_shown;
			row["tribute_enabled"] = any_enabled;
			row["spy_shown"] = r.spy->is_visible();
			row["tint"] = r.tint.is_valid() ? r.tint->get_bg_color() : Color();
			row["tribute_icon"] = r.tribute[0]->get_button_icon().is_valid() ? r.tribute[0]->get_button_icon()->get_path() : String();
			row["tribute_text"] = r.tribute[0]->get_text();
			row["tribute_tooltip"] = r.tribute[0]->get_tooltip_text();
			row["stance_tooltip"] = r.stance[0]->get_tooltip_text();
			row["outcome_tooltip"] = r.outcome->get_tooltip_text();
			rows.push_back(row);
		}
		d["rows"] = rows;
		d["close_text"] = close_btn_ ? close_btn_->get_text() : String();
		d["colors_tooltip"] = colors_btn_ ? colors_btn_->get_tooltip_text() : String();
		return d;
	}

	void close() {
		if (panel_) panel_->set_visible(false);
	}

	// messages.js "spy-response" with an entity: the dialog closes and the camera follows the
	// bribed unit. Run every frame, before refresh() can settle the request.
	void poll_spies() {
		const rtso::Simulation *sim = host_ ? host_->ui_sim() : nullptr;
		const uint8_t me = host_ ? host_->local_player() : 0xFF;
		if (!sim || me >= rtso::MAX_PLAYERS) return;
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
			if (!spy_pending_[p] || sim->statistics(me).successful_bribes <= spy_pending_success_[p]) continue;
			spy_pending_[p] = false;
			show_bribed(*sim, me);
		}
	}

	// DiplomacyDialog.open: only with a player to show (g_ViewedPlayer >= 1); an observer
	// who follows a player sees that player's side.
	void toggle() {
		if (!panel_ || !host_) return;
		if (!panel_->is_visible() && (host_->local_player() >= rtso::MAX_PLAYERS || host_->view_player() >= rtso::MAX_PLAYERS)) return;
		panel_->set_visible(!panel_->is_visible());
		if (panel_->is_visible()) refresh();
	}

	// DiplomacyDialog.updatePanels, run each frame while open (registerSimulationUpdateHandler).
	void refresh() {
		const rtso::Simulation *sim = host_ ? host_->ui_sim() : nullptr;
		if (!sim || !panel_) return;
		const uint8_t me = host_->local_player();
		if (me >= rtso::MAX_PLAYERS || host_->view_player() >= rtso::MAX_PLAYERS) { close(); return; } // onViewedPlayerChange
		const bool ctrl = host_->controls_player(me); // controlsPlayer(g_ViewedPlayer)
		const bool mass_held = host_->mass_tribute_held();
		if (colors_btn_) {
			// onDiplomacyColorsChange, onHotkeyChange.
			const bool on = rtso::display_colors::state().enabled;
			if (on != colors_on_) {
				colors_on_ = on;
				colors_btn_->set_button_icon(rtso::load_ui_texture(on ? "res://assets/textures/ui/archive/icons/diplomacy-on.png"
																	  : "res://assets/textures/ui/archive/icons/diplomacy.png"));
			}
			const String key = String(rtso::hotkeys::table().label("session.diplomacycolors").c_str());
			colors_btn_->set_tooltip_text((key.is_empty() ? String() : rtso::hotkey_tag(key) + " ") + rtso::tr0("Toggle Diplomacy Colors"));
		}
		const bool stances_hidden = sim->ceasefire_active() || sim->teams_locked();
		int shown = 0;
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) shown += sim->player(p).active || p == me;
		resize(shown);
		for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p) {
			Row &r = rows_[p];
			const bool in_match = sim->player(p).active || p == me;
			r.box->set_visible(in_match);
			r.frame->set_visible(in_match);
			if (!in_match) continue;
			const uint32_t rgb = rtso::display_colors::rgb(*sim, p); // DiplomacyPlayerText: g_DiplomacyColors
			const Color col(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f);
			r.tint->set_bg_color(Color(col.r, col.g, col.b, 32.0f / 255.0f));
			set_text(r.name, host_->player_display_name(p));
			r.name->set_tooltip_text(host_->ai_settings_text(p)); // DiplomacyPlayerText.init
			r.name->add_theme_color_override("font_color", col);
			set_text(r.civ, rtso::civ_info(sim->player_civ(p)).name);
			set_text(r.team, team_text(*sim, p));
			set_text(r.theirs, stance_text(*sim, p, me));
			const bool defeated = sim->is_defeated(p);
			const bool won = (sim->winner_mask() >> p) & 1;
			const int outcome = defeated ? 2 : won ? 1 : 0;
			if (outcome != r.outcome_shown) {
				r.outcome_shown = outcome;
				r.outcome->set_texture(outcome ? rtso::load_ui_texture(outcome == 1 ? "res://assets/textures/ui/archive/icons/victory.png"
																				 : "res://assets/textures/ui/archive/icons/defeat.png")
											   : Ref<Texture2D>());
				r.outcome->set_tooltip_text(outcome ? rtso::tr0(outcome == 1 ? "Victorious" : "Defeated") : String());
				r.outcome->set_mouse_filter(outcome ? Control::MOUSE_FILTER_PASS : Control::MOUSE_FILTER_IGNORE);
			}
			// DiplomacyDialogPlayerControl: no controls for yourself or a player out of the game
			// (defeated or won, isPlayerInactive).
			const bool inactive = p == me || defeated || ((sim->winner_mask() >> p) & 1) || sim->is_defeated(me) || ((sim->winner_mask() >> me) & 1);
			const uint8_t cur = effective_stance(*sim, me, p);
			for (int s = 0; s < 3; ++s) {
				r.stance[s]->set_visible(!inactive && !stances_hidden);
				r.stance[s]->set_text(cur == s ? "x" : "");
				r.stance[s]->set_disabled(cur == s || !ctrl);
			}
			const String colored_name = "[color=#" + String::num_int64(rtso::display_colors::rgb(*sim, p), 16).lpad(6, "0") + "]" +
					host_->player_display_name(p) + "[/color]";
			for (int t = 0; t < 4; ++t) {
				r.tribute[t]->set_visible(!inactive);
				// TributeButton.onMassTributeRelease: releasing Shift sends a gathered mass tribute.
				r.tribute[t]->set_disabled(!ctrl);
				if (!mass_held && r.amount[t] >= TRIBUTE_MASS) send_tribute(p, t);
				// TributeButton.update: resourceNameWithinSentence and colorizePlayernameByID.
				r.tribute[t]->set_tooltip_text(rtso::sprintf0(
						rtso::tr0("Tribute %(resourceAmount)s %(resourceType)s to %(playerName)s. Shift-click to tribute %(greaterAmount)s."),
						{{"resourceAmount", String::num_int64(r.amount[t])}, {"resourceType", rtso::tr0(res_name(TRIBUTE_RES[t]))},
						 {"playerName", colored_name}, {"greaterAmount", String::num_int64(next_tribute_amount(r.amount[t]))}}));
			}
			// SpyRequestButton.update: hidden when spies are disabled, and for a mutual ally only
			// while the viewer lacks shared vision (hasSharedLos, Cartography), as 0.28 tests it.
			const bool spy_hidden = inactive || sim->spies_disabled()
					|| (sim->is_ally(p, me) && !sim->player(me).has_tech(rtso::TECH_UNLOCK_SHARED_LOS));
			r.spy->set_visible(!spy_hidden);
			if (!spy_hidden) {
				// The cost (x the target's spyCostMultiplier), what is missing, then what a failed
				// bribe costs (VisionSharing FailureCostRatio). A request in flight disables it
				// until the bribe is answered (onSpyResponse).
				String tip = rtso::tr0("Bribe a random unit from this player and share its vision during a limited period.") + "\n";
				bool can = sim->can_bribe(me, p);
				int short_by = 0; // resourcesToAlphaMask: the missing resources tint the icon red
				if (!sim->player(me).has_tech(rtso::TECH_UNLOCK_SPIES)) {
					// getRequirementsTooltip: "Requires %(technology)s" in objectionFont.
					tip += rtso::objection_font(rtso::sprintf0(rtso::tr0("Requires %(technology)s"), {{"technology", rtso::tr0("Espionage")}}));
				} else {
					rtso::CostTip cost, failed;
					cost.metal = sim->bribe_cost(me, p);
					failed.metal = sim->bribe_cost(me, p, true);
					tip += rtso::cost_tooltip(cost, true);
					const String need = rtso::needed_resources_tooltip(cost, sim->player_resource(me, rtso::RES_FOOD),
							sim->player_resource(me, rtso::RES_WOOD), sim->player_resource(me, rtso::RES_STONE),
							sim->player_resource(me, rtso::RES_GOLD), true);
					if (!need.is_empty()) {
						tip += "\n" + need;
						can = false;
						short_by = (int)std::max<int64_t>(0, cost.metal - (int64_t)sim->player_resource(me, rtso::RES_GOLD));
					} else {
						tip += "\n" + rtso::tr0("A failed bribe will cost you:") + "\n" + rtso::cost_tooltip(failed, true);
					}
				}
				if (spy_pending_[p]) {
					const auto st = sim->statistics(me);
					if (st.successful_bribes + st.failed_bribes != spy_pending_bribes_[p] ||
							sim->game_time() - spy_pending_time_[p] > SPY_REQUEST_TIMEOUT)
						spy_pending_[p] = false;
				}
				r.spy->set_disabled(!can || spy_pending_[p] || !ctrl);
				r.spy->set_tooltip_text(tip);
				// resourcesToAlphaMask "color:255 0 0 <alpha>" when short of metal, else
				// SpriteModifierDisabled "color:0 0 0 127:grayscale:".
				if (short_by > 0) {
					const float a = std::min(125.0f, short_by / 10.0f + 50.0f) / 255.0f;
					r.spy->set_self_modulate(Color(1.0f, 1.0f - a, 1.0f - a));
				} else {
					// SpyRequestButton: greyed only while a request is pending.
					r.spy->set_self_modulate(spy_pending_[p] ? Color(0.5f, 0.5f, 0.5f) : Color(1, 1, 1));
				}
			}
			// AttackRequestButton.update: for an enemy, while we have an ally to ask.
			bool has_ally = false;
			for (uint8_t q = 0; q < rtso::MAX_PLAYERS; ++q)
				has_ally |= q != me && sim->player(q).active && sim->is_ally(me, q); // IsMutualAlly: stances only, a defeated ally too
			r.attack->set_visible(!inactive && !sim->ceasefire_active() && has_ally && sim->is_hostile(me, p));
			r.attack->set_disabled(!ctrl);
		}
		// DiplomacyDialogCeasefireCounter.
		ceasefire_->set_visible(sim->ceasefire_active());
		if (sim->ceasefire_active()) {
			// translateWithContext("ceasefire", ...) with timeToString: "mm:ss", or
			// "HH:mm:ss" from an hour on, truncated to the second.
			const int64_t t = (int64_t)std::floor(sim->ceasefire_remaining());
			const String time = t < 3600 ? vformat("%02d:%02d", t / 60, t % 60) : vformat("%02d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
			set_text(ceasefire_, rtso::sprintf0(rtso::tr0("Remaining ceasefire time: %(time)s."), {{"time", time}}));
		}
	}

	// StanceButton.onPress: Commands.js "diplomacy" (lockstep cmd 22; offline the sim applies it now).
	void stance_changed(int stance, int player_idx) {
		rtso::Simulation *sim = host_ ? host_->ui_sim() : nullptr;
		const uint8_t me = host_ ? host_->local_player() : 0xFF;
		if (!sim || player_idx < 0 || player_idx >= rtso::MAX_PLAYERS || me >= rtso::MAX_PLAYERS || player_idx == me) return;
		if (!host_->controls_player(me)) return;
		if (sim->teams_locked() || sim->ceasefire_active()) return; // the buttons are hidden then
		if (host_->turn_cmds())
			host_->post_lockstep(22, nullptr, 0, 0, 0, (uint32_t)player_idx, (uint8_t)stance);
		else
			sim->cmd_set_diplomacy(me, (uint8_t)player_idx, (uint8_t)stance);
		refresh();
	}

	// TributeButton.onPress: Shift-click raises the amount to the next 500, a plain
	// click sends it (Commands.js "tribute", lockstep cmd 27).
	void tribute_pressed(int player_idx, int res_type) {
		if (!host_ || !host_->ui_sim() || player_idx < 0 || player_idx >= rtso::MAX_PLAYERS) return;
		if (!host_->controls_player(host_->local_player())) return;
		int t = 0;
		while (t < 4 && TRIBUTE_RES[t] != res_type) ++t;
		if (t == 4) return;
		Row &r = rows_[player_idx];
		if (host_->mass_tribute_held()) r.amount[t] = next_tribute_amount(r.amount[t]);
		else send_tribute((uint8_t)player_idx, t);
		refresh();
	}

	// SpyRequestButton.onPress: Commands.js "spy-request" (lockstep cmd 42).
	void spy_pressed(int player_idx) {
		rtso::Simulation *sim = host_ ? host_->ui_sim() : nullptr;
		const uint8_t me = host_ ? host_->local_player() : 0xFF;
		if (!sim || player_idx < 0 || player_idx >= rtso::MAX_PLAYERS || !sim->can_bribe(me, (uint8_t)player_idx)) return;
		if (!host_->controls_player(me)) return;
		// No line or sound of its own: the sim's "There are no bribable units" notice is
		// the only feedback, and the button stays disabled until the response.
		if (host_->turn_cmds()) {
			host_->post_lockstep(42, nullptr, 0, 0, 0, (uint32_t)player_idx, 0);
			const auto st = sim->statistics(me);
			spy_pending_[player_idx] = true;
			spy_pending_bribes_[player_idx] = st.successful_bribes + st.failed_bribes;
			spy_pending_success_[player_idx] = st.successful_bribes;
			spy_pending_time_[player_idx] = sim->game_time();
		} else if (sim->cmd_bribe(me, (uint8_t)player_idx) == rtso::Simulation::BRIBE_OK) {
			show_bribed(*sim, me);
		}
		host_->update_hud();
		refresh();
	}

	// AttackRequestButton.onPress: Commands.js "attack-request" (lockstep cmd 46).
	void attack_request_pressed(int player_idx) {
		rtso::Simulation *sim = host_ ? host_->ui_sim() : nullptr;
		const uint8_t me = host_ ? host_->local_player() : 0xFF;
		if (!sim || player_idx < 0 || player_idx >= rtso::MAX_PLAYERS || me >= rtso::MAX_PLAYERS || player_idx == me) return;
		if (!host_->controls_player(me)) return;
		if (host_->turn_cmds()) host_->post_lockstep(46, nullptr, 0, 0, 0, (uint32_t)player_idx, 0);
		else sim->cmd_attack_request(me, (uint8_t)player_idx);
		refresh();
	}

private:
	bool colors_on_ = false;
	godot::Ref<godot::FontVariation> bold_;
	static constexpr float ATTACK_W = 20.0f, OUTCOME_W = 25.0f;
	static constexpr float NAME_W = 125.0f, CIV_W = 100.0f, TEAM_W = 50.0f, THEIRS_W = 60.0f, STANCE_W = 20.0f,
		TRIBUTE_W = 20.0f, SPY_W = 20.0f;
	// SpyRequestButton.spyRequests: a bribe sent and not yet answered, per target; the
	// answer is a change of our bribe counts, or nothing for a bribe refused outright.
	static constexpr float SPY_REQUEST_TIMEOUT = 5.0f;
	bool spy_pending_[rtso::MAX_PLAYERS] = {};
	uint32_t spy_pending_bribes_[rtso::MAX_PLAYERS] = {};
	uint32_t spy_pending_success_[rtso::MAX_PLAYERS] = {};
	void show_bribed(const rtso::Simulation &sim, uint8_t me) {
		const auto &spies = sim.spies();
		for (auto it = spies.rbegin(); it != spies.rend(); ++it)
			if (it->player == me) {
				close();
				host_->follow_unit(it->unit_id);
				return;
			}
	}
	float spy_pending_time_[rtso::MAX_PLAYERS] = {};

	struct Row {
		PanelContainer *frame = nullptr;
		Ref<StyleBoxFlat> tint;
		HBoxContainer *box = nullptr;
		TextureRect *outcome = nullptr;
		int outcome_shown = -1;
		Label *name = nullptr, *civ = nullptr, *team = nullptr, *theirs = nullptr;
		Button *stance[3] = {};
		Button *tribute[4] = {};
		Button *spy = nullptr;
		Button *attack = nullptr;
		int amount[4] = {};
	};

	// An iconButton: the icon stretched over a flat w x w button.
	static Button *icon_button(const String &icon, float w = 20.0f) {
		Button *b = memnew(Button);
		b->set_flat(true);
		b->set_custom_minimum_size(Vector2(w, w));
		b->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
		b->set_button_icon(rtso::load_ui_texture(icon));
		b->set_expand_icon(true);
		b->set_focus_mode(Control::FOCUS_NONE);
		b->set_meta("tooltip_style", "session_bold");
		return b;
	}

	Label *add_label(HBoxContainer *box, float w) const {
		Label *l = memnew(Label);
		l->set_custom_minimum_size(Vector2(w, 20));
		l->set_vertical_alignment(VERTICAL_ALIGNMENT_TOP);
		diplomacy_text(l);
		l->set_clip_text(true);
		box->add_child(l);
		return l;
	}
	// DiplomacyText / TitleText: white sans-bold-stroke-14.
	void diplomacy_text(Label *l) const {
		l->add_theme_font_override("font", bold_);
		l->add_theme_font_size_override("font_size", 14);
		l->add_theme_color_override("font_color", Color(1, 1, 1));
		l->add_theme_color_override("font_outline_color", Color(0, 0, 0));
		l->add_theme_constant_override("outline_size", 3);
	}
	// DiplomacyDialog.resize: 560 px plus 20 per tribute button, 170 plus 20 per player, about 50%-70.
	void resize(int players) {
		const float half_w = (560.0f + 4 * TRIBUTE_W) / 2.0f, half_h = (170.0f + players * 20.0f) / 2.0f;
		panel_->set_offset(godot::SIDE_LEFT, -half_w);
		panel_->set_offset(godot::SIDE_RIGHT, half_w);
		panel_->set_offset(godot::SIDE_TOP, -70.0f - half_h);
		panel_->set_offset(godot::SIDE_BOTTOM, -70.0f + half_h);
	}
	static void set_text(Label *l, const String &s) {
		if (l && l->get_text() != s) l->set_text(s);
	}

	// TributeButton.performTribute, then back to the default amount.
	void send_tribute(uint8_t to, int t) {
		rtso::Simulation *sim = host_->ui_sim();
		const uint8_t me = host_->local_player();
		Row &r = rows_[to];
		const int amount = r.amount[t];
		r.amount[t] = TRIBUTE_DEFAULT;
		if (!sim || me >= rtso::MAX_PLAYERS || to == me || !host_->controls_player(me)) return;
		const uint8_t res = TRIBUTE_RES[t];
		// The button's click is its only sound; "You have sent ..." comes from the tribute
		// notice once the turn runs.
		// Sent even when it can't be paid: the sim answers "Insufficient resources - ..." (Player.js).
		if (host_->turn_cmds()) {
			host_->post_lockstep(27, nullptr, 0, (float)amount, 0, (uint32_t)to, res);
		} else if (sim->cmd_tribute(me, to, res, amount)) {
			host_->update_hud();
		}
	}

	Panel *panel_ = nullptr;
	Row rows_[rtso::MAX_PLAYERS];
	Label *ceasefire_ = nullptr;
	Button *close_btn_ = nullptr;
	Button *colors_btn_ = nullptr;
	RtsoDiplomacyHost *host_ = nullptr;
	RtsoChatWindow *chat_ = nullptr;
};

#endif
