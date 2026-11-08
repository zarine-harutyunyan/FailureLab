#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "failurelab/model.hpp"
#include <algorithm>
#include <doctest/doctest.h>
#include <limits>

using namespace failurelab;
namespace {
Scenario basic() {
  Scenario s;
  s.name = "test";
  s.request_count = 1;
  s.service = {{Time{10}, Time{10}}, 0};
  s.retry.full_jitter = false;
  return s;
}
void invariant(const Scenario &s, const Result &r) {
  CHECK(r.succeeded + r.final_service_error + r.final_timeout + r.final_deadline +
            r.final_circuit_open ==
        s.request_count);
  CHECK(r.latencies.size() == s.request_count);
  CHECK(r.success_latencies.size() == r.succeeded);
  auto max_attempts = r.strategy == Strategy::baseline ? 1U : s.retry.max_attempts;
  CHECK(r.attempts <= static_cast<std::uint64_t>(s.request_count) * max_attempts);
  CHECK(r.retries <= r.attempts);
  CHECK(r.succeeded + r.service_errors + r.timeouts == r.attempts);
  CHECK(r.late_completions == r.timeouts);
  CHECK(r.rejections == r.final_circuit_open);
  CHECK(r.processed_events <= static_cast<std::uint64_t>(s.request_count) * (2 + 3 * max_attempts));
  Time previous{0};
  std::vector<unsigned> starts(s.request_count), finals(s.request_count);
  for (const auto &t : r.trace) {
    CHECK(t.at >= previous);
    previous = t.at;
    if (t.event == "attempt_start") {
      CHECK(t.at < Time{s.arrival_interval.count() * t.request} + s.deadline);
      ++starts[t.request];
    }
    if (t.event.starts_with("final_"))
      ++finals[t.request];
  }
  if (!r.trace.empty() && !r.trace_truncated) {
    for (unsigned n : finals)
      CHECK(n == 1);
    for (unsigned n : starts)
      CHECK(n <= max_attempts);
    CHECK(r.attempts == static_cast<std::uint64_t>(
                            std::count_if(r.trace.begin(), r.trace.end(), [](const Trace &t) {
                              return t.event == "attempt_start";
                            })));
  }
  auto j = result_json(s, r);
  if (r.strategy == Strategy::breaker) {
    const auto &d = j.at("breaker_state_duration_ms");
    CHECK(d.at("CLOSED").get<std::int64_t>() + d.at("OPEN").get<std::int64_t>() +
              d.at("HALF_OPEN").get<std::int64_t>() ==
          r.completion_duration.count());
  }
}
} // namespace
TEST_CASE("fixed and exponential schedules, cap and safe arithmetic") {
  Retry p{20, Time{10}, Time{35}, false};
  Random r{1};
  CHECK(backoff(p, Strategy::fixed, 1, r) == Time{10});
  CHECK(backoff(p, Strategy::fixed, 20, r) == Time{10});
  CHECK(backoff(p, Strategy::exponential, 1, r) == Time{10});
  CHECK(backoff(p, Strategy::exponential, 2, r) == Time{20});
  CHECK(backoff(p, Strategy::exponential, 3, r) == Time{35});
  p.base_delay = Time{max_time / 2 + 1};
  p.max_backoff = Time{max_time};
  CHECK(backoff(p, Strategy::exponential, 20, r) == Time{max_time});
  p.base_delay = Time{0};
  CHECK(backoff(p, Strategy::exponential, UINT32_MAX, r) == Time{0});
  CHECK(add(Time{max_time - 1}, Time{1}) == Time{max_time});
  CHECK_THROWS_AS(add(Time{max_time}, Time{1}), ConfigError);
  CHECK_THROWS_AS(add(Time{-1}, Time{0}), ConfigError);
}
TEST_CASE("portable random golden values and jitter stream independence") {
  Random a{0};
  CHECK(a.next() == 0xe220a8397b1dcdafULL);
  CHECK(a.next() == 0x6e789e6aa1b965f4ULL);
  Retry p{4, Time{10}, Time{30}, true};
  Random b{42}, c{42};
  bool varied = false;
  auto first = backoff(p, Strategy::exponential, 3, b);
  c = Random{42};
  CHECK(first == backoff(p, Strategy::exponential, 3, c));
  for (int i = 0; i < 1000; ++i) {
    auto x = backoff(p, Strategy::exponential, 3, b);
    CHECK(x == backoff(p, Strategy::exponential, 3, c));
    CHECK(x >= Time{0});
    CHECK(x <= Time{30});
    varied = varied || x != first;
  }
  CHECK(varied);
  auto s = basic();
  s.service.latency = {Time{2}, Time{100}};
  auto x = service_sample(s, 3, 2, Time{0});
  for (int i = 0; i < 100; ++i)
    (void)b.next();
  auto y = service_sample(s, 3, 2, Time{0});
  CHECK(x.latency == y.latency);
  CHECK(x.error == y.error);
  CHECK(Random{0}.bounded(0) == 0);
  Random full{0};
  CHECK(full.bounded(UINT64_MAX) == 0xe220a8397b1dcdafULL);
}
TEST_CASE("breaker every transition, exclusive probes and stale generations") {
  Breaker b({2, Time{10}});
  auto old = b.admit(Time{0});
  REQUIRE(old);
  b.settle(Time{1}, *old, false);
  CHECK(b.state() == State::closed);
  b.settle(Time{2}, *old, true); // reset the failure streak
  b.settle(Time{3}, *old, false);
  CHECK(b.state() == State::closed);
  b.settle(Time{4}, *old, false);
  CHECK(b.state() == State::open);
  CHECK_FALSE(b.admit(Time{13}));
  auto probe = b.admit(Time{14});
  REQUIRE(probe);
  CHECK(b.state() == State::half_open);
  CHECK_FALSE(b.admit(Time{14}));
  b.settle(Time{15}, *old, true);
  CHECK(b.state() == State::half_open);
  b.settle(Time{16}, *probe, false);
  CHECK(b.state() == State::open);
  CHECK_FALSE(b.admit(Time{25}));
  auto second = b.admit(Time{26});
  REQUIRE(second);
  b.settle(Time{27}, *probe, true);
  CHECK(b.state() == State::half_open);
  b.settle(Time{28}, *second, true);
  CHECK(b.state() == State::closed);
  b.settle(Time{29}, *old, false);
  CHECK(b.state() == State::closed);
  CHECK(b.transitions().size() == 5);
  CHECK_THROWS_AS(Breaker(BreakerConfig{0, Time{1}}), ConfigError);
}
TEST_CASE("exact retry start schedules") {
  auto s = basic();
  s.service.failure_probability = 1;
  s.service.latency = {Time{1}, Time{1}};
  s.retry = {4, Time{10}, Time{25}, false};
  auto fixed = simulate(s, Strategy::fixed, 100);
  auto exp = simulate(s, Strategy::exponential, 100);
  std::vector<std::int64_t> f, e;
  for (const auto &t : fixed.trace)
    if (t.event == "attempt_start")
      f.push_back(t.at.count());
  for (const auto &t : exp.trace)
    if (t.event == "attempt_start")
      e.push_back(t.at.count());
  CHECK(f == std::vector<std::int64_t>{0, 11, 22, 33});
  CHECK(e == std::vector<std::int64_t>{0, 11, 32, 58});
  invariant(s, fixed);
  invariant(s, exp);
}
TEST_CASE("deadline and timeout ties take precedence and late work drains") {
  auto s = basic();
  s.deadline = Time{10};
  s.attempt_timeout = Time{10};
  auto r = simulate(s, Strategy::baseline, 100);
  CHECK(r.succeeded == 0);
  CHECK(r.final_deadline == 1);
  CHECK(r.timeouts == 1);
  CHECK(r.late_completions == 1);
  invariant(s, r);
  s.deadline = Time{100};
  r = simulate(s, Strategy::baseline, 100);
  CHECK(r.final_timeout == 1);
  CHECK(r.late_completions == 1);
  s.service.latency = {Time{500}, Time{500}};
  s.retry = {3, Time{0}, Time{0}, false};
  s.deadline = Time{25};
  r = simulate(s, Strategy::exponential, 100);
  CHECK(r.attempts == 3);
  CHECK(r.timeouts == 3);
  CHECK(r.final_deadline == 1);
  CHECK(r.late_completions == 3);
  CHECK(r.completion_duration == Time{25});
  CHECK(r.drain_duration == Time{520});
  invariant(s, r);
}
TEST_CASE("retry exactly at deadline never starts; exhausted attempts keep failure category") {
  auto s = basic();
  s.service.failure_probability = 1;
  s.deadline = Time{20};
  s.retry.base_delay = Time{10};
  auto r = simulate(s, Strategy::fixed, 100);
  CHECK(r.attempts == 1);
  CHECK(r.final_deadline == 1);
  CHECK(r.completion_duration == Time{20});
  s.retry.max_attempts = 1;
  r = simulate(s, Strategy::fixed, 100);
  CHECK(r.final_service_error == 1);
  CHECK(r.completion_duration == Time{10});
}
TEST_CASE("stable simultaneous arrivals and zero-time events") {
  auto s = basic();
  s.request_count = 5;
  s.arrival_interval = Time{0};
  s.service = {{Time{0}, Time{0}}, 1};
  s.breaker.failure_threshold = 1;
  auto r = simulate(s, Strategy::breaker, 100);
  CHECK(r.attempts == 1);
  CHECK(r.service_errors == 1);
  CHECK(r.rejections == 5);
  REQUIRE(r.trace.size() >= 3);
  CHECK(r.trace[0].request == 0);
  CHECK(r.trace[0].event == "attempt_start");
  CHECK(r.trace[1].event == "service_error");
  invariant(s, r);
  s.service.failure_probability = 0;
  r = simulate(s, Strategy::baseline, 100);
  CHECK(r.succeeded == 5);
  CHECK(result_json(s, r).at("successful_requests_per_second").is_null());
}
TEST_CASE("in-flight earlier generation completions and late timeouts cannot close breaker") {
  auto s = basic();
  s.request_count = 3;
  s.arrival_interval = Time{1};
  s.service = {{Time{20}, Time{20}}, 0};
  s.attempt_timeout = Time{5};
  s.windows = {{Time{0}, Time{1}, {{Time{2}, Time{2}}, 1}}};
  s.retry.max_attempts = 1;
  s.breaker = {1, Time{100}};
  auto r = simulate(s, Strategy::breaker, 100);
  CHECK(r.transitions.size() == 1);
  CHECK(r.transitions[0].to == State::open);
  CHECK(r.attempts == 2);
  CHECK(r.timeouts == 1);
  CHECK(r.late_completions == 1);
  CHECK(r.rejections == 1);
  invariant(s, r);
}
TEST_CASE("recovery half-open probe timeout reopens then successful probe closes") {
  auto s = basic();
  s.request_count = 12;
  s.arrival_interval = Time{10};
  s.attempt_timeout = Time{5};
  s.service = {{Time{1}, Time{1}}, 0};
  s.retry.max_attempts = 1;
  s.breaker = {1, Time{20}};
  s.windows = {{Time{0}, Time{40}, {{Time{50}, Time{50}}, 1}}};
  auto r = simulate(s, Strategy::breaker, 1000);
  CHECK(r.timeouts == 2);
  CHECK(r.late_completions == 2);
  REQUIRE(r.transitions.size() == 5);
  CHECK(r.transitions[0].to == State::open);
  CHECK(r.transitions[1].to == State::half_open);
  CHECK(r.transitions[2].to == State::open);
  CHECK(r.transitions[3].to == State::half_open);
  CHECK(r.transitions[4].to == State::closed);
  CHECK(r.succeeded > 0);
  invariant(s, r);
}
TEST_CASE("window sampling at start, half-open ranges and restoration") {
  auto s = basic();
  s.windows = {{Time{10}, Time{20}, {{Time{100}, Time{100}}, 1}}};
  CHECK_FALSE(service_sample(s, 0, 1, Time{9}).error);
  CHECK(service_sample(s, 0, 1, Time{10}).error);
  CHECK(service_sample(s, 0, 1, Time{19}).latency == Time{100});
  CHECK_FALSE(service_sample(s, 0, 1, Time{20}).error);
  s.request_count = 3;
  s.arrival_interval = Time{10};
  s.attempt_timeout = Time{200};
  auto r = simulate(s, Strategy::baseline, 100);
  CHECK(r.succeeded == 2);
  CHECK(r.service_errors == 1);
  CHECK(r.completion_duration == Time{110});
  invariant(s, r);
}
TEST_CASE("nearest-rank percentiles and empty subsets") {
  CHECK_FALSE(percentile({}, 50));
  CHECK(percentile({Time{1}, Time{100}, Time{2}, Time{3}}, 50) == 2);
  CHECK(percentile({Time{1}, Time{100}, Time{2}, Time{3}}, 95) == 100);
  CHECK_THROWS_AS(percentile({}, 0), std::invalid_argument);
  auto s = basic();
  s.service.failure_probability = 1;
  auto r = simulate(s, Strategy::baseline);
  CHECK(result_json(s, r).at("successful_request_latency").at("p99_ms").is_null());
}
TEST_CASE("strict configuration validation and resource limits") {
  auto good = config_json(basic());
  CHECK(parse_config(good).name == "test");
  for (auto key : {"schema_version", "name", "seed", "request_count", "arrival_interval_ms",
                   "deadline_ms", "attempt_timeout_ms", "service", "retry", "circuit_breaker"}) {
    auto j = good;
    j.erase(key);
    CHECK_THROWS_AS(parse_config(j), ConfigError);
  }
  for (auto value : {nlohmann::json(-1), nlohmann::json(0), nlohmann::json(1.5),
                     nlohmann::json("1"), nlohmann::json(true), nlohmann::json(100001)}) {
    auto j = good;
    j["request_count"] = value;
    CHECK_THROWS_AS(parse_config(j), ConfigError);
  }
  auto j = good;
  j["typo"] = 1;
  CHECK_THROWS_AS(parse_config(j), ConfigError);
  j = good;
  j["service"]["failure_probability"] = 1.1;
  CHECK_THROWS_AS(parse_config(j), ConfigError);
  j = good;
  j["service"]["latency"]["min_ms"] = 20;
  CHECK_THROWS_AS(parse_config(j), ConfigError);
  j = good;
  j["retry"]["max_attempts"] = 21;
  CHECK_THROWS_AS(parse_config(j), ConfigError);
  j = good;
  j["retry"]["full_jitter"] = 1;
  CHECK_THROWS_AS(parse_config(j), ConfigError);
  j = good;
  j["retry"]["base_delay_ms"] = 2000;
  CHECK_THROWS_AS(parse_config(j), ConfigError);
  j = good;
  j["deadline_ms"] = 0;
  CHECK_THROWS_AS(parse_config(j), ConfigError);
  j = good;
  j["arrival_interval_ms"] = max_time;
  j["request_count"] = 2;
  CHECK_THROWS_AS(parse_config(j), ConfigError);
  j = good;
  j["request_count"] = 100000;
  j["retry"]["max_attempts"] = 20;
  CHECK_THROWS_AS(parse_config(j), ConfigError);
  auto s = basic();
  s.windows = {{Time{1}, Time{3}, s.service}, {Time{2}, Time{4}, s.service}};
  CHECK_THROWS_AS(parse_config(config_json(s)), ConfigError);
  s.windows[1].start = Time{3};
  CHECK_NOTHROW(parse_config(config_json(s)));
  CHECK_THROWS_AS(simulate(basic(), Strategy::baseline, 100001), ConfigError);
}
TEST_CASE("deterministic generated workloads preserve invariants and repeatability") {
  Random gen{145};
  for (int i = 0; i < 160; ++i) {
    auto s = basic();
    s.seed = gen.next();
    s.request_count = static_cast<std::uint32_t>(gen.bounded(30) + 1);
    s.arrival_interval = Time{gen.bounded(5)};
    s.deadline = Time{gen.bounded(80) + 1};
    s.attempt_timeout = Time{gen.bounded(20) + 1};
    s.service = {{Time{0}, Time{gen.bounded(50)}}, static_cast<double>(gen.bounded(10)) / 10.0};
    s.retry = {static_cast<std::uint32_t>(gen.bounded(5) + 1), Time{gen.bounded(10)}, Time{30},
               gen.bounded(1) == 1};
    s.breaker = {static_cast<std::uint32_t>(gen.bounded(4) + 1), Time{gen.bounded(20) + 1}};
    s.windows = {{Time{10}, Time{25}, {{Time{20}, Time{30}}, 1}}};
    for (auto strategy : strategies) {
      auto r = simulate(s, strategy, 10000);
      invariant(s, r);
      CHECK(result_json(s, r) == result_json(s, simulate(s, strategy, 10000)));
    }
  }
}
TEST_CASE("trace is optional and bounded") {
  auto s = basic();
  CHECK(simulate(s, Strategy::baseline).trace.empty());
  auto r = simulate(s, Strategy::baseline, 1);
  CHECK(r.trace.size() == 1);
  CHECK(r.trace_truncated);
}

TEST_CASE("arrival horizon uses exactly request_count minus one intervals") {
  auto s = basic();
  s.request_count = 2;
  s.arrival_interval = Time{600'000'000'000};
  CHECK_NOTHROW(parse_config(config_json(s)));
  auto r = simulate(s, Strategy::baseline, 100);
  CHECK(r.succeeded == 2);
  CHECK(r.arrival_duration == Time{600'000'000'000});
  CHECK(r.completion_duration == Time{600'000'000'010});
  invariant(s, r);
  s.arrival_interval = Time{max_time};
  CHECK_THROWS_AS(parse_config(config_json(s)), ConfigError);
}
TEST_CASE("terminal summaries expose fast-failure and success-only latency separately") {
  auto s = basic();
  s.service.failure_probability = 1;
  auto text = summary(s, {simulate(s, Strategy::baseline)});
  CHECK(text.find("success p95") != std::string::npos);
  CHECK(text.find("n/a") != std::string::npos);
  CHECK(text.find("baseline: 1 / 0 / 0 / 0") != std::string::npos);
  CHECK(text.find("success latency excludes") != std::string::npos);
}
