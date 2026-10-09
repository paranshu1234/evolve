#include "CircuitTrace.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <locale>
#include <sstream>
#include <utility>

namespace evolve::circuit {
namespace {
constexpr const char* FormatMarker = "# evolve-circuit-trace-v1";
constexpr const char* CsvHeader = "time_minutes,X,Y,Z,PX,PY,PZ";

bool asciiDigit(char ch) { return ch >= '0' && ch <= '9'; }
bool asciiLower(char ch) { return ch >= 'a' && ch <= 'z'; }
bool asciiUpper(char ch) { return ch >= 'A' && ch <= 'Z'; }

// Limit allocation while reading, rather than checking an unbounded getline or
// loading an entire file before applying the byte limit. CRLF counts as two bytes.
class BoundedLines {
public:
    BoundedLines(std::istream& input, const std::string& source, const TraceLimits& limits)
        : input_(input), source_(source), limits_(limits) {}

    bool next(std::string& line) {
        line.clear();
        bool consumed = false;
        for (;;) {
            int value;
            try {
                value = input_.get();
            } catch (const std::ios_base::failure&) {
                if (input_.bad() || !input_.eof()) fail("I/O error while reading trace");
                value = std::char_traits<char>::eof();
            }
            if (value == std::char_traits<char>::eof()) {
                if (input_.bad() || (!input_.eof() && input_.fail()))
                    fail("I/O error while reading trace");
                if (!consumed) return false;
                break;
            }
            if (bytes_ == limits_.maxBytes) fail("file exceeds maxBytes limit (" + std::to_string(limits_.maxBytes) + ")");
            ++bytes_;
            consumed = true;
            const char ch = static_cast<char>(value);
            if (ch == '\n') break;
            if (line.size() == limits_.maxLineBytes)
                fail("line exceeds maxLineBytes limit (" + std::to_string(limits_.maxLineBytes) + ")");
            line.push_back(ch);
        }
        ++lineNumber_;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    }

    std::size_t lineNumber() const noexcept { return lineNumber_; }
private:
    [[noreturn]] void fail(const std::string& message) const {
        throw TraceError(source_, lineNumber_ + 1, message);
    }
    std::istream& input_;
    const std::string& source_;
    const TraceLimits& limits_;
    std::size_t bytes_{0};
    std::size_t lineNumber_{0};
};

// Explicit decimal grammar excludes whitespace, locale-dependent separators,
// hexadecimal, NaN/Inf and trailing junk before locale-independent conversion.
bool decimalSyntax(const std::string& text) {
    std::size_t i = 0;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
    bool digit = false;
    while (i < text.size() && asciiDigit(text[i])) { digit = true; ++i; }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && asciiDigit(text[i])) { digit = true; ++i; }
    }
    if (!digit) return false;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
        const auto first = i;
        while (i < text.size() && asciiDigit(text[i])) ++i;
        if (i == first) return false;
    }
    return i == text.size();
}

double number(const std::string& text, const std::string& source,
              std::size_t line, const char* column) {
    const std::string explanation = std::string("column '") + column + "' must contain a finite nonnegative decimal number";
    if (!decimalSyntax(text)) throw TraceError(source, line, explanation);
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double result = 0.0;
    stream >> std::noskipws >> result;
    if (stream.fail() || !stream.eof() || !std::isfinite(result) || result < 0.0)
        throw TraceError(source, line, explanation + " representable as a double");
    // Some standard-library conversions silently round an underflowed value
    // to zero without failbit. Do not silently turn a nonzero (or negative)
    // biological quantity into zero, nor accept an underflowed initial time.
    if (result == 0.0) {
        const auto exponent = text.find_first_of("eE");
        const auto mantissaEnd = exponent == std::string::npos ? text.size() : exponent;
        for (std::size_t i = 0; i < mantissaEnd; ++i) {
            if (text[i] >= '1' && text[i] <= '9')
                throw TraceError(source, line, explanation + "; nonzero value underflows double precision");
        }
    }
    return result;
}

void parseMetadata(const std::string& line, TraceMetadata& metadata,
                   const std::string& source, std::size_t lineNumber,
                   const TraceLimits& limits) {
    if (line == FormatMarker) throw TraceError(source, lineNumber, "duplicate format marker");
    if (line.compare(0, 2, "# ") != 0)
        throw TraceError(source, lineNumber, "metadata must use '# key=value'");
    const auto equals = line.find('=', 2);
    if (equals == std::string::npos || equals == 2 || equals + 1 == line.size())
        throw TraceError(source, lineNumber, "metadata must have a nonempty key and value: '# key=value'");
    const auto key = line.substr(2, equals - 2);
    const auto value = line.substr(equals + 1);
    if (!asciiLower(key.front()) || !std::all_of(key.begin(), key.end(), [](char ch) {
            return asciiLower(ch) || asciiDigit(ch) || ch == '_';
        }))
        throw TraceError(source, lineNumber, "metadata keys must start with a lowercase letter and use lowercase letters, digits or underscores");
    if (value.front() == ' ' || value.back() == ' ' || !std::all_of(value.begin(), value.end(), [](char ch) {
            return ch >= ' ' && ch <= '~';
        }))
        throw TraceError(source, lineNumber, "metadata value for '" + key + "' must be printable ASCII without surrounding whitespace");
    if (metadata.attributes.find(key) != metadata.attributes.end())
        throw TraceError(source, lineNumber, "duplicate metadata key '" + key + "'");
    if (metadata.attributes.size() >= limits.maxMetadataEntries)
        throw TraceError(source, lineNumber, "metadata exceeds maxMetadataEntries limit (" + std::to_string(limits.maxMetadataEntries) + ")");
    metadata.attributes.emplace(key, value);
}

void validateMetadata(TraceMetadata& metadata, const std::string& source, std::size_t line) {
    auto required = [&](const char* key) -> std::string {
        const auto found = metadata.attributes.find(key);
        if (found == metadata.attributes.end())
            throw TraceError(source, line, std::string("missing required metadata '") + key + "' before CSV header");
        return found->second;
    };
    metadata.modelId = required("model_id");
    metadata.modelSha256 = required("model_sha256");
    metadata.solver = required("solver");
    metadata.timeUnit = required("time_unit");
    metadata.quantityUnit = required("quantity_unit");
    if (metadata.modelId != "BIOMD0000000012")
        throw TraceError(source, line, "model_id must be BIOMD0000000012");
    if (metadata.modelSha256.size() != 64 || !std::all_of(metadata.modelSha256.begin(), metadata.modelSha256.end(), [](char ch) {
            return asciiDigit(ch) || (ch >= 'a' && ch <= 'f');
        }))
        throw TraceError(source, line, "model_sha256 must contain exactly 64 lowercase hexadecimal characters");
    const std::string prefix = "libRoadRunner ";
    const std::string suffix = " CVODE";
    if (metadata.solver.size() <= prefix.size() + suffix.size() ||
        metadata.solver.compare(0, prefix.size(), prefix) != 0 ||
        metadata.solver.compare(metadata.solver.size() - suffix.size(), suffix.size(), suffix) != 0)
        throw TraceError(source, line, "solver must use 'libRoadRunner <version> CVODE'");
    const auto version = metadata.solver.substr(prefix.size(), metadata.solver.size() - prefix.size() - suffix.size());
    if (!asciiDigit(version.front()) || !std::all_of(version.begin(), version.end(), [](char ch) {
            return asciiDigit(ch) || asciiLower(ch) || asciiUpper(ch) || ch == '.' || ch == '-' || ch == '+' || ch == '_';
        }))
        throw TraceError(source, line, "solver must declare a nonempty libRoadRunner version starting with a digit");
    if (metadata.timeUnit != "minute") throw TraceError(source, line, "time_unit must be minute");
    if (metadata.quantityUnit != "item_per_cell") throw TraceError(source, line, "quantity_unit must be item_per_cell");
}

TraceRow parseRow(const std::string& line, const std::string& source, std::size_t lineNumber) {
    std::array<std::string, SpeciesCount + 1> fields;
    std::size_t start = 0;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const auto comma = line.find(',', start);
        if ((i + 1 < fields.size() && comma == std::string::npos) ||
            (i + 1 == fields.size() && comma != std::string::npos))
            throw TraceError(source, lineNumber, "CSV row must contain exactly 7 fields: time_minutes,X,Y,Z,PX,PY,PZ");
        fields[i] = line.substr(start, comma == std::string::npos ? comma : comma - start);
        if (comma != std::string::npos) start = comma + 1;
    }
    TraceRow result;
    result.timeMinutes = number(fields[0], source, lineNumber, "time_minutes");
    for (std::size_t i = 0; i < SpeciesCount; ++i)
        result.values[i] = number(fields[i + 1], source, lineNumber, SpeciesIds[i]);
    return result;
}

void finitePositive(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0.0)
        throw std::invalid_argument(std::string(name) + " must be finite and positive");
}
} // namespace

TraceError::TraceError(const std::string& source, std::size_t line, const std::string& message)
    : std::runtime_error(source + ":" + std::to_string(line) + ": " + message), line_(line) {}

CircuitTrace::CircuitTrace(TraceMetadata metadata, std::vector<TraceRow> rows)
    : metadata_(std::move(metadata)), rows_(std::move(rows)) {}

CircuitTrace CircuitTrace::read(std::istream& input, const std::string& sourceName, TraceLimits limits) {
    if (limits.maxBytes == 0 || limits.maxRows < 2 || limits.maxLineBytes == 0 || limits.maxMetadataEntries < 5)
        throw std::invalid_argument("trace limits require positive byte/line limits, at least 2 rows and at least 5 metadata entries");
    BoundedLines reader(input, sourceName, limits);
    std::string line;
    if (!reader.next(line) || line != FormatMarker)
        throw TraceError(sourceName, 1, "first line must be '# evolve-circuit-trace-v1' (UTF-8 BOM is not supported)");
    TraceMetadata metadata;
    std::vector<TraceRow> rows;
    bool header = false;
    while (reader.next(line)) {
        if (!header) {
            if (line == CsvHeader) {
                validateMetadata(metadata, sourceName, reader.lineNumber());
                header = true;
            } else if (!line.empty() && line.front() == '#') {
                parseMetadata(line, metadata, sourceName, reader.lineNumber(), limits);
            } else {
                throw TraceError(sourceName, reader.lineNumber(), "expected metadata or exact CSV header 'time_minutes,X,Y,Z,PX,PY,PZ'");
            }
            continue;
        }
        if (line == CsvHeader) throw TraceError(sourceName, reader.lineNumber(), "duplicate CSV header");
        if (!line.empty() && line.front() == '#')
            throw TraceError(sourceName, reader.lineNumber(), "metadata and comments are not permitted after the CSV header");
        if (rows.size() >= limits.maxRows)
            throw TraceError(sourceName, reader.lineNumber(), "trace exceeds maxRows limit (" + std::to_string(limits.maxRows) + ")");
        auto row = parseRow(line, sourceName, reader.lineNumber());
        if (rows.empty()) {
            if (row.timeMinutes != 0.0) throw TraceError(sourceName, reader.lineNumber(), "first data row must start at time_minutes=0");
        } else if (row.timeMinutes <= rows.back().timeMinutes) {
            throw TraceError(sourceName, reader.lineNumber(), "time_minutes must be strictly increasing");
        }
        rows.push_back(row);
    }
    if (!header) throw TraceError(sourceName, reader.lineNumber() + 1, "missing CSV header 'time_minutes,X,Y,Z,PX,PY,PZ'");
    if (rows.size() < 2) throw TraceError(sourceName, reader.lineNumber() + 1, "trace requires at least 2 data rows");
    return CircuitTrace(std::move(metadata), std::move(rows));
}

CircuitTrace CircuitTrace::load(const std::string& path, TraceLimits limits) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) throw TraceError(path, 0, "cannot open trace file");
    return read(input, path, limits);
}

TraceRow CircuitTrace::sampleAt(double timeMinutes) const {
    if (!std::isfinite(timeMinutes)) throw std::invalid_argument("sample time must be finite");
    if (timeMinutes <= 0.0) return rows_.front();
    if (timeMinutes >= duration()) return rows_.back();
    const auto right = std::lower_bound(rows_.begin(), rows_.end(), timeMinutes,
        [](const TraceRow& row, double time) { return row.timeMinutes < time; });
    if (right->timeMinutes == timeMinutes) return *right;
    const auto& left = *(right - 1);
    const double fraction = (timeMinutes - left.timeMinutes) / (right->timeMinutes - left.timeMinutes);
    TraceRow result;
    result.timeMinutes = timeMinutes;
    for (std::size_t i = 0; i < SpeciesCount; ++i)
        result.values[i] = left.values[i] + (right->values[i] - left.values[i]) * fraction;
    return result;
}

PlaybackController::PlaybackController(double durationMinutes, double speedMinutesPerSecond)
    : duration_(durationMinutes), speed_(speedMinutesPerSecond) {
    finitePositive(duration_, "durationMinutes");
    finitePositive(speed_, "speedMinutesPerSecond");
}

void PlaybackController::play() noexcept { playing_ = positionMinutes_ < duration_; }

void PlaybackController::reset() noexcept {
    positionMinutes_ = 0.0L;
    compensation_ = 0.0L;
    playing_ = false;
}

void PlaybackController::setSpeed(double speedMinutesPerSecond) {
    finitePositive(speedMinutesPerSecond, "speedMinutesPerSecond");
    speed_ = speedMinutesPerSecond;
    compensation_ = 0.0L;
}

void PlaybackController::seek(double timeMinutes) {
    if (!std::isfinite(timeMinutes)) throw std::invalid_argument("seek time must be finite");
    positionMinutes_ = std::clamp(timeMinutes, 0.0, duration_);
    compensation_ = 0.0L;
    if (positionMinutes_ >= duration_) playing_ = false;
}

void PlaybackController::advance(double elapsedSeconds) {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0)
        throw std::invalid_argument("elapsedSeconds must be finite and nonnegative");
    if (!playing_ || elapsedSeconds == 0.0) return;
    const long double remaining = static_cast<long double>(duration_) - positionMinutes_;
    // Divide before multiplying so even DBL_MAX elapsed/speed saturates safely
    // on platforms where long double has the same range as double.
    if (static_cast<long double>(elapsedSeconds) >= remaining / speed_) {
        positionMinutes_ = duration_;
        compensation_ = 0.0L;
        playing_ = false;
        return;
    }
    const long double delta = static_cast<long double>(elapsedSeconds) * speed_ - compensation_;
    const long double next = positionMinutes_ + delta;
    compensation_ = (next - positionMinutes_) - delta;
    positionMinutes_ = next;
    if (static_cast<double>(positionMinutes_) >= duration_) {
        positionMinutes_ = duration_;
        compensation_ = 0.0L;
        playing_ = false;
    }
}

} // namespace evolve::circuit
