#include "core/CircuitScene.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& action, const char* message) {
    bool threw = false;
    try { action(); } catch (const std::exception&) { threw = true; }
    require(threw, message);
}
std::string traceHeader() {
    return "# evolve-circuit-trace-v1\n# model_id=BIOMD0000000012\n# model_sha256="
        + std::string(64, 'a')
        + "\n# solver=libRoadRunner 2.9.2 CVODE\n# time_unit=minute\n# quantity_unit=item_per_cell\n"
          "# note=<tag key=\"value\">&'test'\n"
          "time_minutes,X,Y,Z,PX,PY,PZ\n";
}
evolve::circuit::CircuitTrace traceFrom(const std::string& rows) {
    std::istringstream input(traceHeader() + rows);
    return evolve::circuit::CircuitTrace::read(input, "scene-test");
}
std::string svg(const evolve::circuit::CircuitScene& scene) {
    std::ostringstream output; evolve::circuit::writeSceneSvg(scene, output); return output.str();
}
bool containsText(const evolve::circuit::CircuitScene& scene, const std::string& needle) {
    return std::any_of(scene.commands.begin(), scene.commands.end(), [&](const auto& command) {
        return command.type == evolve::circuit::SceneCommandType::Text && command.text.find(needle) != std::string::npos;
    });
}
void checkGeometry(const evolve::circuit::CircuitScene& scene) {
    using evolve::circuit::SceneCommandType;
    require(scene.width == 1100 && scene.height == 720, "logical canvas dimensions");
    require(scene.commands.size() < 250, "bounded scene command count");
    bool finite = true, bounded = true;
    for (const auto& command : scene.commands) {
        for (const auto value : {command.x, command.y, command.width, command.height, command.radius,
                                  command.fontSize, command.strokeWidth})
            finite = finite && std::isfinite(value);
        if (command.type == SceneCommandType::Rect)
            bounded = bounded && command.x >= 0 && command.y >= 0
                && command.x + command.width <= scene.width && command.y + command.height <= scene.height;
        if (command.type == SceneCommandType::Text)
            bounded = bounded && command.x >= 0 && command.x <= scene.width && command.y >= 0 && command.y <= scene.height;
        for (const auto& point : command.points) {
            finite = finite && std::isfinite(point.x) && std::isfinite(point.y);
            bounded = bounded && point.x >= 0 && point.x <= scene.width && point.y >= 0 && point.y <= scene.height;
        }
    }
    require(finite, "all scene geometry is finite");
    require(bounded, "all scene geometry stays within canvas");
}
} // namespace

int main() {
    try {
        using namespace evolve::circuit;
        const auto trace = traceFrom("0,1,2,3,4,5,6\n5,7,8,9,10,11,12\n10,2,3,4,5,6,7\n");
        const auto originalRows = trace.rows(); const auto originalMetadata = trace.metadata().attributes;
        const auto scene = buildCircuitScene(trace, 2.5);
        checkGeometry(scene);
        for (const auto& label : {"The repressilator", "BIOMD0000000012", "LacI", "TetR", "cI",
                                  "LacI represses TetR; TetR represses cI; cI represses LacI", "Messenger RNA",
                                  "Protein", "items / cell", "Simulation time (min)", "REPLAY CLOCK",
                                  "Playback does not re-solve", "whole-cell or human HBB", "incomplete unit annotations",
                                  "libRoadRunner 2.9.2 CVODE", "DECLARED SBML SHA-256"})
            require(containsText(scene, label), "required scientific and provenance label present");
        require(containsText(scene, "4.0"), "gene card uses interpolated current mRNA value");
        int traces = 0, dashedTraces = 0, dottedTraces = 0;
        for (const auto& command : scene.commands) if (command.type == SceneCommandType::Polyline) {
            ++traces;
            if (command.dashPattern == std::vector<double>{7,4}) ++dashedTraces;
            if (command.dashPattern == std::vector<double>{2,4}) ++dottedTraces;
            require(command.points.size() == trace.rows().size(), "small traces retain every stored sample");
        }
        require(traces == 6 && dashedTraces == 2 && dottedTraces == 2, "six chart traces have accessible line styles");
        require(svg(scene) == svg(buildCircuitScene(trace, 2.5)), "rendering is deterministic");
        require(svg(buildCircuitScene(trace, -50)) == svg(buildCircuitScene(trace, 0)), "negative replay time clamps");
        require(svg(buildCircuitScene(trace, 500)) == svg(buildCircuitScene(trace, 10)), "late replay time clamps");
        rejects([&] { buildCircuitScene(trace, std::numeric_limits<double>::infinity()); }, "infinite time rejected");
        rejects([&] { buildCircuitScene(trace, std::numeric_limits<double>::quiet_NaN()); }, "NaN time rejected");
        require(originalMetadata == trace.metadata().attributes && trace.rows().size() == originalRows.size(), "rendering preserves trace metadata and row count");
        bool unchanged = true;
        for (std::size_t i = 0; i < originalRows.size(); ++i)
            unchanged = unchanged && trace.rows()[i].timeMinutes == originalRows[i].timeMinutes
                && trace.rows()[i].values == originalRows[i].values;
        require(unchanged, "rendering never changes stored numerical trajectory");
        checkGeometry(buildCircuitScene(traceFrom("0,0,0,0,0,0,0\n1,0,0,0,0,0,0\n"), 0.5));
        checkGeometry(buildCircuitScene(traceFrom("0,1e308,0,1e-200,0,0,0\n1e308,1e308,0,0,0,0,0\n"), 5e307));

        auto escaped = scene;
        SceneCommand untrusted; untrusted.type = SceneCommandType::Text;
        untrusted.text = trace.metadata().attributes.at("note");
        untrusted.fill = "\"<&'";
        escaped.commands.push_back(untrusted);
        const auto escapedSvg = svg(escaped);
        require(escapedSvg.find("&lt;tag key=&quot;value&quot;&gt;&amp;&apos;test&apos;") != std::string::npos,
                "untrusted metadata text is XML escaped");
        require(escapedSvg.find("fill=\"&quot;&lt;&amp;&apos;\"") != std::string::npos, "SVG attribute values escaped");
        require(escapedSvg.find("<tag") == std::string::npos, "metadata cannot create SVG elements");
        auto invalid = scene; invalid.commands[0].width = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { svg(invalid); }, "SVG writer rejects nonfinite geometry");
        invalid = scene; invalid.commands[0].dashPattern = {-1};
        rejects([&] { svg(invalid); }, "SVG writer rejects invalid dash lengths");

        std::ostringstream dense; dense << traceHeader();
        constexpr std::size_t count = 20000, spikeAt = 9991;
        for (std::size_t i = 0; i < count; ++i)
            dense << i << ',' << (i == spikeAt ? 1000 : 0) << ",0,0,0,0,0\n";
        std::istringstream denseInput(dense.str());
        const auto denseTrace = CircuitTrace::read(denseInput, "dense-scene-test");
        const auto denseScene = buildCircuitScene(denseTrace, 500);
        checkGeometry(denseScene);
        bool spikeRetained = false;
        for (const auto& command : denseScene.commands) if (command.type == SceneCommandType::Polyline) {
            require(command.points.size() <= 1442, "dense line geometry is bounded");
            require(command.points.front().x + 400 < command.points.back().x, "decimation retains complete time span");
            if (command.stroke == "#70DCC1")
                for (const auto point : command.points) spikeRetained = spikeRetained || point.y < 480;
        }
        require(spikeRetained, "extrema-preserving decimation retains narrow spikes");
        std::cout << "PASS: " << checks << " circuit scene checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
