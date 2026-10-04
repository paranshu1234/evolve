#pragma once

#include "core/Geometry.h"
#include "core/Project.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace evolve {

// A renderer-owned value, not another editable project. Build a fresh snapshot
// after Project changes, and compare revision with Project::revision() before
// applying asynchronous results or picks from an older rendered scene.
//
// All strings, vertices and pick points are copied/owned by the snapshot. Mesh
// positions stay in the core's schematic display units (Y up). PickPoint::index
// is the zero-based index in sequence; both strands map to that same index.
// Baseline comparison geometry and the grid do not add selectable pick points.
struct SceneSnapshot {
    std::uint64_t revision{};
    std::string sequence;
    std::string baseline;
    std::size_t selected{};
    bool compare{};
    bool showGrid{};
    SceneMesh mesh;
};

// Presentation choices do not edit Project or advance its revision. As in
// buildHelix, a selected index outside the sequence means no highlighted base.
// Call while Project is stable: Project and snapshot construction are not
// synchronized for concurrent access. The returned value can then be moved to
// another thread without retaining any reference to Project.
SceneSnapshot buildSceneSnapshot(const Project& project, std::size_t selected,
    bool compare, bool showGrid);

// Unreal bridge contract: UE uses Z up and centimeters. The core is schematic,
// not molecular/atomic coordinates. One display unit is rendered as 100 cm:
//   UE X = core Z * 100, UE Y = core X * 100, UE Z = core Y * 100.
// This cyclic axis permutation preserves triangle winding. Convert positions
// and pick points with coreToUnrealPosition; convert normals/directions without
// scaling using coreToUnrealDirection. Color channels are never transformed.
inline constexpr float UnrealCentimetersPerDisplayUnit = 100.0f;
Vec3 coreToUnrealPosition(Vec3 position);
Vec3 coreToUnrealDirection(Vec3 direction);

} // namespace evolve
