#ifndef RTSO_PRODUCTION_QUEUE_H
#define RTSO_PRODUCTION_QUEUE_H

// P4 stage-2 leaf widget: 16-slot visual production queue & rally-point query
// (0 A.D. gui/session/selection_panels.js + production_queue.xml + CCmpRallyPoint parity).
// Owns the 16 queue slot panels, queue progress display, slot GUI input & cancel
// dispatch, and the rally point query helper.

#include <godot_cpp/classes/control.hpp>
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

#include <cstddef>
#include <cstdint>

#include "../network/net_types.h"
#include "../simulation/simulation.h"
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

inline void update_rally_marker(godot::MeshInstance3D *rally_flag, const rtso::Simulation *sim, uint32_t building_id, uint8_t team = 0) {
	if (!rally_flag) return;
	RallyPointInfo rpi = get_rally_point(sim, building_id, team);
	if (rpi.has_rally) {
		rally_flag->set_global_position(godot::Vector3(rpi.x, 0.8f, rpi.z));
		rally_flag->set_visible(true);
	} else {
		rally_flag->set_visible(false);
	}
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
		godot::ProgressBar *progress = nullptr;
		godot::Label *chip = nullptr;
	};

	void build(godot::Object *owner, godot::Control *parent_panel, RtsoProductionQueueHost *host) {
		using namespace godot;
		host_ = host;

		queue_bar_ = memnew(HBoxContainer);
		queue_bar_->set_position(Vector2(10, 122));
		queue_bar_->set_size(Vector2(552, 54));
		queue_bar_->add_theme_constant_override("separation", 2);
		parent_panel->add_child(queue_bar_);

		// 0 A.D. shows the whole queue (selection_panels.js Queue: 16 items).
		for (int q = 0; q < kSlots; ++q) {
			Panel *sp = memnew(Panel);
			sp->set_custom_minimum_size(Vector2(32, 36));

			Ref<StyleBoxFlat> slot_sb;
			slot_sb.instantiate();
			slot_sb->set_bg_color(Color(0.09f, 0.11f, 0.15f, 0.90f));
			slot_sb->set_border_width_all(1);
			slot_sb->set_border_color(Color(0.35f, 0.38f, 0.48f, 0.70f));
			slot_sb->set_corner_radius_all(4);
			sp->add_theme_stylebox_override("panel", slot_sb);

			TextureRect *ico = memnew(TextureRect);
			ico->set_position(Vector2(2, 2));
			ico->set_size(Vector2(28, 26));
			ico->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			ico->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
			ico->set_visible(false);
			sp->add_child(ico);

			ProgressBar *pb = memnew(ProgressBar);
			pb->set_position(Vector2(2, 30));
			pb->set_size(Vector2(28, 4));
			pb->set_show_percentage(false);

			Ref<StyleBoxFlat> pb_fg;
			pb_fg.instantiate();
			pb_fg->set_bg_color(Color(0.95f, 0.75f, 0.20f));
			pb_fg->set_corner_radius_all(1);
			pb->add_theme_stylebox_override("fill", pb_fg);
			pb->set_visible(false);
			sp->add_child(pb);

			Label *chip = memnew(Label);
			chip->set_position(Vector2(8, 1));
			chip->set_size(Vector2(22, 12));
			chip->add_theme_font_size_override("font_size", 9);
			chip->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
			chip->add_theme_color_override("font_color", Color(1.0f, 0.9f, 0.3f));
			chip->set_visible(false);
			sp->add_child(chip);

			sp->connect("gui_input", Callable(owner, "_on_queue_slot_gui_input").bind(q));

			queue_slots_[q].panel = sp;
			queue_slots_[q].icon = ico;
			queue_slots_[q].progress = pb;
			queue_slots_[q].chip = chip;
			queue_bar_->add_child(sp);
		}
	}

	void update(const rtso::Simulation::Building *found_b) {
		trainer_unit_ = 0;
		show(found_b ? &found_b->queue : nullptr, found_b ? found_b->type : 0);
	}

	// A unit's Trainer queue (triremes, Cyrus II); a click cancels through the unit.
	void update_unit(uint32_t unit_id, const std::vector<rtso::ProductionOrder> *queue) {
		trainer_unit_ = unit_id;
		if (queue) show(queue, rtso::BLD_TYPE_COUNT);
		else clear();
	}

	void show(const std::vector<rtso::ProductionOrder> *queue, uint8_t researcher) {
		using namespace godot;
		if (queue) {
			for (int q = 0; q < kSlots; ++q) {
				// Like 0.28's Queue panel, only queued items have a slot.
				queue_slots_[q].panel->set_visible(q < (int)queue->size());
				if (q < (int)queue->size()) {
					const rtso::ProductionOrder &po = (*queue)[q];
					// A technology shows its researcher's portrait; a batch shows its size.
					// A unit's research shows the structure that also researches it (BLD_TYPE_COUNT).
					const uint8_t rb = researcher < rtso::BLD_TYPE_COUNT ? researcher : rtso::tech_info(po.tech).researcher;
					queue_slots_[q].icon->set_texture(po.is_tech()
							? rtso::load_ui_texture("res://assets/textures/ui/portraits/" + String(rtso::building_type_info(rb).portrait_id) + ".png")
							: rtso::portraits::unit(po.tmpl, po.unit_type));
					queue_slots_[q].icon->set_visible(true);
					queue_slots_[q].panel->set_tooltip_text(po.is_tech() ? String::utf8(rtso::tech_info(po.tech).name)
							: String::utf8(rtso::unit_type_info(po.unit_type < rtso::UNIT_TYPE_COUNT ? po.unit_type : 0).name));
					queue_slots_[q].chip->set_text(po.is_tech() ? String("T") : (po.count > 1 ? "x" + String::num_int64(po.count) : String()));
					queue_slots_[q].chip->set_visible(po.is_tech() || po.count > 1);

					if (q == 0) {
						queue_slots_[q].progress->set_visible(true);
						queue_slots_[q].progress->set_max((*queue)[0].duration);
						queue_slots_[q].progress->set_value((*queue)[0].timer);
					} else {
						queue_slots_[q].progress->set_visible(false);
					}
				} else {
					queue_slots_[q].icon->set_visible(false);
					queue_slots_[q].progress->set_visible(false);
					queue_slots_[q].chip->set_visible(false);
				}
			}
		} else {
			clear();
		}
	}

	void clear() {
		for (int q = 0; q < kSlots; ++q) {
			queue_slots_[q].panel->set_visible(false);
			queue_slots_[q].icon->set_visible(false);
			queue_slots_[q].progress->set_visible(false);
			queue_slots_[q].chip->set_visible(false);
		}
	}

	void handle_gui_input(const godot::Ref<godot::InputEvent> &event, int slot_idx, godot::Viewport *vp) {
		using namespace godot;
		Ref<InputEventMouseButton> mb = event;
		if (mb.is_valid() && mb->is_pressed()) {
			if (mb->get_button_index() == MOUSE_BUTTON_RIGHT || mb->get_button_index() == MOUSE_BUTTON_LEFT) {
				cancel(slot_idx);
				if (vp) vp->set_input_as_handled();
			}
		}
	}

	void cancel(int slot_idx) {
		if (!host_) return;
		uint32_t bld_id = host_->ui_selected_building_id();
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
	godot::HBoxContainer *queue_bar_ = nullptr;
	static constexpr int kSlots = 16; // PRODUCTION_QUEUE_MAX
	QueueSlotUI queue_slots_[kSlots];
	RtsoProductionQueueHost *host_ = nullptr;
	uint32_t trainer_unit_ = 0; // the unit whose queue is shown (update_unit), else 0
};

#endif
