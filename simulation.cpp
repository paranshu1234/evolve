#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

struct SimulationConfig {
    std::size_t steps{100};
    double time_step{0.1};
    double initial_state{0.0};
    unsigned int random_seed{42};
    bool verbose{false};

    void validate() const {
        if (steps == 0) {
            throw std::invalid_argument("steps must be greater than zero");
        }
        if (!std::isfinite(time_step) || time_step <= 0.0) {
            throw std::invalid_argument("time_step must be finite and greater than zero");
        }
        if (!std::isfinite(initial_state)) {
            throw std::invalid_argument("initial_state must be finite");
        }
    }
};

struct SimulationResult {
    std::size_t completed_steps{0};
    double elapsed_time{0.0};
    double final_state{0.0};
    double minimum_state{0.0};
    double maximum_state{0.0};
    bool successful{false};
};

class Simulation {
public:
    explicit Simulation(const SimulationConfig& config) : config_(config) {
        config_.validate();
    }

    SimulationResult run() {
        SimulationResult result{};
        double state = config_.initial_state;
        result.final_state = state;
        result.minimum_state = state;
        result.maximum_state = state;

        // Extension point: initialize domain-specific entities, resources,
        // probability distributions, and input datasets here.
        for (std::size_t step = 0; step < config_.steps; ++step) {
            const double current_time = static_cast<double>(step) * config_.time_step;

            // Extension point: replace this no-op with the validated
            // domain-specific state transition or event-processing logic.
            const double next_state = state;

            if (!std::isfinite(next_state)) {
                throw std::runtime_error("simulation produced a non-finite state");
            }

            state = next_state;
            result.completed_steps = step + 1;
            result.elapsed_time = current_time + config_.time_step;
            result.minimum_state = std::min(result.minimum_state, state);
            result.maximum_state = std::max(result.maximum_state, state);

            if (config_.verbose) {
                std::cout << "step=" << (step + 1) << " time=" << result.elapsed_time
                          << " state=" << state << '\n';
            }
        }

        // Extension point: collect domain KPIs, uncertainty intervals, and
        // decision metrics required by the executive process and financial plan.
        result.final_state = state;
        result.successful = true;
        return result;
    }

private:
    SimulationConfig config_;
};

namespace {
void print_usage(const char* program) {
    std::cout << "Usage: " << program
              << " [--steps N] [--time-step X] [--initial-state X]"
                 " [--seed N] [--verbose]\n";
}

std::size_t parse_size(const std::string& value, const char* name) {
    std::size_t consumed = 0;
    const unsigned long long parsed = std::stoull(value, &consumed);
    if (consumed != value.size() || parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument(std::string("invalid value for ") + name);
    }
    return static_cast<std::size_t>(parsed);
}

double parse_double(const std::string& value, const char* name) {
    std::size_t consumed = 0;
    const double parsed = std::stod(value, &consumed);
    if (consumed != value.size() || !std::isfinite(parsed)) {
        throw std::invalid_argument(std::string("invalid value for ") + name);
    }
    return parsed;
}
}  // namespace

int main(int argc, char* argv[]) {
    try {
        SimulationConfig config{};
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            const auto require_value = [&](const char* option) -> std::string {
                if (index + 1 >= argc) {
                    throw std::invalid_argument(std::string("missing value for ") + option);
                }
                return argv[++index];
            };

            if (argument == "--steps") {
                config.steps = parse_size(require_value("--steps"), "--steps");
            } else if (argument == "--time-step") {
                config.time_step = parse_double(require_value("--time-step"), "--time-step");
            } else if (argument == "--initial-state") {
                config.initial_state = parse_double(require_value("--initial-state"), "--initial-state");
            } else if (argument == "--seed") {
                const auto seed = parse_size(require_value("--seed"), "--seed");
                if (seed > std::numeric_limits<unsigned int>::max()) {
                    throw std::invalid_argument("--seed is out of range");
                }
                config.random_seed = static_cast<unsigned int>(seed);
            } else if (argument == "--verbose") {
                config.verbose = true;
            } else if (argument == "--help" || argument == "-h") {
                print_usage(argv[0]);
                return EXIT_SUCCESS;
            } else {
                throw std::invalid_argument("unknown option: " + argument);
            }
        }

        Simulation simulation(config);
        const SimulationResult result = simulation.run();

        std::cout << std::fixed << std::setprecision(3)
                  << "Simulation completed\n"
                  << "  status: " << (result.successful ? "success" : "failure") << '\n'
                  << "  steps: " << result.completed_steps << '\n'
                  << "  elapsed time: " << result.elapsed_time << '\n'
                  << "  final state: " << result.final_state << '\n'
                  << "  minimum state: " << result.minimum_state << '\n'
                  << "  maximum state: " << result.maximum_state << '\n'
                  << "  seed reserved for future stochastic model: " << config.random_seed << '\n';
        return result.successful ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "Simulation error: " << error.what() << '\n';
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
}
