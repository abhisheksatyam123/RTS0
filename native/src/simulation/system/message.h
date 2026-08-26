#ifndef NATIVE_SRC_SYSTEM_MESSAGE_H
#define NATIVE_SRC_SYSTEM_MESSAGE_H

// Port of src/Core/System/Message.cs -> C++ GDExtension
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

// System: entity/component manager — SoA port of 0ad ComponentManager

} // namespace rtso

#endif // NATIVE_SRC_SYSTEM_MESSAGE_H
