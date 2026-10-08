#ifndef RTSO_DIPLOMACY_DIALOG_H
#define RTSO_DIPLOMACY_DIALOG_H

// P4 stage-1 leaf widget: diplomacy & tribute dialog (0 A.D.
// gui/session/diplomacy/DiplomacyDialog.js parity). Owns the 8-player
// stance matrix; stance changes ride lockstep cmd 22 and tribute lockstep
// cmd 26 (Simulation::cmd_tribute) via RtsoDiplomacyHost.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/h_separator.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstddef>
#include <cstdint>

#include "../simulation/simulation.h"
#include "chat_window.h"
#include "safe_select.h"

using namespace godot;

// Hooks the dialog needs from the RtsoMain coordinator. All but ui_sim()
// are existing RtsoMain methods overriding these directly; ui_sim() is a
// thin inline forward to the existing Simulation member.
class RtsoDiplomacyHost {
public:
	virtual ~RtsoDiplomacyHost() = default;
	virtual bool net_lockstep() const = 0; // existing RtsoMain hook
	// Existing RtsoMain hook -> TurnManager cmd 22 dispatch (stance).
	virtual void post_lockstep(uint8_t cmd_type, const uint32_t *ids, size_t n,
			float x, float z, uint32_t tid, uint8_t param) = 0;
	virtual void play_sfx(const char *path) = 0; // existing RtsoMain hook
	virtual void play_alarm(const char *path) = 0; // existing RtsoMain hook
	virtual void update_hud() = 0; // existing RtsoMain hook
	// The player this client plays as: the row of the stance matrix it owns.
	virtual uint8_t local_player() const = 0; // existing RtsoMain hook
	virtual rtso::Simulation *ui_sim() = 0; // existing sim member forward
};

class RtsoDiplomacyDialog {
public:
	// owner: signal target object (RtsoMain, bound methods). parent: ui
	// CanvasLayer. host: coordinator hooks. chat: feedback log sink.
	void build(Object *owner, Node *parent, RtsoDiplomacyHost *host,
			RtsoChatWindow *chat) {
		host_ = host;
		chat_ = chat;
		// =========================================================================
		// 6. DIPLOMACY & TRIBUTE DIALOG (8-Player Matrix)
		// =========================================================================
		panel_ = memnew(Panel);
		panel_->set_anchor(SIDE_LEFT, 0.5f);
		panel_->set_anchor(SIDE_RIGHT, 0.5f);
		panel_->set_anchor(SIDE_TOP, 0.5f);
		panel_->set_anchor(SIDE_BOTTOM, 0.5f);
		panel_->set_offset(SIDE_LEFT, -320.0f);
		panel_->set_offset(SIDE_RIGHT, 320.0f);
		panel_->set_offset(SIDE_TOP, -210.0f);
		panel_->set_offset(SIDE_BOTTOM, 210.0f);

		Ref<StyleBoxFlat> diplo_style;
		diplo_style.instantiate();
		diplo_style->set_bg_color(Color(0.06f, 0.07f, 0.10f, 0.97f));
		diplo_style->set_corner_radius_all(8);
		diplo_style->set_border_width_all(2);
		diplo_style->set_border_color(Color(0.85f, 0.72f, 0.25f, 0.95f));
		panel_->add_theme_stylebox_override("panel", diplo_style);

		VBoxContainer *diplo_vbox = memnew(VBoxContainer);
		diplo_vbox->set_anchor(SIDE_LEFT, 0.0f);
		diplo_vbox->set_anchor(SIDE_RIGHT, 1.0f);
		diplo_vbox->set_anchor(SIDE_TOP, 0.0f);
		diplo_vbox->set_anchor(SIDE_BOTTOM, 1.0f);
		diplo_vbox->set_offset(SIDE_LEFT, 16.0f);
		diplo_vbox->set_offset(SIDE_RIGHT, -16.0f);
		diplo_vbox->set_offset(SIDE_TOP, 12.0f);
		diplo_vbox->set_offset(SIDE_BOTTOM, -12.0f);
		diplo_vbox->add_theme_constant_override("separation", 6);
		panel_->add_child(diplo_vbox);

		Label *diplo_title = memnew(Label);
		diplo_title->set_text("🏛️ Hellenic League — Diplomacy & Tribute Matrix");
		diplo_title->add_theme_font_size_override("font_size", 15);
		diplo_title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		diplo_title->add_theme_color_override("font_color", Color(1.0f, 0.88f, 0.4f));
		diplo_vbox->add_child(diplo_title);

		HSeparator *diplo_sep1 = memnew(HSeparator);
		diplo_vbox->add_child(diplo_sep1);

		const char *player_names[8] = {
			"🔵 Athens (You)",
			"🔴 Sparta (AI)",
			"🟡 Persia (AI)",
			"🟣 Rome (AI)",
			"⚪ Carthage (AI)",
			"🟢 Macedon (AI)",
			"🟠 Gaul (AI)",
			"🟤 Maurya (AI)"
		};

		for (int p = 0; p < 8; ++p) {
			HBoxContainer *prow = memnew(HBoxContainer);
			prow->set_custom_minimum_size(Vector2(0, 26));
			prow->add_theme_constant_override("separation", 8);

			player_names_[p] = memnew(Label);
			player_names_[p]->set_custom_minimum_size(Vector2(165, 24));
			player_names_[p]->set_text(player_names[p]);
			player_names_[p]->add_theme_font_size_override("font_size", 11);
			prow->add_child(player_names_[p]);

			stances_[p] = memnew(RtsoSafeSelect);
			stances_[p]->set_custom_minimum_size(Vector2(95, 24));
			stances_[p]->add_theme_font_size_override("font_size", 10);
			if (p == 0) {
				stances_[p]->add_item("Self", 0);
				stances_[p]->set_disabled(true);
			} else {
				stances_[p]->add_item("Ally", 0);
				stances_[p]->add_item("Neutral", 1);
				stances_[p]->add_item("Enemy", 2);
				stances_[p]->select(2); // Enemy
				stances_[p]->connect("item_selected", Callable(owner, "_on_diplo_stance_changed").bind(p));
			}
			prow->add_child(stances_[p]);

			if (p == 0) {
				Label *self_lbl = memnew(Label);
				self_lbl->set_text("— Player Base Command —");
				self_lbl->add_theme_font_size_override("font_size", 10);
				self_lbl->add_theme_color_override("font_color", Color(0.6f, 0.6f, 0.7f));
				prow->add_child(self_lbl);
			} else {
				const char *t_icons[4] = {"🍞 +100", "🌲 +100", "🪙 +100", "🪨 +100"};
				int t_res[4] = {rtso::RES_FOOD, rtso::RES_WOOD, rtso::RES_GOLD, rtso::RES_STONE};
				for (int r = 0; r < 4; ++r) {
					Button *tb = memnew(Button);
					tb->set_custom_minimum_size(Vector2(68, 22));
					tb->set_text(t_icons[r]);
					tb->add_theme_font_size_override("font_size", 9);
					tb->connect("pressed", Callable(owner, "_on_diplo_tribute_pressed").bind(p, t_res[r]));
					prow->add_child(tb);
				}
			}

			diplo_vbox->add_child(prow);
		}

		HSeparator *diplo_sep2 = memnew(HSeparator);
		diplo_vbox->add_child(diplo_sep2);

		close_btn_ = memnew(Button);
		close_btn_->set_custom_minimum_size(Vector2(130, 28));
		close_btn_->set_text("Close (D / Esc)");
		close_btn_->set_h_size_flags(Control::SIZE_SHRINK_CENTER);
		close_btn_->add_theme_font_size_override("font_size", 11);
		close_btn_->connect("pressed", Callable(owner, "toggle_diplomacy"));
		diplo_vbox->add_child(close_btn_);

		panel_->set_visible(false);
		parent->add_child(panel_);
	}

	bool is_open() const { return panel_ != nullptr && panel_->is_visible(); }
	void close() {
		if (panel_) panel_->set_visible(false);
	}

	void toggle() {
		if (!panel_) return;
		panel_->set_visible(!panel_->is_visible());
		if (panel_->is_visible()) {
			host_->play_sfx("res://assets/sounds/ui/diplomacy.ogg");
		}
	}

	// Lockstep stance change: cmd 22 (TurnManager dispatch) when online,
	// validated sim cmd offline. 0 A.D. DiplomacyDialog parity.
	void stance_changed(int stance, int player_idx) {
		if (!host_ || !host_->ui_sim() || player_idx < 0 || player_idx >= 8) return;
		// Lockstep: stance MUST be a turn command (cmd_type 22, turn_manager.cpp dispatch)
		// — diplomacy_state() is hashed (turn_manager.cpp:213), so a local-only change
		// desyncs peers. Offline applies immediately via the validated sim cmd.
		// 0 A.D. Diplomacy.js / DiplomacyDialog.js OnStanceChange parity.
		if (host_->net_lockstep())
			host_->post_lockstep(22, nullptr, 0, 0, 0, (uint32_t)player_idx, (uint8_t)stance);
		else
			host_->ui_sim()->cmd_set_diplomacy(host_->local_player(), (uint8_t)player_idx, (uint8_t)stance);
		String st_name = (stance == 0) ? "Ally" : ((stance == 1) ? "Neutral" : "Enemy");
		UtilityFunctions::print("[RTS0] Set diplomacy stance with Player ", player_idx + 1, " to ", st_name);
		if (chat_) {
			chat_->append_line("[color=#eebb22]🏛️ Diplomacy with Player " + String::num_int64(player_idx + 1) + " set to " + st_name + "[/color]\n");
		}
	}

	// Tribute (0 A.D. Player.js TributeResource). Lockstep: a turn command
	// (cmd 26), since resources are hashed; offline the sim applies it now.
	void tribute_pressed(int player_idx, int res_type) {
		if (!host_ || !host_->ui_sim() || player_idx < 0 || player_idx >= 8 || res_type < 0 || res_type >= 4) return;
		const uint8_t me = host_->local_player();
		if (player_idx == me) return;
		rtso::Simulation *sim = host_->ui_sim();
		bool ok;
		if (host_->net_lockstep()) {
			// Checked here only to give feedback; the sim validates again when the turn runs.
			ok = sim->player_resource(me, (uint8_t)res_type) >= 100;
			if (ok) host_->post_lockstep(26, nullptr, 0, 100.0f, 0, (uint32_t)player_idx, (uint8_t)res_type);
		} else {
			ok = sim->cmd_tribute(me, (uint8_t)player_idx, (uint8_t)res_type, 100);
		}
		if (ok) {
			const char *r_names[4] = {"Wood", "Gold", "Stone", "Food"};
			String r_name = (res_type >= 0 && res_type < 4) ? r_names[res_type] : "Resource";
			UtilityFunctions::print("[RTS0] Sent 100 ", r_name, " tribute to Player ", player_idx + 1);
			if (chat_) {
				chat_->append_line("[color=#55ffaa]🎁 Sent 100 " + r_name + " tribute to Player " + String::num_int64(player_idx + 1) + "[/color]\n");
			}
			host_->play_sfx("res://assets/sounds/ui/bonus.ogg");
			host_->update_hud();
		} else {
			host_->play_alarm("res://assets/sounds/ui/alarm.ogg");
		}
	}

private:
	Panel *panel_ = nullptr;
	RtsoSafeSelect *stances_[8] = { nullptr };
	Label *player_names_[8] = { nullptr };
	Button *close_btn_ = nullptr;
	RtsoDiplomacyHost *host_ = nullptr;
	RtsoChatWindow *chat_ = nullptr;
};

#endif
