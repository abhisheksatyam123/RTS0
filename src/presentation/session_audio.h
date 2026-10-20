#ifndef RTSO_SESSION_AUDIO_H
#define RTSO_SESSION_AUDIO_H

// In-game sound events, ported from 0 A.D. (GPL-2.0-or-later code, CC BY-SA 3.0
// sounds in assets/audio/):
// - Sound groups (audio/**/*.xml): a group is every file in its folder with the
//   group's name prefix, picked at random (RandOrder), with the group's Gain.
// - Alarms heard by the owner: research done (alarm_techcomplete), new phase
//   (alarm_phase), victory, defeat, no idle unit, invalid placement, and the
//   "trained" sound group of template_unit_{infantry,cavalry,support_female_citizen,...}.
// - Structures: complete_<structure> when building finishes (Foundation.js
//   PlaySound("constructed")), sel_<structure> when the player selects one.
// - Unit voices (template_unit_*.xml SoundGroups voice/{lang}/civ/civ_{phenotype}_*):
//   per unit phenotype (male/female) and the civ's language.
// - Deaths (Health.js PlaySound("death"), attack_sound_data.h): each unit's and animal's
//   template death group (a human's {phenotype}_death, a horse's, a camel's, a dog's
//   generic animal death, a ship's warship_death; siege has none), heard where it
//   happens while the viewer sees that spot, every death of the frame.
// - BattleDetection (simulation/components/BattleDetection.js) fed with the
//   damage the player deals to other players, which drives the music state.
// - Combat (template Sound/SoundGroups, attack_sound_data.h): a unit's attack_melee
//   group on each melee hit, its attack_ranged group as it shoots (a structure's
//   once per round of arrows, BuildingAI.js FireArrows) and its attack_impact_ranged
//   group where the projectile lands (DelayedDamage.js), its attack_slaughter group
//   on each Slaughter strike at a domestic animal, and an animal's own attack_melee
//   on each bite; heard where they happen while the viewer sees that spot.

#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../simulation/simulation.h"
#include "../simulation/tech_data.h"
#include "attack_detection.h"
#include "music_manager.h" // load_audio

namespace rtso {

#include "attack_sound_data.h"

inline const char *kAudioRoot = "res://assets/audio/";

// Every .ogg in assets/audio/<dir>/ whose name starts with prefix (a sound group).
inline const std::vector<godot::String> &sound_group(const char *dir, const char *prefix) {
	static auto &cache = *new std::unordered_map<std::string, std::vector<godot::String>>(); // leaked: no Godot dtor after exit
	const std::string key = std::string(dir) + "|" + prefix;
	auto it = cache.find(key);
	if (it != cache.end()) return it->second;
	std::vector<godot::String> &out = cache[key];
	const godot::String folder = godot::String(kAudioRoot) + dir + "/";
	const godot::String pre = godot::String(prefix).to_lower();
	godot::PackedStringArray files = godot::DirAccess::get_files_at(folder);
	for (int i = 0; i < files.size(); ++i) {
		godot::String f = files[i];
		if (f.ends_with(".import")) f = f.substr(0, f.length() - 7); // exported builds list imports
		if (!f.ends_with(".ogg") || !f.to_lower().begins_with(pre)) continue;
		const godot::String p = folder + f;
		bool dup = false;
		for (const auto &e : out) dup = dup || e == p;
		if (!dup) out.push_back(p);
	}
	return out;
}

// Template Identity/Lang -> voice folder. 0 A.D.'s templates set Lang only
// for the Romans (latin), Persians (persian), Kushites (napatan) and the
// Hellenes (greek); every other civ falls back to Identity.js GetLang's
// "greek", as 0 A.D. has no punic, celtic, iberian, sanskrit or chinese voices.
inline const char *voice_language(uint8_t civ) {
	switch (civ) {
		case CIV_ROMANS: return "latin";
		case CIV_PERSIANS: return "persian";
		case CIV_KUSHITES: return "napatan";
		default: return "greek";
	}
}

enum class VoiceKind : uint8_t { NONE, MALE, FEMALE, DOG, HORSE, ELEPHANT };

// Phenotype of a unit type: template_unit_support_female_citizen and archer
// maidens are female; siege, ships and traders have no voice in 0 A.D.
inline VoiceKind voice_kind(uint8_t unit_type) {
	switch (unit_type) {
		case UNIT_FEMALE_CITIZEN: case UNIT_ARCHER_MAIDEN: return VoiceKind::FEMALE;
		case UNIT_CATAPULT: case UNIT_SIEGE_RAM: case UNIT_BOLT_SHOOTER: case UNIT_TRIREME:
		case UNIT_FISHING_BOAT: case UNIT_MERCHANT_SHIP: return VoiceKind::NONE;
		case UNIT_WAR_DOG: return VoiceKind::DOG;
		case UNIT_WORKER_ELEPHANT: return VoiceKind::ELEPHANT;
		default: return VoiceKind::MALE;
	}
}

// 0 A.D. voice sound group names (select, order_walk, order_attack, ...) mapped
// to the line stems each language folder has, most specific first.
inline const std::vector<const char *> &voice_stems(const std::string &action) {
	static const std::unordered_map<std::string, std::vector<const char *>> m = {
		{"select", {"what_is_it", "hello", "my_lord", "my_king", "yes", "ready", "your_soldier", "Asyouwish"}},
		{"move", {"walk", "march", "forward", "I_will_go", "Imcoming", "by_your_order", "yes"}},
		{"attack", {"attack", "go_out_against", "fight", "to_war"}},
		{"build", {"build"}},
		{"repair", {"repair", "build"}},
		{"gather", {"gather", "work_land", "gather_together", "wood", "mine"}},
		// 0.28 collect_treasure: greek male As_you_wish, greek female and latin gather_together, persian/kushite gather.
		{"collect_treasure", {"Asyouwish", "gather_together", "gather"}},
		{"farm", {"work_land", "gather"}},
		{"fish", {"fish", "gather"}},
		{"hunt", {"hunt", "gather"}},
		{"herd", {"herd", "gather"}},
		{"garrison", {"garrison"}},
		{"heal", {"heal"}},
		{"retreat", {"retreat", "walk"}},
		{"trained", {"ready", "yes", "my_lord", "what_is_it", "hello"}},
	};
	static const std::vector<const char *> none;
	auto it = m.find(action);
	return it == m.end() ? none : it->second;
}

// A unit's death group: its roster template's, else its UnitType's generic template's;
// a human's {phenotype}_death by its phenotype (voice_kind).
inline uint8_t unit_death_sound(uint16_t tmpl, uint8_t unit_type) {
	const uint8_t g = tmpl < ATTACK_SOUND_ROSTER_ROWS ? roster_death_sound(tmpl) : generic_death_sound(unit_type);
	return g == ATTACK_SOUND_PHENOTYPE ? phenotype_death_sound(voice_kind(unit_type) == VoiceKind::FEMALE) : g;
}

// A unit's attack_melee, attack_ranged and attack_impact_ranged groups: its roster
// template's, else its UnitType's generic template's.
inline EntityAttackSounds unit_attack_sounds(uint16_t tmpl, uint8_t unit_type) {
	if (tmpl < ATTACK_SOUND_ROSTER_ROWS) return roster_attack_sounds()[tmpl];
	if (unit_type < ATTACK_SOUND_GENERIC_ROWS) return generic_attack_sounds()[unit_type];
	return {ATTACK_SOUND_NONE, ATTACK_SOUND_NONE, ATTACK_SOUND_NONE, ATTACK_SOUND_NONE};
}

// One Sound of group g at random (RandOrder) and its pitch (RandPitch); empty for
// ATTACK_SOUND_NONE.
template <class Rng>
inline godot::String pick_attack_sound(uint8_t g, Rng &rng, float *pitch) {
	*pitch = 1.0f;
	if (g == ATTACK_SOUND_NONE || g >= ATTACK_SOUND_GROUP_COUNT) return godot::String();
	const AttackSoundGroup &grp = attack_sound_group(g);
	int n = 0;
	while (grp.files[n]) ++n;
	if (n == 0) return godot::String();
	const float u = (float)(rng() % 1000) / 1000.0f;
	*pitch = grp.pitch_lo + (grp.pitch_hi - grp.pitch_lo) * u;
	return godot::String(kAudioRoot) + grp.dir + "/" + grp.files[rng() % n];
}

class RtsoSessionAudio {
public:
	// stream, where, volume (dB) and pitch scale.
	using SpatialFn = std::function<void(const godot::Ref<godot::AudioStream> &, godot::Vector3, float, float)>;

	void build(godot::Node *parent) {
		for (auto &p : pool_) {
			p = memnew(godot::AudioStreamPlayer);
			parent->add_child(p);
		}
		rng_.seed(std::random_device{}());
	}
	void set_spatial(SpatialFn fn) { spatial_ = std::move(fn); }

	// Plays a random member of a sound group, omnipresent (0 A.D. Omnipresent /
	// Distanceless sound groups). gain is the group's linear Gain.
	bool play_group(const char *dir, const char *prefix, float gain = 1.0f) {
		const auto &g = sound_group(dir, prefix);
		if (g.empty()) return false;
		return play_path(g[rng_() % g.size()], gain);
	}

	// A random member of a sound group heard at (x, z) on the map (positional groups).
	bool play_spatial_group(const char *dir, const char *prefix, float x, float z, float gain = 1.0f) {
		const auto &g = sound_group(dir, prefix);
		if (g.empty() || !spatial_) return false;
		const godot::String path = g[rng_() % g.size()];
		godot::Ref<godot::AudioStream> s = stream(path);
		if (s.is_null()) return false;
		spatial_(s, godot::Vector3(x, 0.5f, z), gain > 0.001f ? 20.0f * std::log10(gain) : -80.0f, 1.0f);
		last_played_ = path;
		return true;
	}

	bool play_path(const godot::String &path, float gain = 1.0f) {
		godot::Ref<godot::AudioStream> s = stream(path);
		if (s.is_null()) return false;
		godot::AudioStreamPlayer *p = pool_[next_++ % kPool];
		if (!p) return false;
		p->set_stream(s);
		// sound.uigain for interface sounds, sound.actiongain for the rest.
		p->set_bus(path.contains("/audio/interface/") ? "UI" : "Action");
		p->set_volume_db(gain > 0.001f ? 20.0f * std::log10(gain) : -80.0f);
		p->play();
		last_played_ = path;
		return true;
	}

	// The unit's own voice line for a 0 A.D. sound group (select, order_*,
	// trained). One voice at a time, like 0 A.D.'s Priority-ordered voice.
	bool play_voice(uint8_t civ, uint8_t unit_type, const std::string &action) {
		const VoiceKind kind = voice_kind(unit_type);
		if (kind == VoiceKind::NONE) return false;
		if (kind == VoiceKind::DOG) return play_group("voice/global", action == "select" ? "civ_dog_bark" : "civ_dog_move", 0.8f);
		if (kind == VoiceKind::HORSE) {
			const char *pre = action == "select" ? "horse_select" : action == "attack" ? "horse_attack" : "horse_select";
			return play_group("actor/fauna/animal", pre, 0.8f);
		}
		if (kind == VoiceKind::ELEPHANT) {
			const char *pre = action == "select" ? "elephant_select" : action == "attack" ? "elephant_attack" : "elephant_order";
			return play_group("actor/fauna/animal", pre, 0.8f);
		}
		const std::string dir = std::string("voice/") + voice_language(civ) + "/civ";
		const char *sex = kind == VoiceKind::FEMALE ? "civ_female_" : "civ_male_";
		for (const char *stem : voice_stems(action)) {
			const std::string pre = std::string(sex) + stem + "_";
			if (play_group(dir.c_str(), pre.c_str(), 1.0f)) return true;
		}
		// A language without this phenotype's line: borrow the other phenotype's,
		// then Greek's (the fullest set).
		const char *other = kind == VoiceKind::FEMALE ? "civ_male_" : "civ_female_";
		for (const char *stem : voice_stems(action))
			if (play_group(dir.c_str(), (std::string(other) + stem + "_").c_str(), 1.0f)) return true;
		if (std::string(voice_language(civ)) != "greek")
			for (const char *stem : voice_stems(action))
				if (play_group("voice/greek/civ", (std::string(sex) + stem + "_").c_str(), 1.0f)) return true;
		return false;
	}

	// sel_<structure> (interface/select/building).
	void play_building_select(uint8_t type) { play_group("interface/select/building", building_sound_name(type, true), 1.0f); }

	// template_player.xml SoundGroups, played by Player.OnGlobalPlayerDefeated to
	// each player: "defeated" when it falls, "defeated_ally" when an ally does,
	// "defeated_enemy" (alarm_alert_1) when an enemy does. There is no "won" group.
	void play_defeat() { play_group("interface/alarm", "alarmdefeat", 1.0f); }
	void play_defeated_ally() { play_group("interface/alarm", "alarm_defeated_ally", 1.0f); }
	void play_defeated_enemy() { play_group("interface/alarm", "alarmalert1", 1.0f); }
	// AlertRaiser alert_raise / alert_end: alarm_alert_0 / alarm_alert_1 (owner only, Gain 0.25).
	void play_alert(bool end) { play_group("interface/alarm", end ? "alarmalert1" : "alarmalert0", 0.25f); }
	// Gate.js sel_gate on lock and unlock (select/building/sel_gate, owner only).
	void play_gate_lock() { play_group("interface/select/building", "sel_gate", 1.0f); }
	void play_no_idle_unit() { play_group("interface/alarm", "alarm_no_idle_unit", 1.0f); }
	void play_invalid_placement() { play_group("interface/alarm", "alarm_invalid_building_placement", 0.3f); }

	// Diffs the world each frame for events 0 A.D. sends as messages: structures
	// finished, techs researched, units trained and units killed. Feeds
	// BattleDetection with the damage `me` deals to other players (counted by the sim).
	void tick(const Simulation *sim, uint8_t me, double t, bool voices_on) {
		if (!sim) return;
		if (me != seat_) {
			seat_ = me;
			primed_ = false;
			known_.clear();
			battle_ = BattleDetection();
			battle_dealt_ = sim->battle_damage_dealt(me);
		}
		const bool fire = primed_; // the first frame of a seat only takes a snapshot

		// Structures finished (owner hears complete_<structure>).
		std::unordered_map<uint32_t, float> progress;
		for (const auto &b : sim->buildings()) {
			if (b.wrecked) continue;
			if (b.team == me) {
				progress[b.id] = b.build_progress;
				auto it = bld_progress_.find(b.id);
				if (fire && it != bld_progress_.end() && it->second < 1.0f && b.build_progress >= 1.0f)
					play_group("interface/complete/building", building_sound_name(b.type, false), 1.0f);
			}
		}
		bld_progress_.swap(progress);

		// Gate.js gate_opening / gate_closing (actor/gate/stonegate_open, _close) where a
		// gate the viewer sees opens or closes.
		for (const auto &b : sim->buildings()) {
			if (b.wrecked || !is_gate_type(b.type)) continue;
			auto it = gate_open_.find(b.id);
			if (fire && spatial_ && it != gate_open_.end() && it->second != b.gate_open &&
					sim->is_entity_visible_to_viewer(b.x, b.z, b.team, me))
				play_spatial_group("actor/gate", b.gate_open ? "stonegate_open" : "stonegate_close", b.x, b.z, 1.0f);
			gate_open_[b.id] = b.gate_open;
		}

		// Techs (soundComplete): phases use alarm_phase, the others alarm_upgradearmory (Gain 0.25).
		const auto &ps = sim->player(me);
		for (uint8_t tech = 1; tech < TECH_COUNT; ++tech) {
			const bool has = ps.has_tech(tech);
			if (has && !techs_[tech] && fire) {
				if (tech == TECH_PHASE_TOWN || tech == TECH_PHASE_CITY) play_group("interface/alarm", "alarmresearchphase", 1.0f);
				else play_group("interface/alarm", "alarmupgradearmory", 0.25f);
			}
			techs_[tech] = has;
		}

		// Units: trained (new own units), deaths (visible), damage dealt by me.
		auto uv = sim->units();
		// The ids alive last frame, ascending (a sorted vector: a hash map rebuilt per
		// frame for ~900 units was ~3% of a late-game frame).
		std::vector<uint32_t> &seen = seen_next_;
		seen.clear();
		auto was_seen = [this](uint32_t id) { return std::binary_search(seen_.begin(), seen_.end(), id); };
		for (size_t i = 0; i < uv.count; ++i) {
			const uint32_t id = uv.id[i];
			const bool prev_seen = was_seen(id);
			if (!uv.alive[i]) {
				// Garrisoned units are not alive either, but keep their health.
				if (prev_seen && uv.hp[i] <= 0.0f && fire)
					play_death(sim, me, unit_death_sound(uv.tmpl ? uv.tmpl[i] : CIV_UNIT_TEMPLATE_NONE, uv.unit_type[i]),
							uv.team[i], uv.pos_x[i], uv.pos_z[i]);
				if (uv.hp[i] > 0.0f) known_.insert(id);
				continue;
			}
			seen.push_back(id);
			if (!prev_seen) {
				if (!known_.insert(id).second) continue; // back out of a garrison
				if (fire && uv.team[i] == me && !trained_this_frame_) {
					trained_this_frame_ = true;
					play_trained(sim->player_civ(me), uv.unit_type[i], voices_on);
				}
				continue;
			}
		}
		if (!std::is_sorted(seen.begin(), seen.end())) std::sort(seen.begin(), seen.end());
		seen_.swap(seen);
		trained_this_frame_ = false;
		// Animals: a living one that turned into a carcass died (Health.js death; a
		// removed one, eaten up or scripted away, makes no sound).
		auto fv = sim->fauna();
		std::unordered_set<uint32_t> fauna_seen;
		fauna_seen.reserve(fauna_seen_.size() + 8);
		for (size_t i = 0; i < fv.count; ++i) {
			const bool living = fv.alive[i] && fv.behavior[i] != FAUNA_CARCASS;
			if (!living && fire && fauna_seen_.count(fv.id[i]))
				play_death(sim, me, fauna_death_sound(fv.fauna_type[i]), fv.owner[i], fv.pos_x[i], fv.pos_z[i]);
			if (living) fauna_seen.insert(fv.id[i]);
			// UnitAI FLEEING enter: PlaySound("panic"), which only fauna_chicken has (chickens.xml).
			if (fv.fauna_type[i] == FAUNA_CHICKEN) {
				const bool fleeing = living && fv.behavior[i] == FAUNA_FLEE;
				auto it = chicken_fleeing_.find(fv.id[i]);
				if (fleeing && fire && it != chicken_fleeing_.end() && !it->second &&
						sim->is_entity_visible_to_viewer(fv.pos_x[i], fv.pos_z[i], fv.owner[i], me))
					play_spatial_group("actor/fauna/animal", "chicken_", fv.pos_x[i], fv.pos_z[i], 1.0f);
				if (living) chicken_fleeing_[fv.id[i]] = fleeing;
				else chicken_fleeing_.erase(fv.id[i]);
			}
		}
		fauna_seen_.swap(fauna_seen);
		if (fire) tick_work_sounds(sim, me, uv, t);
		// BattleDetection.js OnGlobalAttacked: damage my entities dealt to other players,
		// counted by the sim at each hit (Simulation::battle_damage_dealt).
		const double dealt = sim->battle_damage_dealt(me);
		if (dealt > battle_dealt_ && fire) battle_.add_damage((float)(dealt - battle_dealt_), t);
		battle_dealt_ = dealt; // also re-bases after a load restarts the count
		battle_.update(t);
		primed_ = true;
	}

	// Combat sounds for the hits since the last frame and the projectiles that left
	// or landed since then (see the file comment).
	void tick_combat(const Simulation *sim, uint8_t me, const std::vector<Simulation::AttackedEvent> &hits,
			const std::vector<Simulation::FaunaStrikeEvent> &strikes, double t) {
		if (!sim || !spatial_) return;
		if (t - combat_window_start_ >= 0.25 || combat_window_start_ > t) {
			combat_window_start_ = t;
			combat_in_window_ = 0;
		}
		const auto uv = sim->units();
		unit_index_.clear();
		auto unit_at = [&](uint32_t id) -> int64_t {
			if (unit_index_.empty()) {
				unit_index_.reserve(uv.count);
				for (size_t i = 0; i < uv.count; ++i) unit_index_.emplace(uv.id[i], i);
			}
			auto it = unit_index_.find(id);
			return it == unit_index_.end() ? -1 : (int64_t)it->second;
		};
		const auto &blds = sim->buildings();
		bld_index_.clear();
		auto building_at = [&](uint32_t id) -> int64_t {
			if (bld_index_.empty())
				for (size_t i = 0; i < blds.size(); ++i) bld_index_[blds[i].id] = i;
			auto it = bld_index_.find(id);
			return it == bld_index_.end() ? -1 : (int64_t)it->second;
		};

		// attack_melee: the actor's attack animation sounds on the swing that hits; an
		// animal's bite plays its own template's.
		Simulation::FaunaView fv{};
		bool fauna_read = false;
		for (const auto &h : hits) {
			if (!h.melee) continue;
			if (const int64_t u = unit_at(h.attacker_id); u >= 0) {
				const uint8_t g = unit_attack_sounds(uv.tmpl[u], uv.unit_type[u]).a;
				play_combat(sim, me, g, uv.pos_x[u], 1.0f, uv.pos_z[u], uv.team[u]);
				continue;
			}
			if (!fauna_read) fv = sim->fauna(), fauna_read = true;
			for (size_t f = 0; f < fv.count; ++f)
				if (fv.id[f] == h.attacker_id) {
					play_combat(sim, me, fauna_attack_sound(fv.fauna_type[f]), fv.pos_x[f], 1.0f, fv.pos_z[f], GAIA_PLAYER);
					break;
				}
		}
		// A hunter's melee or Slaughter strike on an animal: attack_melee or attack_slaughter.
		for (const auto &st : strikes) {
			const int64_t u = unit_at(st.attacker_id);
			if (u < 0) continue;
			const EntityAttackSounds snd = unit_attack_sounds(uv.tmpl[u], uv.unit_type[u]);
			play_combat(sim, me, st.slaughter ? snd.d : snd.a, uv.pos_x[u], 1.0f, uv.pos_z[u], uv.team[u]);
		}

		// attack_ranged as a projectile leaves, attack_impact_ranged where it lands.
		std::unordered_map<uint32_t, Flight> &flights = flights_next_; // reused: keeps its buckets
		flights.clear();
		for (const auto &p : sim->projectiles()) {
			if (!p.alive) continue;
			auto it = flights_.find(p.id);
			Flight f = it != flights_.end() ? it->second : Flight{};
			if (it == flights_.end() && primed_combat_) {
				uint8_t launch = ATTACK_SOUND_NONE;
				if (const int64_t u = unit_at(p.source_id); u >= 0) {
					const EntityAttackSounds snd = unit_attack_sounds(uv.tmpl[u], uv.unit_type[u]);
					launch = snd.b;
					f.impact = snd.c;
				} else if (const int64_t b = building_at(p.source_id); b >= 0) {
					const EntityAttackSounds snd = building_attack_sounds(blds[b].type);
					f.impact = snd.b;
					// BuildingAI.js FireArrows plays attack_ranged once per round, not per arrow.
					auto last = round_sound_.find(p.source_id);
					if (last == round_sound_.end() || t - last->second > 0.3 || last->second > t) {
						launch = snd.a;
						round_sound_[p.source_id] = t;
					}
				}
				play_combat(sim, me, launch, p.start_x, p.start_y, p.start_z, p.team);
			}
			f.x = p.target_x, f.y = p.target_y, f.z = p.target_z;
			f.team = p.team;
			flights[p.id] = f;
		}
		for (const auto &e : flights_)
			if (!flights.count(e.first)) play_combat(sim, me, e.second.impact, e.second.x, e.second.y, e.second.z, e.second.team);
		flights_.swap(flights);
		if (round_sound_.size() > 256) round_sound_.clear();
		primed_combat_ = true;
	}
	int combat_sounds_played() const { return combat_sounds_; }
	int combat_sounds_culled() const { return combat_culled_; }
	// Tests: a melee hit's sound at (x, z) as tick_combat would play it.
	bool debug_combat_sound(const Simulation *sim, uint8_t me, float x, float z) {
		combat_in_window_ = 0;
		return play_combat(sim, me, unit_attack_sounds(civ_unit_template_by_id("athen/infantry_spearman_b"), UNIT_SPEARMAN).a, x, 1.0f, z, me);
	}

	// Where the camera looks; work sounds play only near it.
	void set_listener(float x, float z) {
		listener_x_ = x;
		listener_z_ = z;
	}
	int work_sounds_played() const { return work_sounds_; }
	int workers_at_work() const { return workers_at_work_; } // gathering or building, anywhere
	int workers_in_earshot() const { return workers_heard_; }
	godot::Vector2 listener() const { return godot::Vector2(listener_x_, listener_z_); }

	bool in_battle() const { return battle_.in_battle(); }
	godot::String last_played() const { return last_played_; }

	static const char *building_sound_name(uint8_t type, bool select) {
		const char *n = "universal";
		switch (building_research_base(type)) {
			case BLD_TOWN_CENTER: n = "civ_center"; break;
			case BLD_HOUSE: n = "house"; break;
			case BLD_BARRACKS: n = "barracks"; break;
			case BLD_STOREHOUSE: n = "storehouse"; break;
			case BLD_TOWER: case BLD_SENTRY_TOWER: case BLD_OUTPOST: n = "tower"; break;
			case BLD_FARMSTEAD: n = "farmstead"; break;
			case BLD_MARKET: n = "market"; break;
			case BLD_FORTRESS: n = "fortress"; break;
			case BLD_GATE: case BLD_PALISADE_GATE: case BLD_SIEGE_WALL_GATE: n = "gate"; break;
			case BLD_TEMPLE: n = "temple"; break;
			case BLD_WONDER: n = "wonder"; break;
			case BLD_DOCK: n = "dock"; break;
			case BLD_WALL_SEGMENT: case BLD_PALISADE: case BLD_WALL_TOWER: case BLD_SIEGE_WALL: n = "wall"; break;
			case BLD_CORRAL: n = "corral"; break;
			case BLD_FIELD: n = "field"; break;
			case BLD_STABLE: n = "stable"; break;
			case BLD_RANGE: n = select ? "universal" : "range"; break;
			case BLD_FORGE: n = "forge"; break;
			case BLD_ELEPHANT_STABLE: n = "elephant_stable"; break;
			case BLD_THEATER: n = "theater"; break;
			default: break;
		}
		static thread_local std::string s;
		s = std::string(select ? "sel_" : "complete_") + n;
		return s.c_str();
	}

private:
	// Gather and build sounds: 0 A.D.'s CUnitAnimation plays the Sound group of
	// the animation (gather_tree, gather_grain, build...) once per loop, at the
	// clip's sound point, else its event point (art/variants/biped/*gather_*.xml,
	// *build.xml); a clip with neither, as farming, plays none. Each worker's loop
	// starts when it starts working, at the clip's length over its speed with
	// VisualActor's 5% desync; a cap keeps a crowd from drowning the rest.
	static const char *work_group(uint8_t res_type, uint8_t sub, const char **dir) {
		*dir = "resource/mining";
		if (sub == RES_SUB_MEAT) { *dir = "resource/gathering"; return "gather_meat"; }  // gather_meat.xml
		if (sub == RES_SUB_FISH) return nullptr;                                         // ships have no gather sound
		if (sub == RES_SUB_RUINS) return "mine_metal";                                   // mining.xml
		switch (res_type) {
			case RES_WOOD: *dir = "resource/lumbering"; return "lumber_tree";            // lumbering.xml
			case RES_FOOD: *dir = "resource/foraging"; return "leaves";                  // forage_leaves.xml
			case RES_STONE: return "mine_stone";                                         // pickaxe.xml
			case RES_GOLD: return "mine_metal";                                          // mining.xml
			default: return nullptr;
		}
	}

	// One loop of a work clip: its length in seconds (the .dae's duration over the
	// actor's speed) and where in it the sound plays (0-1), < 0 when it plays none.
	struct WorkClip { float period, pos; };
	static WorkClip work_clip(bool build, uint8_t res_type, uint8_t sub, bool female) {
		if (build) return female ? WorkClip{ 0.8f / 0.7f, 0.75f } : WorkClip{ 0.8f, 0.5f }; // citizen/build.dae 0.8 s
		if (res_type == RES_FOOD) {
			if (sub == RES_SUB_GRAIN) return { 2.667f, -1.0f };                            // farming.dae, no event
			if (sub == RES_SUB_MEAT) return { 4.133f, 0.0f };                              // gather_meat.dae, sound 0
			if (sub == RES_SUB_FISH) return { 1.0f, -1.0f };
			return female ? WorkClip{ 4.133f, 0.0f } : WorkClip{ 6.633f, 0.0f };           // gather_fruit_{f,m}.dae
		}
		if (res_type == RES_WOOD) return { 2.267f / 1.2f, 0.25f };                         // gather_wood.dae, speed 120
		if (res_type == RES_STONE || res_type == RES_GOLD)
			return { 2.333f / (female ? 1.0f : 1.2f), 0.4f };                              // gather_mine.dae
		return { 1.0f, -1.0f };
	}

	void tick_work_sounds(const Simulation *sim, uint8_t me, const Simulation::UnitView &uv, double t) {
		if (!spatial_) return;
		constexpr float kHear2 = 40.0f * 40.0f;
		constexpr int kMaxPerHalfSecond = 3;
		if (t - work_window_start_ >= 0.5 || work_window_start_ > t) {
			work_window_start_ = t;
			work_in_window_ = 0;
		}
		if (t - res_index_time_ > 2.0 || res_index_time_ > t) {
			res_index_time_ = t;
			res_index_.clear();
			auto rv = sim->resources();
			for (size_t r = 0; r < rv.count; ++r) res_index_[rv.id[r]] = r;
		}
		auto rv = sim->resources();
		std::unordered_map<uint32_t, WorkLoop> next;
		workers_at_work_ = workers_heard_ = 0;
		for (size_t i = 0; i < uv.count; ++i) {
			const uint8_t st = uv.action_state[i];
			if (!uv.alive[i] || (st != ACTION_GATHER && st != ACTION_BUILD)) continue;
			++workers_at_work_;
			uint8_t res_type = 0xFF, sub = 0;
			if (st == ACTION_GATHER && uv.target_resource_id) {
				auto rit = res_index_.find(uv.target_resource_id[i]);
				if (rit != res_index_.end() && rit->second < rv.count && rv.id[rit->second] == uv.target_resource_id[i]) {
					res_type = rv.type[rit->second];
					sub = rv.subtype[rit->second];
				}
			}
			const uint32_t id = uv.id[i];
			const uint16_t tmpl = uv.tmpl ? uv.tmpl[i] : CIV_UNIT_TEMPLATE_NONE;
			const uint8_t ut = uv.unit_type[i];
			const bool female = (ut == UNIT_FEMALE_CITIZEN || ut == UNIT_VILLAGER) && !civilian_is_male(tmpl, id);
			const uint16_t kind = st == ACTION_BUILD ? 0xFFFF : (uint16_t)((res_type << 8) | sub);
			auto it = work_loops_.find(id);
			WorkLoop w;
			if (it != work_loops_.end() && it->second.kind == kind) {
				w = it->second;
			} else {
				// SelectAnimation starts the clip; VisualActor desyncs its speed by up to 5%.
				const WorkClip c = work_clip(st == ACTION_BUILD, res_type, sub, female);
				const float desync = 1.0f + 0.05f * (((id * 2654435761u) >> 16) % 2001 / 1000.0f - 1.0f);
				w = WorkLoop{ kind, t, c.period * desync, c.pos, 0 };
			}
			const float dx = uv.pos_x[i] - listener_x_, dz = uv.pos_z[i] - listener_z_;
			const bool heard = dx * dx + dz * dz <= kHear2 && sim->is_entity_visible_to_viewer(uv.pos_x[i], uv.pos_z[i], uv.team[i], me);
			if (heard) ++workers_heard_;
			if (w.pos >= 0.0f && w.period > 0.0f) {
				// Sound points passed since the clip started.
				const double passed = std::floor((t - w.start) / w.period - w.pos) + 1.0;
				if (passed > (double)w.played) {
					w.played = (uint32_t)passed;
					if (heard) {
						const char *dir = nullptr;
						const char *prefix = nullptr;
						if (st == ACTION_BUILD) {
							dir = "resource/construction";
							prefix = "con_wood"; // con_wood.xml
						} else if (res_type != 0xFF) {
							prefix = work_group(res_type, sub, &dir);
						}
						if (prefix && work_in_window_ < kMaxPerHalfSecond) {
							const auto &g = sound_group(dir, (std::string(prefix) + "_").c_str());
							if (!g.empty()) {
								godot::Ref<godot::AudioStream> snd = stream(g[rng_() % g.size()]);
								if (snd.is_valid()) {
									spatial_(snd, godot::Vector3(uv.pos_x[i], 0.5f, uv.pos_z[i]), -8.0f, 1.0f);
									++work_in_window_;
									++work_sounds_;
								}
							}
						}
					}
				}
			}
			next[id] = w;
		}
		work_loops_.swap(next);
	}

	struct WorkLoop {
		uint16_t kind = 0;
		double start = 0.0;
		float period = 1.0f, pos = -1.0f;
		uint32_t played = 0;
	};
	std::unordered_map<uint32_t, WorkLoop> work_loops_;
	float listener_x_ = 0.0f, listener_z_ = 0.0f;
	std::unordered_map<uint32_t, size_t> res_index_;
	double res_index_time_ = -100.0;
	double work_window_start_ = 0.0;
	int work_in_window_ = 0;
	int work_sounds_ = 0;
	int workers_at_work_ = 0;
	int workers_heard_ = 0;

	// A combat sound heard at (x, y, z) when `me` sees that spot, at most
	// kMaxCombatPerWindow every quarter second so a battle doesn't drown the rest.
	// 0.28 SoundGroup::PlayNext: a sound beyond the group's distance from the camera is
	// culled before it is played (AudioStreamPlayer3D::play is the costly part).
	static constexpr float kCombatHear = 75.0f; // the spatial players' max distance
	bool play_combat(const Simulation *sim, uint8_t me, uint8_t g, float x, float y, float z, uint8_t team) {
		static constexpr int kMaxCombatPerWindow = 6;
		if (g == ATTACK_SOUND_NONE || combat_in_window_ >= kMaxCombatPerWindow) return false;
		const float dx = x - listener_x_, dz = z - listener_z_;
		if (dx * dx + dz * dz > kCombatHear * kCombatHear) {
			++combat_culled_;
			return false;
		}
		if (!sim->is_entity_visible_to_viewer(x, z, team, me)) return false;
		float pitch = 1.0f;
		const godot::String path = pick_attack_sound(g, rng_, &pitch);
		if (path.is_empty()) return false;
		godot::Ref<godot::AudioStream> s = stream(path);
		if (s.is_null()) return false;
		const float gain = attack_sound_group(g).gain;
		spatial_(s, godot::Vector3(x, y, z), gain > 0.001f ? 20.0f * std::log10(gain) : -80.0f, pitch);
		++combat_in_window_;
		++combat_sounds_;
		last_played_ = path;
		return true;
	}

	struct Flight {
		float x = 0.0f, y = 0.0f, z = 0.0f; // where it is aimed
		uint8_t impact = ATTACK_SOUND_NONE;
		uint8_t team = 0;
	};
	std::unordered_map<uint32_t, Flight> flights_, flights_next_;
	std::unordered_map<uint32_t, double> round_sound_; // structure id -> its last round's sound
	std::unordered_map<uint32_t, size_t> unit_index_, bld_index_;
	double combat_window_start_ = 0.0;
	int combat_in_window_ = 0;
	int combat_sounds_ = 0;
	int combat_culled_ = 0;
	bool primed_combat_ = false;

	static constexpr int kPool = 8;

	godot::Ref<godot::AudioStream> stream(const godot::String &path) {
		const std::string key = path.utf8().get_data();
		auto it = streams_.find(key);
		if (it != streams_.end()) return it->second;
		godot::Ref<godot::AudioStream> s = load_audio(path);
		streams_[key] = s;
		return s;
	}

	// template_unit_*.xml <trained>: alarm_create_{infantry,cav,female,worker,priest,warship},
	// elephant_trained for elephants, complete_kennel for dogs, siege_select for the
	// Germans' wagon, ram_trained (ram_move.ogg) for siege engines (template_unit_siege.xml
	// and template_unit_siege_{ram,tower}.xml; the bolt shooter and stone thrower inherit
	// it); alarm_create_infantry (fs_gravel) for the male civilian.
	void play_trained(uint8_t civ, uint8_t type, bool voices_on) {
		switch (type) {
			case UNIT_FEMALE_CITIZEN: play_group("interface/alarm", "alarmcreatefemale", 1.0f); break;
			case UNIT_TRADER: play_group("interface/alarm", "alarmcreateworker", 1.0f); break;
			case UNIT_CAVALRY: case UNIT_CAMEL: case UNIT_CHARIOT: case UNIT_SCYTHED_CHARIOT:
				play_group("interface/alarm", "alarmcreatecavalry", 1.0f);
				break;
			case UNIT_ELEPHANT: case UNIT_WORKER_ELEPHANT:
				play_group("actor/fauna/animal", "elephant_trained", 1.0f);
				break;
			case UNIT_WAR_DOG: play_group("interface/complete/building", "complete_kennel", 1.0f); break;
			case UNIT_CATAPULT: case UNIT_SIEGE_RAM: case UNIT_BOLT_SHOOTER:
				play_group("attack/siege", "ram_move", 1.0f);
				break;
			case UNIT_WAGON: play_group("attack/siege", "siege_select", 1.0f); break;
			case UNIT_HEALER: play_group("interface/alarm", "alarmcreatepriest", 1.0f); break;
			case UNIT_TRIREME: case UNIT_FISHING_BOAT: case UNIT_MERCHANT_SHIP:
				play_group("interface/alarm", "alarm_warship", 1.0f);
				break;
			default: play_group("actor/singlesteps", "fs_gravel", 1.0f); break;
		}
		if (voices_on) play_voice(civ, type, "trained");
	}

	// Health.js PlaySound("death"): group g where the entity died, while the viewer sees it.
	void play_death(const Simulation *sim, uint8_t me, uint8_t g, uint8_t team, float x, float z) {
		if (!spatial_ || g == ATTACK_SOUND_NONE || !sim->is_entity_visible_to_viewer(x, z, team, me)) return;
		float pitch = 1.0f;
		const godot::String path = pick_attack_sound(g, rng_, &pitch);
		if (path.is_empty()) return;
		godot::Ref<godot::AudioStream> s = stream(path);
		if (s.is_null()) return;
		const float gain = attack_sound_group(g).gain;
		spatial_(s, godot::Vector3(x, 0.5f, z), gain > 0.001f ? 20.0f * std::log10(gain) : -80.0f, pitch);
		last_played_ = path;
	}

	godot::AudioStreamPlayer *pool_[kPool] = {};
	unsigned next_ = 0;
	std::unordered_map<std::string, godot::Ref<godot::AudioStream>> streams_;
	std::unordered_map<uint32_t, float> bld_progress_;
	std::unordered_map<uint32_t, bool> gate_open_; // gate id -> open last frame
	std::unordered_map<uint32_t, bool> chicken_fleeing_; // chicken id -> fleeing last frame
	std::vector<uint32_t> seen_, seen_next_; // living unit ids last frame (ascending) and its scratch
	std::unordered_set<uint32_t> fauna_seen_; // animals alive last frame
	std::unordered_set<uint32_t> known_;
	bool techs_[256] = {};
	uint8_t seat_ = 0xFF;
	bool primed_ = false;
	bool trained_this_frame_ = false;
	BattleDetection battle_;
	double battle_dealt_ = 0.0; // Simulation::battle_damage_dealt(seat_) at the last frame
	SpatialFn spatial_;
	std::mt19937 rng_;
	godot::String last_played_;
};

} // namespace rtso

#endif
