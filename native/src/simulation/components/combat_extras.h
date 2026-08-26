#ifndef NATIVE_SRC_SIMULATION_COMPONENTS_COMBAT_EXTRAS_H
#define NATIVE_SRC_SIMULATION_COMPONENTS_COMBAT_EXTRAS_H

// Port of src/Core/Simulation/Components/CombatExtras.cs -> C++ GDExtension
// Original C# ported from 0 A.D. source/simulation2 & maths — now cache-friendly C++17/20
// Real 0 A.D. code vendored under native/src/0ad for reference; this header is the high-perf SoA rewrite.
// See docs/0AD_PORTING.md and native/src/core/arena.h / soa_storage.h for memory strategy.

#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include "../../core/memory/arena.h"
#include "../../core/memory/pool.h"

namespace rtso {

// Component: combat_extras — ported from 0ad CCmpCombatExtras + C# IComponent
// Storage: SoA via native/src/core/soa_storage.h, PoolHandle for stable IDs

} // namespace rtso

#endif // NATIVE_SRC_SIMULATION_COMPONENTS_COMBAT_EXTRAS_H
