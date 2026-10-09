#pragma once

#include <array>
#include <cstddef>
#include <istream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace evolve::circuit {

inline constexpr std::size_t SpeciesCount = 6;
// BioModels BIOMD0000000012 species order: LacI/TetR/cI mRNA, then proteins.
inline constexpr std::array<const char*, SpeciesCount> SpeciesIds{{"X", "Y", "Z", "PX", "PY", "PZ"}};

struct TraceRow {
    double timeMinutes{};
    std::array<double, SpeciesCount> values{};
};

struct TraceMetadata {
    std::string modelId;
    std::string modelSha256;
    std::string solver;
    std::string timeUnit;
    std::string quantityUnit;
    // All declared metadata, including the required fields. These are supplied
    // by the producer: a declared model hash does not authenticate CSV values.
    std::map<std::string, std::string> attributes;
};

struct TraceLimits {
    std::size_t maxBytes{32U * 1024U * 1024U};
    std::size_t maxRows{200001};
    std::size_t maxLineBytes{4096};
    std::size_t maxMetadataEntries{64};
};

class TraceError : public std::runtime_error {
public:
    TraceError(const std::string& source, std::size_t line, const std::string& message);
    std::size_t line() const noexcept { return line_; }
private:
    std::size_t line_;
};

// Read-only, precomputed biological trajectory. Loading and interpolation never
// run a biological solver or derive quantities from DNA/renderer geometry.
class CircuitTrace {
public:
    static CircuitTrace read(std::istream& input,
                             const std::string& sourceName = "stream",
                             TraceLimits limits = {});
    static CircuitTrace load(const std::string& path, TraceLimits limits = {});

    const std::vector<TraceRow>& rows() const noexcept { return rows_; }
    const TraceMetadata& metadata() const noexcept { return metadata_; }
    double duration() const noexcept { return rows_.back().timeMinutes; }
    // Presentation-only linear interpolation; finite out-of-range times clamp.
    // Nonfinite arguments throw std::invalid_argument.
    TraceRow sampleAt(double timeMinutes) const;

private:
    CircuitTrace(TraceMetadata metadata, std::vector<TraceRow> rows);
    TraceMetadata metadata_;
    std::vector<TraceRow> rows_;
};

// Simulation-clock presentation of a saved trajectory, independent of renderer
// frame count. Units: simulation minutes per elapsed wall-clock second.
class PlaybackController {
public:
    explicit PlaybackController(double durationMinutes, double speedMinutesPerSecond = 1.0);
    void play() noexcept;
    void pause() noexcept { playing_ = false; }
    // Deterministically returns to zero, paused, retaining the selected speed.
    void reset() noexcept;
    void setSpeed(double speedMinutesPerSecond);
    // Finite targets clamp; seeking preserves pause. The endpoint always pauses.
    void seek(double timeMinutes);
    // Elapsed seconds must be finite and nonnegative, even when paused.
    void advance(double elapsedSeconds);

    double simulationMinutes() const noexcept { return static_cast<double>(positionMinutes_); }
    bool playing() const noexcept { return playing_; }
    double speedMinutesPerSecond() const noexcept { return speed_; }
    double duration() const noexcept { return duration_; }

private:
    double duration_;
    double speed_;
    long double positionMinutes_{0.0L};
    long double compensation_{0.0L};
    bool playing_{false};
};

} // namespace evolve::circuit
