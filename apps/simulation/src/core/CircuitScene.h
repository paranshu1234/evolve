#pragma once

#include "CircuitTrace.h"
#include <iosfwd>
#include <string>
#include <vector>

namespace evolve::circuit {

// Portable display list shared by native GDI and the SVG preview. Text y values
// are baselines. Colors are #RRGGBB or "none"; all geometry is in canvas units.
enum class SceneCommandType { Rect, Line, Polyline, Text };
enum class TextAnchor { Start, Middle, End };
struct ScenePoint { double x{}, y{}; };
struct SceneCommand {
    SceneCommandType type{SceneCommandType::Rect};
    double x{}, y{}, width{}, height{}, radius{};
    std::string fill{"none"}, stroke{"none"}, text;
    double strokeWidth{1.0}, fontSize{12.0};
    bool bold{};
    TextAnchor textAnchor{TextAnchor::Start};
    std::vector<ScenePoint> points;
    std::vector<double> dashPattern;
};
struct CircuitScene {
    double width{1100.0}, height{720.0};
    std::vector<SceneCommand> commands;
};

// Reads the trace without modifying it. Replay time must be finite and is
// clamped to the available interval. Dense traces are min/max decimated for
// bounded geometry while retaining their complete time span.
CircuitScene buildCircuitScene(const CircuitTrace& trace, double replayTimeMinutes);
void writeSceneSvg(const CircuitScene& scene, std::ostream& output);

} // namespace evolve::circuit
