#include "failurelab/model.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>

namespace failurelab {
namespace {
using Json = nlohmann::json;
[[noreturn]] void invalid(const std::string &path, const std::string &reason) {
  throw ConfigError(path + ": " + reason);
}
void fields(const Json &j, const std::string &path, std::initializer_list<std::string> allowed,
            std::initializer_list<std::string> required) {
  if (!j.is_object())
    invalid(path, "expected an object");
  for (const auto &[key, unused] : j.items()) {
    (void)unused;
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
      invalid(std::string(path).append(".").append(key), "unknown field");
  }
  for (const auto &key : required)
    if (!j.contains(key))
      invalid(std::string(path).append(".").append(key), "required field missing");
}
std::uint64_t integer(const Json &j, const std::string &path, std::uint64_t low,
                      std::uint64_t high) {
  if (!j.is_number_integer() ||
      (j.is_number_integer() && !j.is_number_unsigned() && j.get<std::int64_t>() < 0))
    invalid(path, "expected a nonnegative integer");
  auto value = j.get<std::uint64_t>();
  if (value < low || value > high)
    invalid(path, "must be in [" + std::to_string(low) + ", " + std::to_string(high) + "]");
  return value;
}
Time duration(const Json &j, const std::string &path, bool positive = false) {
  return Time{integer(j, path, positive ? 1 : 0, static_cast<std::uint64_t>(max_time))};
}
Behavior behavior(const Json &j, const std::string &path) {
  fields(j, path, {"latency", "failure_probability"}, {"latency", "failure_probability"});
  const auto &l = j.at("latency");
  if (!l.is_object() || !l.contains("type") || !l.at("type").is_string())
    invalid(path + ".latency.type", "expected fixed or uniform");
  auto type = l.at("type").get<std::string>();
  Latency latency;
  if (type == "fixed") {
    fields(l, path + ".latency", {"type", "value_ms"}, {"type", "value_ms"});
    latency.min = latency.max = duration(l.at("value_ms"), path + ".latency.value_ms");
  } else if (type == "uniform") {
    fields(l, path + ".latency", {"type", "min_ms", "max_ms"}, {"type", "min_ms", "max_ms"});
    latency.min = duration(l.at("min_ms"), path + ".latency.min_ms");
    latency.max = duration(l.at("max_ms"), path + ".latency.max_ms");
    if (latency.max < latency.min)
      invalid(path + ".latency.max_ms", "must be >= min_ms");
  } else
    invalid(path + ".latency.type", "expected fixed or uniform");
  const auto &p = j.at("failure_probability");
  if (!p.is_number())
    invalid(path + ".failure_probability", "expected a number in [0, 1]");
  auto probability = p.get<double>();
  if (!std::isfinite(probability) || probability < 0 || probability > 1)
    invalid(path + ".failure_probability", "must be finite and in [0, 1]");
  return {latency, probability};
}
Json behavior_json(const Behavior &b) {
  return {
      {"latency",
       {{"type", "uniform"}, {"min_ms", b.latency.min.count()}, {"max_ms", b.latency.max.count()}}},
      {"failure_probability", b.failure_probability}};
}
} // namespace
Time add(Time a, Time b) {
  if (a.count() < 0 || b.count() < 0 || a.count() > max_time - b.count())
    throw ConfigError("time: calculation exceeds supported range");
  return a + b;
}
Scenario parse_config(const Json &j) {
  fields(j, "$",
         {"schema_version", "name", "seed", "request_count", "arrival_interval_ms", "deadline_ms",
          "attempt_timeout_ms", "service", "windows", "retry", "circuit_breaker"},
         {"schema_version", "name", "seed", "request_count", "arrival_interval_ms", "deadline_ms",
          "attempt_timeout_ms", "service", "retry", "circuit_breaker"});
  Scenario s;
  integer(j.at("schema_version"), "$.schema_version", 1, 1);
  if (!j.at("name").is_string())
    invalid("$.name", "expected a string");
  s.name = j.at("name").get<std::string>();
  if (s.name.empty() || s.name.size() > 120 ||
      std::any_of(s.name.begin(), s.name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
    invalid("$.name", "expected 1–120 bytes without control characters");
  s.seed = integer(j.at("seed"), "$.seed", 0, std::numeric_limits<std::uint64_t>::max());
  s.request_count =
      static_cast<std::uint32_t>(integer(j.at("request_count"), "$.request_count", 1, 100000));
  s.arrival_interval = duration(j.at("arrival_interval_ms"), "$.arrival_interval_ms");
  s.deadline = duration(j.at("deadline_ms"), "$.deadline_ms", true);
  s.attempt_timeout = duration(j.at("attempt_timeout_ms"), "$.attempt_timeout_ms", true);
  s.service = behavior(j.at("service"), "$.service");
  if (j.contains("windows")) {
    if (!j.at("windows").is_array() || j.at("windows").size() > 1000)
      invalid("$.windows", "expected array with at most 1000 entries");
    std::size_t i = 0;
    for (const auto &w : j.at("windows")) {
      auto path = "$.windows[" + std::to_string(i++) + "]";
      fields(w, path, {"start_ms", "end_ms", "service"}, {"start_ms", "end_ms", "service"});
      Window window{duration(w.at("start_ms"), path + ".start_ms"),
                    duration(w.at("end_ms"), path + ".end_ms"),
                    behavior(w.at("service"), path + ".service")};
      if (window.start >= window.end)
        invalid(path, "start_ms must be < end_ms");
      s.windows.push_back(window);
    }
    std::sort(s.windows.begin(), s.windows.end(),
              [](const Window &a, const Window &b) { return a.start < b.start; });
    for (std::size_t i2 = 1; i2 < s.windows.size(); ++i2)
      if (s.windows[i2].start < s.windows[i2 - 1].end)
        invalid("$.windows", "overlapping windows are forbidden");
  }
  const auto &r = j.at("retry");
  fields(r, "$.retry", {"max_attempts", "base_delay_ms", "max_backoff_ms", "full_jitter"},
         {"max_attempts", "base_delay_ms", "max_backoff_ms", "full_jitter"});
  s.retry.max_attempts =
      static_cast<std::uint32_t>(integer(r.at("max_attempts"), "$.retry.max_attempts", 1, 20));
  s.retry.base_delay = duration(r.at("base_delay_ms"), "$.retry.base_delay_ms");
  s.retry.max_backoff = duration(r.at("max_backoff_ms"), "$.retry.max_backoff_ms");
  if (s.retry.base_delay > s.retry.max_backoff)
    invalid("$.retry.max_backoff_ms", "must be >= base_delay_ms");
  if (!r.at("full_jitter").is_boolean())
    invalid("$.retry.full_jitter", "expected boolean");
  s.retry.full_jitter = r.at("full_jitter").get<bool>();
  const auto &b = j.at("circuit_breaker");
  fields(b, "$.circuit_breaker", {"failure_threshold", "open_duration_ms"},
         {"failure_threshold", "open_duration_ms"});
  s.breaker.failure_threshold = static_cast<std::uint32_t>(
      integer(b.at("failure_threshold"), "$.circuit_breaker.failure_threshold", 1, 100000));
  s.breaker.open_duration =
      duration(b.at("open_duration_ms"), "$.circuit_breaker.open_duration_ms", true);
  if (static_cast<std::uint64_t>(s.request_count) * s.retry.max_attempts > 1'000'000)
    invalid("$.retry.max_attempts", "request_count * max_attempts must be <= 1000000");
  if (s.request_count > 1 && s.arrival_interval.count() > max_time / (s.request_count - 1))
    invalid("$.arrival_interval_ms", "arrival horizon exceeds supported range");
  Time horizon{s.arrival_interval.count() * (s.request_count - 1)};
  auto last_deadline = add(horizon, s.deadline);
  Time max_latency = s.service.latency.max;
  for (const auto &w : s.windows)
    max_latency = std::max(max_latency, w.behavior.latency.max);
  add(last_deadline, max_latency);
  add(last_deadline, s.breaker.open_duration);
  return s;
}
Scenario load_config(const std::filesystem::path &path) {
  std::error_code ec;
  auto size = std::filesystem::file_size(path, ec);
  if (ec || size > 1024ULL * 1024ULL)
    throw ConfigError(path.string() + ": cannot read scenario or file exceeds 1 MiB");
  std::ifstream input(path);
  if (!input)
    throw ConfigError(path.string() + ": cannot open scenario");
  // Bound the read itself as well as the stat: the file may grow between those operations.
  constexpr std::size_t limit = std::size_t{1024} * 1024;
  std::string contents(limit + 1, '\0');
  input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
  auto bytes = static_cast<std::size_t>(input.gcount());
  if (input.bad() || bytes > limit)
    throw ConfigError(path.string() + ": read failed or scenario exceeds 1 MiB");
  contents.resize(bytes);
  try {
    // Reject duplicate keys, which would otherwise silently replace configuration.
    std::vector<std::set<std::string>> keys;
    auto callback = [&keys](int depth, Json::parse_event_t event, Json &parsed) {
      if (depth > 64)
        throw ConfigError("JSON nesting exceeds limit of 64");
      if (event == Json::parse_event_t::object_start)
        keys.emplace_back();
      if (event == Json::parse_event_t::key &&
          !keys.back().insert(parsed.get<std::string>()).second)
        throw ConfigError("duplicate JSON key: " + parsed.get<std::string>());
      if (event == Json::parse_event_t::object_end)
        keys.pop_back();
      return true;
    };
    return parse_config(Json::parse(contents, callback));
  } catch (const ConfigError &e) {
    throw ConfigError(path.string() + ": " + e.what());
  } catch (const Json::exception &e) {
    throw ConfigError(path.string() + ": invalid JSON: " + e.what());
  }
}
const Behavior &behavior_at(const Scenario &s, Time now) {
  for (const auto &w : s.windows)
    if (now >= w.start && now < w.end)
      return w.behavior;
  return s.service;
}
Json config_json(const Scenario &s) {
  Json windows = Json::array();
  for (const auto &w : s.windows)
    windows.push_back({{"start_ms", w.start.count()},
                       {"end_ms", w.end.count()},
                       {"service", behavior_json(w.behavior)}});
  return {{"schema_version", s.schema_version},
          {"name", s.name},
          {"seed", s.seed},
          {"request_count", s.request_count},
          {"arrival_interval_ms", s.arrival_interval.count()},
          {"deadline_ms", s.deadline.count()},
          {"attempt_timeout_ms", s.attempt_timeout.count()},
          {"service", behavior_json(s.service)},
          {"windows", windows},
          {"retry",
           {{"max_attempts", s.retry.max_attempts},
            {"base_delay_ms", s.retry.base_delay.count()},
            {"max_backoff_ms", s.retry.max_backoff.count()},
            {"full_jitter", s.retry.full_jitter}}},
          {"circuit_breaker",
           {{"failure_threshold", s.breaker.failure_threshold},
            {"open_duration_ms", s.breaker.open_duration.count()}}}};
}
} // namespace failurelab
