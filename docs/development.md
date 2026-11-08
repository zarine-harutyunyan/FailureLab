# Development and testing

Use CMake 3.25+, Ninja, Python 3.9+ and a C++20 compiler. CI targets Ubuntu 24.04 with GCC and
Clang and macOS 15 with Apple Clang. These are target configurations, not a claim that remote
CI ran. See [validation status](validation.md). Windows is not supported in this release.

## Build, test and install

```sh
cmake --preset debug -DBUILD_TESTING=ON
cmake --build --preset debug --parallel 4
ctest --preset debug
cmake --preset release -DBUILD_TESTING=ON
cmake --build --preset release --parallel 4
ctest --preset release
cmake --install build/release --prefix "$PWD/build/install"
build/install/bin/failurelab --version
```

Both presets enable tests in a fresh build. Explicit `BUILD_TESTING=ON` also restores that
setting after a previous local build used OFF. The installed binary is independent of the
source tree; bundled scenarios and the JSON license are in `share/failurelab` under the prefix.

Dependencies use versioned, checksum-verified archives in `CMakeLists.txt`. The first configure
requires GitHub access. No dependency download occurs when merely running the executable.
For offline builds, first obtain the exact upstream archives and verify the recorded SHA-256,
extract them outside the source tree, then provide absolute paths:

```sh
cmake --preset debug -DFETCHCONTENT_SOURCE_DIR_JSON=/path/to/json-3.11.3 -DFETCHCONTENT_SOURCE_DIR_DOCTEST=/path/to/doctest-2.4.11
```

Source overrides bypass archive verification, so the caller is responsible for authenticating
them. For an application-only build, `-DBUILD_TESTING=OFF` avoids doctest and Python discovery;
that build is not a substitute for release test acceptance.

## Sanitizers

```sh
cmake --preset sanitize
cmake --build --preset sanitize --parallel 4
ctest --preset sanitize
```

The preset enables AddressSanitizer and UndefinedBehaviorSanitizer at compile and link time
for all project C++ targets. Test environments halt on findings; UBSan prints diagnostics.
LeakSanitizer is enabled on Linux, disabled on Apple because that runtime does not support it.
No finding is ignored by a recovery setting. To exercise integration manually on macOS:

```sh
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 python3 tests/integration.py build/sanitize/failurelab .
```

## Formatting and analysis

Install pinned development tools in a virtual environment (see README), then:

```sh
python3 scripts/format.py
python3 scripts/format.py --fix
cmake --preset tidy
cmake --build --preset tidy --parallel 4
python3 scripts/analyze.py --build build/debug
```

The format checker requires clang-format 18.1.8 and fails on differences. clang-tidy 18.1.8
checks Clang analyzer, bug-prone, portability and performance diagnostics on production sources;
its findings fail the build. CI runs it on Linux. The native analyzer script uses the compiler
and flags from `compile_commands.json`, checks every production translation unit and makes
findings fatal. It needs a Clang-configured build; GCC does not implement `--analyze`.

Older clang-tidy may not understand a newer Apple SDK's libc++ compiler intrinsics. Do not
suppress SDK parse errors to claim a pass. Use the installed matching Clang analyzer locally,
and run the pinned tidy job on Linux. Current observed limitations are recorded in validation.

## Tests

`unit` is a doctest suite testing retry schedules, safe caps, RNG golden vectors, jitter bounds,
deadline/timeout ties, late work, stale breaker generations, transitions, probe exclusivity,
stable ordering, window selection, nearest-rank percentiles, empty subsets and strict config.
It generates 160 deterministic workloads across four policies, checking partition/counter,
event/time, attempt/deadline and trace invariants plus identical repeat runs.

`integration` runs the real CLI using Python's standard library. It validates and compares all
five bundled scenarios, parses JSON and CSV, verifies recovery, compares byte-identical repeated
reports, tests help/version/seed/error/trace limits, and checks existing, symlinked, locked,
unrelated and unwritable outputs. It also generates 40 small scenarios across four strategies,
checking every traced finalization and attempt against the input budget and event bound.
Regression cases also cover large valid arrival horizons, nesting-limit diagnostics, exact
1 MiB inputs, bounded trace truncation and preservation of extra files during overwrite. CTest
presets fail when no tests are discovered instead of reporting an empty suite as success.
The permission-denial case is skipped for a root user, which can bypass Unix mode bits;
a regular-file parent still exercises an unconditional output failure path.

The tests do not assert that retries or the breaker always improve success. No wall-clock
performance threshold is part of correctness. Test files live under `tests/`; generated reports
use temporary directories. Test failures are never converted to successful process exits.

## CI

`.github/workflows/ci.yml` builds/tests debug and release on Linux GCC/Clang and macOS Clang,
validates all scenarios, smoke-tests the README demo, checks installation, and builds/tests
a binary archive from its extracted directory. Separate Linux
jobs run ASan/UBSan (with leak detection), pinned formatting and clang-tidy. Third-party
Actions use immutable commit references and read-only contents permissions; checkout does not
persist credentials. The workflow does not deploy, publish or upload artifacts.

## Package without publishing

```sh
cmake --build --preset release --target package
python3 scripts/package_smoke.py build/release/packages/*.tar.gz
```

CPack creates a `.tar.gz` named with application version, OS and CPU under
`build/release/packages/`. The smoke script extracts it in a temporary directory and runs its
executable against all installed scenarios, then parses a four-strategy comparison report. It
also checks notices and excludes installation of the test framework. Runtime recipients need
only compatible OS system libraries. There is no signing, notarization, cross-compilation,
public distribution or upload step. Build and verify a separate archive on each target.
