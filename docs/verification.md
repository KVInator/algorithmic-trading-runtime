# Scope and verification

The completed bounded scope is a C++20 research-to-execution study: three intraday
strategy families, a separate daily-risk experiment, common order adapters,
synthetic broker lifecycle/recovery, execution measurements and implementation
benchmarks. The [technical report](report.pdf) states the mathematics, findings
and limitations; [running](running.md) describes reproduction.

## Roadmap acceptance map

| Milestone | Acceptance evidence | Status |
| --- | --- | --- |
| 1. Native integration | Supplied native pipeline result: 58 checks/three cases, matching interrupted paths; legacy journal/abrupt-exit contracts retained | Accepted |
| 2. Research specification | Three causal strategy specifications and separate registered risk experiment, fixed controls/uncertainty, independent references and retained negative findings | Accepted |
| 3. Strategy/execution integration | Eighteen constructed cases, 224 checks, 216 saved frames; native first-date bar-reference replay previously verified; common adapters and parity/rounding cases | Accepted |
| 4. Simulated broker protocol | Six cases/35 checks; raw message capture, Decimal accounting, quote budgets and paired restart; native archive independently verified | Accepted |
| 5. Execution measurements | Native 144-account archive, 108 paired policy and 120 assumption comparisons, 206 fills; independent timing, censoring and cost arithmetic | Accepted |
| 6. Release benchmarks and CI | Native 120-row archive; 100 hashes, all mode groups and 96 Linux-identical journals; portable Release contracts and hosted ASan/UBSan | Accepted |
| 7. Unified publication/final tree | Standalone HTML/PDF, eight evidence figures, source/evidence manifests, consolidated offline demo, cleanup and final verification | Final validation in progress |

## Retained evidence and provenance

The distributed `docs/evidence/manifest.json` records immutable source-document
links/digests and native output identities. `results.json` contains selected
recorded summaries and synthetic saved-state fields; runtime CSVs are copied
verbatim. Original daily historical paths are not reconstructed from summaries.
The report's medians, plots and unit conversions are display transformations.

Native execution measurement verification checks 6,294 artifact hashes, 6,002
capture records, 144 Decimal ledgers, 2,078 journal inputs, 206 fills, finite quote
budgets, source/receipt times, censoring and paired/sensitivity arithmetic.
Native benchmark verification checks 100 hashes, 120 rows, fifteen mode groups,
quantiles/rates, every CSV field, accounting, duplicates and recovery closure.
All 96 benchmark journals match Linux byte-for-byte. Of 230 source records, 226
match exactly and four match after the documented Windows CRLF conversion.

Native benchmark manifest SHA-256:
`94585a586a04b7568a13a5a50674498edeef550f327f04faa270ad81d6d4b501`.
Its source-inventory identity:
`b27db9a88e6a71ca2a9c60134aabc39afccb67dcf039fe30c7e54143dd471fc3`.
The reported native full 29-group suite takes 56.41 seconds. The full benchmark
runs as MSVC 19.51.36260.0 Release with `/O2 /Ob2 /DNDEBUG` and warnings as errors.
These observations support that build/host, not a universal throughput target.

The [pre-cleanup hosted checkpoint](https://github.com/KVInator/algorithmic-trading-runtime/actions/runs/40177057941)
passes 29 Release groups on Windows and Linux, independent benchmark/broker
checks and six ASan/UBSan groups with leak detection. Final cleanup verification
will be recorded below after the current source and publication checks complete.

The final local Linux Release tree passes all 29 routine groups (6.54 seconds)
and all 25 quick groups (3.42 seconds). Four affected accounting/recovery groups
and the complete demo pass ASan/UBSan locally; hosted leak detection remains part
of CI. The independent demo verifier checks 350 artifact hashes and both saved-state
oracles. Publication verification checks fourteen hashes, eight figures, two embedded
views and document links. The 17-page PDF is rendered and visually inspected;
repeated publication builds produce identical artifact bytes. Deliberate timing-data
alteration and embedded script injection are rejected by the publication verifier.

## Finalization changes

The final `project_demo` composes the same strategy and broker engines and exports
259 checks across 24 cases. The independent verifier checks eighteen strategy
cases/216 frames and the six broker cases. Momentum serialization is separated
from its research engine into `MomentumStudyReport`; numerical rules are unchanged.

Retired material comprises eleven preliminary/duplicate launchers, eight obsolete
report exporters, one unused recovery-demo workflow, four superseded proposal/
prerequisite notes and three completed/duplicate narrative checklists. Obsolete
export-format assertions are removed with their exporters. Causal, accounting,
precision, corruption, restart and independent numerical regressions remain.
All 29 meaningful routine contract groups are retained. Git history preserves
retired source; existing private data and run evidence remain locally intact.

## Interpretation and publication boundary

No registered study advances, and no later-period strategy data is opened.
Synthetic broker/execution measurements establish declared contracts, not external
broker connectivity, queue calibration or a live trading edge. Copy/history costs,
journal caps and observation-model limits remain explicit.

No additional strategy search, paid service, multi-venue routing, market making,
Level-II reconstruction or distributed platform is required by this scope.
The repository remains on its established review branch until a separate merge.
Default-branch presentation and external reviewer access must be distinguished
from completed technical work; merge/visibility changes are separate operations.

Six scenario/control generators used only by regressions live in `tests/support/`
and are compiled only into their consuming test targets. Production libraries
retain the final runtime, broker, research and public-demo responsibilities.
