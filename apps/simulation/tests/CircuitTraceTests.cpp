#include "core/CircuitTrace.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace evolve::circuit;
int checks = 0;

void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void close(double actual, double expected, const std::string& message, double tolerance = 1e-11) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}

void rejects(const std::function<void()>& action, const std::string& expected, const std::string& message) {
    ++checks;
    try {
        action();
    } catch (const std::exception& error) {
        if (std::string(error.what()).find(expected) != std::string::npos) return;
        throw std::runtime_error(message + ": unexpected error: " + error.what());
    }
    throw std::runtime_error(message + ": accepted invalid input");
}

std::string prefix() {
    return "# evolve-circuit-trace-v1\n"
           "# model_id=BIOMD0000000012\n"
           "# model_sha256=" + std::string(64, 'a') + "\n"
           "# solver=libRoadRunner 2.9.2 CVODE\n"
           "# time_unit=minute\n"
           "# quantity_unit=item_per_cell\n";
}
const std::string csvHeader = "time_minutes,X,Y,Z,PX,PY,PZ\n";
const std::string data = "0,0,20,0,0,0,0\n2,2,16,4,6,8,10\n6,10,8,12,14,16,18\n";
std::string valid() { return prefix() + csvHeader + data; }
CircuitTrace parse(const std::string& content, TraceLimits limits = {}) {
    std::istringstream input(content);
    return CircuitTrace::read(input, "test.csv", limits);
}
std::string replaced(std::string input, const std::string& from, const std::string& to) {
    const auto found = input.find(from);
    if (found == std::string::npos) throw std::runtime_error("test replacement not found: " + from);
    input.replace(found, from.size(), to);
    return input;
}
void badTrace(const std::string& input, const std::string& diagnostic, const std::string& message) {
    rejects([&] { parse(input); }, diagnostic, message);
}

struct CommaDecimal : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    char do_thousands_sep() const override { return '.'; }
    std::string do_grouping() const override { return "\3"; }
};

class FailingBuffer : public std::streambuf {
public:
    explicit FailingBuffer(const std::string& initial) : initial_(initial) {
        char* begin = &initial_[0];
        setg(begin, begin, begin + initial_.size());
    }
protected:
    int_type underflow() override { throw std::ios_base::failure("deliberate I/O failure"); }
private:
    std::string initial_;
};

void testReaderAndMetadata() {
    const auto trace = parse(valid());
    require(trace.rows().size() == 3, "read all rows");
    require(trace.duration() == 6.0, "duration uses final simulation time");
    require(trace.rows().front().values[1] == 20.0, "species column order preserved");
    require(trace.metadata().modelId == "BIOMD0000000012", "model ID recorded");
    require(trace.metadata().modelSha256 == std::string(64, 'a'), "declared model hash retained");
    require(trace.metadata().solver == "libRoadRunner 2.9.2 CVODE", "solver provenance retained");
    require(trace.metadata().timeUnit == "minute" && trace.metadata().quantityUnit == "item_per_cell", "units retained");
    require(trace.metadata().attributes.size() == 5, "required metadata exposed read-only");
    require(SpeciesCount == 6 && std::string(SpeciesIds[0]) == "X" && std::string(SpeciesIds[5]) == "PZ", "species contract");
    auto withOptional = parse(prefix() + "# notes=<&\"' experimental>\n# run_id=experiment_1\n" + csvHeader + data);
    require(withOptional.metadata().attributes.at("notes") == "<&\"' experimental>", "metadata retained without interpreting markup");
    require(withOptional.metadata().attributes.at("run_id") == "experiment_1", "optional metadata accepted");
    auto differentInitialState = parse(prefix() + csvHeader + "0,3,4,5,6,7,8\n1,0,0,0,0,0,0\n");
    require(differentInitialState.rows().front().values[0] == 3, "parser does not enforce baseline experiment initial values");
    auto allZero = parse(prefix() + csvHeader + "0,0,0,0,0,0,0\n1,0,0,0,0,0,0\n");
    require(allZero.sampleAt(0.5).values[5] == 0, "all-zero trace accepted");
    std::string crlf;
    for (char ch : valid()) { if (ch == '\n') crlf.push_back('\r'); crlf.push_back(ch); }
    require(parse(crlf).rows().size() == 3, "CRLF trace accepted");
    auto noNewline = valid(); noNewline.pop_back();
    require(parse(noNewline).rows().size() == 3, "complete final row needs no newline");
    require(parse(prefix() + csvHeader + "0,+0,2.0e1,.0,0.,-0,0\n1e0,1E+0,0.1e1,1.,.1e1,+1,1\n").duration() == 1, "decimal/scientific notation accepted");
    const auto originalLocale = std::locale();
    std::locale::global(std::locale(originalLocale, new CommaDecimal));
    try {
        const auto localeTrace = parse(prefix() + csvHeader + "0,0.5,20,0,0,0,0\n1.5,1.25,0,0,0,0,0\n");
        require(localeTrace.duration() == 1.5 && localeTrace.rows()[1].values[0] == 1.25, "decimal conversion ignores global locale");
    } catch (...) { std::locale::global(originalLocale); throw; }
    std::locale::global(originalLocale);

    const std::string path = "evolve-circuit-trace-test.csv";
    { std::ofstream file(path, std::ios::binary); file << valid(); require(static_cast<bool>(file), "test file write"); }
    try { require(CircuitTrace::load(path).rows().size() == 3, "file reader"); }
    catch (...) { std::remove(path.c_str()); throw; }
    require(std::remove(path.c_str()) == 0, "test file removed");
    rejects([] { CircuitTrace::load("evolve-definitely-missing-test-trace.csv"); }, "cannot open", "missing path diagnosed");
}

void testInvalidMetadataAndStructure() {
    badTrace("", "first line", "empty input rejected");
    badTrace(replaced(valid(), "v1", "v2"), "first line", "future format rejected");
    badTrace("\xEF\xBB\xBF" + valid(), "BOM", "BOM rejected explicitly");
    badTrace("\n" + valid(), "first line", "leading blank line rejected");
    badTrace(prefix(), "missing CSV header", "missing header rejected");
    badTrace(prefix() + csvHeader, "at least 2", "zero rows rejected");
    badTrace(prefix() + csvHeader + "0,0,20,0,0,0,0\n", "at least 2", "single row rejected");
    for (const std::string key : {"model_id", "model_sha256", "solver", "time_unit", "quantity_unit"}) {
        auto content = valid();
        const auto begin = content.find("# " + key + "=");
        content.erase(begin, content.find('\n', begin) - begin + 1);
        badTrace(content, "missing required metadata '" + key + "'", "missing " + key);
    }
    badTrace(replaced(valid(), "BIOMD0000000012", "BIOMD0000000013"), "model_id", "unsupported model rejected");
    badTrace(replaced(valid(), std::string(64, 'a'), std::string(63, 'a')), "64 lowercase", "short model hash rejected");
    badTrace(replaced(valid(), std::string(64, 'a'), std::string(65, 'a')), "64 lowercase", "long model hash rejected");
    badTrace(replaced(valid(), std::string(64, 'a'), std::string(64, 'A')), "64 lowercase", "uppercase model hash rejected");
    badTrace(replaced(valid(), std::string(64, 'a'), std::string(64, 'g')), "64 lowercase", "nonhex model hash rejected");
    for (const std::string solver : {"mock", "libRoadRunner CVODE", "libRoadRunner  CVODE", "libRoadRunner 2.9.2 RK4", "libRoadRunner x CVODE", "libRoadRunner 2 9 CVODE"})
        badTrace(replaced(valid(), "libRoadRunner 2.9.2 CVODE", solver), "solver", "invalid solver rejected: " + solver);
    badTrace(replaced(valid(), "time_unit=minute", "time_unit=second"), "time_unit", "wrong time unit rejected");
    badTrace(replaced(valid(), "quantity_unit=item_per_cell", "quantity_unit=molecule"), "quantity_unit", "wrong quantity unit rejected");
    badTrace(prefix() + "# model_id=BIOMD0000000012\n" + csvHeader + data, "duplicate metadata", "duplicate required metadata rejected");
    badTrace(prefix() + "# label=a\n# label=b\n" + csvHeader + data, "duplicate metadata", "duplicate optional metadata rejected");
    badTrace(prefix() + "# evolve-circuit-trace-v1\n" + csvHeader + data, "duplicate format", "duplicate marker rejected");
    for (const std::string metadata : {"#note=value", "# =value", "# note=", "# comment", "# note= value", "# note=value ", "# note=bad\tvalue", "# Note=value", "# a-b=value", "# 2note=value"})
        badTrace(prefix() + metadata + '\n' + csvHeader + data, "metadata", "malformed metadata rejected: " + metadata);
    badTrace(replaced(valid(), csvHeader, "time_minutes,Y,X,Z,PX,PY,PZ\n"), "exact CSV header", "reordered header rejected");
    badTrace(replaced(valid(), csvHeader, "time_minutes,X,Y,Z,PX,PY,PZ \n"), "exact CSV header", "whitespace header rejected");
    badTrace(valid() + csvHeader, "duplicate CSV header", "duplicate data header rejected");
    badTrace(prefix() + csvHeader + "# notes=late\n" + data, "not permitted after", "late metadata rejected");
    badTrace(valid() + "\n", "exactly 7", "blank data row rejected");
    badTrace(prefix() + "\n" + csvHeader + data, "expected metadata", "blank metadata row rejected");
    bool lineError = false;
    try { parse(prefix() + csvHeader + "0,0,20,0,0,0,0\n1,0,0\n"); }
    catch (const TraceError& error) {
        lineError = true;
        require(error.line() == 9, "error preserves accurate line number");
        require(std::string(error.what()).find("test.csv:9:") == 0, "error includes source and line");
    }
    require(lineError, "line-number test rejects malformed input");
}

void testInvalidNumbersAndRows() {
    const auto before = prefix() + csvHeader + "0,0,20,0,0,0,0\n";
    for (const std::string row : {"1,1,2,3,4,5", "1,1,2,3,4,5,6,7", "1,1,2,3,4,5,6,", "1"})
        badTrace(before + row + '\n', "exactly 7", "malformed/truncated row rejected");
    for (const std::string token : {"", " ", "1 ", " 1", "1x", "nan", "NaN", "inf", "+inf", "-inf", "0x1p0", "1e9999", "1e-9999", "-1e-9999", "2e-324", "-1", "-1e-3", "+", ".", "1e", "1e+", "1_000", "1f", "\"1\"", "1\t"})
        badTrace(before + "1," + token + ",2,3,4,5,6\n", "column 'X'", "invalid species numeric value: " + token);
    badTrace(prefix() + csvHeader + "1e-9999,0,0,0,0,0,0\n1,0,0,0,0,0,0\n", "underflows", "underflow cannot bypass initial zero time requirement");
    require(parse(before + "1,4.9406564584124654e-324,2,3,4,5,6\n").rows().back().values[0] > 0,
            "representable subnormal values accepted");
    for (const std::string token : {"nan", "-1", "1e9999", "0", "+0.0", "0e3"})
        badTrace(before + token + ",1,2,3,4,5,6\n", token == "0" || token == "+0.0" || token == "0e3" ? "strictly increasing" : "time_minutes", "invalid time: " + token);
    badTrace(prefix() + csvHeader + "1,0,0,0,0,0,0\n2,1,1,1,1,1,1\n", "start at time_minutes=0", "first sample must be time zero");
    badTrace(before + "2,1,2,3,4,5,6\n1,1,2,3,4,5,6\n", "strictly increasing", "decreasing times rejected");
    badTrace(before + "1,1,2,3,4,5,6\n1,1,2,3,4,5,6\n", "strictly increasing", "duplicate times rejected");
    badTrace(before + "1,1,2,3,4,5,-1\n", "column 'PZ'", "last species checked");
    std::string nul = before + "1,1"; nul.push_back('\0'); nul += ",2,3,4,5,6\n";
    badTrace(nul, "column 'X'", "embedded NUL rejected");
    badTrace(before + "1,1,2,3,4,5,6\rgarbage\n", "column 'PZ'", "embedded CR rejected");
}

void testBoundsAndIO() {
    TraceLimits limits;
    limits.maxBytes = valid().size();
    require(parse(valid(), limits).rows().size() == 3, "byte limit exact boundary accepted");
    --limits.maxBytes;
    rejects([&] { parse(valid(), limits); }, "maxBytes", "byte limit enforced");
    limits = {}; limits.maxRows = 3;
    require(parse(valid(), limits).rows().size() == 3, "row limit exact boundary accepted");
    limits.maxRows = 2;
    rejects([&] { parse(valid(), limits); }, "maxRows", "row limit enforced");
    limits = {}; limits.maxLineBytes = std::string("# model_sha256=").size() + 64;
    require(parse(valid(), limits).rows().size() == 3, "line limit exact boundary accepted");
    --limits.maxLineBytes;
    rejects([&] { parse(valid(), limits); }, "maxLineBytes", "line limit enforced while reading");
    limits = {}; limits.maxMetadataEntries = 5;
    require(parse(valid(), limits).rows().size() == 3, "metadata limit exact boundary accepted");
    rejects([&] { parse(prefix() + "# extra=value\n" + csvHeader + data, limits); }, "maxMetadataEntries", "metadata limit enforced");
    for (int choice = 0; choice < 4; ++choice) {
        limits = {};
        if (choice == 0) limits.maxBytes = 0;
        if (choice == 1) limits.maxRows = 1;
        if (choice == 2) limits.maxLineBytes = 0;
        if (choice == 3) limits.maxMetadataEntries = 4;
        rejects([&] { parse(valid(), limits); }, "trace limits", "invalid parser limits rejected");
    }
    rejects([&] { parse(prefix() + csvHeader + std::string(5000, '1')); }, "maxLineBytes", "giant unterminated row bounded");
    FailingBuffer buffer(valid());
    std::istream broken(&buffer);
    rejects([&] { CircuitTrace::read(broken, "broken.csv"); }, "I/O error", "failed read is not treated as EOF success");
    std::istringstream exceptionStream(valid());
    exceptionStream.exceptions(std::ios::failbit | std::ios::badbit);
    require(CircuitTrace::read(exceptionStream).rows().size() == 3, "exception-enabled stream handles ordinary EOF");
    std::istringstream alreadyFailed(valid()); alreadyFailed.setstate(std::ios::failbit);
    rejects([&] { CircuitTrace::read(alreadyFailed); }, "I/O error", "pre-failed stream rejected");
}

void testInterpolation() {
    const auto trace = parse(valid());
    const auto original = trace.rows();
    require(trace.sampleAt(-100).timeMinutes == 0, "negative sample time clamps to first sample");
    require(trace.sampleAt(100).timeMinutes == 6, "late sample time clamps to final sample");
    require(trace.sampleAt(0).values == original[0].values, "initial sample exact");
    require(trace.sampleAt(2).values == original[1].values, "interior stored sample exact");
    require(trace.sampleAt(6).values == original[2].values, "last sample exact");
    const auto first = trace.sampleAt(1);
    require(first.timeMinutes == 1, "sample time retained for interpolation");
    for (std::size_t i = 0; i < SpeciesCount; ++i)
        close(first.values[i], (original[0].values[i] + original[1].values[i]) / 2, "all species interpolate");
    const auto irregular = trace.sampleAt(3);
    close(irregular.values[0], 4, "nonuniform time grid interpolates by time");
    close(irregular.values[1], 14, "decreasing quantity interpolates");
    require(trace.rows()[1].values == original[1].values, "interpolation never mutates trajectory");
    for (double invalid : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        rejects([&] { trace.sampleAt(invalid); }, "finite", "nonfinite sampling rejected");
    const auto large = parse(prefix() + csvHeader + "0,1e308,0,0,0,0,0\n1,0,1e308,0,0,0,0\n");
    require(std::isfinite(large.sampleAt(0.5).values[0]) && large.sampleAt(0.5).values[0] == 5e307, "large finite quantity interpolation remains finite");
}

void testPlayback() {
    PlaybackController clock(100);
    require(!clock.playing() && clock.simulationMinutes() == 0 && clock.speedMinutesPerSecond() == 1, "initial clock state is paused at zero");
    require(clock.duration() == 100, "clock duration");
    clock.advance(10); require(clock.simulationMinutes() == 0, "paused elapsed time ignored");
    clock.play(); clock.advance(2.5); close(clock.simulationMinutes(), 2.5, "clock advances using elapsed seconds");
    clock.pause(); clock.advance(20); close(clock.simulationMinutes(), 2.5, "pause freezes simulation time");
    clock.seek(40); require(!clock.playing() && clock.simulationMinutes() == 40, "seek preserves pause");
    clock.setSpeed(2); clock.play(); clock.advance(3); close(clock.simulationMinutes(), 46, "speed is simulation minutes per second");
    clock.seek(10); require(clock.playing() && clock.simulationMinutes() == 10, "seek preserves active playback before endpoint");
    clock.advance(0); close(clock.simulationMinutes(), 10, "zero elapsed time is no-op");
    clock.seek(-100); require(clock.simulationMinutes() == 0 && clock.playing(), "negative seek clamps to start");
    clock.seek(1000); require(clock.simulationMinutes() == 100 && !clock.playing(), "seek past duration clamps and pauses");
    clock.play(); require(!clock.playing(), "play at endpoint does not roll over");
    clock.reset(); require(clock.simulationMinutes() == 0 && !clock.playing() && clock.speedMinutesPerSecond() == 2, "reset is paused at zero and retains selected speed");
    clock.play(); clock.advance(60); require(clock.simulationMinutes() == 100 && !clock.playing(), "elapsed overrun clamps and stops");
    clock.advance(600); require(clock.simulationMinutes() == 100, "endpoint does not roll over");
    clock.reset(); clock.play(); clock.advance(50); require(clock.simulationMinutes() == 100 && !clock.playing(), "exact endpoint pauses");
    clock.seek(0); require(!clock.playing(), "seek back from stopped endpoint remains paused");

    PlaybackController thirty(100), sixty(100), chunk(100), irregular(100);
    thirty.play(); sixty.play(); chunk.play(); irregular.play();
    for (int i = 0; i < 300; ++i) thirty.advance(1.0 / 30.0);
    for (int i = 0; i < 600; ++i) sixty.advance(1.0 / 60.0);
    chunk.advance(10);
    for (double elapsed : {0.01, 0.2, 1.79, 3.0, 0.5, 4.5}) irregular.advance(elapsed);
    close(thirty.simulationMinutes(), chunk.simulationMinutes(), "30Hz and one elapsed chunk agree");
    close(sixty.simulationMinutes(), chunk.simulationMinutes(), "60Hz and one elapsed chunk agree");
    close(irregular.simulationMinutes(), chunk.simulationMinutes(), "irregular frame times agree");
    const auto trace = parse(valid());
    PlaybackController presentation(trace.duration(), 0.25);
    presentation.play(); presentation.advance(4);
    close(trace.sampleAt(presentation.simulationMinutes()).values[1], 18, "presentation sampled from clock and immutable trajectory");
    PlaybackController exact(10); exact.play();
    for (int i = 0; i < 600; ++i) exact.advance(1.0 / 60.0);
    require(exact.simulationMinutes() == 10 && !exact.playing(), "fractional frames clamp at represented endpoint");
    for (int repetition = 0; repetition < 10; ++repetition) {
        thirty.reset(); thirty.play(); thirty.advance(2.5);
        require(thirty.simulationMinutes() == 2.5, "repeated reset is deterministic");
    }
    PlaybackController overflow(100, std::numeric_limits<double>::max());
    overflow.play(); overflow.advance(std::numeric_limits<double>::max());
    require(overflow.simulationMinutes() == 100 && !overflow.playing(), "overflowing elapsed-times-speed saturates safely");
    PlaybackController tiny(std::numeric_limits<double>::min(), std::numeric_limits<double>::max());
    tiny.play(); tiny.advance(0); require(tiny.simulationMinutes() == 0 && tiny.playing(), "zero delta never prematurely stops tiny duration");
    tiny.advance(1); require(tiny.simulationMinutes() == tiny.duration() && !tiny.playing(), "tiny duration clamps safely");
}

void testInvalidPlayback() {
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    for (double value : {0.0, -1.0, infinity, -infinity, nan}) {
        rejects([&] { PlaybackController clock(value); }, "durationMinutes", "invalid clock duration");
        rejects([&] { PlaybackController clock(10, value); }, "speedMinutesPerSecond", "invalid initial speed");
        PlaybackController clock(10, 2); clock.play(); clock.advance(1);
        rejects([&] { clock.setSpeed(value); }, "speedMinutesPerSecond", "invalid speed change");
        require(clock.speedMinutesPerSecond() == 2 && clock.simulationMinutes() == 2 && clock.playing(), "rejected speed update preserves state");
    }
    for (double value : {infinity, -infinity, nan}) {
        PlaybackController clock(10); clock.play(); clock.advance(2);
        rejects([&] { clock.seek(value); }, "finite", "nonfinite seek rejected");
        require(clock.simulationMinutes() == 2 && clock.playing(), "failed seek preserves state");
    }
    for (double value : {-1.0, infinity, -infinity, nan}) {
        PlaybackController clock(10);
        rejects([&] { clock.advance(value); }, "elapsedSeconds", "invalid elapsed rejected while paused");
        clock.play(); clock.advance(1);
        rejects([&] { clock.advance(value); }, "elapsedSeconds", "invalid elapsed rejected while playing");
        require(clock.simulationMinutes() == 1 && clock.playing(), "failed advance preserves state");
    }
}
} // namespace

int main() {
    try {
        testReaderAndMetadata();
        testInvalidMetadataAndStructure();
        testInvalidNumbersAndRows();
        testBoundsAndIO();
        testInterpolation();
        testPlayback();
        testInvalidPlayback();
        std::cout << "PASS: " << checks << " circuit trace and playback checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
