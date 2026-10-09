#include "core/CircuitScene.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 4) {
            std::cerr << "Usage: evolve_circuit_preview TRACE.csv OUT.svg [time_minutes]\n";
            return 2;
        }
        namespace fs = std::filesystem;
        const auto inputPath = fs::weakly_canonical(fs::absolute(argv[1]));
        const auto outputPath = fs::weakly_canonical(fs::absolute(argv[2]));
        // Reject lexical aliases, symlinks and hard links before touching output.
        if (inputPath == outputPath || (fs::exists(outputPath) && fs::equivalent(inputPath, outputPath)))
            throw std::invalid_argument("Input trace and output SVG must be different files");
        double time = 0;
        if (argc == 4) {
            const std::string token = argv[3]; std::size_t used = 0;
            time = std::stod(token, &used);
            if (used != token.size() || !std::isfinite(time) || time < 0)
                throw std::invalid_argument("time_minutes must be a finite nonnegative number");
        }
        const auto trace = evolve::circuit::CircuitTrace::load(argv[1]);
        const auto scene = evolve::circuit::buildCircuitScene(trace, time);
        // Validate and serialize the complete display list before opening an
        // existing output. Invalid input or scene geometry cannot truncate it.
        std::ostringstream serialized;
        evolve::circuit::writeSceneSvg(scene, serialized);
        std::ofstream output(outputPath, std::ios::binary);
        if (!output) throw std::runtime_error("Could not open output SVG");
        output << serialized.str();
        output.close();
        if (!output) throw std::runtime_error("Could not finish output SVG");
        std::cout << "Wrote " << argv[2] << " from " << trace.rows().size()
                  << " stored trajectory samples. Replay does not re-solve the model.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Circuit preview: " << error.what() << '\n';
        return 1;
    }
}
