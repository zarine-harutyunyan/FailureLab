# 60–90-second screen recording

Build release beforehand; use a fresh output path. Enlarge the terminal font. No edits or
network access should be needed while recording.

**0–15 seconds — explain the failure.** “FailureLab is my C++20 portfolio simulator. This
scenario offers 200 requests; its one dependency returns errors between simulated 300 and
1000 milliseconds, then recovers. The engine advances a virtual clock rather than making
network calls.” Briefly show `scenarios/recovery.json`.

**15–25 seconds — validate.**

```sh
build/release/failurelab validate --config scenarios/recovery.json
```

**25–45 seconds — compare.**

```sh
build/release/failurelab compare --config scenarios/recovery.json --output results/recording
```

If repeating the recording with the same path, add `--overwrite` explicitly.

**45–65 seconds — show the report.**

```sh
cat results/recording/summary.txt
python3 -m json.tool results/recording/failurelab-report.json
```

The CSV can also be opened in a spreadsheet. JSON contains final failure categories, attempt
errors/timeouts, state transitions and the exact replayable configuration.

**65–90 seconds — explain one tradeoff.** “With this seed, retries rescue some requests but
nearly double calls. The breaker reduces downstream attempts below baseline, yet it also
rejects requests after recovery while waiting to probe. I compare success rate alongside
latency and attempts. This model has no capacity feedback, so the extra calls do not prove
retry-induced overload.” Use the actual table on screen, not an assumed universal winner.

After recording, keep the JSON/CSV as supporting material. Refer to the project as a simulation
and portfolio project. Do not present its simulated milliseconds as hardware benchmarks.
