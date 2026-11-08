#include "failurelab/model.hpp"
#include <algorithm>
#include <queue>
#include <tuple>

namespace failurelab {
namespace {
enum class Kind : std::uint8_t { deadline, timeout, completion, start };
struct Event {
  Time at;
  Kind kind;
  std::uint64_t order;
  std::uint32_t request;
  std::size_t attempt;
  bool operator>(const Event &other) const {
    return std::tie(at, kind, order) > std::tie(other.at, other.kind, other.order);
  }
};
struct Request {
  Time arrival{0}, deadline{0};
  std::uint32_t started{0};
  bool done{false};
  std::optional<std::size_t> active;
};
struct Attempt {
  std::uint32_t number;
  std::uint64_t generation;
  bool error;
  bool settled{false};
};
class Engine {
public:
  Engine(const Scenario &scenario, Strategy strategy, std::size_t trace_limit)
      : s_(scenario), strategy_(strategy), limit_(trace_limit), breaker_(s_.breaker),
        jitter_{mix(s_.seed ^ 0x4a49545445520001ULL)}, requests_(s_.request_count) {
    r_.strategy = strategy;
    r_.offered = s_.request_count;
    r_.arrival_duration = Time{s_.arrival_interval.count() * (s_.request_count - 1)};
  }
  Result run() {
    for (std::uint32_t id = 0; id < s_.request_count; ++id) {
      auto &q = requests_[id];
      q.arrival = Time{s_.arrival_interval.count() * id};
      q.deadline = add(q.arrival, s_.deadline);
      push(q.arrival, Kind::start, id);
      push(q.deadline, Kind::deadline, id);
    }
    while (!events_.empty()) {
      auto e = events_.top();
      events_.pop();
      if (e.at < now_)
        throw std::logic_error("event time moved backwards");
      now_ = e.at;
      ++r_.processed_events;
      if (r_.processed_events > 4'200'000)
        throw std::logic_error("event resource bound exceeded");
      auto &q = requests_[e.request];
      if (e.kind == Kind::completion) {
        r_.drain_duration = std::max(r_.drain_duration, now_);
        auto &a = attempts_.at(e.attempt);
        if (a.settled) {
          ++r_.late_completions;
          trace(e.request, a.number, "late_completion");
          continue;
        }
        settle(q, a, !a.error);
        trace(e.request, a.number, a.error ? "service_error" : "success");
        if (a.error) {
          ++r_.service_errors;
          failed(e.request, "service_error");
        } else
          finish(e.request, "success");
      } else if (e.kind == Kind::deadline) {
        if (q.done)
          continue;
        if (q.active) {
          auto &a = attempts_.at(*q.active);
          if (!a.settled) {
            settle(q, a, false);
            ++r_.timeouts;
            trace(e.request, a.number, "timeout");
          }
        }
        finish(e.request, "deadline");
      } else if (e.kind == Kind::timeout) {
        auto &a = attempts_.at(e.attempt);
        if (a.settled)
          continue;
        settle(q, a, false);
        ++r_.timeouts;
        trace(e.request, a.number, "timeout");
        failed(e.request, "timeout");
      } else {
        if (q.done)
          continue;
        if (now_ >= q.deadline)
          throw std::logic_error("attempt at or after deadline");
        std::uint64_t generation = 0;
        if (strategy_ == Strategy::breaker) {
          auto token = breaker_.admit(now_);
          if (!token) {
            ++r_.rejections;
            trace(e.request, q.started, "circuit_rejected");
            finish(e.request, "circuit_open");
            continue;
          }
          generation = *token;
        }
        ++q.started;
        ++r_.attempts;
        if (q.started > 1)
          ++r_.retries;
        auto sample = service_sample(s_, e.request, q.started, now_);
        q.active = attempts_.size();
        attempts_.push_back({q.started, generation, sample.error, false});
        trace(e.request, q.started, "attempt_start");
        push(add(now_, sample.latency), Kind::completion, e.request, *q.active);
        push(add(now_, std::min(s_.attempt_timeout, q.deadline - now_)), Kind::timeout, e.request,
             *q.active);
      }
    }
    if (r_.succeeded + r_.final_service_error + r_.final_timeout + r_.final_deadline +
                r_.final_circuit_open !=
            r_.offered ||
        r_.latencies.size() != r_.offered)
      throw std::logic_error("request partition invariant failed");
    auto started_requests = static_cast<std::uint64_t>(std::count_if(
        requests_.begin(), requests_.end(), [](const Request &q) { return q.started != 0; }));
    if (r_.attempts != r_.succeeded + r_.service_errors + r_.timeouts ||
        r_.late_completions != r_.timeouts || r_.rejections != r_.final_circuit_open ||
        r_.attempts != started_requests + r_.retries ||
        r_.processed_events > 2 * r_.offered + 3 * r_.attempts)
      throw std::logic_error("attempt accounting or event bound invariant failed");
    r_.drain_duration = std::max(r_.drain_duration, r_.completion_duration);
    r_.transitions = breaker_.transitions();
    return std::move(r_);
  }

private:
  void push(Time at, Kind kind, std::uint32_t request, std::size_t attempt = 0) {
    events_.push({at, kind, next_order_++, request, attempt});
  }
  void trace(std::uint32_t id, std::uint32_t attempt, const std::string &event) {
    if (limit_ == 0)
      return;
    if (r_.trace.size() < limit_)
      r_.trace.push_back({now_, id, attempt, event});
    else
      r_.trace_truncated = true;
  }
  void settle(Request &q, Attempt &a, bool success) {
    a.settled = true;
    q.active.reset();
    if (strategy_ == Strategy::breaker)
      breaker_.settle(now_, a.generation, success);
  }
  void finish(std::uint32_t id, const std::string &outcome) {
    auto &q = requests_[id];
    if (q.done)
      throw std::logic_error("request completed twice");
    q.done = true;
    auto latency = now_ - q.arrival;
    r_.latencies.push_back(latency);
    r_.completion_duration = std::max(r_.completion_duration, now_);
    if (outcome == "success") {
      ++r_.succeeded;
      r_.success_latencies.push_back(latency);
    } else if (outcome == "service_error")
      ++r_.final_service_error;
    else if (outcome == "timeout")
      ++r_.final_timeout;
    else if (outcome == "deadline")
      ++r_.final_deadline;
    else if (outcome == "circuit_open")
      ++r_.final_circuit_open;
    else
      throw std::logic_error("unknown outcome");
    trace(id, q.started, "final_" + outcome);
  }
  void failed(std::uint32_t id, const std::string &reason) {
    const auto &q = requests_[id];
    auto max_attempts = strategy_ == Strategy::baseline ? 1U : s_.retry.max_attempts;
    if (q.started >= max_attempts) {
      finish(id, reason);
      return;
    }
    auto delay = backoff(s_.retry, strategy_, q.started, jitter_);
    if (delay >= q.deadline - now_) {
      // The request remains pending until its deadline, with no active downstream attempt.
      trace(id, q.started, "retry_exceeds_budget");
      return;
    }
    push(add(now_, delay), Kind::start, id);
  }
  const Scenario &s_;
  Strategy strategy_;
  std::size_t limit_;
  Breaker breaker_;
  Random jitter_;
  Result r_;
  Time now_{0};
  std::uint64_t next_order_{0};
  std::vector<Request> requests_;
  std::vector<Attempt> attempts_;
  std::priority_queue<Event, std::vector<Event>, std::greater<>> events_;
};
} // namespace
Result simulate(const Scenario &scenario, Strategy strategy, std::size_t trace_limit) {
  // The public entry point enforces exactly the same limits as file-based input.
  auto checked = parse_config(config_json(scenario));
  if (trace_limit > 100000)
    throw ConfigError("trace limit must be <= 100000");
  return Engine(checked, strategy, trace_limit).run();
}
std::optional<std::int64_t> percentile(std::vector<Time> values, unsigned percent) {
  if (percent == 0 || percent > 100)
    throw std::invalid_argument("percentile must be 1..100");
  if (values.empty())
    return std::nullopt;
  std::sort(values.begin(), values.end());
  auto rank = (values.size() * percent + 99) / 100;
  return values[rank - 1].count();
}
} // namespace failurelab
