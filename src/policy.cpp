#include "failurelab/model.hpp"
#include <algorithm>

namespace failurelab {
std::string name(Strategy s) {
  switch (s) {
  case Strategy::baseline:
    return "baseline";
  case Strategy::fixed:
    return "fixed";
  case Strategy::exponential:
    return "exponential";
  case Strategy::breaker:
    return "breaker";
  }
  throw std::logic_error("invalid strategy");
}
Strategy parse_strategy(const std::string &s) {
  for (auto strategy : strategies)
    if (name(strategy) == s)
      return strategy;
  throw ConfigError("--strategy: expected baseline, fixed, exponential or breaker");
}
std::uint64_t mix(std::uint64_t x) {
  x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31U);
}
std::uint64_t Random::next() {
  state += 0x9e3779b97f4a7c15ULL;
  return mix(state);
}
std::uint64_t Random::bounded(std::uint64_t inclusive_max) {
  if (inclusive_max == UINT64_MAX)
    return next();
  auto range = inclusive_max + 1;
  auto threshold = (std::uint64_t{0} - range) % range;
  for (;;) {
    auto value = next();
    if (value >= threshold)
      return value % range;
  }
}
Sample service_sample(const Scenario &s, std::uint32_t request, std::uint32_t attempt, Time now) {
  Random random{
      mix(s.seed ^ 0x5345525649434501ULL ^ (static_cast<std::uint64_t>(request) << 32U) ^ attempt)};
  const auto &b = behavior_at(s, now);
  auto latency =
      b.latency.min +
      Time{random.bounded(static_cast<std::uint64_t>((b.latency.max - b.latency.min).count()))};
  // Top 53 bits map exactly to binary64 in [0,1), including probabilities 0 and 1.
  auto u = static_cast<double>(random.next() >> 11U) * 0x1.0p-53;
  return {latency, u < b.failure_probability};
}
Time backoff(const Retry &retry, Strategy strategy, std::uint32_t failed_attempt, Random &random) {
  auto delay = retry.base_delay.count();
  if (strategy != Strategy::fixed) {
    for (std::uint32_t i = 1; i < failed_attempt && delay < retry.max_backoff.count(); ++i) {
      if (delay > retry.max_backoff.count() / 2)
        delay = retry.max_backoff.count();
      else
        delay *= 2;
      if (delay == 0)
        break;
    }
    delay = std::min(delay, retry.max_backoff.count());
    if (retry.full_jitter)
      delay = static_cast<std::int64_t>(random.bounded(static_cast<std::uint64_t>(delay)));
  }
  return Time{delay};
}
std::string name(State s) {
  switch (s) {
  case State::closed:
    return "CLOSED";
  case State::open:
    return "OPEN";
  case State::half_open:
    return "HALF_OPEN";
  }
  throw std::logic_error("invalid breaker state");
}
Breaker::Breaker(BreakerConfig config) : config_(config) {
  if (config.failure_threshold == 0 || config.open_duration <= Time{0})
    throw ConfigError("circuit_breaker: threshold and open duration must be positive");
}
void Breaker::change(Time now, State next) {
  transitions_.push_back({now, state_, next});
  state_ = next;
  ++generation_;
  failures_ = 0;
  probe_ = false;
  if (next == State::open)
    reopen_at_ = add(now, config_.open_duration);
}
std::optional<std::uint64_t> Breaker::admit(Time now) {
  if (state_ == State::open && now >= reopen_at_)
    change(now, State::half_open);
  if (state_ == State::open || (state_ == State::half_open && probe_))
    return std::nullopt;
  if (state_ == State::half_open)
    probe_ = true;
  return generation_;
}
void Breaker::settle(Time now, std::uint64_t generation, bool success) {
  if (generation != generation_ || state_ == State::open)
    return;
  if (state_ == State::half_open) {
    change(now, success ? State::closed : State::open);
    return;
  }
  if (success)
    failures_ = 0;
  else if (++failures_ >= config_.failure_threshold)
    change(now, State::open);
}
} // namespace failurelab
