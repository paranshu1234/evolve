#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace evolve {
constexpr std::size_t MaxBases = 256;
inline constexpr const char* DemoSequence = "ATGCGTACCTAGGCTAACGTTAGC";
char complement(char base);
std::string parseSequence(const std::string& text);
double gcContent(const std::string& sequence);

struct Analysis {
    std::uint64_t revision{};
    double baselineGc{};
    double scenarioGc{};
    std::size_t edits{};
    std::string model{"mock-composition-v0.1"};
};

// Authoritative project state; no dependencies on Windows, graphics or UI.
class Project {
public:
    Project();
    void importSequence(const std::string& text);
    bool edit(std::size_t index, char base);
    bool undo();
    bool redo();
    bool restoreBaseline();
    Analysis analyze() const;
    bool accept(const Analysis& result);
    std::string serialize() const;
    void deserialize(const std::string& text);
    const std::string& baseline() const { return baseline_; }
    const std::string& sequence() const { return sequence_; }
    std::uint64_t revision() const { return revision_; }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool hasResult() const { return hasResult_; }
    const Analysis& result() const { return result_; }
private:
    void changed();
    std::string baseline_, sequence_;
    std::vector<std::string> undo_, redo_;
    std::uint64_t revision_{0};
    bool hasResult_{false};
    Analysis result_;
};
}
