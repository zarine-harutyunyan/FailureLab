# Validation status — 2026-11-08, version 1.0.1

The previous blocked unit-test gap is closed. A fresh build directory successfully fetched both
checksum-pinned dependencies, built the application and unit tests, and passed CTest on this
macOS machine. Debug, release and supported local sanitizer tests also passed. The initial
GitHub Actions run subsequently passed Linux GCC/Clang, macOS, and Linux sanitizer jobs.
The repository is private; application archives have not been publicly released.

## Environment

macOS arm64, Apple Clang 21.0.0 (`clang-2100.0.123.102`), CMake 3.31.6, Ninja 1.11.1.3,
Python 3.9.6 and clang-format 18.1.8. Build/analysis tools are in a temporary virtual environment
at `/private/tmp/failurelab-tools`; no global tools were installed. Runtime execution needs
neither Python nor a compiler. CPack produces a local archive for this host OS/CPU.

## Executed checks

| Check | Result |
| --- | --- |
| Fresh CMake configure, pinned archive downloads and release build in `build/clean` | Passed |
| Debug build and CTest (unit + integration) | Passed |
| Release build and CTest (unit + integration) | Passed |
| Fresh-directory release CTest (unit + integration) | Passed |
| ASan + UBSan build and full CTest, halt-on-error | Passed; Apple leak detection disabled |
| 16 doctest cases, including 160 generated workloads across four strategies | Passed |
| CLI suite: five scenarios, four strategies, 40 generated scenarios, repeated runs | Passed |
| JSON/CSV parsing, byte-identical repeatability, deadlines and exact attempt accounting | Passed |
| CLI/input/output failures, bounded traces, 1 MiB boundary, nesting and extra-file preservation | Passed |
| Empty CTest discovery fails instead of returning success | Passed |
| Native Apple Clang static analyzer, all five production translation units | Passed |
| clang-format 18.1.8 | Passed |
| Release installation and extracted CPack archive smoke test | Passed |
| Demo and committed recovery report regeneration | Passed |
| clang-tidy 18.1.8 | Not a local pass: incompatible with this newer Apple SDK |
| Linux GCC/Clang and macOS GitHub build/test matrix | Passed |
| Linux ASan/UBSan and leak detection on GitHub Actions | Passed |
| Pinned Linux clang-tidy job | See the CI record below for the initial finding and follow-up |
| Windows | Outside supported release scope |

The permission-denial integration case ran as a non-root user. It is explicitly skipped if
run as root, which can bypass Unix mode bits; unconditional invalid-output cases still run.
CTest's macOS environment disables LeakSanitizer because Apple's runtime does not support it.
AddressSanitizer and UndefinedBehaviorSanitizer are enabled and halt on findings. Linux CI
enables leak checking and passed in the initial GitHub Actions run.

clang-tidy 18.1.8 previously exposed enum-size and string-concatenation findings, which were
corrected. It could not parse newer Apple libc++ intrinsics (`__builtin_clzg`,
`__is_nothrow_convertible`), so no tidy pass is claimed. The matching installed Apple compiler's
static analyzer passes with findings fatal.

## GitHub CI record

The [initial run](https://github.com/zarine-harutyunyan/FailureLab/actions/runs/42433679742)
passed Linux GCC, Linux Clang, macOS and Linux sanitizer jobs. The quality job passed formatting
but found an implicit integer widening in the 1 MiB read-limit constant. The constant now
performs multiplication in `std::size_t`, without changing the limit or runtime behavior.

The first run also showed that CMake preferred a system `clang-tidy-18` over the installed
pinned tool. CMake now prefers `clang-tidy` on PATH and verifies version 18.1.8 explicitly.
The pinned-tool follow-up reached the CLI and flagged incrementing an argument index inside
a condition. The increment is now a separate statement, preserving the short-circuit bounds
check. No analyzer check was disabled or downgraded. The check remains mandatory and treats
findings as errors. See the repository's
[Actions page](https://github.com/zarine-harutyunyan/FailureLab/actions) for the follow-up result.

## Reproduce the checks

With CMake/Ninja on PATH, the usual presets enable tests:

```sh
cmake --preset debug
cmake --build --preset debug --parallel 4
ctest --preset debug
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset release
cmake --preset sanitize
cmake --build --preset sanitize --parallel 4
ctest --preset sanitize
python3 scripts/format.py
python3 scripts/analyze.py --build build/debug
python3 scripts/update_examples.py
cmake --build --preset release --target package
python3 scripts/package_smoke.py build/release/packages/*.tar.gz
```

This improvement pass also exercised a fresh directory without dependency source overrides:

```sh
cmake -S . -B build/clean -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/clean --parallel 4
ctest --test-dir build/clean --output-on-failure --no-tests=error
```

The existing debug/release/sanitizer directories reused previously verified JSON sources and
a checksum-verified doctest extraction through FetchContent source overrides. The fresh build
fetched both dependencies through the declared CMake URLs and hashes. Thus the offline-source
path and normal fresh dependency-fetch path were both exercised.

## Improvements verified in 1.0.1

The parser now enforces its documented nesting limit and bounds the read itself rather than
only trusting a pre-read file-size check. Arrival horizon validation uses exactly N−1 intervals,
allowing valid large gaps without weakening overflow rejection. Overwrite refuses extra files
or non-regular entries inside a report directory. CLI subcommands accept `--help`, help/version
stay consistent, and invalid seed syntax is diagnosed before scenario loading. The event engine
checks exact attempts, retries, timeouts/late work, rejections and processed-event bounds.

Terminal reports separate all-request latency from successful-request latency and expose failure
categories, throughput, downstream drain and breaker durations. The binary archive includes
all scenarios, a terminal quickstart, documentation and notices, excludes the test framework,
and is smoke-tested from an extracted temporary directory. It is unsigned, OS/CPU specific,
and not a claim of compatibility with older operating systems.

## Remaining boundaries

Cross-platform build/test and Linux sanitizer checks have now run on GitHub. There is no Windows support,
macOS signing/notarization, real networking, queueing or capacity model. Generated numbers are
simulation results, not hardware benchmarks or evidence of production traffic experience.
