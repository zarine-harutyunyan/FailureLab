# Technical interview guide

**Why discrete-event simulation?** Most simulated time is waiting for arrivals, responses,
timeouts or retry delays. A priority queue jumps directly to the next event, making experiments
fast and repeatable without threads or real sleeps. It also makes exact boundary cases easy
to test. The tradeoff is that results depend on deliberately simplified service assumptions.

**How do equal-time events work?** Events sort by integer time, explicit event kind and insertion
sequence. Deadlines precede timeouts, which precede completions, which precede starts. Thus a
response at the deadline cannot succeed and a response at timeout loses the tie. The final
sequence field makes otherwise identical events stable. A zero-latency completion can execute
before another arrival/start at the same timestamp; this is documented model behavior.

**How is randomness reproducible?** SplitMix64 and integer rejection sampling avoid unspecified
standard distribution algorithms. Service samples are keyed by seed, logical request and
attempt number. Jitter consumes a separate stream. Comparison strategies therefore share
keyed draws without sharing mutable RNG state. Their retry timing can still hit different
outage windows. Same-version/platform/toolchain repetition is guaranteed; cross-platform
bit-identical floating-point formatting is not promised.

**What happens to a late response?** An attempt can settle only once. A timeout/deadline marks
it settled and may schedule a retry. Its original completion remains queued because simulated
service work is not canceled. When it arrives, it increments late-work accounting but cannot
change a request outcome or the breaker. Logical completion duration and downstream drain
duration are separate metrics. Timeout/deadline events that have become irrelevant are ignored.

**How is the breaker designed?** CLOSED counts consecutive settled errors/timeouts and resets
on success. OPEN rejects without starting service work. After cooldown, the first admission
enters HALF_OPEN and owns the single probe slot. Success closes; failure/timeout reopens.
Every transition changes a generation token. Earlier-generation completions cannot affect the
new state, even if they represent real outstanding simulated work. See the state diagram.

**Who owns the objects?** One stack-scoped engine owns vectors of requests/attempts, RNGs,
breaker state, result metrics and a priority queue of value events. Events store numeric
indices rather than pointers into vectors, so vector reallocation does not invalidate them.
The queue drains before engine destruction. RAII manages streams and staging cleanup. No
owning raw pointers, manual `new`/`delete`, callback captures or shared thread lifetimes exist.

**What do tests verify?** Exact policy schedules and boundary rules, breaker transitions,
exclusive probes, stale generations, strict configuration, metrics and repeated-run identity.
Deterministic generated cases exercise combinations of delays, probabilities, deadlines and
windows, checking one final outcome per request, bounded attempts/events and monotonic time.
CLI tests inspect real report files and input/filesystem errors. Sanitizers and static analysis
cover additional runtime and ownership hazards. Consult validation status before saying any
particular suite or platform passed.

**What would real networking need?** An asynchronous transport and real monotonic clock,
thread-safe state/cancellation and object lifetimes, connection pools, backpressure, capacity
and queueing measurements, observability, integration/fault-injection tests, and operational
limits. Retry eligibility would depend on idempotency, request method and error semantics.
The simulator deliberately treats errors/timeouts as retryable and models no side effects;
real requests may have completed remotely even when the client times out. These policies
cannot be transplanted into a production client without that additional work.

**How would you extend it carefully?** Start with a measured question. For retry-induced
overload, implement and validate a finite-capacity queue before drawing conclusions from
amplification. Preserve reproducible event semantics and add behavioral tests before adding
multiple dependencies or concurrency. The current focused scope makes the invariants easier
to explain and audit.
