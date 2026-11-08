FailureLab — local command-line reliability simulator

This archive contains an executable built for the OS and CPU named in the archive filename.
It is a discrete-event simulation, not a set of running microservices. No browser or server
is needed. Run these commands in a terminal from this extracted directory:

  bin/failurelab --version
  bin/failurelab validate --config share/failurelab/scenarios/recovery.json
  bin/failurelab compare --config share/failurelab/scenarios/recovery.json --output results/comparison

Then inspect results/comparison/summary.txt, results.csv or failurelab-report.json.
Add --overwrite only when intentionally replacing an existing generated report.
Use bin/failurelab --help for seed overrides and optional bounded tracing.

The executable uses system C++ and OS libraries. Compatibility with older OS versions or a
different CPU is not guaranteed. On macOS an unsigned downloaded executable may be subject
to Gatekeeper; run only software whose origin you trust. This local archive is not signed or
notarized. No automatic downloads or installations occur when running the executable.

Original source remains unlicensed for public reuse. Third-party notices are included under
share/doc/failurelab and share/failurelab/licenses. Source build instructions, modeling
assumptions and validation status are in share/doc/failurelab/README.md and its docs folder.
