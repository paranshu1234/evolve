#include "CircuitScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace evolve::circuit {
namespace {
constexpr const char* Background = "#0B111B";
constexpr const char* Panel = "#121C29";
constexpr const char* Border = "#263448";
constexpr const char* Text = "#EEF4FB";
constexpr const char* Muted = "#A2B2C6";
constexpr const char* Dim = "#768CA6";
constexpr const char* Grid = "#273447";
constexpr const char* Amber = "#F2C477";
constexpr std::array<const char*, 3> Colors{{"#70DCC1", "#F2C477", "#A6AAFF"}};
constexpr std::array<const char*, 3> Names{{"LacI", "TetR", "cI"}};
constexpr std::size_t PlotBuckets = 360;

void rect(CircuitScene& scene, double x, double y, double w, double h,
          const std::string& fill, const std::string& stroke = "none", double radius = 0) {
    SceneCommand c; c.type = SceneCommandType::Rect; c.x = x; c.y = y;
    c.width = w; c.height = h; c.radius = radius; c.fill = fill; c.stroke = stroke;
    scene.commands.push_back(std::move(c));
}
void line(CircuitScene& scene, ScenePoint a, ScenePoint b, const std::string& color,
          double width = 1, std::vector<double> dash = {}) {
    SceneCommand c; c.type = SceneCommandType::Line; c.points = {a, b};
    c.stroke = color; c.strokeWidth = width; c.dashPattern = std::move(dash);
    scene.commands.push_back(std::move(c));
}
void text(CircuitScene& scene, double x, double y, const std::string& value,
          double size = 12, const std::string& color = Text, bool bold = false,
          TextAnchor anchor = TextAnchor::Start) {
    SceneCommand c; c.type = SceneCommandType::Text; c.x = x; c.y = y;
    c.text = value; c.fontSize = size; c.fill = color; c.bold = bold; c.textAnchor = anchor;
    scene.commands.push_back(std::move(c));
}
std::string shortText(std::string value, std::size_t count) {
    // Metadata is display-only; line breaks and control characters cannot alter
    // the layout. XML metacharacters are escaped by the SVG writer, not stripped.
    for (char& ch : value) if (static_cast<unsigned char>(ch) < 32) ch = ' ';
    if (value.size() > count) value = value.substr(0, count - 3) + "...";
    return value;
}
std::string number(double value, int precision = 1) {
    std::ostringstream out; out.imbue(std::locale::classic());
    if (std::abs(value) >= 1.0e6 || (value != 0 && std::abs(value) < 0.01))
        out << std::scientific << std::setprecision(1) << value;
    else out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}
std::vector<double> lineStyle(std::size_t series) {
    if (series == 1) return {7, 4};
    if (series == 2) return {2, 4};
    return {};
}
void inhibition(CircuitScene& scene, ScenePoint start, ScenePoint end, const char* color) {
    line(scene, start, end, color, 2.0);
    const double dx = end.x - start.x, dy = end.y - start.y;
    const double length = std::hypot(dx, dy);
    const double px = -dy / length * 7.0, py = dx / length * 7.0;
    line(scene, {end.x - px, end.y - py}, {end.x + px, end.y + py}, color, 2.5);
}
void node(CircuitScene& scene, double x, double y, std::size_t series) {
    rect(scene, x, y, 78, 36, "#1A2A39", Colors[series], 9);
    text(scene, x + 39, y + 24, Names[series], 16, Colors[series], true, TextAnchor::Middle);
}
// Keep first, local min, local max, and last in each bucket, in source order.
// This avoids the missed spikes of stride sampling and bounds each line to
// 4 * PlotBuckets + 2 points, independent of accepted file size.
std::vector<std::size_t> plotIndices(const std::vector<TraceRow>& rows, std::size_t series) {
    std::vector<std::size_t> indices;
    if (rows.size() <= PlotBuckets * 4) {
        indices.reserve(rows.size());
        for (std::size_t i = 0; i < rows.size(); ++i) indices.push_back(i);
        return indices;
    }
    indices.reserve(PlotBuckets * 4 + 2);
    for (std::size_t bucket = 0; bucket < PlotBuckets; ++bucket) {
        const std::size_t begin = bucket * rows.size() / PlotBuckets;
        const std::size_t end = (bucket + 1) * rows.size() / PlotBuckets;
        std::size_t minimum = begin, maximum = begin;
        for (std::size_t i = begin + 1; i < end; ++i) {
            if (rows[i].values[series] < rows[minimum].values[series]) minimum = i;
            if (rows[i].values[series] > rows[maximum].values[series]) maximum = i;
        }
        std::array<std::size_t, 4> candidates{{begin, minimum, maximum, end - 1}};
        std::sort(candidates.begin(), candidates.end());
        for (const auto i : candidates)
            if (indices.empty() || indices.back() != i) indices.push_back(i);
    }
    return indices;
}

void chart(CircuitScene& scene, const CircuitTrace& trace, double now, double x,
           std::size_t offset, const std::string& title, const std::string& quantity) {
    constexpr double Y = 390, W = 512, H = 242;
    const double left = x + 55, right = x + W - 19, top = Y + 72, bottom = Y + 193;
    rect(scene, x, Y, W, H, Panel, Border, 13);
    text(scene, x + 18, Y + 27, title, 17, Text, true);
    text(scene, x + 18, Y + 48, quantity + " (items / cell)", 10, Muted);
    for (std::size_t k = 0; k < 3; ++k) {
        const double lx = x + 230 + k * 87;
        line(scene, {lx, Y + 43}, {lx + 24, Y + 43}, Colors[k], 2, lineStyle(k));
        text(scene, lx + 30, Y + 47, Names[k], 11, Colors[k]);
    }
    const auto& rows = trace.rows();
    double maximum = 0;
    for (const auto& row : rows) for (std::size_t k = 0; k < 3; ++k)
        maximum = std::max(maximum, row.values[offset + k]);
    // Dividing by maximum before scaling avoids overflow with valid, very large
    // doubles. A zero-valued trace uses a stable [0,1] display range.
    const double scaleMaximum = maximum > 0 ? maximum : 1.0;
    const double start = rows.front().timeMinutes, end = rows.back().timeMinutes;
    const double duration = end - start;
    const auto px = [&](double time) {
        return left + (duration > 0 ? std::clamp((time - start) / duration, 0.0, 1.0) : 0.0) * (right - left);
    };
    const auto py = [&](double value) {
        return bottom - std::clamp(value / scaleMaximum, 0.0, 1.0) * (bottom - top - 8);
    };
    for (int tick = 0; tick <= 3; ++tick) {
        const double fraction = static_cast<double>(tick) / 3.0;
        const double yy = bottom - fraction * (bottom - top - 8);
        line(scene, {left, yy}, {right, yy}, Grid);
        text(scene, left - 9, yy + 4, number(scaleMaximum * fraction, maximum < 10 ? 1 : 0),
             10, Dim, false, TextAnchor::End);
    }
    const int lastTick = duration > 0 ? 4 : 0;
    for (int tick = 0; tick <= lastTick; ++tick) {
        const double fraction = static_cast<double>(tick) / 4.0;
        const double xx = left + fraction * (right - left);
        line(scene, {xx, bottom}, {xx, bottom + 4}, Dim);
        text(scene, xx, bottom + 18, number(start + duration * fraction, duration < 10 ? 1 : 0),
             10, Dim, false, TextAnchor::Middle);
    }
    // The bright cursor marks the replay time over the unchanged full trace.
    line(scene, {px(now), top - 4}, {px(now), bottom}, "#DBE5F2", 1, {3, 4});
    for (std::size_t k = 0; k < 3; ++k) {
        SceneCommand path; path.type = SceneCommandType::Polyline;
        path.stroke = Colors[k]; path.strokeWidth = 1.9; path.dashPattern = lineStyle(k);
        for (const auto i : plotIndices(rows, offset + k))
            path.points.push_back({px(rows[i].timeMinutes), py(rows[i].values[offset + k])});
        // A one-sample trace is a short visible mark, not an invalid empty path.
        if (path.points.size() == 1) {
            const auto point = path.points.front();
            path.points.push_back({point.x + 4, point.y});
        }
        scene.commands.push_back(std::move(path));
        const auto current = trace.sampleAt(now);
        const double cy = py(current.values[offset + k]);
        rect(scene, px(now) - 2.5, cy - 2.5, 5, 5, Colors[k], "none", 2);
    }
    text(scene, (left + right) / 2, Y + H - 13, "Simulation time (min)", 10, Muted,
         false, TextAnchor::Middle);
}
std::string escapeXml(const std::string& value) {
    std::string result; result.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&apos;"; break;
        default:
            if (static_cast<unsigned char>(ch) >= 32 || ch == '\n' || ch == '\r' || ch == '\t') result += ch;
            break;
        }
    }
    return result;
}
void requireFinite(double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Scene contains non-finite geometry");
}
} // namespace

CircuitScene buildCircuitScene(const CircuitTrace& trace, double replayTimeMinutes) {
    if (!std::isfinite(replayTimeMinutes)) throw std::invalid_argument("Replay time must be finite");
    if (trace.rows().empty()) throw std::invalid_argument("Cannot render an empty circuit trace");
    const double now = std::clamp(replayTimeMinutes, trace.rows().front().timeMinutes, trace.rows().back().timeMinutes);
    const auto current = trace.sampleAt(now);
    const auto& metadata = trace.metadata();
    CircuitScene scene;
    rect(scene, 0, 0, scene.width, scene.height, Background);
    text(scene, 28, 30, "EVOLVE  /  CIRCUIT WORKBENCH", 11, Colors[0], true);
    text(scene, 28, 70, "The repressilator", 32, Text, true);
    text(scene, 29, 96, "A three-gene oscillator in E. coli", 13, Muted);
    rect(scene, 790, 26, 282, 71, Panel, Border, 10);
    text(scene, 807, 47, "CURATED MODEL  /  EDUCATIONAL", 10, Muted, true);
    text(scene, 807, 70, shortText(metadata.modelId, 29), 17, Text, true);
    text(scene, 807, 86, "Elowitz & Leibler, 2000", 10, Dim);

    rect(scene, 28, 120, 414, 198, Panel, Border, 13);
    text(scene, 46, 145, "NEGATIVE FEEDBACK  /  T-BARS = REPRESSION", 10, Muted, true);
    inhibition(scene, {207, 204}, {159, 238}, Colors[0]);
    inhibition(scene, {159, 265}, {303, 265}, Colors[1]);
    inhibition(scene, {327, 244}, {278, 204}, Colors[2]);
    node(scene, 196, 166, 0);
    node(scene, 76, 247, 1);
    node(scene, 318, 247, 2);
    text(scene, 234, 306, "LacI represses TetR; TetR represses cI; cI represses LacI", 10, Dim, false, TextAnchor::Middle);
    for (std::size_t k = 0; k < 3; ++k) {
        const double x = 458 + static_cast<double>(k) * 210;
        rect(scene, x, 120, 194, 198, Panel, Border, 13);
        rect(scene, x + 16, 138, 3, 23, Colors[k], "none", 1);
        text(scene, x + 29, 156, Names[k], 21, Colors[k], true);
        text(scene, x + 17, 185, "mRNA", 11, Muted);
        text(scene, x + 17, 211, number(current.values[k]), 24, Text, true);
        line(scene, {x + 17, 226}, {x + 177, 226}, Border);
        text(scene, x + 17, 246, "Protein", 11, Muted);
        text(scene, x + 17, 273, number(current.values[k + 3]), 24, Text, true);
        text(scene, x + 17, 300, "items / cell", 10, Dim);
    }
    rect(scene, 28, 333, 1044, 42, "#152330", Border, 9);
    text(scene, 43, 358, "REPLAY CLOCK", 10, Colors[0], true);
    text(scene, 151, 360, number(now) + " / " + number(trace.rows().back().timeMinutes, 0) + " min", 17, Text, true);
    text(scene, 1055, 358, "Precomputed trajectory  |  Playback does not re-solve the model", 11, Muted, false, TextAnchor::End);

    chart(scene, trace, now, 28, 0, "Messenger RNA", "mRNA abundance");
    chart(scene, trace, now, 560, 3, "Protein", "Protein abundance");
    text(scene, 28, 655, "MODEL LIMITS", 10, Amber, true);
    text(scene, 130, 655, "Idealized deterministic six-state circuit; not a whole-cell or human HBB prediction.", 11, Muted);
    text(scene, 28, 677, "UNIT CAUTION", 10, Amber, true);
    text(scene, 130, 677, "Source SBML has incomplete unit annotations; items / cell follows the model convention.", 11, Muted);
    line(scene, {28, 690}, {1072, 690}, Border);
    text(scene, 28, 707, "SOLVER  " + shortText(metadata.solver, 45), 10, Dim);
    text(scene, 550, 707, "DECLARED SBML SHA-256  " + shortText(metadata.modelSha256, 19), 10, Dim);
    text(scene, 1072, 707, std::to_string(trace.rows().size()) + " stored samples", 10, Dim, false, TextAnchor::End);
    return scene;
}

void writeSceneSvg(const CircuitScene& scene, std::ostream& output) {
    requireFinite(scene.width); requireFinite(scene.height);
    if (scene.width <= 0 || scene.height <= 0) throw std::invalid_argument("Scene dimensions must be positive");
    // Format in a private stream so a caller's locale and stream flags cannot
    // corrupt SVG coordinates, and failed validation does not emit a partial SVG.
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(12);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << scene.width
        << "\" height=\"" << scene.height << "\" viewBox=\"0 0 " << scene.width << ' ' << scene.height << "\">\n"
        << "<title>Evolve repressilator circuit inspector</title>\n"
        << "<desc>Educational three-gene negative feedback circuit, current molecular amounts, and full numerical trajectories. Playback does not re-solve the model.</desc>\n";
    for (const auto& c : scene.commands) {
        for (const double value : {c.x, c.y, c.width, c.height, c.radius, c.strokeWidth, c.fontSize}) requireFinite(value);
        for (const auto& point : c.points) { requireFinite(point.x); requireFinite(point.y); }
        for (const auto dash : c.dashPattern) {
            requireFinite(dash);
            if (dash <= 0) throw std::invalid_argument("Scene dash lengths must be positive");
        }
        if (c.width < 0 || c.height < 0 || c.radius < 0 || c.strokeWidth < 0 || c.fontSize <= 0)
            throw std::invalid_argument("Scene sizes must not be negative");
        const auto style = [&] {
            out << " fill=\"" << escapeXml(c.fill) << "\" stroke=\"" << escapeXml(c.stroke)
                << "\" stroke-width=\"" << c.strokeWidth << "\"";
            if (!c.dashPattern.empty()) {
                out << " stroke-dasharray=\"";
                for (std::size_t i = 0; i < c.dashPattern.size(); ++i) { if (i) out << ','; out << c.dashPattern[i]; }
                out << '"';
            }
        };
        if (c.type == SceneCommandType::Rect) {
            out << "<rect x=\"" << c.x << "\" y=\"" << c.y << "\" width=\"" << c.width
                << "\" height=\"" << c.height << "\" rx=\"" << c.radius << '"'; style(); out << "/>\n";
        } else if (c.type == SceneCommandType::Line || c.type == SceneCommandType::Polyline) {
            if (c.points.size() < 2) throw std::invalid_argument("Scene line requires at least two points");
            out << "<polyline points=\"";
            for (std::size_t i = 0; i < c.points.size(); ++i) {
                if (i) out << ' ';
                out << c.points[i].x << ',' << c.points[i].y;
            }
            out << '"'; style(); out << " stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n";
        } else {
            out << "<text x=\"" << c.x << "\" y=\"" << c.y
                << "\" font-family=\"Segoe UI, DejaVu Sans, sans-serif\" font-size=\"" << c.fontSize
                << "\" font-weight=\"" << (c.bold ? "600" : "400") << "\" text-anchor=\""
                << (c.textAnchor == TextAnchor::End ? "end" : c.textAnchor == TextAnchor::Middle ? "middle" : "start") << '"';
            style(); out << '>' << escapeXml(c.text) << "</text>\n";
        }
    }
    out << "</svg>\n";
    output << out.str();
    if (!output) throw std::runtime_error("Could not write circuit scene SVG");
}
} // namespace evolve::circuit
