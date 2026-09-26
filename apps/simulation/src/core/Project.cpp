#include "core/Project.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace evolve {
char complement(char base) {
    switch (base) {
    case 'A': return 'T'; case 'T': return 'A';
    case 'C': return 'G'; case 'G': return 'C';
    default: throw std::invalid_argument("Use A, C, G or T only.");
    }
}

std::string parseSequence(const std::string& text) {
    if (text.size() > 65536) throw std::invalid_argument("Input exceeds the 64 KiB prototype limit.");
    std::istringstream input(text);
    std::string line, result;
    bool header = false;
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        if (line[first] == '>') {
            if (header || !result.empty()) throw std::invalid_argument("Import one FASTA record at a time.");
            header = true;
            continue;
        }
        for (unsigned char c : line) {
            if (std::isspace(c)) continue;
            const char base = static_cast<char>(std::toupper(c));
            complement(base);
            result.push_back(base);
            if (result.size() > MaxBases) throw std::invalid_argument("v0.1 supports up to 256 bases.");
        }
    }
    if (result.size() < 4) throw std::invalid_argument("Provide at least four A/C/G/T bases.");
    return result;
}

double gcContent(const std::string& sequence) {
    if (sequence.empty()) return 0.0;
    return 100.0 * static_cast<double>(std::count_if(sequence.begin(), sequence.end(),
        [](char c) { return c == 'G' || c == 'C'; })) / static_cast<double>(sequence.size());
}

Project::Project() { importSequence(DemoSequence); }
void Project::changed() { ++revision_; hasResult_ = false; }
void Project::importSequence(const std::string& text) {
    auto next = parseSequence(text); // Validate before replacing anything.
    baseline_ = next; sequence_ = next;
    undo_.clear(); redo_.clear(); changed();
}
bool Project::edit(std::size_t index, char base) {
    complement(base);
    if (index >= sequence_.size()) throw std::out_of_range("Base index is outside the sequence.");
    if (sequence_[index] == base) return false;
    undo_.push_back(sequence_); redo_.clear(); sequence_[index] = base; changed(); return true;
}
bool Project::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(sequence_); sequence_ = undo_.back(); undo_.pop_back(); changed(); return true;
}
bool Project::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(sequence_); sequence_ = redo_.back(); redo_.pop_back(); changed(); return true;
}
bool Project::restoreBaseline() {
    if (baseline_ == sequence_) return false;
    undo_.push_back(sequence_); redo_.clear(); sequence_ = baseline_; changed(); return true;
}
Analysis Project::analyze() const {
    Analysis result;
    result.revision = revision_; result.baselineGc = gcContent(baseline_); result.scenarioGc = gcContent(sequence_);
    for (std::size_t i = 0; i < sequence_.size(); ++i) if (sequence_[i] != baseline_[i]) ++result.edits;
    return result;
}
bool Project::accept(const Analysis& result) {
    if (result.revision != revision_) return false;
    result_ = result; hasResult_ = true; return true;
}
std::string Project::serialize() const {
    return "EVOLVE_PROJECT 0.1\n" + baseline_ + "\n" + sequence_ + "\n";
}
void Project::deserialize(const std::string& text) {
    if (text.size() > 65536) throw std::invalid_argument("Project file is too large.");
    std::istringstream input(text);
    std::string magic, baseline, scenario, trailing;
    std::getline(input, magic); std::getline(input, baseline); std::getline(input, scenario);
    if (!magic.empty() && magic.back() == '\r') magic.pop_back();
    if (magic != "EVOLVE_PROJECT 0.1") throw std::invalid_argument("Unsupported Evolve project version.");
    while (std::getline(input, trailing)) {
        if (trailing.find_first_not_of(" \r\t") != std::string::npos)
            throw std::invalid_argument("Unexpected data in project file.");
    }
    auto b = parseSequence(baseline), s = parseSequence(scenario);
    if (b.size() != s.size()) throw std::invalid_argument("Baseline and scenario lengths must match.");
    baseline_ = b; sequence_ = s; undo_.clear(); redo_.clear(); changed();
}
}
