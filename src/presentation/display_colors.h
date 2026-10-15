#ifndef RTSO_DISPLAY_COLORS_H
#define RTSO_DISPLAY_COLORS_H

// The colour each player is drawn in: a port of 0 A.D.'s
// gui/session/DiplomacyColors.js. Normally a player's Game Setup colour; with
// diplomacy colours on (session.diplomacycolors, the diplomacy dialog and
// minimap buttons), the viewer sees itself, its allies, neutrals and enemies
// in the gui.session.diplomacycolors.* colours of the options page, and an
// observer sees each team in the colour of its first active member. Gaia keeps
// its own colour. Presentation only: the simulation never reads it.

#include "simulation/simulation.h"
#include "user_config.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace rtso {
namespace display_colors {

struct State {
	bool enabled = false;
	uint8_t viewer = 0xFF; // the player whose stances pick the colours (0xFF: an observer)
	uint32_t self = 0x153795, ally = 0x56B41F, neutral = 0xE7C805, enemy = 0x961414;
	uint32_t config_generation = ~0u;
};

inline State &state() {
	static State s;
	return s;
}

// A ConfigDB colour, "R G B" in 0..255 (guiToRgbColor), else `fallback`.
inline uint32_t parse_rgb(const std::string &v, uint32_t fallback) {
	int r = -1, g = -1, b = -1;
	if (std::sscanf(v.c_str(), "%d %d %d", &r, &g, &b) != 3) return fallback;
	if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return fallback;
	return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// Re-reads the four colours when the options change (onConfigChange).
inline void refresh_config() {
	State &s = state();
	if (s.config_generation == user_config::generation()) return;
	s.config_generation = user_config::generation();
	auto read = [](const char *stance) {
		const std::string key = std::string("gui.session.diplomacycolors.") + stance;
		return parse_rgb(user_config::get(key), parse_rgb(user_config::default_value(key), 0xFFFFFFu));
	};
	s.self = read("self");
	s.ally = read("ally");
	s.neutral = read("neutral");
	s.enemy = read("enemy");
}

inline void toggle() { state().enabled = !state().enabled; }

// computeTeamColors: the 0xRRGGBB `pid` is drawn in.
inline uint32_t rgb(const Simulation &sim, uint8_t pid) {
	const State &s = state();
	if (!s.enabled || pid >= MAX_PLAYERS) return sim.player_color(pid);
	if (s.viewer >= MAX_PLAYERS) {
		// Observers see team colours: an active player takes its team's first
		// active member's colour (a teamless player's team is only itself).
		if (!sim.player_in_match(pid)) return sim.player_color(pid);
		const uint8_t team = sim.player_team(pid);
		for (uint8_t q = 0; q < pid; ++q)
			if (sim.player_in_match(q) && sim.player_team(q) == team) return sim.player_color(q);
		return sim.player_color(pid);
	}
	if (pid == s.viewer) return s.self;
	// The ceasefire makes every enemy neutral while it lasts (CeasefireManager.js).
	uint8_t st = sim.get_diplomacy(s.viewer, pid);
	if (st == Simulation::DIPLO_ENEMY && sim.ceasefire_active()) st = Simulation::DIPLO_NEUTRAL;
	if (st == Simulation::DIPLO_ALLY) return s.ally;
	if (st == Simulation::DIPLO_NEUTRAL) return s.neutral;
	return s.enemy;
}

// A key that changes whenever any player's drawn colour does (for caches).
inline uint64_t key(const Simulation &sim) {
	uint64_t k = 1469598103934665603ull;
	for (uint8_t p = 0; p < MAX_PLAYERS; ++p) k = (k ^ rgb(sim, p)) * 1099511628211ull;
	return k;
}

} // namespace display_colors
} // namespace rtso

#endif
