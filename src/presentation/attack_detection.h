#ifndef RTSO_ATTACK_DETECTION_H
#define RTSO_ATTACK_DETECTION_H

// Port of 0 A.D. simulation/components/AttackDetection.js (0 A.D. 61a3b95):
// decides which attacks on the local player raise an "under attack"
// notification. An attack within SuppressionRange of an attack notified in the
// last SuppressionTime is suppressed; one within SuppressionTransferRange moves
// that suppression to the new position. Values from template_player.xml
// (160 m, 80 m, 60 s), scaled to RTS0 world units (~4 m each, like ranges in
// sim_types.h). Presentation only: never read by the simulation.

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
	// HandleTimeout: drop events older than SuppressionTime.
	void expire(double t) {
		for (size_t i = 0; i < suppressed_.size();) {
			if (t - suppressed_[i].t >= kSuppressionTime) suppressed_.erase(suppressed_.begin() + (long)i);
			else ++i;
		}
	}
	std::vector<Event> suppressed_;
};

} // namespace rtso

#endif
