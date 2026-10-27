// RtsoMain's feedback: session notices (gui/session/messages.js), civ voice lines and the idle
// unit hotkeys (P4.6, split out of main_node.cpp).

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
#include "obstruction_snap.h"
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
#include <cmath>
#include <sstream>
using namespace godot;
using rtso::sprintf0;

void RtsoMain::show_session_notices(const std::vector<rtso::Simulation::SessionNotice> &notices) {
	if (notices.empty() || !sim || !chat_window_.is_ready()) return; // replays too (session.js handleNotifications)
	using N = rtso::Simulation::SessionNotice;
	// The time notifications follow the viewed player (GetTimeNotifications(g_ViewedPlayer));
	// the chat lines below go by the seat and g_IsObserver (ChatMessageFormatSimulation),
	// and a replay viewer has no seat (Engine.GetPlayerID() == -1).
	const uint8_t me = local_player();
	const bool replay = turn_mgr_.is_replaying();
	const uint8_t seat = replay ? (uint8_t)0xFF : seat_player();
	const bool observer = replay || observing_seat();
	auto mutual_ally = [&](uint8_t a, uint8_t b) {
		return a < rtso::MAX_PLAYERS && b < rtso::MAX_PLAYERS && sim->get_diplomacy(a, b) == rtso::Simulation::DIPLO_ALLY &&
				sim->get_diplomacy(b, a) == rtso::Simulation::DIPLO_ALLY;
	};
	// getLocalizedResourceAmounts: "100 food, 50 wood and 200 metal".
	// tooltips.js: "%(amount)s %(resourceType)s" each, the last joined by
	// "%(previousAmounts)s and %(lastAmount)s", the others by ", ".
	auto amounts_text = [this](const int32_t *amounts, const uint8_t *types, int n) {
		// resourceNameWithinSentence: the resource's name in the "withinSentence"
		// context; English keeps it lower case ("200 food, 100 wood and 300 metal").
		static const char *kNames[rtso::RES_COUNT] = {"Wood", "Metal", "Stone", "Food"}; // RES_* order
		PackedStringArray parts;
		for (int i = 0; i < n; ++i)
			if (amounts[i] > 0 && types[i] < rtso::RES_COUNT) {
				const String name = tr(kNames[types[i]]);
				parts.push_back(sprintf0(tr("%(amount)s %(resourceType)s"), {{"amount", String::num_int64(amounts[i])},
						{"resourceType", name == kNames[types[i]] ? name.to_lower() : name}}));
			}
		if (parts.size() < 2) return parts.is_empty() ? String() : parts[0];
		const String last = parts[parts.size() - 1];
		parts.remove_at(parts.size() - 1);
		return sprintf0(tr("%(previousAmounts)s and %(lastAmount)s"),
				{{"previousAmounts", String(tr(", ")).join(parts)}, {"lastAmount", last}});
	};
	for (const N &n : notices) {
		if (n.kind == N::TEXT && n.tech == N::TEXT_INSUFFICIENT_RESOURCES) {
			// Player.js SubtractResourcesOrNotify: "Insufficient resources - %(resourceAmount1)s
			// %(resourceType1)s, ..." in resource order (food, wood, stone, metal), 10 s centre top.
			if (n.player != me) continue;
			static const uint8_t kOrder[] = {rtso::RES_FOOD, rtso::RES_WOOD, rtso::RES_STONE, rtso::RES_GOLD};
			static const char *kLower[rtso::RES_COUNT] = {"wood", "metal", "stone", "food"}; // RES_* order
			PackedStringArray parts;
			for (uint8_t r : kOrder)
				if (n.amounts[r] > 0) parts.push_back(String::num_int64(n.amounts[r]) + " " + tr(kLower[r]));
			if (parts.is_empty()) continue;
			const uint32_t now = (uint32_t)std::lround(sim->game_time() * 1000.0f);
			text_notices_.push_back({tr("Insufficient resources - ") + String(", ").join(parts), now + 10000});
			continue;
		}
		if (n.kind == N::TEXT) {
			// GuiInterface AddTimeNotification: 10 s in the centre, for the player it names.
			static const char *kText[] = {"The production queue is full.", "Entity is being upgraded. Cannot start production.",
					"Could not auto-queue unit, de-activating.", "Can't find free space to spawn trained units.",
					"Entity is producing. Cannot start upgrading.", "Unable to unload unit(s).",
					"Some unit(s) can't go back to work", "There are no bribable units"};
			if (n.player != me || n.tech >= sizeof(kText) / sizeof(kText[0])) continue;
			const uint32_t now = (uint32_t)std::lround(sim->game_time() * 1000.0f);
			if (n.tech == N::TEXT_SPAWN_BLOCKED) {
				// Trainer.js spawnNotified: once until the batch spawns. The sim retries each
				// turn, so a building heard from within the last 2 s is the same blockage.
				auto it = spawn_notified_.find((uint32_t)n.amounts[0]);
				const bool repeat = it != spawn_notified_.end() && now - it->second <= 2000;
				spawn_notified_[(uint32_t)n.amounts[0]] = now;
				if (repeat) continue;
			}
			text_notices_.push_back({tr(kText[n.tech]), now + 10000});
			continue;
		}
		if (n.kind == N::CHEAT) {
			// messages.js aichat: the player's own chat line, so everyone sees the cheating.
			chat_window_.append_kind(String("<") + colored_player_name(n.player) + "> " +
							tr(n.tech == 0 ? "(Cheat - reveal map)" : n.tech == 1 ? "(Cheat - promoted units)" : "(Cheat - control all units)") + "\n",
					RtsoChatWindow::message_bits("", true));
			continue;
		}
		String line;
		if (n.kind == N::TRIBUTE) {
			// GetTributableCodes order: food, wood, stone, metal.
			static const uint8_t kOrder[4] = {rtso::RES_FOOD, rtso::RES_WOOD, rtso::RES_STONE, rtso::RES_GOLD};
			int32_t a[4];
			for (int i = 0; i < 4; ++i) a[i] = n.amounts[kOrder[i]];
			const String what = amounts_text(a, kOrder, 4);
			if (n.other == seat)
				line = sprintf0(tr("%(player)s has sent you %(amounts)s."), {{"player", colored_player_name(n.player)}, {"amounts", what}});
			else if (n.player == seat)
				line = sprintf0(tr("You have sent %(player2)s %(amounts)s."), {{"player2", colored_player_name(n.other)}, {"amounts", what}});
			else if (user_opts_.tribute_notice &&
					(observer || (sim->teams_locked() && mutual_ally(n.player, seat) && mutual_ally(n.other, seat))))
				line = sprintf0(tr("%(player)s has sent %(player2)s %(amounts)s."),
						{{"player", colored_player_name(n.player)}, {"player2", colored_player_name(n.other)}, {"amounts", what}});
		} else if (n.kind == N::BARTER) {
			if (observer && user_opts_.barter_notice) {
				const String given = amounts_text(&n.amounts[0], &n.other, 1);
				const String gained = amounts_text(&n.amounts[1], &n.tech, 1);
				line = sprintf0(tr("%(player)s bartered %(amountGiven)s for %(amountGained)s."),
						{{"player", colored_player_name(n.player)}, {"amountGiven", given}, {"amountGained", gained}});
			}
		} else if (n.kind == N::PHASE_STARTED || n.kind == N::PHASE_ABORTED || n.kind == N::PHASE_COMPLETED) {
			if (user_opts_.phase_notice == 0 || (n.player != seat && !observer && !mutual_ally(n.player, seat))) continue;
			// getEntityNames(GetTechnologyData(phaseName, civ)): "Kōmopolis (Town Phase)".
			const char *specific = rtso::tech_specific_name(n.tech, sim->player_civ(n.player));
			const String phase = rtso::entity_names(String::utf8(rtso::tech_info(n.tech).name),
					specific ? String::utf8(specific) : String());
			const String player = colored_player_name(n.player);
			if (n.kind == N::PHASE_COMPLETED)
				line = sprintf0(tr("%(player)s has reached the %(phaseName)s."), {{"player", player}, {"phaseName", phase}});
			else if (user_opts_.phase_notice == 2 && n.kind == N::PHASE_STARTED)
				line = sprintf0(tr("%(player)s is advancing to the %(phaseName)s."), {{"player", player}, {"phaseName", phase}});
			else if (user_opts_.phase_notice == 2)
				line = sprintf0(tr("The %(phaseName)s of %(player)s has been aborted."), {{"player", player}, {"phaseName", phase}});
		} else if (n.kind == N::DIPLOMACY) {
			// ChatMessageFormatSimulation.diplomacy's three message sets.
			static const char *kObserver[3] = {"%(player)s is now allied with %(player2)s.",
					"%(player)s is now neutral with %(player2)s.", "%(player)s is now at war with %(player2)s."};
			static const char *kMine[3] = {"You are now allied with %(player)s.",
					"You are now neutral with %(player)s.", "You are now at war with %(player)s."};
			static const char *kTheirs[3] = {"%(player)s is now allied with you.",
					"%(player)s is now neutral with you.", "%(player)s is now at war with you."};
			const int w = n.tech <= rtso::Simulation::DIPLO_ENEMY ? n.tech : 2;
			if (observer)
				line = sprintf0(tr(kObserver[w]), {{"player", colored_player_name(n.player)}, {"player2", colored_player_name(n.other)}});
			else if (n.player == seat) line = sprintf0(tr(kMine[w]), {{"player", colored_player_name(n.other)}});
			else if (n.other == seat) line = sprintf0(tr(kTheirs[w]), {{"player", colored_player_name(n.player)}});
		} else if (n.kind == N::DEFEATED || n.kind == N::WON || n.kind == N::LOST) {
			// ChatMessageFormatSimulation.playerstate, shown to everyone: the defeat reasons of
			// Conquest*.js, Regicide.js, Commands.js "resign" and TriggerHelper.DefeatPlayer, and
			// EndGameManager's, WonderVictory.js's and CaptureTheRelic.js's win lines.
			static const char *kWhy[] = {"lost all critical units and structures", "lost all units", "lost all structures",
					"lost all civic centers", "lost hero", "", "objective", "last players alive", "wonder victory",
					"Capture the Relic", "game mode", "lost civic center", "treasure collected", "lost all structures and critical units",
					"lost all civic centers and critical units and structures", "cheat"};
			const String why = n.tech < sizeof(kWhy) / sizeof(kWhy[0]) ? String(kWhy[n.tech]) : String("game mode");
			if (n.kind == N::DEFEATED) {
				line = sprintf0(tr(n.tech == N::END_RESIGNED ? String("%(player)s has resigned.")
						: "%(player)s has been defeated (" + why + ")."), {{"player", colored_player_name(n.player)}});
			} else {
				std::vector<String> names;
				for (uint8_t p = 0; p < rtso::MAX_PLAYERS; ++p)
					if ((n.other >> p) & 1) names.push_back(colored_player_name(p));
				if (names.empty()) continue;
				// translatePlural of the sim's message; the enumeration comma is translated too.
				String list;
				for (size_t i = 0; i + 1 < names.size(); ++i) list += (i ? tr(", ") : String()) + names[i];
				const bool one = names.size() == 1;
				const String reason = n.kind == N::WON && n.tech == N::END_LAST_ALIVE && one ? String("last player alive") : why;
				const String msg = (one ? String("%(lastPlayer)s ") : String("%(players)s and %(lastPlayer)s ")) +
						(n.kind == N::WON ? (one ? "has won (" : "have won (") : (one ? "has been defeated (" : "have been defeated (")) + reason + ").";
				line = sprintf0(tr(msg), {{"players", list}, {"lastPlayer", names.back()}});
			}
		}
		if (!line.is_empty()) chat_window_.append_line(line + "\n");
	}
}

void RtsoMain::play_civ_voice(const char *action, uint32_t unit_id) {
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
	const uint32_t first = unit_id ? unit_id : (uint32_t)(int64_t)selected_ids[0];
	auto uv = sim->units();
	for (size_t i = 0; i < uv.count; ++i) {
		if (uv.id[i] != first) continue;
		// A male Civilian speaks with the civ's male citizen voice (rtso::civilian_is_male).
		const uint8_t voice_type = rtso::civilian_is_male(uv.tmpl[i], uv.id[i]) ? (uint8_t)rtso::UNIT_VILLAGER : uv.unit_type[i];
		session_audio_.play_voice(sim->player_civ(uv.team[i]), voice_type, act);
		return;
	}
}

// 0 A.D. findIdleUnit (gui/session/input.js:1659) with the class lists from
// session.js g_WorkerTypes / g_MilitaryTypes and hotkeys/misc.xml, and
// GuiInterface FindIdleUnits / IdleUnitFilter: selects the next idle unit after
// the last one found and centres the camera on it. The class list is walked in
// order from the class of the last find (all idle civilians before the
// traders, and so on), units garrisoned or on a turret don't count, and once a
// class runs out the next one follows. selection.add (Shift) appends;
// selection.offscreen (Alt) selects every idle unit of the kind at once.
void RtsoMain::find_idle_unit(IdleKind kind) {
	if (!sim) return;
	std::vector<rtso::TechClasses> classes;
	switch (kind) {
		case IDLE_WORKER: classes = {rtso::TC_CIVILIAN, rtso::TC_TRADER, rtso::TC_FISHINGBOAT, rtso::TC_CITIZEN}; break;
		case IDLE_WARRIOR: classes = {rtso::TC_MELEE, rtso::TC_RANGED}; break;
		case IDLE_BUILDER: classes = {rtso::TechClasses(~0ull, ~0ull)}; break; // "Builder": unit_can_build below
		case IDLE_ANY: classes = {rtso::TechClasses(~0ull, ~0ull)}; break;
	}
	const size_t n = classes.size();
	const bool append = rtso::hotkeys::held("selection.add");
	const bool select_all = rtso::hotkeys::held("selection.offscreen");
	if (select_all || kind != last_idle_kind_) {
		last_idle_unit_ = 0;
		idle_class_index_ = 0;
	}
	last_idle_kind_ = kind;

	// Bucket of each idle unit: the first class it matches, counted from the
	// current class; current-class units at or before the last find go last.
	auto uview = sim->units();
	size_t best = SIZE_MAX, best_bucket = SIZE_MAX, best_index = 0;
	std::vector<size_t> idle;
	for (size_t i = 0; i < uview.count; ++i) {
		if (!uview.alive[i] || uview.team[i] != local_player() || uview.action_state[i] != rtso::ACTION_IDLE) continue;
		if (sim->turret_holder_of(uview.id[i])) continue;
		if (append && selected_ids.has((int32_t)uview.id[i])) continue;
		// The roster row's Identity classes (a female citizen is a villager by UnitType).
		const uint16_t tmpl = uview.tmpl[i];
		const rtso::TechClasses tc = tmpl < rtso::CIV_UNIT_TEMPLATE_COUNT ? rtso::civ_unit_template(tmpl).tech_classes
				: rtso::unit_tech_classes(uview.unit_type[i]);
		// hotkeys/misc.xml findIdleUnit(["Builder"]): the Builder mixin (infantry, women,
		// civilians, slaves), not citizen cavalry.
		if (kind == IDLE_BUILDER && !rtso::unit_can_build(uview.unit_type[i], tmpl)) continue;
		size_t bucket = n;
		for (size_t k = 0; k < n; ++k) {
			if (tc & classes[(idle_class_index_ + k) % n]) { bucket = k; break; }
		}
		if (bucket == n) continue;
		idle.push_back(i);
		const size_t index = bucket;
		if (bucket == 0 && last_idle_unit_ && uview.id[i] <= last_idle_unit_) bucket = n;
		if (bucket < best_bucket || (bucket == best_bucket && uview.id[i] < uview.id[best])) {
			best = i;
			best_bucket = bucket;
			best_index = index;
		}
	}
	if (idle.empty()) {
		last_idle_unit_ = 0;
		idle_class_index_ = 0;
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
	last_idle_unit_ = uview.id[best];
	idle_class_index_ = (idle_class_index_ + best_index) % n;
	selected_ids.push_back((int32_t)uview.id[best]);
	if (cam_rig) cam_rig->set_target_pos(Vector3(uview.pos_x[best], 0, uview.pos_z[best]));
	play_civ_voice("select");
}

// The template's SoundGroups/select (gaia/fauna_*.xml): the lion group for the big hunters,
// horses and donkeys neigh, elephants trumpet, dogs bark. The other animals' select groups
// (sheep, goats, cattle, camels, pigs, chickens, peacocks) have no select files in assets yet.
void RtsoMain::play_fauna_select(uint8_t fauna_type) {
	const std::string n = rtso::fauna_type_info(fauna_type).template_name;
	auto has = [&](const char *s) { return n.find(s) != std::string::npos; };
	if (has("dog_")) session_audio_.play_group("voice/global", "civ_dog_bark", 0.8f);
	else if (has("horse") || has("donkey")) session_audio_.play_group("actor/fauna/animal", "horse_select");
	else if (has("elephant")) session_audio_.play_group("actor/fauna/animal", "elephant_select");
	else if (has("lion") || has("tiger") || has("bear") || has("crocodile") || has("hippopotamus") || has("rhinoceros"))
		session_audio_.play_group("actor/fauna/animal", "lion_select");
}

// selection.js addList: the first added entity's select sound plays when its owner is Gaia
// (or the viewer controls it): trees, fruit, mines, treasures and wild animals.
void RtsoMain::play_inspected_select() {
	if (!sim) return;
	const uint32_t id = inspector_.id();
	switch (inspector_.kind()) {
	case RtsoInspectorView::INSPECT_RESOURCE: {
		auto rv = sim->resources();
		for (size_t r = 0; r < rv.count; ++r) {
			if (rv.id[r] != id) continue;
			if (rv.subtype[r] == rtso::RES_SUB_MEAT || rv.subtype[r] == rtso::RES_SUB_FISH) break; // carcasses, fish: none
			static const char *const kSel[] = {"sel_tree", "sel_metal", "sel_stone", "sel_fruit"};
			if (rv.type[r] < 4) session_audio_.play_group("interface/select/resource", kSel[rv.type[r]]);
			break;
		}
		break;
	}
	case RtsoInspectorView::INSPECT_TREASURE: session_audio_.play_group("interface/select/building", "sel_universal"); break;
	case RtsoInspectorView::INSPECT_FAUNA: {
		auto fv = sim->fauna();
		for (size_t f = 0; f < fv.count; ++f)
			if (fv.id[f] == id) {
				if (fv.owner[f] >= rtso::MAX_PLAYERS || fv.owner[f] == local_player()) play_fauna_select(fv.fauna_type[f]);
				break;
			}
		break;
	}
	case RtsoInspectorView::INSPECT_BUILDING:
		for (const auto &b : sim->buildings())
			if (b.id == id) {
				if (b.team >= rtso::MAX_PLAYERS) session_audio_.play_building_select(b.type);
				break;
			}
		break;
	default: break;
	}
}

void RtsoMain::play_select_added(const PackedInt32Array &before_units, const std::vector<uint32_t> &before_structs) {
	if (!sim) return;
	for (int s = 0; s < selected_ids.size(); ++s)
		if (!before_units.has(selected_ids[s])) {
			const uint32_t id = (uint32_t)(int64_t)selected_ids[s];
			auto uv = sim->units();
			for (size_t i = 0; i < uv.count; ++i)
				if (uv.id[i] == id) {
					play_civ_voice("select", id);
					return;
				}
			auto fv = sim->fauna(); // own livestock
			for (size_t f = 0; f < fv.count; ++f)
				if (fv.id[f] == id) {
					play_fauna_select(fv.fauna_type[f]);
					break;
				}
			return;
		}
	for (uint32_t id : selected_structures())
		if (std::find(before_structs.begin(), before_structs.end(), id) == before_structs.end()) {
			for (const auto &b : sim->buildings())
				if (b.id == id) {
					session_audio_.play_building_select(b.type);
					break;
				}
			return;
		}
}
