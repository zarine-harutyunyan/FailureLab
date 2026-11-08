# Changelog

## 1.0.1 — 2026-11-08

- Enforce JSON nesting and read-size limits, with file paths in validation errors.
- Correct the arrival horizon check to use request_count minus one intervals.
- Refuse overwriting reports containing unrelated files or internal symlinks.
- Show success-only latency, failure categories, throughput and breaker durations in summaries.
- Support subcommand help and validate seed syntax before reading the scenario.
- Check exact attempt/rejection/late-work accounting and fail empty CTest discovery.
- Add CPack archives and an extracted-package smoke test without publishing anything.
- Close the previous unit-test dependency gap and add regression coverage.

## 1.0.0 — 2026-11-08

Initial portfolio release implementation: deterministic event engine, strict schema-1 scenarios,
four retry/breaker strategies, independent service and jitter randomness, bounded traces,
JSON/CSV/text reports, five bundled scenarios, behavioral and CLI tests, CMake presets and CI.
See `docs/validation.md` for checks actually executed and outstanding release verification.
