#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace failurelab {
using Time = std::chrono::milliseconds;
constexpr std::int64_t max_time = 1'000'000'000'000;
struct ConfigError : std::runtime_error {
  using std::runtime_error::runtime_error;
};
struct OutputError : std::runtime_error {
  using std::runtime_error::runtime_error;
};
Time add(Time a, Time b);
struct Latency {
  Time min{0};
  Time max{0};
};
struct Behavior {
  Latency latency;
  double failure_probability{0};
};
struct Window {
  Time start{0};
  Time end{0};
  Behavior behavior;
};
struct Retry {
  std::uint32_t max_attempts{3};
  Time base_delay{10};
  Time max_backoff{1000};
  bool full_jitter{true};
};
struct BreakerConfig {
  std::uint32_t failure_threshold{5};
  Time open_duration{100};
};
struct Scenario {
  std::uint32_t schema_version{1};
  std::string name;
  std::uint64_t seed{1};
  std::uint32_t request_count{1};
  Time arrival_interval{10};
  Time deadline{1000};
  Time attempt_timeout{100};
  Behavior service;
  std::vector<Window> windows;
  Retry retry;
  BreakerConfig breaker;
};
Scenario parse_config(const nlohmann::json &value);
Scenario load_config(const std::filesystem::path &path);
nlohmann::json config_json(const Scenario &s);
const Behavior &behavior_at(const Scenario &s, Time now);
enum class Strategy : std::uint8_t { baseline, fixed, exponential, breaker };
std::string name(Strategy s);
Strategy parse_strategy(const std::string &s);
constexpr Strategy strategies[] = {Strategy::baseline, Strategy::fixed, Strategy::exponential,
                                   Strategy::breaker};

// SplitMix64 with specified unsigned wraparound; no implementation-defined distributions.
std::uint64_t mix(std::uint64_t x);
struct Random {
  std::uint64_t state;
  std::uint64_t next();
  std::uint64_t bounded(std::uint64_t inclusive_max);
};
struct Sample {
  Time latency;
  bool error;
};
Sample service_sample(const Scenario &s, std::uint32_t request, std::uint32_t attempt, Time now);
Time backoff(const Retry &retry, Strategy strategy, std::uint32_t failed_attempt, Random &random);

enum class State : std::uint8_t { closed, open, half_open };
std::string name(State s);
struct Transition {
  Time at;
  State from;
  State to;
};
class Breaker {
public:
  explicit Breaker(BreakerConfig config);
  std::optional<std::uint64_t> admit(Time now);
  void settle(Time now, std::uint64_t generation, bool success);
  State state() const { return state_; }
  const std::vector<Transition> &transitions() const { return transitions_; }

private:
  void change(Time now, State next);
  BreakerConfig config_;
  State state_{State::closed};
  std::uint32_t failures_{0};
  std::uint64_t generation_{0};
  Time reopen_at_{0};
  bool probe_{false};
  std::vector<Transition> transitions_;
};
struct Trace {
  Time at;
  std::uint32_t request;
  std::uint32_t attempt;
  std::string event;
};
struct Result {
  Strategy strategy{Strategy::baseline};
  std::uint64_t offered{0}, succeeded{0}, attempts{0}, retries{0}, service_errors{0}, timeouts{0},
      rejections{0}, late_completions{0};
  std::uint64_t final_service_error{0}, final_timeout{0}, final_deadline{0}, final_circuit_open{0};
  Time arrival_duration{0}, completion_duration{0}, drain_duration{0};
  std::vector<Time> latencies, success_latencies;
  std::vector<Transition> transitions;
  std::vector<Trace> trace;
  bool trace_truncated{false};
  std::uint64_t processed_events{0};
};
Result simulate(const Scenario &scenario, Strategy strategy, std::size_t trace_limit = 0);
std::optional<std::int64_t> percentile(std::vector<Time> values, unsigned percent);
nlohmann::json result_json(const Scenario &scenario, const Result &result);
std::string csv_report(const Scenario &scenario, const std::vector<Result> &results);
std::string summary(const Scenario &scenario, const std::vector<Result> &results);
void write_report(const std::filesystem::path &destination, const Scenario &scenario,
                  const std::vector<Result> &results, bool overwrite);
} // namespace failurelab
