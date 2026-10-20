#ifndef RTSO_ATTACK_DETECTION_H
#define RTSO_ATTACK_DETECTION_H

// Port of 0 A.D. simulation/components/AttackDetection.js (0 A.D. 61a3b95):
// decides which attacks on the local player raise an "under attack"
// notification. An attack within SuppressionRange of an attack notified in the
// last SuppressionTime is suppressed; one within SuppressionTransferRange moves
// that suppression to the new position. Values from template_player.xml
// (160 m, 80 m, 60 s), scaled to RTS0 world units (~4 m each, like ranges in
// sim_types.h). Presentation only: never read by the simulation.

#include <algorithm>
#include <vector>

namespace rtso {

class AttackDetection {
public:
	static constexpr float kMetresPerUnit = 4.0f;
	static constexpr float kSuppressionRange = 160.0f / kMetresPerUnit;
	static constexpr float kSuppressionTransferRange = 80.0f / kMetresPerUnit;
	static constexpr double kSuppressionTime = 60.0; // seconds

	// An attack on one of our entities at (x, z) at game time `t` (seconds).
	// Returns true when it should be notified (AttackAlert's PushNotification).
	bool report(float x, float z, double t, bool domestic_animal = false) {
		expire(t);
		bool priority_increased = false;
		for (Event &e : suppressed_) {
			const float dx = x - e.x, dz = z - e.z;
			const float d2 = dx * dx + dz * dz;
			if (d2 >= kSuppressionRange * kSuppressionRange) continue;
			priority_increased = e.domestic && !domestic_animal;
			const bool priority_decreased = !e.domestic && domestic_animal;
			if (priority_increased || (!priority_decreased && d2 < kSuppressionTransferRange * kSuppressionTransferRange))
				e = {x, z, t, domestic_animal};
			if (priority_increased) break;
			return false;
		}
		if (!priority_increased) suppressed_.push_back({x, z, t, domestic_animal});
		return true;
	}

	void clear() { suppressed_.clear(); }
	size_t suppressed_count() const { return suppressed_.size(); }

private:
	struct Event {
		float x, z;
		double t;
		bool domestic;
	};
	// HandleTimeout: drop events older than SuppressionTime (and any from after a
	// load or rewind took the clock back).
	void expire(double t) {
		for (size_t i = 0; i < suppressed_.size();) {
			if (t - suppressed_[i].t >= kSuppressionTime || suppressed_[i].t > t) suppressed_.erase(suppressed_.begin() + (long)i);
			else ++i;
		}
	}
	std::vector<Event> suppressed_;
};

// Port of 0 A.D. simulation/components/BattleDetection.js with the
// template_player.xml values: damage the player deals to other players is
// summed per 200 ms period over the last 12 periods; when the rate exceeds
// 0.04 hp/ms alertness rises (up to 8), otherwise it falls. Alertness 4 means
// BATTLE, 0 means PEACE. Drives the battle music (gui/session/session.js:747).
class BattleDetection {
public:
	static constexpr double kInterval = 0.2; // TimerInterval, seconds
	static constexpr int kRecordLength = 12;
	static constexpr double kDamageRateThreshold = 0.04; // per millisecond
	static constexpr int kBattleThreshold = 4;
	static constexpr int kPeaceThreshold = 0;
	static constexpr int kAlertnessMax = 8;

	// Damage the player dealt to another (non-Gaia) player at game time t.
	void add_damage(float dmg, double t) {
		if (!timer_running_) {
			timer_running_ = true;
			next_tick_ = t; // StartTimer(0, interval)
			record_control_ = 0;
			damage_ = 0;
		}
		damage_ += dmg;
	}

	// Runs the 200 ms timer up to game time t.
	void update(double t) {
		if (next_tick_ > t + kInterval) next_tick_ = t; // a load or rewind took the clock back
		while (timer_running_ && t >= next_tick_) {
			next_tick_ += kInterval;
			tick();
		}
	}

	bool in_battle() const { return battle_; }
	int alertness() const { return alertness_; }

private:
	void tick() {
		record_.insert(record_.begin(), damage_);
		if ((int)record_.size() > kRecordLength) record_.resize(kRecordLength);
		damage_ = 0;
		if (!alertness_ || record_control_++ == kRecordLength - 1) {
			double sum = 0;
			for (double d : record_) sum += d;
			const double rate = sum / (kRecordLength * kInterval * 1000.0);
			if (rate > kDamageRateThreshold) alertness_ = std::min(kAlertnessMax, alertness_ + 1);
			else alertness_ = std::max(0, alertness_ - 1);
			if (!alertness_) timer_running_ = false;
			if (alertness_ >= kBattleThreshold) battle_ = true;
			else if (alertness_ <= kPeaceThreshold) battle_ = false;
		}
		if (record_control_ > kRecordLength - 1) record_control_ = 0;
	}

	std::vector<double> record_;
	double damage_ = 0;
	double next_tick_ = 0;
	int alertness_ = 0;
	int record_control_ = 0;
	bool timer_running_ = false;
	bool battle_ = false;
};

} // namespace rtso

#endif
