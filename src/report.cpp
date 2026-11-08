#include "failurelab/model.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace failurelab {
namespace {
using Json = nlohmann::json;
Json quantiles(const std::vector<Time> &values) {
  Json output;
  for (unsigned p : {50U, 95U, 99U}) {
    auto value = percentile(values, p);
    output["p" + std::to_string(p) + "_ms"] = value ? Json(*value) : Json(nullptr);
  }
  return output;
}
std::string quote(const std::string &value) {
  std::string out = "\"";
  for (char c : value) {
    if (c == '"')
      out += '"';
    out += c;
  }
  return out + '"';
}
void write(const std::filesystem::path &path, const std::string &content) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw OutputError(path.string() + ": cannot open output file");
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  output.close();
  if (!output)
    throw OutputError(path.string() + ": write or close failed");
}
struct Stage {
  std::filesystem::path path;
  bool preserve{false};
  ~Stage() {
    if (!preserve) {
      std::error_code ec;
      std::filesystem::remove_all(path, ec);
    }
  }
};
} // namespace
Json result_json(const Scenario &s, const Result &r) {
  Json transitions = Json::array();
  Json durations = {{"CLOSED", 0}, {"OPEN", 0}, {"HALF_OPEN", 0}};
  Time previous{0};
  State state = State::closed;
  for (const auto &t : r.transitions) {
    durations[name(state)] =
        durations.at(name(state)).get<std::int64_t>() + (t.at - previous).count();
    transitions.push_back({{"at_ms", t.at.count()}, {"from", name(t.from)}, {"to", name(t.to)}});
    state = t.to;
    previous = t.at;
  }
  durations[name(state)] =
      durations.at(name(state)).get<std::int64_t>() + (r.completion_duration - previous).count();
  Json trace = Json::array();
  for (const auto &t : r.trace)
    trace.push_back({{"at_ms", t.at.count()},
                     {"request", t.request},
                     {"attempt", t.attempt},
                     {"event", t.event}});
  return {
      {"application_version", FAILURELAB_VERSION},
      {"schema_version", s.schema_version},
      {"scenario", s.name},
      {"seed", s.seed},
      {"strategy", name(r.strategy)},
      {"logical_requests_offered", r.offered},
      {"successful_requests", r.succeeded},
      {"failed_requests", r.offered - r.succeeded},
      {"final_failures",
       {{"service_error", r.final_service_error},
        {"timeout", r.final_timeout},
        {"deadline", r.final_deadline},
        {"circuit_open", r.final_circuit_open}}},
      {"downstream_attempts", r.attempts},
      {"retry_attempts", r.retries},
      {"attempt_service_errors", r.service_errors},
      {"attempt_timeouts", r.timeouts},
      {"circuit_rejections", r.rejections},
      {"late_completions", r.late_completions},
      {"success_rate", static_cast<double>(r.succeeded) / static_cast<double>(r.offered)},
      {"attempt_amplification", static_cast<double>(r.attempts) / static_cast<double>(r.offered)},
      {"request_latency", quantiles(r.latencies)},
      {"successful_request_latency", quantiles(r.success_latencies)},
      {"workload_arrival_duration_ms", r.arrival_duration.count()},
      {"completion_duration_ms", r.completion_duration.count()},
      {"downstream_drain_duration_ms", r.drain_duration.count()},
      {"successful_requests_per_second",
       r.completion_duration.count() == 0
           ? Json(nullptr)
           : Json(static_cast<double>(r.succeeded) * 1000.0 /
                  static_cast<double>(r.completion_duration.count()))},
      {"breaker_transitions", transitions},
      {"breaker_state_duration_ms", r.strategy == Strategy::breaker ? durations : Json(nullptr)},
      {"processed_events", r.processed_events},
      {"trace", trace},
      {"trace_truncated", r.trace_truncated}};
}
std::string csv_report(const Scenario &s, const std::vector<Result> &results) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(17);
  out << "application_version,schema_version,scenario,seed,strategy,logical_requests_offered,"
         "successful_requests,failed_requests,final_service_error,final_timeout,final_deadline,"
         "final_circuit_open,downstream_attempts,retry_attempts,attempt_service_errors,attempt_"
         "timeouts,circuit_rejections,late_completions,success_rate,attempt_amplification,request_"
         "p50_ms,request_p95_ms,request_p99_ms,success_p50_ms,success_p95_ms,success_p99_ms,"
         "arrival_duration_ms,completion_duration_ms,downstream_drain_duration_ms,successful_"
         "requests_per_second,breaker_closed_ms,breaker_open_ms,breaker_half_open_ms,breaker_"
         "transition_count,processed_events,trace_truncated\n";
  for (const auto &r : results) {
    auto j = result_json(s, r);
    out << quote(FAILURELAB_VERSION) << ',' << s.schema_version << ',' << quote(s.name) << ','
        << s.seed << ',' << quote(name(r.strategy));
    for (auto key : {"logical_requests_offered", "successful_requests", "failed_requests"})
      out << ',' << j.at(key);
    for (auto key : {"service_error", "timeout", "deadline", "circuit_open"})
      out << ',' << j.at("final_failures").at(key);
    for (auto key :
         {"downstream_attempts", "retry_attempts", "attempt_service_errors", "attempt_timeouts",
          "circuit_rejections", "late_completions", "success_rate", "attempt_amplification"})
      out << ',' << j.at(key);
    for (auto subset : {"request_latency", "successful_request_latency"})
      for (auto p : {"p50_ms", "p95_ms", "p99_ms"}) {
        out << ',';
        if (!j.at(subset).at(p).is_null())
          out << j.at(subset).at(p);
      }
    for (auto key : {"workload_arrival_duration_ms", "completion_duration_ms",
                     "downstream_drain_duration_ms", "successful_requests_per_second"}) {
      out << ',';
      if (!j.at(key).is_null())
        out << j.at(key);
    }
    for (auto state : {"CLOSED", "OPEN", "HALF_OPEN"}) {
      out << ',';
      if (r.strategy == Strategy::breaker)
        out << j.at("breaker_state_duration_ms").at(state);
    }
    out << ',' << r.transitions.size() << ',' << r.processed_events << ','
        << (r.trace_truncated ? "true" : "false") << '\n';
  }
  return out.str();
}
std::string summary(const Scenario &s, const std::vector<Result> &results) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << "FailureLab " << FAILURELAB_VERSION << " | deterministic discrete-event simulation\n"
      << s.name << " | seed " << s.seed << " | schema " << s.schema_version << '\n';
  out << "strategy       success/offered  success%  attempts    amp  all p95  success p95 (ms)\n";
  for (const auto &r : results) {
    auto success_p95 = percentile(r.success_latencies, 95);
    out << std::left << std::setw(15) << name(r.strategy) << std::right << std::setw(5)
        << r.succeeded << '/' << std::left << std::setw(8) << r.offered << std::right << std::fixed
        << std::setprecision(1) << std::setw(8)
        << 100.0 * static_cast<double>(r.succeeded) / static_cast<double>(r.offered)
        << std::setw(10) << r.attempts << std::setw(7) << std::setprecision(3)
        << static_cast<double>(r.attempts) / static_cast<double>(r.offered) << std::setw(9)
        << percentile(r.latencies, 95).value_or(0) << std::setw(17)
        << (success_p95 ? std::to_string(*success_p95) : "n/a") << '\n';
  }
  out << "\nFinal failures: service-error / timeout / deadline / circuit-open\n";
  for (const auto &r : results) {
    out << "  " << name(r.strategy) << ": " << r.final_service_error << " / " << r.final_timeout
        << " / " << r.final_deadline << " / " << r.final_circuit_open
        << "; attempt errors=" << r.service_errors << ", timeouts=" << r.timeouts
        << ", rejected=" << r.rejections << ", late=" << r.late_completions << '\n';
    out << "    logical completion=" << r.completion_duration.count()
        << " ms; downstream drain=" << r.drain_duration.count() << " ms; success throughput=";
    if (r.completion_duration.count() == 0)
      out << "n/a";
    else
      out << std::setprecision(3)
          << static_cast<double>(r.succeeded) * 1000.0 /
                 static_cast<double>(r.completion_duration.count())
          << "/s";
    if (r.strategy == Strategy::breaker) {
      auto durations = result_json(s, r).at("breaker_state_duration_ms");
      out << "; breaker CLOSED/OPEN/HALF_OPEN=" << durations.at("CLOSED") << '/'
          << durations.at("OPEN") << '/' << durations.at("HALF_OPEN") << " ms";
    }
    out << '\n';
  }
  out << "Arrival duration=" << (results.empty() ? 0 : results.front().arrival_duration.count())
      << " ms. All-request latency includes fast failures; success latency excludes them.\n";
  out << "Amplification counts downstream traffic; this model has no capacity or queueing "
         "effects.\n";
  return out.str();
}
void write_report(const std::filesystem::path &destination, const Scenario &s,
                  const std::vector<Result> &results, bool overwrite) {
  namespace fs = std::filesystem;
  try {
    auto dest = fs::absolute(destination).lexically_normal();
    if (dest == dest.root_path() || dest.filename().empty())
      throw OutputError("--output: expected a report directory path");
    fs::create_directories(dest.parent_path());
    auto lock = dest;
    lock += ".failurelab-lock";
    if (!fs::create_directory(lock))
      throw OutputError(lock.string() + ": another writer or leftover report lock exists");
    Stage stage{lock};
    if (fs::is_symlink(fs::symlink_status(dest)))
      throw OutputError(dest.string() + ": refusing symbolic-link destination");
    if (fs::exists(dest) && (!overwrite || !fs::is_directory(dest)))
      throw OutputError(dest.string() +
                        ": destination exists (use --overwrite for a report directory)");
    if (fs::exists(dest) && !fs::is_regular_file(dest / "failurelab-report.json"))
      throw OutputError(dest.string() + ": --overwrite requires an existing FailureLab report");
    if (fs::exists(dest)) {
      const std::set<std::string> expected{"failurelab-report.json", "results.csv", "summary.txt"};
      for (const auto &entry : fs::directory_iterator(dest)) {
        if (!expected.contains(entry.path().filename().string()) ||
            !fs::is_regular_file(entry.symlink_status()))
          throw OutputError(
              entry.path().string() +
              ": refusing to overwrite a report containing additional or non-regular files");
      }
    }
    auto pending = lock / "pending";
    fs::create_directory(pending);
    Json report = {
        {"report_version", 1}, {"configuration", config_json(s)}, {"results", Json::array()}};
    for (const auto &r : results)
      report["results"].push_back(result_json(s, r));
    write(pending / "failurelab-report.json", report.dump(2) + '\n');
    write(pending / "results.csv", csv_report(s, results));
    write(pending / "summary.txt", summary(s, results));
    auto backup = lock / "previous";
    bool had_previous = fs::exists(dest);
    if (had_previous) {
      if (!overwrite)
        throw OutputError(dest.string() + ": destination appeared while writing");
      fs::rename(dest, backup);
    }
    try {
      fs::rename(pending, dest);
    } catch (...) {
      if (had_previous) {
        std::error_code ec;
        fs::rename(backup, dest, ec);
        if (ec) {
          stage.preserve = true;
          throw OutputError(
              "report finalization and restore failed; recover previous report from " +
              backup.string());
        }
      }
      throw;
    }
  } catch (const fs::filesystem_error &e) {
    throw OutputError(destination.string() + ": " + e.what());
  }
}
} // namespace failurelab
