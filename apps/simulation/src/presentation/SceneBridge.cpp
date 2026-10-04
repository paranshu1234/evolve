#include "presentation/SceneBridge.h"

namespace evolve {

SceneSnapshot buildSceneSnapshot(const Project& project, std::size_t selected,
    bool compare, bool showGrid) {
    SceneSnapshot snapshot;
    snapshot.revision = project.revision();
    snapshot.sequence = project.sequence();
    snapshot.baseline = project.baseline();
    snapshot.selected = selected;
    snapshot.compare = compare;
    snapshot.showGrid = showGrid;
    snapshot.mesh = buildHelix(snapshot.sequence, snapshot.baseline,
        snapshot.selected, snapshot.compare, snapshot.showGrid);
    return snapshot;
}

Vec3 coreToUnrealPosition(Vec3 position) {
    return coreToUnrealDirection(position) * UnrealCentimetersPerDisplayUnit;
}

Vec3 coreToUnrealDirection(Vec3 direction) {
    return {direction.z, direction.x, direction.y};
}

} // namespace evolve
