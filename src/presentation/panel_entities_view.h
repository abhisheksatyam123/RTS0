#ifndef RTSO_PANEL_ENTITIES_VIEW_H
#define RTSO_PANEL_ENTITIES_VIEW_H

// 0 A.D. 0.28 gui/session PanelEntityManager.js, PanelEntity.js and
// PanelEntities.xml (GPL-2.0-or-later): a button for each of the viewed
// player's heroes and relics (Player.js panelEntityClasses "Hero Relic",
// session.js g_PanelEntityOrder: heroes first), every player's in the observer
// view, in a row under the top panel. Each shows the template's portrait with a
// health bar, flashes red when it loses health (colorFade_attackUnit), and its
// tooltip carries the name and health. A click selects it (selection.add keeps
// the selection), a double click selects it and moves the camera to it.
// A capturable one (a relic's catafalque) adds a capture bar split per player in
// diplomacy colours, on top when it has no health; the tooltip is the bold name,
// then getCurrentHealthTooltip, getCurrentCaptureTooltip, getAttackTooltip,
// getResistanceTooltip, getEntityTooltip and getAurasTooltip.

#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <algorithm>
#include <vector>

#include "../simulation/simulation.h"
#include "display_colors.h"
#include "l10n.h"
#include "production_queue.h"
#include "selection_details.h"
#include "template_portraits.h"

namespace rtso {

class RtsoPanelEntities {
public:
	static constexpr int kButtons = 19; // PanelEntities.xml repeat count
	static constexpr float kSize = 50.0f;
	static constexpr float kMargin = 1.0f; // setPanelObjectPosition's default margins

	// parent: the HUD layer; owner gets `method`(slot, double_click) on a click.
	void build(godot::Node *parent, godot::Object *owner, const char *method, float top) {
		using namespace godot;
		row_ = memnew(Control);
		row_->set_name("PanelEntities");
		row_->set_position(Vector2(12, top));
		row_->set_size(Vector2(kButtons * (kSize + kMargin), kSize + 6));
		row_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		parent->add_child(row_);
		for (int i = 0; i < kButtons; ++i) {
			Slot &s = slots_[i];
			s.button = memnew(Button);
			s.button->set_name(String("PanelEntityButton") + String::num_int64(i));
			s.button->set_size(Vector2(kSize, kSize));
			s.button->set_position(Vector2(i * (kSize + kMargin), 0));
			s.button->set_visible(false);
			s.button->connect("gui_input", Callable(owner, method).bind(i));
			row_->add_child(s.button);
			s.image = memnew(TextureRect);
			s.image->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			s.image->set_stretch_mode(TextureRect::STRETCH_SCALE);
			s.image->set_position(Vector2(5, 5));
			s.image->set_size(Vector2(kSize - 10, kSize - 10));
			s.image->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			s.button->add_child(s.image);
			s.hit = memnew(ColorRect); // panelEntityHitOverlay
			s.hit->set_position(Vector2(5, 5));
			s.hit->set_size(Vector2(kSize - 10, kSize - 10));
			s.hit->set_color(Color(1, 0, 0, 0));
			s.hit->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			s.button->add_child(s.hit);
			// panelEntityHealthSection: "3 100%-5 100%-3 100%-2", the bar in the healthBackground.
			s.health_bg = memnew(ColorRect);
			s.health_bg->set_position(Vector2(3, kSize - 5));
			s.health_bg->set_size(Vector2(kSize - 6, 3));
			s.health_bg->set_color(Color(0.25f, 0.0f, 0.0f, 1.0f));
			s.health_bg->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			s.button->add_child(s.health_bg);
			s.health = memnew(ColorRect);
			s.health->set_size(Vector2(kSize - 6, 3));
			s.health->set_color(Color(0.0f, 0.85f, 0.15f, 1.0f));
			s.health->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			s.health_bg->add_child(s.health);
			// panelEntityCaptureSection: below the button ("3 100% 100%-3 100%+5"), or in the
			// health bar's place (panelEntitySectionPosTop) when there is no health.
			s.capture_bg = memnew(ColorRect);
			s.capture_bg->set_size(Vector2(kSize - 6, 3));
			s.capture_bg->set_color(Color(0.0f, 0.0f, 0.0f, 1.0f));
			s.capture_bg->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			s.capture_bg->set_visible(false);
			s.button->add_child(s.capture_bg);
			for (auto &part : s.capture) {
				part = memnew(ColorRect);
				part->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
				part->set_visible(false);
				s.capture_bg->add_child(part);
			}
		}
	}

	// player: whose entities show (OBSERVER_VIEW: everyone's); now: seconds, for the flash.
	void refresh(const Simulation &sim, uint8_t player, float now) {
		if (!row_) return;
		auto uview = sim.units();
		struct Want { uint32_t id; int order; size_t i; };
		std::vector<Want> want;
		for (size_t i = 0; i < uview.count; ++i) {
			if (!uview.alive[i]) continue;
			if (player != OBSERVER_VIEW && uview.team[i] != player) continue;
			if (player == OBSERVER_VIEW && uview.team[i] >= MAX_PLAYERS) continue;
			const int order = panel_order(uview.tmpl ? uview.tmpl[i] : CIV_UNIT_TEMPLATE_NONE, uview.unit_type[i]);
			if (order < 0) continue;
			want.push_back({uview.id[i], order, i});
		}
		// insertIfRelevant: by g_PanelEntityOrder, then in the order they came (entity id).
		std::stable_sort(want.begin(), want.end(), [](const Want &a, const Want &b) {
			return a.order != b.order ? a.order < b.order : a.id < b.id;
		});
		if (want.size() > (size_t)kButtons) want.resize(kButtons);
		for (int k = 0; k < kButtons; ++k) {
			Slot &s = slots_[k];
			if (k >= (int)want.size()) {
				s.button->set_visible(false);
				s.id = 0;
				continue;
			}
			const size_t i = want[k].i;
			const uint16_t tmpl = uview.tmpl ? uview.tmpl[i] : CIV_UNIT_TEMPLATE_NONE;
			if (s.id != want[k].id) {
				s.id = want[k].id;
				s.hp = uview.hp[i];
				s.hit_at = -100.0f;
				s.image->set_texture(portrait(tmpl, uview.unit_type[i]));
				s.name = name(tmpl, uview.unit_type[i]);
				s.cp_owner = -1.0f;
				s.tip_at = -100.0f;
			}
			// A catafalque has no Health (template_unit_catafalque.xml), only Capturable.
			const bool relic = uview.unit_type[i] == UNIT_CATAFALQUE;
			if (!relic && uview.hp[i] < s.hp) s.hit_at = now; // onAttacked
			s.hp = uview.hp[i];
			const float max = relic ? 0.0f : uview.max_hp[i];
			const Simulation::Relic *r = nullptr;
			if (relic)
				for (const auto &rr : sim.relics())
					if (rr.id == s.id) r = &rr;
			update_capture(sim, s, r, uview.team[i], max > 0.0f, now);
			s.health_bg->set_visible(max > 0.0f);
			if (max > 0.0f) s.health->set_size(godot::Vector2((kSize - 6) * std::clamp(s.hp / max, 0.0f, 1.0f), 3));
			// colorFade_attackUnit: a red flash fading out over a second.
			const float t = now - s.hit_at;
			s.hit->set_color(godot::Color(1, 0, 0, t >= 0.0f && t < 1.0f ? 0.6f * (1.0f - t) : 0.0f));
			// The tooltip's attack and resistance lines change rarely: rebuilt twice a second.
			if (now - s.tip_at >= 0.5f || now < s.tip_at) {
				s.tip_at = now;
				const godot::String tip = tooltip(sim, s, i, max, r, uview.team[i]);
				if (s.button->get_tooltip_text() != tip) s.button->set_tooltip_text(tip);
			}
			s.button->set_visible(true);
		}
	}

	uint32_t entity_at(int slot) const { return slot >= 0 && slot < kButtons ? slots_[slot].id : 0; }
	godot::Button *button(int slot) const { return slot >= 0 && slot < kButtons ? slots_[slot].button : nullptr; }

	// g_PanelEntityOrder index ("Hero", "Relic"), -1 when not a panel entity.
	static int panel_order(uint16_t tmpl, uint8_t type) {
		if (type == UNIT_CATAFALQUE) return 1;
		const TechClasses c = tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(tmpl).tech_classes : unit_tech_classes(type);
		return (c & TC_HERO) ? 0 : -1;
	}

	// The template's Identity/Icon (data/reference/templates.json units.<id>.icon), else the class portrait.
	static godot::Ref<godot::Texture2D> portrait(uint16_t tmpl, uint8_t type) { return portraits::unit(tmpl, type); }

private:
	struct Slot {
		godot::Button *button = nullptr;
		godot::TextureRect *image = nullptr;
		godot::ColorRect *hit = nullptr, *health_bg = nullptr, *health = nullptr;
		uint32_t id = 0;
		godot::ColorRect *capture_bg = nullptr;
		godot::ColorRect *capture[MAX_PLAYERS + 1] = {};
		float hp = 0.0f, hit_at = -100.0f, cp_owner = -1.0f, tip_at = -100.0f;
		godot::String name;
	};

	// updateCapturePointsBar: the owner's part first, then each other player's, at alpha 128;
	// losing capture points flashes the button like a hit.
	static void update_capture(const Simulation &sim, Slot &s, const Simulation::Relic *r, uint8_t owner, bool has_health, float now) {
		s.capture_bg->set_visible(r != nullptr);
		if (!r) return;
		s.capture_bg->set_position(godot::Vector2(3, has_health ? kSize + 1 : kSize - 5));
		const int owner_slot = owner < MAX_PLAYERS ? owner : MAX_PLAYERS;
		const float max = (float)RELIC_CAPTURE_POINTS, w = kSize - 6;
		if (s.cp_owner >= 0.0f && r->cp[owner_slot] < s.cp_owner) s.hit_at = now;
		s.cp_owner = r->cp[owner_slot];
		float x = 0.0f;
		int used = 0;
		auto part = [&](int slot) {
			const float frac = std::clamp(r->cp[slot] / max, 0.0f, 1.0f);
			if (frac <= 0.0f) return;
			const uint32_t rgb = slot < MAX_PLAYERS ? display_colors::rgb(sim, (uint8_t)slot) : sim.player_color(GAIA_PLAYER);
			godot::ColorRect *c = s.capture[used++];
			c->set_color(godot::Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 128.0f / 255.0f));
			c->set_position(godot::Vector2(x, 0.0f));
			c->set_size(godot::Vector2(w * frac, 3));
			c->set_visible(true);
			x += w * frac;
		};
		part(owner_slot);
		for (int slot = 0; slot <= MAX_PLAYERS; ++slot)
			if (slot != owner_slot) part(slot);
		for (int k = used; k <= MAX_PLAYERS; ++k) s.capture[k]->set_visible(false);
	}

	// nameTooltip (sans-bold-16) and PanelEntity.prototype.Tooltips.
	static godot::String tooltip(const Simulation &sim, const Slot &s, size_t i, float max_hp, const Simulation::Relic *r, uint8_t owner) {
		using namespace seldet;
		auto uv = sim.units();
		const uint16_t tmpl = uv.tmpl ? uv.tmpl[i] : CIV_UNIT_TEMPLATE_NONE;
		const godot::Dictionary entry = portraits::unit_entry(tmpl);
		const godot::Dictionary viewer = entry.get("viewer", godot::Dictionary());
		const Simulation::UnitDetails d = sim.unit_details(i);
		godot::String capture_tip;
		if (r) capture_tip = capture(r->cp[owner < MAX_PLAYERS ? owner : MAX_PLAYERS], (float)RELIC_CAPTURE_POINTS);
		const godot::String auras = unit_auras_tooltip(tmpl);
		const godot::String entity_tip = entry.get("tooltip", godot::String());
		const bool armed = !r;
		return "[font_size=16][b]" + s.name + "[/b][/font_size]\n" +
				join_lines({max_hp > 0.0f ? health(s.hp, max_hp) : godot::String(), capture_tip,
						armed ? attack_tooltip(d, viewer.get("attacks", godot::Array())) : godot::String(),
						r ? godot::String() : resistance_tooltip(d.resistance), entity_tip.is_empty() ? godot::String() : tr0(entity_tip), auras});
	}

	static godot::String name(uint16_t tmpl, uint8_t type) {
		const auto &info = tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_template_unit_info(tmpl, type) : unit_type_info(type);
		const char *specific = tmpl < CIV_UNIT_TEMPLATE_COUNT ? civ_unit_template(tmpl).specific_name : nullptr;
		// g_SpecificNamesPrimary: the specific name, else the generic one.
		return godot::String::utf8(specific && *specific ? specific : info.name);
	}

	godot::Control *row_ = nullptr;
	Slot slots_[kButtons];
};

} // namespace rtso

#endif
