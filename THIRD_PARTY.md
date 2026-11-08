# Dependencies and source licensing

FailureLab's original source has no public source-code license. No permission to redistribute
or reuse that source is granted by this repository. This does not change third-party licenses.

CMake FetchContent retrieves these immutable-version archives and verifies SHA-256:

| Dependency | Version | License | Usage |
| --- | --- | --- | --- |
| [nlohmann/json](https://github.com/nlohmann/json/tree/v3.11.3) | 3.11.3 | MIT | Configuration and structured reports |
| [doctest](https://github.com/doctest/doctest/tree/v2.4.11) | 2.4.11 | MIT | Unit tests only |

Archive hashes are recorded in `CMakeLists.txt`. First configuration needs internet access;
subsequent builds reuse fetched sources. Source overrides are documented in `docs/development.md`.
The JSON MIT notice is also preserved in `third_party/` and installed beside the scenarios.
Upstream copyright notices and license texts are included in the fetched archives. The JSON
library's installed headers retain its notices. If distributing executables, include the
upstream JSON license notice; doctest is not linked into the application executable.

Development-only tools are CMake 3.31.6 (BSD-3-Clause), Ninja 1.11.1.3 (Apache-2.0), and
clang-format/clang-tidy 18.1.8 (Apache-2.0 with LLVM exceptions). These are not application
runtime dependencies. Python 3.9+ (PSF license) runs the integration suite and helper scripts.
The CI action references are pinned to full commits; no external reporting service is used.
