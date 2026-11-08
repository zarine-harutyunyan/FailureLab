#include "failurelab/model.hpp"
#include <charconv>
#include <iostream>
#include <map>
#include <set>

namespace {
const char *help =
    "FailureLab " FAILURELAB_VERSION R"( — deterministic distributed-systems reliability simulation
Usage:
  failurelab --help
  failurelab --version
  failurelab validate --config FILE [--seed UINT64]
  failurelab run --config FILE --strategy baseline|fixed|exponential|breaker --output DIR
  failurelab compare --config FILE --output DIR
Options:
  --seed UINT64       Override the scenario seed
  --trace-limit N     Record at most N events per strategy (0..100000; default 0)
  --overwrite         Replace an existing FailureLab report directory
Examples:
  failurelab validate --config scenarios/recovery.json
  failurelab compare --config scenarios/recovery.json --output results/comparison
Outputs: failurelab-report.json, results.csv, summary.txt in DIR.
No network requests, real delays, queueing or capacity model are simulated.
Exit codes: 0 success; 2 CLI usage; 3 configuration; 4 output I/O; 5 internal error.
)";
struct UsageError : std::runtime_error {
  using std::runtime_error::runtime_error;
};
std::uint64_t number(const std::string &value, const std::string &option) {
  std::uint64_t n{};
  auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
  if (ec != std::errc{} || end != value.data() + value.size())
    throw UsageError(option + ": expected unsigned 64-bit integer");
  return n;
}
int execute(int argc, char **argv) {
  using namespace failurelab;
  if (argc == 2 && std::string(argv[1]) == "--help") {
    std::cout << help;
    return 0;
  }
  if (argc == 2 && std::string(argv[1]) == "--version") {
    std::cout << "FailureLab " << FAILURELAB_VERSION << '\n';
    return 0;
  }
  if (argc < 2)
    throw UsageError("expected validate, run or compare; see --help");
  std::string command = argv[1];
  if (command != "validate" && command != "run" && command != "compare")
    throw UsageError("unknown command: " + command);
  if (argc == 3 && std::string(argv[2]) == "--help") {
    std::cout << help;
    return 0;
  }
  std::map<std::string, std::string> options;
  bool overwrite = false;
  for (int i = 2; i < argc; ++i) {
    std::string key = argv[i];
    if (key == "--overwrite") {
      if (overwrite)
        throw UsageError("duplicate --overwrite");
      overwrite = true;
      continue;
    }
    if (key != "--config" && key != "--output" && key != "--seed" && key != "--strategy" &&
        key != "--trace-limit")
      throw UsageError("unknown option: " + key);
    if (options.contains(key))
      throw UsageError("duplicate option: " + key);
    if (++i == argc || std::string(argv[i]).starts_with("--"))
      throw UsageError(key + ": missing value");
    options[key] = argv[i];
  }
  if (!options.contains("--config"))
    throw UsageError("--config is required");
  if (command == "validate" && (options.contains("--output") || options.contains("--strategy") ||
                                options.contains("--trace-limit") || overwrite))
    throw UsageError("validate accepts only --config and --seed");
  if (command == "compare" && options.contains("--strategy"))
    throw UsageError("compare runs all four strategies; omit --strategy");
  if (command == "run" && !options.contains("--strategy"))
    throw UsageError("run requires --strategy");
  if (command != "validate" && !options.contains("--output"))
    throw UsageError("--output is required");
  std::size_t trace_limit = 0;
  if (options.contains("--trace-limit")) {
    auto n = number(options.at("--trace-limit"), "--trace-limit");
    if (n > 100000)
      throw UsageError("--trace-limit must be <= 100000");
    trace_limit = static_cast<std::size_t>(n);
  }
  std::optional<std::uint64_t> seed;
  if (options.contains("--seed"))
    seed = number(options.at("--seed"), "--seed");
  auto scenario = load_config(options.at("--config"));
  if (seed)
    scenario.seed = *seed;
  if (command == "validate") {
    std::cout << "Valid: " << scenario.name << " (schema 1, seed " << scenario.seed << ")\n";
    return 0;
  }
  std::vector<Result> results;
  if (command == "compare")
    for (auto strategy : strategies)
      results.push_back(simulate(scenario, strategy, trace_limit));
  else
    results.push_back(simulate(scenario, parse_strategy(options.at("--strategy")), trace_limit));
  write_report(options.at("--output"), scenario, results, overwrite);
  std::cout << summary(scenario, results) << "Report: " << options.at("--output") << '\n';
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  try {
    return execute(argc, argv);
  } catch (const UsageError &e) {
    std::cerr << "usage: " << e.what() << '\n';
    return 2;
  } catch (const failurelab::ConfigError &e) {
    std::cerr << "configuration: " << e.what() << '\n';
    return 3;
  } catch (const failurelab::OutputError &e) {
    std::cerr << "output: " << e.what() << '\n';
    return 4;
  } catch (const std::exception &e) {
    std::cerr << "internal: " << e.what() << '\n';
    return 5;
  }
}
