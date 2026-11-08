# Configuration reference (schema 1)

JSON root must be an object. Every field below is required except `windows`. Unknown fields
at every object level and duplicate JSON keys are rejected. No comments or trailing commas.
Files must be regular readable files of at most 1 MiB; nesting is limited to 64. Version 1
is the only supported schema. Error messages identify the offending field/path.

| Field | Type/range | Meaning |
| --- | --- | --- |
| `schema_version` | integer 1 | Configuration schema |
| `name` | string, 1–120 bytes, no control characters | Report label |
| `seed` | integer 0..18446744073709551615 | Unsigned 64-bit seed |
| `request_count` | integer 1..100000 | Logical requests offered |
| `arrival_interval_ms` | integer 0..10^12 | Request i arrives at i × interval |
| `deadline_ms` | integer 1..10^12 | Budget from each request's arrival |
| `attempt_timeout_ms` | integer 1..10^12 | Upper limit, bounded by remaining budget |
| `service` | behavior object | Baseline behavior |
| `windows` | optional array, <=1000 | Time-based behavior overrides |
| `retry` | retry object | Used by applicable strategies |
| `circuit_breaker` | breaker object | Used by breaker strategy |

All time values and derived horizons must fit the supported 10^12 ms time range. Numbers
written as floating-point values (e.g. `10.0`) are rejected for integer fields. Negative,
boolean-as-number and out-of-range integers are rejected. Inputs with zero arrival interval,
latency or retry delay are allowed and remain bounded by attempt counts. Attempt timeouts
larger than the deadline are allowed because they are bounded at attempt start.

## Behavior object

`latency` and `failure_probability` are required. Latency is either:

- `{"type":"fixed","value_ms":20}`
- `{"type":"uniform","min_ms":10,"max_ms":30}`

Uniform integer bounds are inclusive, in `[0,10^12]`, and `min_ms <= max_ms`.
Probability is a finite JSON number in `[0,1]`; success and simulated error responses use the
same latency distribution. Errors are visible when a completion settles before its timeout.
An error response arriving after timeout is late work, not an additional service-error count.

## Windows

Each window has required `start_ms`, `end_ms`, and a complete `service` behavior object. Time
is absolute from simulation origin. Windows are `[start,end)`, with `start < end`; adjacent
windows are allowed. Input order does not matter: the parser sorts them. Overlap is an error.
A deterministic outage uses failure probability 1. A slow window raises latency enough to
exceed timeouts. After a window ends, baseline behavior resumes unless another window starts.

```json
{"start_ms":300,"end_ms":1000,"service":{
  "latency":{"type":"fixed","value_ms":20},"failure_probability":1
}}
```

The entire behavior is sampled **once at attempt start**. Work crossing a boundary retains
its original sample; retries consult behavior anew. Windows outside the workload are allowed.

## Retry object

All fields are required:

| Field | Range | Meaning |
| --- | --- | --- |
| `max_attempts` | integer 1..20 | Includes initial attempt |
| `base_delay_ms` | integer 0..10^12 | Fixed delay / first exponential cap |
| `max_backoff_ms` | integer base_delay..10^12 | Exponential delay cap |
| `full_jitter` | boolean | Uniform integer delay from 0 through cap |

`request_count * max_attempts <= 1000000`, including when selecting baseline. Baseline starts
at most one attempt; fixed retries use base delay without jitter. Exponential and breaker use
the same configured jitter setting and delay cap `min(max_backoff, base_delay*2^(k-1))` after
failed attempt k. Multiplication saturates safely before overflow. Only service errors and
attempt timeouts can retry. Circuit-open rejections finish immediately. Waiting for an
unaffordable retry consumes the remaining budget and ends as a deadline failure.

## Breaker object

`failure_threshold`: integer 1..100000 consecutive settled service errors/timeouts.
`open_duration_ms`: integer 1..10^12, positive. See [state semantics](circuit-breaker.md).
These fields are validated even when running a non-breaker strategy.

## Reports and exit codes

JSON report version is 1; `configuration` is the complete effective replayable scenario and
`results` is an ordered array (baseline, fixed, exponential, breaker for compare). Each result
has application/configuration versions, scenario, seed and strategy. CSV has one row per
strategy, a header, decimal point `.` and quoted string fields; undefined numeric values are
empty cells. JSON represents undefined values as `null`, never NaN/Infinity. Transitions and
bounded detailed traces appear in JSON. Request/attempt trace identifiers are zero/one based
respectively; `final_*` and `circuit_rejected` may have attempt zero if no call started.

Exit 0: command succeeded (simulation failures are data, not process failures).
Exit 2: CLI syntax, missing/duplicate/unknown options, invalid seed/trace option.
Exit 3: invalid/missing/unreadable scenario, time/resource validation, unknown strategy.
Exit 4: destination conflicts, locks, permission/write/finalization errors.
Exit 5: unexpected internal invariant or runtime failure. Expected errors never print a stack.
