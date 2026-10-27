#ifndef RTSO_PRODUCTION_QUEUE_H
#define RTSO_PRODUCTION_QUEUE_H

// P4 stage-2 leaf widget: 16-slot visual production queue & rally-point query
// (0 A.D. gui/session/selection_panels.js + production_queue.xml + CCmpRallyPoint parity).
// Owns the 16 queue slot panels, queue progress display, slot GUI input & cancel
// dispatch, and the rally point query helper.

#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/panel.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/style_box_flat.hpp>
#include <godot_cpp/classes/texture_rect.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include <cstdint>

#include "../network/net_types.h"
#include "../simulation/simulation.h"
#include "l10n.h"
#include "research_progress_view.h"
#include "template_portraits.h"

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

namespace rtso {

// 0 A.D. CCmpRallyPoint / gui/session/selection_panels.js GetRallyPoint parity
struct RallyPointInfo {
	bool has_rally = false;
	float x = 0.0f;
	float z = 0.0f;
};

inline RallyPointInfo get_rally_point(const rtso::Simulation *sim, uint32_t building_id, uint8_t team = 0) {
	RallyPointInfo info;
	if (!sim || building_id == 0) return info;
	for (const auto &b : sim->buildings()) {
		if (b.id == building_id && b.team == team && b.has_rally_point) {
			info.has_rally = true;
			info.x = b.rally_x;
			info.z = b.rally_z;
			break;
		}
	}
	return info;
}

} // namespace rtso

// Host hooks required from RtsoMain coordinator
class RtsoProductionQueueHost {
public:
	virtual ~RtsoProductionQueueHost() = default;
	virtual bool net_lockstep() const = 0;
	// Orders go through the TurnManager (always in a match, so replays record them).
	virtual bool turn_cmds() const = 0;
	virtual void post_lockstep(uint8_t cmd_type, const uint32_t *ids, size_t n,
			float x = 0, float z = 0, uint32_t tid = 0, uint8_t param = 0) = 0;
	virtual void play_sfx(const char *path) = 0;
	virtual void update_command_panel() = 0;
	virtual void update_inspector() = 0;
	virtual rtso::Simulation *ui_sim() = 0;
	virtual uint32_t ui_selected_building_id() const = 0;
	virtual uint8_t local_player() const = 0;
};

class RtsoProductionQueue {
public:
	struct QueueSlotUI {
		godot::Panel *panel = nullptr;
		godot::TextureRect *icon = nullptr;
		godot::ColorRect *progress = nullptr; // unitQueueProgressSlider
		godot::Label *chip = nullptr;         // unitQueueCount
		godot::TextureRect *paused = nullptr; // unitQueuePausedIcon
		int queue_index = -1;                 // the item a left click removes; -1 for a ghost
		uint32_t producer = 0;                // the structure whose queue holds it (0: the selected one)
		godot::String kind;                   // the Template Viewer's page on a right click
		int id = -1, civ = 0;
	};

	// 0.28 selection_panels_right/queue_panel.xml: the production icon (its tooltip
	// "Production queue", the first item's m:ss over it), then a button per item.
	void build(godot::Object *owner, godot::Control *parent_panel, RtsoProductionQueueHost *host) {
		using namespace godot;
		host_ = host;
		owner_ = owner;

		queue_bar_ = memnew(HBoxContainer);
		queue_bar_->set_position(Vector2(10, 122));
		queue_bar_->set_size(Vector2(552, 54));
		queue_bar_->add_theme_constant_override("separation", 1);
		parent_panel->add_child(queue_bar_);

		production_icon_ = memnew(TextureRect);
		production_icon_->set_name("QueueProductionIcon");
		production_icon_->set_custom_minimum_size(Vector2(32, 36));
		production_icon_->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		production_icon_->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		production_icon_->set_texture(rtso::load_ui_texture("res://assets/textures/ui/archive/icons/production.png"));
		production_icon_->set_tooltip_text(rtso::tr0("Production queue"));
		production_icon_->set_visible(false);
		queue_bar_->add_child(production_icon_);
		time_remaining_ = memnew(Label);
		time_remaining_->set_name("queueTimeRemaining");
		time_remaining_->set_anchors_preset(Control::PRESET_FULL_RECT);
		time_remaining_->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		time_remaining_->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
		time_remaining_->add_theme_font_size_override("font_size", 12);
		time_remaining_->add_theme_color_override("font_outline_color", Color(0, 0, 0, 1));
		time_remaining_->add_theme_constant_override("outline_size", 3);
		time_remaining_->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		production_icon_->add_child(time_remaining_);

		// The portrait's "grayscale:" for an auto-queue ghost.
		Ref<Shader> gray;
		gray.instantiate();
		gray->set_code("shader_type canvas_item;\nvoid fragment() {\n\tvec4 c = texture(TEXTURE, UV);\n"
				"\tfloat g = dot(c.rgb, vec3(0.3, 0.59, 0.11));\n\tCOLOR = vec4(vec3(g), c.a);\n}\n");
		grayscale_.instantiate();
		grayscale_->set_shader(gray);

		const Ref<Texture2D> paused_tex = rtso::load_ui_texture("res://assets/textures/ui/archive/icons/stop.png");
		// 0 A.D. shows the whole queue (selection_panels.js Queue: 16 items).
		for (int q = 0; q < kSlots; ++q) {
			Panel *sp = memnew(Panel);
			sp->set_name("unitQueueButton" + String::num_int64(q));
			sp->set_custom_minimum_size(Vector2(32, 36));

			Ref<StyleBoxFlat> slot_sb;
			slot_sb.instantiate();
			slot_sb->set_bg_color(Color(0.09f, 0.11f, 0.15f, 0.90f));
			slot_sb->set_border_width_all(1);
			slot_sb->set_border_color(Color(0.35f, 0.38f, 0.48f, 0.70f));
			slot_sb->set_corner_radius_all(4);
			sp->add_theme_stylebox_override("panel", slot_sb);

			TextureRect *ico = memnew(TextureRect);
			ico->set_position(Vector2(2, 3));
			ico->set_size(Vector2(28, 30));
			ico->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			ico->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
			ico->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			ico->set_visible(false);
			sp->add_child(ico);

			// queueProgressSlider (0 255 0 128): its top moves down as the item advances;
			// "color:0 150 250 50" over the whole button for a ghost.
			ColorRect *pb = memnew(ColorRect);
			pb->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			pb->set_visible(false);
			sp->add_child(pb);

			Label *chip = memnew(Label);
			chip->set_position(Vector2(0, 0));
			chip->set_size(Vector2(32, 36));
			chip->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
			chip->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
			chip->add_theme_font_size_override("font_size", 14);
			chip->add_theme_color_override("font_color", Color(1, 1, 1));
			chip->add_theme_color_override("font_outline_color", Color(0, 0, 0, 1));
			chip->add_theme_constant_override("outline_size", 4);
			chip->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			chip->set_visible(false);
			sp->add_child(chip);

			// unitQueuePausedIcon: stretched:session/icons/stop.png over the button.
			TextureRect *pz = memnew(TextureRect);
			pz->set_name("unitQueuePausedIcon");
			pz->set_texture(paused_tex);
			pz->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			pz->set_stretch_mode(TextureRect::STRETCH_SCALE);
			pz->set_position(Vector2(2, 3));
			pz->set_size(Vector2(28, 30));
			pz->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			pz->set_visible(false);
			sp->add_child(pz);

			sp->connect("gui_input", Callable(owner, "_on_queue_slot_gui_input").bind(q));

			queue_slots_[q].panel = sp;
			queue_slots_[q].icon = ico;
			queue_slots_[q].progress = pb;
			queue_slots_[q].chip = chip;
			queue_slots_[q].paused = pz;
			queue_bar_->add_child(sp);
		}
	}

	void update(const rtso::Simulation::Building *found_b) {
		trainer_unit_ = 0;
		trainer_garrisoned_ = false;
		if (found_b) show(&found_b->queue, found_b->type, found_b->autoqueue, found_b->team);
		else clear();
	}

	// Queue.getItems for several selected structures: every structure's first item, then
	// every second item, and so on, with each one's auto-queue ghosts after.
	void update_multi(const std::vector<const rtso::Simulation::Building *> &producers) {
		trainer_unit_ = 0;
		trainer_garrisoned_ = false;
		std::vector<Item> items;
		bool found = true;
		for (int i = 0; found && (int)items.size() < kSlots; ++i) {
			found = false;
			for (const auto *b : producers)
				if (b && i < (int)b->queue.size() && (int)items.size() < kSlots) {
					items.push_back({&b->queue, b->id, i, false, b->team});
					found = true;
				}
		}
		std::vector<Item> repeat;
		for (const Item &it : items)
			for (const auto *b : producers)
				if (b && b->id == it.producer && b->autoqueue && !(*it.queue)[it.qi].is_tech()) repeat.push_back({it.queue, it.producer, it.qi, true, it.player});
		for (size_t r = 0; !repeat.empty() && (int)items.size() < kSlots; ++r) items.push_back(repeat[r % repeat.size()]);
		show_items(items);
	}

	// A unit's Trainer queue (triremes, Cyrus II); a click cancels through the unit.
	void update_unit(uint32_t unit_id, const std::vector<rtso::ProductionOrder> *queue) {
		trainer_unit_ = unit_id;
		// ProductionQueue.OnGarrisonedStateChanged: a garrisoned trainer pauses its queue.
		trainer_garrisoned_ = false;
		if (const rtso::Simulation *sim = host_ ? host_->ui_sim() : nullptr) {
			const auto uv = sim->units();
			for (size_t i = 0; i < uv.count; ++i)
				if (uv.id[i] == unit_id) { trainer_garrisoned_ = !uv.alive[i]; break; }
		}
		if (queue && host_) show(queue, rtso::BLD_TYPE_COUNT, false, host_->local_player());
		else clear();
	}

	// g_SelectionPanels.Queue: getItems (the queue, then the auto-queue's ghost
	// repeats of its units up to 16) and setupButton.
	void show(const std::vector<rtso::ProductionOrder> *queue, uint8_t researcher, bool autoqueue, uint8_t player) {
		(void)researcher;
		if (!queue || queue->empty()) {
			clear();
			return;
		}
		std::vector<Item> items;
		for (int i = 0; i < (int)queue->size() && (int)items.size() < kSlots; ++i) items.push_back({queue, 0, i, false, player});
		std::vector<Item> repeat;
		if (autoqueue)
			for (int i = 0; i < (int)queue->size(); ++i)
				if (!(*queue)[i].is_tech()) repeat.push_back({queue, 0, i, true, player});
		for (size_t r = 0; !repeat.empty() && (int)items.size() < kSlots; ++r) items.push_back(repeat[r % repeat.size()]);
		show_items(items);
	}

	struct Item {
		const std::vector<rtso::ProductionOrder> *queue;
		uint32_t producer; // 0: the selected structure or unit
		int qi;
		bool ghost;
		uint8_t player;
	};

	void show_items(const std::vector<Item> &items) {
		using namespace godot;
		if (items.empty()) {
			clear();
			return;
		}
		const rtso::Simulation *sim = host_ ? host_->ui_sim() : nullptr;

		production_icon_->set_visible(true);
		time_remaining_->set_text("");
		for (int q = 0; q < kSlots; ++q) {
			QueueSlotUI &slot = queue_slots_[q];
			if (q >= (int)items.size()) {
				hide_slot(slot);
				continue;
			}
			const bool ghost = items[q].ghost;
			const int qi = items[q].qi;
			const uint8_t player = items[q].player;
			const int civ = sim && player < rtso::MAX_PLAYERS ? sim->player_civ(player) : 0;
			const rtso::ProductionOrder &po = (*items[q].queue)[qi];
			slot.queue_index = ghost ? -1 : qi;
			slot.producer = items[q].producer;
			slot.panel->set_visible(true);

			String name;
			Ref<Texture2D> tex;
			if (po.is_tech()) {
				name = rtso::tr0(String::utf8(rtso::tech_info(po.tech).name));
				tex = rtso::RtsoResearchProgress::icon(po.tech);
				slot.kind = "tech";
				slot.id = po.tech;
			} else {
				const uint8_t ut = po.unit_type < rtso::UNIT_TYPE_COUNT ? po.unit_type : 0;
				const uint16_t tmpl = po.tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? po.tmpl
						: (po.unit_type < rtso::UNIT_TYPE_COUNT ? rtso::civ_default_template((uint8_t)civ, ut) : rtso::CIV_UNIT_TEMPLATE_NONE);
				const auto &info = tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? rtso::civ_template_unit_info(tmpl, ut) : rtso::unit_type_info(ut);
				const char *specific = tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? rtso::civ_unit_template(tmpl).specific_name : nullptr;
				name = rtso::entity_names(String::utf8(info.name), String::utf8(specific ? specific : ""));
				tex = rtso::portraits::unit(po.tmpl, po.unit_type);
				slot.kind = tmpl != rtso::CIV_UNIT_TEMPLATE_NONE ? "unit" : "";
				slot.id = tmpl;
			}
			slot.civ = civ;
			slot.icon->set_texture(tex);
			slot.icon->set_visible(tex.is_valid());
			// queuedItem.paused: ProductionQueue.PauseProduction pauses a garrisoned trainer's front
			// item; a started item that is no longer at the front is paused too (as ResearchProgress).
			const bool paused = !ghost && (qi == 0 ? trainer_garrisoned_ : (po.started || po.timer > 0.0f));
			// "stretched:color:0 0 0 127:grayscale:" while paused, "grayscale:" for a ghost.
			slot.icon->set_material(ghost || paused ? Ref<Material>(grayscale_) : Ref<Material>());
			slot.icon->set_self_modulate(paused ? Color(0.5f, 0.5f, 0.5f) : Color(1, 1, 1));
			slot.paused->set_visible(paused);

			String tip = name;
			if (ghost) tip += "\n" + rtso::tr0("The auto-queue will try to train this item later.");
			// Trainer.js Item.Start: the population the batch at the front still lacks.
			if (!ghost && qi == 0 && sim && player < rtso::MAX_PLAYERS && !po.started && !po.is_tech() && po.unit_type != rtso::UNIT_TYPE_NONE) {
				const int needed = sim->order_pop(po, player) - (sim->pop_max(player) - sim->pop_current(player));
				if (needed > 0)
					tip += "\n" + rtso::objection_font(rtso::tr0("Insufficient population capacity:")) + "\n" +
							rtso::sprintf0(rtso::tr0("%(population)s %(neededSlots)s"),
									{{"population", rtso::resource_icon("population")}, {"neededSlots", String::num_int64(needed)}});
			}
			if (!slot.kind.is_empty()) tip += "\n" + rtso::tr0("Right-click for more information.");
			if (paused) tip += "\n" + rtso::tr0("This item is paused."); // selection_panels.js:616, after it
			slot.panel->set_tooltip_text(tip);

			slot.chip->set_text(po.count > 1 ? String::num_int64(po.count) : String());
			slot.chip->set_visible(po.count > 1);

			if (ghost) {
				slot.progress->set_color(Color(0.0f, 150 / 255.0f, 250 / 255.0f, 50 / 255.0f));
				slot.progress->set_position(Vector2(2, 3));
				slot.progress->set_size(Vector2(28, 30));
				slot.progress->set_visible(true);
			} else {
				// queuedItem.progress: the front item's, and a paused item keeps what it had.
				const bool shows = qi == 0 || paused;
				const float progress = shows && po.duration > 0.0f ? std::clamp(po.timer / po.duration, 0.0f, 1.0f) : 0.0f;
				if (q == 0) {
					const int left = (int)std::ceil(std::max(po.duration - po.timer, 0.0f));
					time_remaining_->set_text(vformat("%d:%02d", left / 60, left % 60)); // countdown format "m:ss"
				}
				slot.progress->set_color(Color(0.0f, 1.0f, 0.0f, 128 / 255.0f));
				slot.progress->set_position(Vector2(2, 3 + 30.0f * progress));
				slot.progress->set_size(Vector2(28, 30.0f * (1.0f - progress)));
				slot.progress->set_visible(shows);
			}
		}
	}

	void clear() {
		if (production_icon_) production_icon_->set_visible(false);
		for (int q = 0; q < kSlots; ++q) hide_slot(queue_slots_[q]);
	}

	// A left click removes the item (removeFromProductionQueue), a right click opens
	// its Template Viewer (onPressRight showTemplateDetails); a ghost only shows it.
	void handle_gui_input(const godot::Ref<godot::InputEvent> &event, int slot_idx, godot::Viewport *vp) {
		using namespace godot;
		Ref<InputEventMouseButton> mb = event;
		if (mb.is_null() || !mb->is_pressed() || slot_idx < 0 || slot_idx >= kSlots) return;
		const QueueSlotUI &slot = queue_slots_[slot_idx];
		if (mb->get_button_index() == MOUSE_BUTTON_LEFT) {
			if (slot.queue_index >= 0) cancel(slot.queue_index, slot.producer);
			if (vp) vp->set_input_as_handled();
		} else if (mb->get_button_index() == MOUSE_BUTTON_RIGHT) {
			if (!slot.kind.is_empty() && owner_) owner_->call("_on_template_details_input", event, slot.kind, slot.id, slot.civ);
			if (vp) vp->set_input_as_handled();
		}
	}

	// Tests: the slot's tooltip, count and whether it is an auto-queue ghost.
	godot::Dictionary debug_slot(int i) const {
		godot::Dictionary d;
		if (i < 0 || i >= kSlots || !queue_slots_[i].panel) return d;
		const QueueSlotUI &s = queue_slots_[i];
		d["visible"] = s.panel->is_visible();
		d["tooltip"] = s.panel->get_tooltip_text();
		d["count"] = s.chip->get_text();
		d["ghost"] = s.panel->is_visible() && s.queue_index < 0;
		d["paused"] = s.panel->is_visible() && s.paused->is_visible();
		d["producer"] = (int64_t)s.producer;
		d["kind"] = s.kind;
		d["id"] = s.id;
		d["time"] = production_icon_ && production_icon_->is_visible() ? time_remaining_->get_text() : godot::String();
		return d;
	}

	// Removes queue item `slot_idx` of `producer` (0: the selected structure or trainer unit).
	void cancel(int slot_idx, uint32_t producer = 0) {
		if (!host_) return;
		uint32_t bld_id = producer != 0 ? producer : host_->ui_selected_building_id();
		rtso::Simulation *sim = host_->ui_sim();
		if (bld_id == 0 && trainer_unit_ != 0 && sim) {
			if (host_->turn_cmds())
				host_->post_lockstep((uint8_t)rtso::NetCommandType::NET_CMD_TRAIN_FROM_UNIT, nullptr, 0, (float)slot_idx, 0,
						trainer_unit_, 1);
			else if (!sim->cmd_cancel_unit_train(host_->local_player(), trainer_unit_, (size_t)slot_idx))
				return;
			host_->play_sfx("res://assets/sounds/ui/cancel.ogg");
			return;
		}
		if (bld_id != 0 && sim) {
			if (host_->turn_cmds()) {
				host_->post_lockstep(20, nullptr, 0, 0, 0, bld_id, (uint8_t)slot_idx);
				host_->play_sfx("res://assets/sounds/ui/cancel.ogg");
				return;
			}
			if (sim->cmd_cancel_train(host_->local_player(), bld_id, (size_t)slot_idx)) {
				host_->play_sfx("res://assets/sounds/ui/cancel.ogg");
				host_->update_command_panel();
				host_->update_inspector();
			}
		}
	}

private:
	static void hide_slot(QueueSlotUI &s) {
		s.queue_index = -1;
		s.producer = 0;
		s.kind = godot::String();
		if (!s.panel) return;
		s.panel->set_visible(false);
		s.icon->set_visible(false);
		s.progress->set_visible(false);
		s.chip->set_visible(false);
		s.paused->set_visible(false);
	}

	godot::HBoxContainer *queue_bar_ = nullptr;
	godot::TextureRect *production_icon_ = nullptr;
	godot::Label *time_remaining_ = nullptr;
	godot::Ref<godot::ShaderMaterial> grayscale_;
	godot::Object *owner_ = nullptr;
	static constexpr int kSlots = 16; // PRODUCTION_QUEUE_MAX
	QueueSlotUI queue_slots_[kSlots];
	RtsoProductionQueueHost *host_ = nullptr;
	uint32_t trainer_unit_ = 0; // the unit whose queue is shown (update_unit), else 0
	bool trainer_garrisoned_ = false; // that unit is inside a holder: its queue is paused
};

#endif
