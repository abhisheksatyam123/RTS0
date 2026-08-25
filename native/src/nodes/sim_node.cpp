#include "sim_node.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

RtsoSimNode::RtsoSimNode() {
	sim = new rtso::Simulation();
}
RtsoSimNode::~RtsoSimNode() {
	delete sim;
}

void RtsoSimNode::_bind_methods() {
	ClassDB::bind_method(D_METHOD("spawn_initial"), &RtsoSimNode::spawn_initial);
	ClassDB::bind_method(D_METHOD("tick", "dt"), &RtsoSimNode::tick);
	ClassDB::bind_method(D_METHOD("cmd_move", "unit_ids", "target"), &RtsoSimNode::cmd_move);
	ClassDB::bind_method(D_METHOD("cmd_build_house", "pos"), &RtsoSimNode::cmd_build_house);
	ClassDB::bind_method(D_METHOD("get_gold"), &RtsoSimNode::get_gold);
	ClassDB::bind_method(D_METHOD("get_unit_count"), &RtsoSimNode::get_unit_count);
	ClassDB::bind_method(D_METHOD("get_building_count"), &RtsoSimNode::get_building_count);
	ClassDB::bind_method(D_METHOD("get_unit_position", "idx"), &RtsoSimNode::get_unit_position);
	ClassDB::bind_method(D_METHOD("get_unit_ids"), &RtsoSimNode::get_unit_ids);
	ClassDB::bind_method(D_METHOD("get_tick_us_avg"), &RtsoSimNode::get_tick_us_avg);
}

void RtsoSimNode::_ready() {
	spawn_initial();
	UtilityFunctions::print("[RtsoSimNode] ready — C++ SoA sim (arena ", (int)rtso::Simulation::MAP_HALF * 2, "m) gold=", get_gold(), " units=", get_unit_count(), ")");
}

void RtsoSimNode::_process(double delta) {
	accumulator += delta;
	int ticks = 0;
	while (accumulator >= rtso::Simulation::FIXED_STEP) {
		sim->tick(rtso::Simulation::FIXED_STEP);
		accumulator -= rtso::Simulation::FIXED_STEP;
		++ticks;
		if (ticks > 8) break; // spiral guard
	}
}

void RtsoSimNode::spawn_initial() { sim->spawn_initial(); }
void RtsoSimNode::tick(float dt) { sim->tick(dt); }

void RtsoSimNode::cmd_move(const PackedInt32Array &unit_ids, Vector3 target) {
	if (unit_ids.size() == 0) return;
	std::vector<uint32_t> ids;
	ids.reserve(unit_ids.size());
	for (int i = 0; i < unit_ids.size(); ++i) ids.push_back((uint32_t)unit_ids[i]);
	sim->cmd_move(ids.data(), ids.size(), target.x, target.z);
}
void RtsoSimNode::cmd_build_house(Vector3 pos) { sim->cmd_build_house(pos.x, pos.z); }

int RtsoSimNode::get_gold() const { return sim->gold(); }
int RtsoSimNode::get_unit_count() const { return (int)sim->unit_count(); }
int RtsoSimNode::get_building_count() const { return (int)sim->building_count(); }
Vector3 RtsoSimNode::get_unit_position(int idx) const {
	auto v = sim->units();
	if (idx < 0 || (size_t)idx >= v.count) return Vector3();
	return Vector3(v.pos_x[idx], 0, v.pos_z[idx]);
}
PackedInt32Array RtsoSimNode::get_unit_ids() const {
	PackedInt32Array out;
	auto v = sim->units();
	for (size_t i = 0; i < v.count; ++i) out.push_back((int)v.id[i]);
	return out;
}
double RtsoSimNode::get_tick_us_avg() const {
	auto s = sim->stats();
	if (s.ticks == 0) return 0;
	return (double)s.tick_us / (double)s.ticks;
}
