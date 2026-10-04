#include "presentation/SceneBridge.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

bool near(float a, float b) { return std::abs(a - b) < 0.001f; }
bool same(evolve::Vec3 a, evolve::Vec3 b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}
bool finite(evolve::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool sameMesh(const evolve::SceneMesh& a, const evolve::SceneMesh& b) {
    if (a.vertices.size() != b.vertices.size() || a.pickPoints.size() != b.pickPoints.size()) return false;
    for (std::size_t i = 0; i < a.vertices.size(); ++i) {
        if (!same(a.vertices[i].position, b.vertices[i].position)
            || !same(a.vertices[i].normal, b.vertices[i].normal)
            || !same(a.vertices[i].color, b.vertices[i].color)) return false;
    }
    for (std::size_t i = 0; i < a.pickPoints.size(); ++i) {
        if (a.pickPoints[i].index != b.pickPoints[i].index
            || !same(a.pickPoints[i].position, b.pickPoints[i].position)) return false;
    }
    return true;
}

void requireProjectParity(const evolve::Project& project, const evolve::SceneSnapshot& snapshot) {
    require(snapshot.revision == project.revision(), "snapshot captures authoritative revision");
    require(snapshot.sequence == project.sequence(), "snapshot captures current scenario");
    require(snapshot.baseline == project.baseline(), "snapshot captures baseline");
    require(sameMesh(snapshot.mesh, evolve::buildHelix(project.sequence(), project.baseline(),
        snapshot.selected, snapshot.compare, snapshot.showGrid)), "snapshot mesh matches portable core");
}

void testSnapshotAndHistory() {
    using namespace evolve;
    Project project;
    const auto initial = buildSceneSnapshot(project, 0, false, false);
    requireProjectParity(project, initial);
    require(initial.selected == 0 && !initial.compare && !initial.showGrid, "view options captured");
    const auto original = project.sequence();
    const auto originalRevision = project.revision();
    auto current = project.analyze();
    require(project.accept(current), "initial result accepted by Project");

    const auto viewOnly = buildSceneSnapshot(project, 2, true, true);
    requireProjectParity(project, viewOnly);
    require(viewOnly.selected == 2 && viewOnly.compare && viewOnly.showGrid, "view options retained");
    require(project.revision() == originalRevision && project.hasResult()
        && !project.canUndo() && !project.canRedo(), "view construction leaves state and history unchanged");

    require(project.edit(0, 'C'), "Project performs edit");
    const auto edited = buildSceneSnapshot(project, 0, true, false);
    requireProjectParity(project, edited);
    require(edited.revision > initial.revision && edited.sequence[0] == 'C', "edit appears with newer revision");
    require(edited.baseline == original && initial.sequence == original, "baseline and old snapshot survive edit");
    require(!project.accept(current) && !project.hasResult(), "Project rejects analysis from old snapshot revision");
    current = project.analyze();
    require(current.revision == edited.revision && current.edits == 1 && project.accept(current),
        "edited analysis uses snapshot revision");

    require(project.undo(), "Project performs undo");
    const auto undone = buildSceneSnapshot(project, 0, false, false);
    requireProjectParity(project, undone);
    require(undone.sequence == initial.sequence && undone.revision > edited.revision,
        "undo restores sequence with a new revision");
    require(sameMesh(undone.mesh, initial.mesh), "undo restores original geometry");
    require(!project.hasResult() && !project.accept(current), "undo invalidates edited analysis");
    const auto undoResult = project.analyze();
    require(project.accept(undoResult), "undo analysis accepted");

    require(project.redo(), "Project performs redo");
    const auto redone = buildSceneSnapshot(project, 0, true, false);
    requireProjectParity(project, redone);
    require(redone.sequence == edited.sequence && redone.revision > undone.revision,
        "redo restores edit with a new revision");
    require(sameMesh(redone.mesh, edited.mesh), "redo restores edited geometry");
    require(!project.hasResult() && !project.accept(current) && !project.accept(undoResult),
        "redo rejects results even when an old sequence matches");
    require(project.accept(project.analyze()), "current redo analysis accepted");

    const auto revision = project.revision();
    require(!project.edit(0, 'C') && project.revision() == revision && project.hasResult(),
        "no-op edit keeps revision and accepted result");
    require(project.restoreBaseline(), "Project restores baseline");
    requireProjectParity(project, buildSceneSnapshot(project, 1, false, true));
    require(project.sequence() == original && project.revision() > revision && !project.hasResult(),
        "restoring baseline invalidates results");

    project.importSequence(">new\nACGTACGT\n");
    requireProjectParity(project, buildSceneSnapshot(project, 7, false, true));
    require(!project.canUndo() && !project.canRedo() && initial.sequence == original,
        "import resets Project history without changing old snapshot");
}

void testOwnedCopiesAndPresentationOptions() {
    using namespace evolve;
    Project project;
    const auto initial = buildSceneSnapshot(project, 0, false, false);
    auto duplicate = initial;
    require(initial.sequence.data() != duplicate.sequence.data()
        && initial.baseline.data() != duplicate.baseline.data()
        && initial.mesh.vertices.data() != duplicate.mesh.vertices.data()
        && initial.mesh.pickPoints.data() != duplicate.mesh.pickPoints.data(), "snapshots own independent storage");
    duplicate.sequence[0] = 'C';
    duplicate.baseline[0] = 'G';
    duplicate.mesh.vertices[0].position.x += 1000.0f;
    duplicate.mesh.pickPoints[0].index = MaxBases;
    require(initial.sequence == project.sequence() && initial.baseline == project.baseline()
        && sameMesh(initial.mesh, buildHelix(project.sequence(), project.baseline(), 0, false, false)),
        "modifying a copy cannot mutate Project or original mesh");

    const auto noSelection = buildSceneSnapshot(project, std::numeric_limits<std::size_t>::max(), false, false);
    require(noSelection.selected == std::numeric_limits<std::size_t>::max(), "out of range selection retained as no highlight");
    require(!sameMesh(initial.mesh, noSelection.mesh), "selected highlight affects geometry");
    const auto grid = buildSceneSnapshot(project, 0, false, true);
    const auto compare = buildSceneSnapshot(project, 0, true, false);
    const auto both = buildSceneSnapshot(project, 0, true, true);
    require(grid.mesh.vertices.size() > initial.mesh.vertices.size(), "grid adds geometry");
    require(compare.mesh.vertices.size() > initial.mesh.vertices.size(), "comparison adds baseline geometry");
    require(both.mesh.vertices.size() - compare.mesh.vertices.size()
        == grid.mesh.vertices.size() - initial.mesh.vertices.size(), "grid and comparison options are independent");
    for (const auto* snapshot : {&initial, &grid, &compare, &both}) {
        require(snapshot->mesh.pickPoints.size() == snapshot->sequence.size() * 2,
            "only scenario strand endpoints are pickable");
        for (std::size_t i = 0; i < snapshot->mesh.pickPoints.size(); ++i) {
            require(snapshot->mesh.pickPoints[i].index == i / 2, "both strand picks map to the scenario base index");
        }
    }

    const auto survivesProject = [] {
        Project temporary;
        temporary.importSequence("ACGT");
        return buildSceneSnapshot(temporary, 3, true, true);
    }();
    require(survivesProject.sequence == "ACGT" && survivesProject.mesh.pickPoints.size() == 8,
        "snapshot lifetime is independent of source Project");
}

void testUnrealCoordinateContract() {
    using namespace evolve;
    require(UnrealCentimetersPerDisplayUnit == 100.0f, "fixed schematic scale is 100 cm per display unit");
    require(same(coreToUnrealPosition({1, 2, 3}), {300, 100, 200}), "position axes and centimeter conversion");
    require(same(coreToUnrealPosition({-1, -2, -3}), {-300, -100, -200}), "negative position conversion");
    require(same(coreToUnrealPosition({0, 0, 0}), {0, 0, 0}), "origin preserved");
    require(same(coreToUnrealDirection({0, 1, 0}), {0, 0, 1}), "core Y up maps to UE Z up without normal scaling");
    const Vec3 a{1, 2, 3}, b{4, 5, 6};
    require(same(coreToUnrealDirection(cross(a, b)),
        cross(coreToUnrealDirection(a), coreToUnrealDirection(b))), "axis mapping preserves winding");

    Project project;
    project.importSequence("ACGT");
    for (bool compare : {false, true}) {
        const auto snapshot = buildSceneSnapshot(project, 3, compare, false);
        const auto& pick = snapshot.mesh.pickPoints.front();
        require(pick.index == 0, "converted pick keeps first base index");
        require(same(coreToUnrealPosition(pick.position), {0, compare ? 440.0f : 160.0f, -93.0f}),
            "pick position uses same centimeter mapping as visible mesh");
        const auto upper = coreToUnrealPosition(snapshot.mesh.pickPoints[2].position);
        require(near(upper.z, -31.0f), "base spacing maps to 62 centimeters on the vertical axis");
        for (const auto& vertex : snapshot.mesh.vertices) {
            const auto normal = coreToUnrealDirection(vertex.normal);
            require(near(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z, 1.0f),
                "normal remains unit length after axis conversion");
        }
    }
}

void testFiniteBoundedMesh() {
    using namespace evolve;
    Project project;
    for (const std::size_t count : {std::size_t{4}, MaxBases}) {
        project.importSequence(std::string(count, 'A'));
        const auto snapshot = buildSceneSnapshot(project, count - 1, true, true);
        require(!snapshot.mesh.vertices.empty() && snapshot.mesh.vertices.size() % 3 == 0,
            "portable snapshot contains a triangle list");
        require(snapshot.mesh.vertices.size() < 2000000, "maximum geometry allocation remains bounded");
        require(snapshot.mesh.pickPoints.size() == count * 2, "pick count bounded by authoritative sequence");
        const auto limit = std::numeric_limits<float>::max();
        Vec3 low{limit, limit, limit}, high{-limit, -limit, -limit};
        bool allFinite = true, validColors = true, picksInBounds = true;
        for (const auto& vertex : snapshot.mesh.vertices) {
            const auto position = coreToUnrealPosition(vertex.position);
            allFinite = allFinite && finite(position) && finite(coreToUnrealDirection(vertex.normal));
            validColors = validColors && finite(vertex.color)
                && vertex.color.x >= 0 && vertex.color.x <= 1
                && vertex.color.y >= 0 && vertex.color.y <= 1
                && vertex.color.z >= 0 && vertex.color.z <= 1;
            low = {std::min(low.x, position.x), std::min(low.y, position.y), std::min(low.z, position.z)};
            high = {std::max(high.x, position.x), std::max(high.y, position.y), std::max(high.z, position.z)};
        }
        require(allFinite && finite(low) && finite(high), "UE mesh and bounds are finite at minimum and maximum sizes");
        require(validColors, "vertex colors stay normalized RGB");
        require(low.x < high.x && low.y < high.y && low.z < high.z, "bounds have nonzero extent");
        require(low.x >= -801 && high.x <= 801 && low.y >= -801 && high.y <= 801
            && low.z >= -8100 && high.z <= 8100, "centimeter extents remain inside schematic limits");
        for (const auto& pick : snapshot.mesh.pickPoints) {
            const auto position = coreToUnrealPosition(pick.position);
            picksInBounds = picksInBounds && finite(position) && pick.index < count
                && position.x >= low.x && position.x <= high.x
                && position.y >= low.y && position.y <= high.y
                && position.z >= low.z && position.z <= high.z;
        }
        require(picksInBounds, "all pick points retain valid indices and lie inside visible bounds");
    }
}
} // namespace

int main() {
    try {
        testSnapshotAndHistory();
        testOwnedCopiesAndPresentationOptions();
        testUnrealCoordinateContract();
        testFiniteBoundedMesh();
        std::cout << "PASS: " << checks << " scene bridge checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
