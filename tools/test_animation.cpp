// Stream J: animation clip coverage check (standalone, no Godot runtime).
// Mirrors kPsaAliasTable in src/animation/psa_loader.cpp and the unit-type ->
// clip resolution table in src/animation/animation_controller.cpp::load().
// 0AD refs: source/graphics/UnitAnimation.h:60, UnitAnimation.cpp:113
// (SetAnimationState), public.zip art/variants/biped/attack_ranged_archer.xml,
// art/variants/biped/attack_melee_female.xml, art/variants/biped/death_infantry.xml.
#include <iostream>
#include <string>
#include <utility>

static const std::pair<const char*, const char*> kExpectedResolutions[] = {
	{"citizen_idle", "citizen_idle"},
	{"citizen_run", "citizen_run"},
	{"spearman_attack", "biped_attack"},
	{"archer_attack", "archer_attack"},
	{"cavalry_run", "cavalry_walk"},
	{"horse_attack", "horse_attack"},
	{"trader_run", "trader_walk"},
	{"trader_attack", "biped_attack"},
	{"trireme_run", "trireme_walk"},
	{"trireme_attack", "trireme_idle"},
	{"rowing_death", "rowing_idle"},
	{"female_attack", "biped_attack"},
	{"female_death", "biped_death"},
	{"hero_idle", "biped_idle"},
	{"sheep_attack", "sheep_idle"},
	{"war_dog_idle", "dog_idle"},
	{"bolt_shooter_attack", "bolt_attack"},
	{"catapult_walk", "catapult_idle"},
	{"scythed_chariot_run", "chariot_run"},
	{"healer_attack", "healer_heal"},
	{"cart_trader_idle", "camel_idle"},
	{"maur_trader_walk", "maur_ch_move"},
};

int main() {
	std::size_t n = sizeof(kExpectedResolutions) / sizeof(kExpectedResolutions[0]);
	std::cout << "Stream J clip coverage table entries: " << n << std::endl;
	for (std::size_t i = 0; i < n; ++i) {
		std::cout << "  " << kExpectedResolutions[i].first << " -> "
		          << kExpectedResolutions[i].second << std::endl;
	}
	std::cout << "Use python tools/test_animation.py for on-disk verification." << std::endl;
	return 0;
}
