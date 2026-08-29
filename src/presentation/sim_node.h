#ifndef RTSO_SIM_NODE_H
#define RTSO_SIM_NODE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include "../simulation/simulation.h"

using namespace godot;

namespace rtso {
class Simulation;
}

class RtsoSimNode : public Node3D {
	GDCLASS(RtsoSimNode, Node3D);

	rtso::Simulation *sim = nullptr;
	double accumulator = 0.0;

protected:
	static void _bind_methods();

public:
	RtsoSimNode();
	~RtsoSimNode();

	void _ready() override;
	void _process(double delta) override;

	// GDScript/C# API — lockstep commands
	void spawn_initial();
	void generate_random_map(int map_type = 5, int seed = 0, int num_players = 2, float map_half = 64.0f);
	void tick(float dt);
	void cmd_move(const PackedInt32Array &unit_ids, Vector3 target);
	void cmd_build_house(Vector3 pos);
	int get_gold() const;
	int get_unit_count() const;
	int get_building_count() const;
	Vector3 get_unit_position(int idx) const;
	PackedInt32Array get_unit_ids() const;
	double get_tick_us_avg() const;
};

#endif
