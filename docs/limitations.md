# Known limitations

- One workload generator and one downstream service. No dependency graph, real sockets,
  concurrency, server process, cloud, database, dashboard or tracing backend.
- No queueing, CPU/connection-pool contention, capacity, load-dependent failure, packet loss
  or correlated failure outside explicit behavior windows. Traffic amplification has no
  causal effect on downstream latency or failure probability in this model.
- Behavior is sampled at attempt start and retained across window boundaries. Timed-out work
  continues until its sampled completion; logical cancellation does not cancel service work.
- Uniform/fixed integer millisecond latency only. Independent Bernoulli errors per keyed
  attempt; no heavy-tailed distribution or calibrated real-system workload claims.
- Breaker uses consecutive settled failures, one half-open probe and lazy cooldown transition.
  It does not implement rolling windows, minimum volume, multiple probes or per-host isolation.
- Fixed-delay retries deliberately ignore jitter. Exponential policies share the jitter flag.
  Circuit rejections are final. A retry delay that cannot fit the remaining time waits until the logical deadline.
- No cross-version or cross-platform byte-identical output promise. Same inputs on the same
  version/toolchain/platform reproduce; specified integer RNG improves portability.
- Resource limits bound work, but maximum inputs can consume hundreds of MiB and linear window
  lookup can be expensive. Use modest experiments. No hard OS memory allocation guarantee.
- Report staging handles ordinary I/O errors, not power-loss durability, hostile concurrent
  writers or atomic replacement of nonempty directories. Inspect stale locks after a crash.
- CSV provides aggregate metrics, state durations and transition count; full transitions and
  optional bounded events are in JSON. A truncated trace cannot reconstruct every event.
- Linux GCC/Clang and macOS build/test jobs have passed on GitHub, alongside Linux sanitizers.
  Windows is unsupported. Toolchain-specific analysis limits are explicit in
  [validation.md](validation.md).

These are modeling and validation boundaries of a portfolio project, not claims of professional
C++ employment, production traffic experience or a production networking implementation.

Local binary archives are OS/CPU specific and unsigned. They improve local demo setup but do
not guarantee compatibility with older OS releases or replace per-platform validation.
