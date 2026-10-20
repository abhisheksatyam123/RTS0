// Fog-of-war presentation for the local viewer: mirages and the LOS shading bitmap.
//
// Derived from 0 A.D., GPL-2.0-or-later (Copyright (C) Wildfire Games):
//   simulation/components/Fogging.js, simulation/components/Mirage.js,
//   CCmpRangeManager::ComputeLosVisibility (source/simulation2/components/CCmpRangeManager.cpp),
//   CLOSTexture::GenerateBitmap / InterpolateLOS (source/graphics/LOSTexture.cpp).
//
// Presentation only. Nothing here is hashed, saved or sent over the lockstep net:
// it reads Simulation::fow_grid(viewer) and the entity lists and keeps what the
// viewer last saw. Pure C++ (no Godot) so tools/test_fog_mirage.cpp can drive it.
#ifndef RTSO_FOG_MIRAGE_H
#define RTSO_FOG_MIRAGE_H

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rtso {

class Simulation;

// The viewer's Simulation::FOW_* state per LOS cell (sim.fow_res() squared) from
// the simulation's per-player line of sight (Simulation::los(), 0 A.D.'s
// CCmpRangeManager LOS with VisionSharing), the same LOS the sim and Petra use.
void los_view_grid(const Simulation &sim, uint8_t viewer, std::vector<uint8_t> &out);
// The whole map shows: "Revealed map", an observer (OBSERVER_VIEW, 0xFF) or a
// defeated player (0 A.D. reveals the map to both).
bool los_sees_all(const Simulation &sim, uint8_t viewer);
// A unit the viewer sees: an ally's always, others in the viewer's LOS.
bool los_sees_unit(const Simulation &sim, uint8_t viewer, float x, float z, uint8_t team);
// An animal the viewer sees: template_unit_fauna.xml RetainInFog keeps it shown, picked and
// ordered on in explored fog (VIS_FOGGED), hidden only where the viewer never explored.
bool los_sees_fauna(const Simulation &sim, uint8_t viewer, float x, float z, uint8_t owner);

// A structure as the viewer knows it: live when in sight, a mirage when fogged.
struct FoggedBuilding {
	uint32_t id = 0;
	float x = 0.0f, z = 0.0f;
	float half_size = 0.0f;
	float rot = 0.0f; // Simulation::Building::rot
	float hp = 0.0f, max_hp = 0.0f;
	float build_progress = 1.0f;
	float capture_points = 0.0f, max_capture_points = 0.0f; // Capturable, mirrored like Health
	uint8_t type = 0;
	uint8_t team = 0;
	uint8_t civ = 0;
	bool committed = false;
	bool wrecked = false;
	bool is_burning = false;
	bool upgrading = false; // Upgrade.js in progress: the actor's "upgrading" variant
	uint8_t production = 0; // ProductionQueue.js SetAnimation: 1 "researching", 2 "training"
	bool miraged = false; // true: last-seen copy (0 A.D. Mirage entity), not the live structure
};

// A resource supply (tree, mine, bush) as the viewer knows it.
struct FoggedResource {
	uint32_t id = 0;
	float x = 0.0f, z = 0.0f;
	int32_t amount = 0;
	uint16_t gatherers = 0; // ResourceSupply numGatherers, frozen with the amount (ResourceSupplyMirage)
	uint8_t type = 0;
	uint8_t subtype = 0;
	uint8_t flora = 0; // FloraKind
	uint8_t fauna = 0; // a carcass's animal (fauna type + 1), 0 none
	uint8_t owner = 0xFF; // a carcass's owner (special/filter/resource.xml keeps Ownership)
	bool miraged = false;
};

// A treasure as the viewer knows it. Treasure.Reward activates its Fogging, so a
// treasure taken out of the viewer's sight stays drawn in the fog until seen again.
struct FoggedTreasure {
	uint32_t id = 0;
	float x = 0.0f, z = 0.0f;
	uint8_t type = 0;
	bool miraged = false;
};

// 0 A.D. Fogging.js + Mirage.js for one viewer. Structures and resource supplies
// carry the Fogging component in 0 A.D.; units do not (they just vanish in fog).
//
//  - Own and allied structures always show live (RetainInFog, owner's LOS).
//  - Anything else in a visible cell shows live, and the viewer remembers it.
//  - In an explored but fogged cell the viewer sees the remembered copy (a
//    mirage), or nothing if it never saw the entity (built in the fog).
//  - A mirage outlives its entity until the viewer sees the spot again
//    (Mirage.OnVisibilityChanged destroys it once VIS_HIDDEN).
//  - Unexplored cells show nothing.
class FogMirages {
public:
	void reset();

	// Rebuild the drawable lists from the sim. Cheap to call every frame: it only
	// does work when the sim ticked, the viewer changed or the map changed.
	void update(const Simulation &sim, uint8_t viewer);

	// The structures and resources the viewer should see, live or miraged.
	const std::vector<FoggedBuilding> &buildings() const { return out_buildings_; }
	const std::vector<FoggedResource> &resources() const { return out_resources_; }
	const std::vector<FoggedTreasure> &treasures() const { return out_treasures_; }
	// The viewer's FOW grid update() last built (los_view_grid), fow_res() squared.
	const std::vector<uint8_t> &view() const { return view_; }
	size_t building_mirage_count() const { return building_memory_.size(); }
	size_t resource_mirage_count() const { return resource_memory_.size(); }
	size_t treasure_mirage_count() const { return treasure_memory_.size(); }

	// Low-level API, used by update() and by tests with a hand-made LOS grid.
	// los is res*res cells of Simulation::FOW_* states over [-map_half, map_half]^2.
	void begin(const uint8_t *los, int res, float map_half);
	// seen_before: the sim's Fogging seen bit (Building::seen_mask) for a structure this cache has
	// no copy of, after a load, a rejoin or a new viewer: its mirage starts from how it stands now.
	void add_building(const FoggedBuilding &live, bool allied, bool seen_before = false);
	void add_resource(const FoggedResource &live);
	void add_treasure(const FoggedTreasure &live);
	void end();

	uint8_t los_at(float x, float z) const;

private:
	std::unordered_map<uint32_t, uint16_t> gatherers_; // update()'s scratch: supply id -> gatherers
	struct BuildingMemory {
		FoggedBuilding seen;
		uint32_t stamp = 0;
	};
	struct ResourceMemory {
		FoggedResource seen;
		uint32_t stamp = 0;
	};
	struct TreasureMemory {
		FoggedTreasure seen;
		uint32_t stamp = 0;
	};

	const uint8_t *los_ = nullptr;
	std::vector<uint8_t> view_;
	int res_ = 0;
	float map_half_ = 0.0f;
	uint32_t stamp_ = 0;

	std::unordered_map<uint32_t, BuildingMemory> building_memory_;
	std::unordered_map<uint32_t, ResourceMemory> resource_memory_;
	std::unordered_map<uint32_t, TreasureMemory> treasure_memory_;
	std::vector<FoggedBuilding> out_buildings_;
	std::vector<FoggedResource> out_resources_;
	std::vector<FoggedTreasure> out_treasures_;

	// update() cache key
	bool have_key_ = false;
	uint8_t key_viewer_ = 0;
	uint64_t key_ticks_ = 0;
	size_t key_buildings_ = 0, key_resources_ = 0, key_treasures_ = 0;
	float key_half_ = 0.0f;
	int key_res_ = 0;
};

// 0 A.D. CLOSTexture: one byte per LOS cell, 255 visible / 127 explored /
// 0 unexplored, blurred with a 7-tap binomial filter, then eased toward each
// new bitmap over about half a second (InterpolateLOS, los_interp.fs).
class LosShading {
public:
	static constexpr int BLUR = 7;

	// Rebuild the target bitmap from a FOW grid (called when the fog changed).
	void set_target(const uint8_t *fow, int res);
	// Ease the displayed bitmap toward the target. Returns false once it has
	// settled on the target, so the caller can skip the texture upload.
	bool interpolate(float dt);
	// Jump straight to the target (map load, viewer switch).
	void snap();

	int res() const { return res_; }
	const std::vector<uint8_t> &target() const { return target_; }
	const std::vector<uint8_t> &shown() const { return shown_; }

	// CLOSTexture::GenerateBitmap, exposed for tests.
	static void generate_bitmap(const uint8_t *fow, int res, std::vector<uint8_t> &out);

private:
	int res_ = 0;
	float since_recompute_ = 0.0f;
	std::vector<uint8_t> target_;
	std::vector<uint8_t> shown_;
	bool settled_ = true;
	std::vector<uint8_t> last_fow_;
};

} // namespace rtso

#endif
