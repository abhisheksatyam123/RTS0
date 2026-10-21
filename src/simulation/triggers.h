#ifndef RTSO_TRIGGERS_H
#define RTSO_TRIGGERS_H

// Map trigger scripting, ported from 0 A.D. (GPL-2.0-or-later):
//   binaries/data/mods/public/simulation/components/Trigger.js      (events, RegisterTrigger, DoAfterDelay, DoRepeatedly)
//   binaries/data/mods/public/simulation/components/TriggerPoint.js (named trigger points)
//   binaries/data/mods/public/simulation/components/Timer.js        (timer ordering: by time, then by id)
//   binaries/data/mods/public/simulation/components/GuiInterface.js AddTimeNotification
//
// 0 A.D. runs a map's *_triggers.js as JavaScript against the simulation. RTS0's
// simulation is C++, so each map script is ported to C++ actions on Simulation
// (simulation.cpp, "Map trigger scripts") and this header holds the engine they
// run on: the registered triggers, the timer queue, trigger points and the time
// notifications shown to players. Everything here is deterministic simulation
// state (lockstep peers run the same timers on the same turns), saved through
// tick_state_io and mixed into the sync hash.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rtso {

// Trigger.js eventNames, the events a trigger can listen on.
enum TriggerEvent : uint8_t {
	TRIGGER_ON_INIT_GAME = 0,       // first turn, after Game Setup has been applied
	TRIGGER_ON_INTERVAL,            // timer, every interval_ms after delay_ms
	TRIGGER_ON_RANGE,               // a unit entered or left a trigger point's range
	TRIGGER_ON_STRUCTURE_BUILT,     // a foundation finished
	TRIGGER_ON_CONSTRUCTION_STARTED,
	TRIGGER_ON_TRAINING_QUEUED,
	TRIGGER_ON_TRAINING_FINISHED,
	TRIGGER_ON_RESEARCH_QUEUED,
	TRIGGER_ON_RESEARCH_FINISHED,
	TRIGGER_ON_OWNERSHIP_CHANGED,   // capture or conversion to Gaia
	TRIGGER_ON_PLAYER_DEFEATED,
	TRIGGER_ON_PLAYER_WON,
	TRIGGER_ON_DIPLOMACY_CHANGED,
	TRIGGER_ON_TREASURE_COLLECTED,
	TRIGGER_ON_PLAYER_COMMAND,
	TRIGGER_ON_DESERIALIZED,        // a saved game was loaded
	TRIGGER_ON_CINEMA_PATH_ENDED,   // CinemaManager: the path at the front of the queue ended (name = its name)
	TRIGGER_ON_CINEMA_QUEUE_ENDED,  // CinemaManager: the last queued path ended
	TRIGGER_ON_ATTACK_DETECTED,     // AttackDetection.js AttackAlert: a player's entity was attacked, past suppression
	TRIGGER_EVENT_COUNT
};

// The ported actions (Trigger.prototype.<name> in the map scripts). Values are
// saved, so append only.
enum TriggerAction : uint16_t {
	TRIGGER_ACTION_NONE = 0,
	// maps/random/flood_triggers.js
	TRIGGER_ACTION_FLOOD_INIT = 1,
	TRIGGER_ACTION_FLOOD_RISE_STEP = 2,   // RiseWaterLevelStep(finalWaterLevel = arg)
	TRIGGER_ACTION_FLOOD_WARNING = 3,     // DisplayWarning
	// maps/random/extinct_volcano_triggers.js
	TRIGGER_ACTION_VOLCANO_INIT = 4,
	TRIGGER_ACTION_VOLCANO_NOTIFY = 5,    // RaisingWaterNotification
	TRIGGER_ACTION_VOLCANO_RISE_STEP = 6, // RaiseWaterLevelStep
	// maps/random/polar_sea_triggers.js
	TRIGGER_ACTION_POLAR_SEA_INIT = 7,    // DisableTechnologies, first wave
	TRIGGER_ACTION_POLAR_SEA_WAVE = 8,    // SpawnWolvesAndAttack
	// maps/scenarios/treasure_islands.js
	TRIGGER_ACTION_TREASURE_ISLANDS_INIT = 9,
	TRIGGER_ACTION_TREASURE_ISLANDS_INTRO = 10,     // IntroductionMessage
	TRIGGER_ACTION_TREASURE_ISLANDS_COLLECTED = 11, // TreasureCollected
	TRIGGER_ACTION_TREASURE_ISLANDS_BATTLE = 12,    // BattleMessage
	TRIGGER_ACTION_TREASURE_ISLANDS_VICTORY = 13,   // Victory(playerID = arg)
	// maps/random/survivalofthefittest_triggers.js
	TRIGGER_ACTION_SURVIVAL_INIT = 14,             // InitSurvival
	TRIGGER_ACTION_SURVIVAL_WAVE = 15,             // StartAnEnemyWave
	TRIGGER_ACTION_SURVIVAL_TREASURES = 16,        // PlaceTreasures
	TRIGGER_ACTION_SURVIVAL_OWNERSHIP = 17,        // OnOwnershipChanged
	// maps/scripts/Tutorial.js with maps/tutorials/*.js (TriggerState::vars[7] names which)
	TRIGGER_ACTION_TUTORIAL = 18,                  // InitTutorial, then every event the goals listen on
	// scenarios/death_canyon_invasion_force_triggers.js
	TRIGGER_ACTION_DEATH_CANYON_INIT = 19,         // DisableBuilding
	// maps/random/elephantine_triggers.js
	TRIGGER_ACTION_ELEPHANTINE_INIT = 20,          // InitElephantine
	// maps/random/danubius_triggers.js
	TRIGGER_ACTION_DANUBIUS_INIT = 21,             // InitDanubius
	TRIGGER_ACTION_DANUBIUS_CC_ATTACKERS = 22,     // SpawnCCAttackers
	TRIGGER_ACTION_DANUBIUS_SHIPS = 23,            // SpawnShips
	TRIGGER_ACTION_DANUBIUS_FILL_SHIPS = 24,       // FillShips
	TRIGGER_ACTION_DANUBIUS_UNLOAD_ORDER = 25,     // UngarrisonShipsOrder
	TRIGGER_ACTION_DANUBIUS_SHIP_RANGE = 26,       // CheckShipRange
	TRIGGER_ACTION_DANUBIUS_OWNERSHIP = 27,        // DanubiusOwnershipChange
	TRIGGER_ACTION_JEBEL_BARKAL_INIT = 28,         // jebel_barkal_triggers.js JebelBarkal_Init
	TRIGGER_ACTION_JEBEL_BARKAL_CITY_PATROL = 29,  // JebelBarkal_SpawnCityPatrolGroups
	TRIGGER_ACTION_JEBEL_BARKAL_ATTACK = 30,       // JebelBarkal_SpawnAttackerGroups
	TRIGGER_ACTION_JEBEL_BARKAL_OWNERSHIP = 31,    // JebelBarkal_OwnershipChange
	TRIGGER_ACTION_GALLIC_FIELDS = 32,             // skirmishes/gallic_fields_3p.js: arg 0 init, 1 SpawnAndAttack
	TRIGGER_ACTION_TRIGGERS_DEMO_RANGE = 33,       // scenarios/triggers_demo.js RangeAction (logs the OnRange data)
	TRIGGER_ACTION_TRIGGERS_DEMO_INIT = 34,        // scenarios/triggers_demo.js top level
	TRIGGER_ACTION_TRIGGERS_DEMO_LOG = 35,         // its <Event>Action listeners: warn(uneval(data))
	TRIGGER_ACTION_TRIGGERS_DEMO_INTERVAL = 36,    // IntervalAction: the yes-no dialog, three times
	TRIGGER_ACTION_PICKUP_TEST = 37,               // scenarios/pickup_test_map_triggers.js: arg 0 init, 1 experiments, 2 second walker
	TRIGGER_ACTION_UNIT_CHASING = 38,              // scenarios/unit_chasing_test_triggers.js: arg 0 init, 1 SetupUnits
	TRIGGER_ACTION_UNIT_CHASING_OWNERSHIP = 39,    // its OwnershipChanged
	TRIGGER_ACTION_UNIT_DANCING = 40,              // scenarios/unit_dancing_test_triggers.js: arg 0 init, 1 SetupUnits, 2 RemoveSpread, 3 EndGame
	TRIGGER_ACTION_UNIT_DANCING_EVENT = 41,        // its ReportResults hooks (a dancer killed, or idle)
	TRIGGER_ACTION_UNIT_PUSHING = 42,              // scenarios/unit_pushing_test.js: arg 0 init, 1 Setup, 2 EndGame
	TRIGGER_ACTION_UNIT_MOTION = 43,               // scenarios/unit_motion_integration_test.js: arg 0 init, 1 SetupUnits, 2 enclosed_timeout
	TRIGGER_ACTION_CINEMA_DEMO = 44,               // scenarios/cinema_demo.js: arg 0 init, 1 CounterMessage, 2 StartCutscene
	TRIGGER_ACTION_WALL_DEMO_INIT = 45,            // random/wall_demo_triggers.js: SetLosRevealAll(-1, true)
	TRIGGER_ACTION_COUNT
};

// Payload of an event (Trigger.js passes a JS object; these are the fields the
// ported scripts read).
struct TriggerEventData {
	uint32_t entity = 0;   // unit or building id
	uint32_t other = 0;    // foundation id, trainer id, ...
	uint8_t player = 0xFF; // acting / owning player
	uint8_t from = 0xFF;   // OnOwnershipChanged
	uint8_t to = 0xFF;
	uint16_t value = 0;    // template, tech or command type
	uint16_t count = 0;    // batch size
	uint8_t event = 0xFF;  // the TriggerEvent being run (set by Simulation::trigger_event)
	const char *name = nullptr; // OnCinemaPathEnded: the path's name
	// OnRange (TriggerPoint.js OnRangeUpdate): other is the TriggerRangeQuery index; the
	// entities that entered and left this turn, and the query's currentCollection after.
	const std::vector<uint32_t> *added = nullptr;
	const std::vector<uint32_t> *removed = nullptr;
	const std::vector<uint32_t> *collection = nullptr;
	// OnAttackDetected: entity = target, other = attacker, player = targetOwner,
	// from = attackerOwner, value = targetIsDomesticAnimal, and the target's position.
	float x = 0.0f, z = 0.0f;
};

// OnPlayerCommand: the Commands.js command types the ported scripts look at (value).
// entity is the command's target, other a second target.
enum TriggerCommand : uint16_t {
	TRIGGER_CMD_NONE = 0,
	TRIGGER_CMD_GATHER,          // "gather" on a resource (entity); a hunt names the animal (from = 1)
	TRIGGER_CMD_CONSTRUCT,       // "construct": entity = the new foundation, count = its BuildingType
	TRIGGER_CMD_REPAIR,          // "repair": units sent to build the structure entity
	TRIGGER_CMD_SET_RALLY,       // "set-rallypoint": entity = resource, other = structure (0 if none)
	TRIGGER_CMD_UNSET_RALLY,     // "unset-rallypoint"
	TRIGGER_CMD_DIALOG_READY,    // "dialog-answer" {"tutorial": "ready"}: a tutorial's Ready button
	TRIGGER_CMD_OTHER,           // anything else
};

// Timer.js timer: fires at time_ms, then every repeat_ms when repeat_ms > 0.
struct TriggerTimer {
	uint32_t id = 0;
	uint32_t time_ms = 0;
	uint32_t repeat_ms = 0;
	uint16_t action = TRIGGER_ACTION_NONE;
	uint16_t trigger = 0xFFFF; // index of the OnInterval registration that owns it, 0xFFFF for DoAfterDelay
	float arg = 0.0f;          // eventData the ported action reads
};
static_assert(sizeof(TriggerTimer) == 20, "TriggerTimer is saved raw: keep it free of padding");

// Trigger.js RegisterTrigger(event, name, triggerData).
struct TriggerRegistration {
	uint16_t action = TRIGGER_ACTION_NONE;
	uint8_t event = TRIGGER_ON_INIT_GAME;
	uint8_t enabled = 0;
	uint32_t interval_ms = 0; // OnInterval
	uint32_t delay_ms = 0;    // OnInterval first delay
	uint32_t timer_id = 0;    // OnInterval: running timer, 0 when disabled
	float range = 0.0f;       // OnRange: max range around the trigger points
	uint8_t point_ref = 0;    // OnRange: trigger point reference ('A', 'B', ...)
	uint8_t players = 0;      // OnRange: bitmask of players counted (0 = all)
	uint8_t reserved[2] = {};
};
static_assert(sizeof(TriggerRegistration) == 24, "TriggerRegistration is saved raw: keep it free of padding");

// TriggerPoint.js: a named spot on the map scripts spawn at or watch.
struct TriggerPoint {
	float x = 0.0f;
	float z = 0.0f;
	uint8_t ref = 0;
	uint8_t owner = 0xFE;   // the point entity's owner: a player id, or 0xFE (GAIA_PLAYER); saves before v50 load 0
	uint8_t reserved[2] = {};
};
static_assert(sizeof(TriggerPoint) == 12, "TriggerPoint is saved raw: keep it free of padding");

// TriggerPoint.js RegisterRangeTrigger: the RangeManager active query an OnRange trigger
// keeps on one of its trigger points, with the point's currentCollection for it.
struct TriggerRangeQuery {
	uint16_t trigger = 0xFFFF; // the OnRange registration (index into triggers)
	uint16_t point = 0;        // index into points
	float min_range = 0.0f;    // world units
	float max_range = -1.0f;   // world units; below 0: no maximum
	uint8_t players = 0xFF;    // bitmask of the player ids counted (0 A.D.'s data.players)
	uint8_t units_only = 0;    // requiredComponent IID_UnitAI: units, not structures
	uint8_t reserved[2] = {};
	std::vector<uint32_t> collection; // entity ids in range, in the order they came in
};

// GuiInterface.AddTimeNotification: a message shown to players until end_ms.
struct TimeNotification {
	uint32_t id = 0;
	uint32_t end_ms = 0;
	uint8_t players = 0xFF; // bitmask of player ids it is shown to
	uint8_t reserved[3] = {};
	char text[180] = {};
};
static_assert(sizeof(TimeNotification) == 192, "TimeNotification is saved raw: keep it free of padding");

// ProcessCommand(0, {"type": "attack", "queued": true}) for each target in turn: a
// script-spawned Gaia animal works through its targets, nearest first. targets[0]
// is the one it is attacking now.
struct TriggerAttackOrder {
	uint32_t fauna_id = 0;
	uint32_t targets[3] = {};
	uint32_t count = 0;
};
static_assert(sizeof(TriggerAttackOrder) == 20, "TriggerAttackOrder is saved raw: keep it free of padding");

// An entity a script follows for OnOwnershipChanged: when its owner changes (captured,
// turned Gaia, or destroyed: 0xFF, 0 A.D.'s INVALID_PLAYER) the event fires with
// from = owner and to = the new owner. role and player are the script's own labels.
struct TriggerWatch {
	uint32_t id = 0;
	uint8_t owner = 0xFF;
	uint8_t building = 0; // 1: a structure id, 2: an animal id, 0: a unit id
	uint8_t role = 0;
	uint8_t player = 0;
};
static_assert(sizeof(TriggerWatch) == 8, "TriggerWatch is saved raw: keep it free of padding");

// AttackDetection.js suppressedList: an attack a player was alerted to. Others within
// SuppressionRange during SuppressionTime are not; one within SuppressionTransferRange
// moves the suppression to itself.
struct TriggerAttackSuppression {
	float x = 0.0f, z = 0.0f;
	uint32_t time_ms = 0;
	uint8_t player = 0;
	uint8_t domestic = 0; // targetIsDomesticAnimal: a lower-priority alert a regular one overrides
	uint8_t reserved[2] = {};
};
static_assert(sizeof(TriggerAttackSuppression) == 16, "TriggerAttackSuppression is saved raw: keep it free of padding");

struct TriggerState {
	uint32_t time_ms = 0;       // Timer.js GetTime(): simulation time since the match started
	uint32_t next_timer_id = 1; // Timer.js id counter
	uint32_t next_notification_id = 1;
	uint8_t init_game_done = 0; // OnInitGame has fired
	uint8_t difficulty = 3;     // Trigger.js SetDifficulty (data/settings/trigger_difficulties.json: 3 = Medium)
	uint8_t reserved[2] = {};
	std::vector<TriggerRegistration> triggers;
	std::vector<TriggerTimer> timers;
	std::vector<TriggerPoint> points;
	std::vector<TimeNotification> notifications;
	// Script variables (cmpTrigger.<name> in the map scripts), e.g. treasure_islands.js
	// treasureCount.players. Their meaning is the map's.
	int32_t vars[8] = {};
	// cmpPlayer.SetDisabledTechnologies, per player: a 256-bit set of tech ids.
	uint64_t disabled_techs[8][4] = {};
	std::vector<TriggerAttackOrder> attack_orders;
	// cmpPlayer.SetDisabledTemplates: structures (bld_bit per BuildingType, save v63
	// widened it past 64 ids) and unit roster
	// templates each player may not build or train.
	BldMask disabled_buildings[8] = {}; // per player, bld_bit() of each BuildingType it cannot build
	std::vector<uint16_t> disabled_templates; // for every player (the ported scripts disable the same ones for all)
	std::vector<uint32_t> invulnerable;       // Resistance.SetInvulnerability(true): unit ids
	std::vector<TriggerWatch> watched;
	std::vector<TriggerRangeQuery> range_queries; // OnRange, save v68
	std::vector<TriggerAttackSuppression> attack_suppressed; // OnAttackDetected, save v86

	// template_player.xml AttackDetection (160 m, 80 m, 60 s) at 4 m per world unit.
	static constexpr float kAttackSuppressionRange = 160.0f / 4.0f;
	static constexpr float kAttackSuppressionTransferRange = 80.0f / 4.0f;
	static constexpr uint32_t kAttackSuppressionTime = 60000;

	void clear() { *this = TriggerState(); }
	bool active() const { return !triggers.empty() || !timers.empty(); }

	// Timer.js SetTimeout / SetInterval, relative to now.
	uint32_t set_timeout(uint32_t delay_ms, uint16_t action, float arg = 0.0f) {
		return add_timer(delay_ms, 0, action, 0xFFFF, arg);
	}
	uint32_t set_interval(uint32_t delay_ms, uint32_t interval_ms, uint16_t action, float arg = 0.0f) {
		return add_timer(delay_ms, interval_ms ? interval_ms : 1, action, 0xFFFF, arg);
	}
	void cancel_timer(uint32_t id) {
		timers.erase(std::remove_if(timers.begin(), timers.end(), [id](const TriggerTimer &t) { return t.id == id; }), timers.end());
	}

	// Trigger.js RegisterTrigger. Returns the registration's index.
	uint16_t register_trigger(uint8_t event, uint16_t action, bool enabled, uint32_t interval_ms = 0, uint32_t delay_ms = 0) {
		TriggerRegistration r;
		r.event = event;
		r.action = action;
		r.interval_ms = interval_ms;
		r.delay_ms = delay_ms;
		triggers.push_back(r);
		const uint16_t idx = (uint16_t)(triggers.size() - 1);
		if (enabled) enable_trigger(idx);
		return idx;
	}
	// Trigger.js RegisterTrigger("OnRange", ...) with entities = GetTriggerPoints(point_ref):
	// one query per trigger point carrying that reference.
	uint16_t register_range_trigger(uint16_t action, uint8_t point_ref, float min_range, float max_range,
			uint8_t players, bool units_only, bool enabled) {
		const uint16_t idx = register_trigger(TRIGGER_ON_RANGE, action, enabled);
		TriggerRegistration &r = triggers[idx];
		r.range = max_range;
		r.point_ref = point_ref;
		r.players = players;
		for (size_t i = 0; i < points.size(); ++i) {
			if (points[i].ref != point_ref) continue;
			TriggerRangeQuery q;
			q.trigger = idx;
			q.point = (uint16_t)i;
			q.min_range = min_range;
			q.max_range = max_range;
			q.players = players;
			q.units_only = units_only ? 1 : 0;
			range_queries.push_back(q);
		}
		return idx;
	}
	void enable_trigger(uint16_t idx) {
		if (idx >= triggers.size()) return;
		TriggerRegistration &r = triggers[idx];
		if (r.enabled) return;
		r.enabled = 1;
		if (r.event == TRIGGER_ON_INTERVAL && r.interval_ms > 0)
			r.timer_id = add_timer(r.delay_ms, r.interval_ms, r.action, idx, 0.0f);
	}
	void disable_trigger(uint16_t idx) {
		if (idx >= triggers.size()) return;
		TriggerRegistration &r = triggers[idx];
		if (!r.enabled) return;
		r.enabled = 0;
		if (r.timer_id) { cancel_timer(r.timer_id); r.timer_id = 0; }
	}

	void add_point(uint8_t ref, float x, float z, uint8_t owner = 0xFE) {
		TriggerPoint p;
		p.ref = ref;
		p.owner = owner;
		p.x = x;
		p.z = z;
		points.push_back(p);
	}

	// The GUI translates text (0 A.D. markForTranslation): a format with "%(name)s"
	// parameters carries them after it as "\x1fname=value" (translateParameters).
	void add_notification(const char *text, uint32_t duration_ms, uint8_t players = 0xFF) {
		TimeNotification n;
		n.id = next_notification_id++;
		n.end_ms = time_ms + duration_ms;
		n.players = players;
		std::strncpy(n.text, text, sizeof(n.text) - 1);
		notifications.push_back(n);
	}

	void disable_tech(uint8_t player, uint8_t tech) {
		if (player < 8) disabled_techs[player][tech >> 6] |= 1ull << (tech & 63);
	}
	bool tech_disabled(uint8_t player, uint8_t tech) const {
		return player < 8 && ((disabled_techs[player][tech >> 6] >> (tech & 63)) & 1u);
	}
	bool building_disabled(uint8_t player, uint8_t type) const {
		return player < 8 && disabled_buildings[player].test(type);
	}
	bool template_disabled(uint16_t tmpl) const {
		for (uint16_t t : disabled_templates) if (t == tmpl) return true;
		return false;
	}
	bool is_invulnerable(uint32_t id) const {
		for (uint32_t u : invulnerable) if (u == id) return true;
		return false;
	}

	// AttackDetection.js AttackAlert's suppression for `player`, attacked at (x, z):
	// true when the attack is detected (OnAttackDetected fires). HandleTimeout drops
	// alerts SuppressionTime old.
	bool attack_alert(uint8_t player, float x, float z, bool domestic) {
		attack_suppressed.erase(std::remove_if(attack_suppressed.begin(), attack_suppressed.end(),
				[this](const TriggerAttackSuppression &e) { return time_ms - e.time_ms >= kAttackSuppressionTime; }),
				attack_suppressed.end());
		TriggerAttackSuppression ev;
		ev.x = x; ev.z = z; ev.time_ms = time_ms; ev.player = player; ev.domestic = domestic ? 1 : 0;
		bool priority_increased = false;
		for (TriggerAttackSuppression &e : attack_suppressed) {
			if (e.player != player) continue;
			const float dx = x - e.x, dz = z - e.z, d2 = dx * dx + dz * dz;
			if (d2 >= kAttackSuppressionRange * kAttackSuppressionRange) continue;
			priority_increased = e.domestic && !domestic;
			const bool priority_decreased = !e.domestic && domestic;
			if (priority_increased || (!priority_decreased && d2 < kAttackSuppressionTransferRange * kAttackSuppressionTransferRange))
				e = ev;
			if (priority_increased) break;
			return false;
		}
		if (!priority_increased) attack_suppressed.push_back(ev);
		return true;
	}

	// Index of the timer with this id, or -1 (it ran out or was cancelled).
	int find_timer(uint32_t id) const {
		for (size_t i = 0; i < timers.size(); ++i)
			if (timers[i].id == id) return (int)i;
		return -1;
	}

	// Drops notifications whose time is up.
	void expire_notifications() {
		notifications.erase(std::remove_if(notifications.begin(), notifications.end(),
				[this](const TimeNotification &n) { return n.end_ms <= time_ms; }), notifications.end());
	}

	uint32_t hash() const {
		uint32_t h = 2166136261u;
		auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
		mix(time_ms); mix(next_timer_id); mix(init_game_done); mix(difficulty);
		mix((uint32_t)triggers.size());
		for (const auto &r : triggers) { mix(r.action); mix(r.event); mix(r.enabled); mix(r.timer_id); }
		mix((uint32_t)timers.size());
		for (const auto &t : timers) {
			uint32_t arg_bits;
			std::memcpy(&arg_bits, &t.arg, sizeof(arg_bits));
			mix(t.id); mix(t.time_ms); mix(t.repeat_ms); mix(t.action); mix(arg_bits);
		}
		mix((uint32_t)points.size());
		for (int32_t v : vars) mix((uint32_t)v);
		for (const auto &p : disabled_techs)
			for (uint64_t w : p) { mix((uint32_t)w); mix((uint32_t)(w >> 32)); }
		for (const BldMask &m : disabled_buildings)
			for (uint64_t w : m.w) { mix((uint32_t)w); mix((uint32_t)(w >> 32)); }
		mix((uint32_t)disabled_templates.size());
		for (uint16_t t : disabled_templates) mix(t);
		mix((uint32_t)invulnerable.size());
		for (uint32_t u : invulnerable) mix(u);
		mix((uint32_t)watched.size());
		for (const auto &w : watched) { mix(w.id); mix(w.owner); mix(w.role); }
		mix((uint32_t)range_queries.size());
		for (const auto &q : range_queries) {
			mix(q.trigger); mix(q.point); mix(q.players); mix((uint32_t)q.collection.size());
			for (uint32_t e : q.collection) mix(e);
		}
		mix((uint32_t)attack_suppressed.size());
		for (const auto &e : attack_suppressed) { mix(e.time_ms); mix(e.player); mix(e.domestic); }
		mix((uint32_t)attack_orders.size());
		for (const auto &o : attack_orders) {
			mix(o.fauna_id); mix(o.count);
			for (uint32_t t : o.targets) mix(t);
		}
		return h;
	}

private:
	uint32_t add_timer(uint32_t delay_ms, uint32_t repeat_ms, uint16_t action, uint16_t trigger, float arg) {
		TriggerTimer t;
		t.id = next_timer_id++;
		t.time_ms = time_ms + delay_ms;
		t.repeat_ms = repeat_ms;
		t.action = action;
		t.trigger = trigger;
		t.arg = arg;
		timers.push_back(t);
		return t.id;
	}
};

} // namespace rtso

#endif // RTSO_TRIGGERS_H
