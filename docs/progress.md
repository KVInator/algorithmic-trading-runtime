# Completion milestone ledger

Updated 24 October 2026. The [completion plan](completion_plan.md) governs scope
and acceptance. This ledger distinguishes implemented foundations, completed
substeps and open end-to-end milestones; contract counts are verification
evidence rather than a completion percentage.

| Milestone | Status | Evidence and remaining acceptance |
| --- | --- | --- |
| 1. Native integration checkpoint | Complete for `370f274` | MSVC build and twenty routine groups reported successful. All eight pipeline artifacts, 83 complete journal frames, eighty account checkpoints and the 151-file source inventory are verified against current Linux evidence. |
| 2. Strategy research | Historical implementation and Linux comparison complete; native verification pending | Three strategy specifications now have historical comparisons. SPY-MR-01 retains 476 dates, forty market cases, 83 primary active dates and a cost-dominated result. The new Windows build, twenty-two routine groups and historical study output remain to verify. |
| 3. Shared execution path | Partially implemented | The original momentum adapter has synthetic fill/restart evidence. Continuation/reversion adapters, historical reference replay and account-convention comparisons remain. |
| 4. Paper broker | Not implemented | Broker-generated order events, transport semantics, TIF/expiry, operational capture and observed reconciliation remain. |
| 5. Execution measurement | Not implemented | Quote/event capture, timestamp alignment, execution-cost/fill/delay tables and the declared paper observation window remain. |
| 6. C++ performance and CI | Partially implemented verification foundation | Existing contract and sanitizer checks are available. Formal transition/invariant explanation, Release benchmarks and portable CI remain. |
| 7. Final technical publication | Not complete | Individual reports and research notes exist. The unified analytical HTML/PDF report, offline showcase and final repository presentation remain. |

## Reversion specification and feasibility checkpoint

At this preceding checkpoint SPY-MR-01 was proposed and unscored. The [brief](../research/intraday_reversion_brief.md)
defines a fixed opening reference, a noon decision, prior-session estimation
and common-window controls. It records the difference between conditional OU
interpretation and empirical evidence of stationary residuals.

The authoritative C++ measurement accepts exactly 150 completed minutes,
produces 25 five-minute closing residuals and never updates its opening reference
after 10:00. Pooled OLS constructs only within-session transitions and keeps
unrestricted coefficients when a stationary interpretation is unavailable.
The historical audit checks data coverage and positive opening volume only.

GCC Debug builds with warnings as errors and all twenty routine contract groups
pass. The new contract group also passes AddressSanitizer/UndefinedBehaviorSanitizer
with leak detection disabled. The default preflight passes 22 constructed checks,
verifies all 496 frozen sessions and identifies 476 dates with the proposed inputs
after warm-up. It records zero historical model fits and zero strategy scores.

An independent 50-digit Decimal calculation reproduces all four unrestricted
synthetic fits and their residual variances. Separate checks reproduce the opening
reference and residual samples, validate the five artifact hashes against the
149-file source inventory and check all 496 coverage cells and report links.
These are numerical and structural checks, not finite-sample estimator or
historical profitability evidence. Native verification of this preflight is
recorded below; the pipeline checkpoint is recorded in the native integration section.

## Windows reversion verification

For implementation revision `8e19546`, the MSVC 19.51.36260.0 Debug build was
reported successful on 23 October 2026. The supplied console log records all
twenty routine groups passing in 307.50 seconds. The separate native preflight
passes all 22 checks, verifies 496 sessions and records 476 input-ready dates,
zero historical model fits and zero strategy scores. Its frozen study-manifest
identity matches the Linux run.

The uploaded summary SHA-256 is
`3b69053b03f48a44ebbec765fb356d4b6783a858af35dbbd0f80509c42c1bf04`.
All five artifact hashes match. Coverage and the exact synthetic prefix are
byte-identical to Linux. All four controls retain identical statuses and input
sequences; fitted numerical fields agree within relative tolerance 1e-12 and
absolute tolerance 1e-15. The largest absolute parameter difference is
approximately 1.07e-14. The 149-entry source inventory has 141 exact byte matches
and eight exact CRLF variants, with no unexplained source differences.

The supplied timing log attributes 175.19 seconds to `daily_risk_study_contracts`
and 101.46 seconds to `risk_input_contracts`, together 89.97% of the suite.
These groups exercise full-calendar synthetic study/replay and persisted
acquisition/reload scenarios. The new reversion contract takes 0.19 seconds.
The preflight's frozen-input verification completes at 188.2 seconds; the
log does not isolate disk, decoder, hashing or other system costs. Build timings
were not supplied, so the cause of the reported slow build is not established.
No performance improvement is inferred from successful correctness checks.

## Maintenance checkpoint: class interfaces and documentation

Cleanup follows baseline `001b1d4`. Diagnostic services now compose existing
engines through class interfaces. Report objects bind const results for their
related exports, and `ReportText` replaces eight local HTML encoders and three
quoted-CSV encoders. The independent Python report verifier binds one report
directory; its arithmetic syntax tree and command-line behavior are unchanged.

The root README is reduced from 340 to 96 lines. The [runbook](running.md)
contains detailed commands/prerequisites, the [architecture note](architecture.md)
describes ownership, and the [research index](../research/README.md) separates
active specifications from retained selection history. Existing protocols,
briefs, policies, references and published result records retain their bytes.

The GCC Debug build with warnings as errors and all twenty routine groups pass.
Ten affected contract groups and five diagnostic entry points also pass
AddressSanitizer/UndefinedBehaviorSanitizer with leak detection disabled.
The shared encoder checks cover owned input, markup characters, CSV quotes,
commas/newlines, empty fields and preservation of UTF-8 bytes.

Baseline comparisons reproduce 22 diagnostic artifacts across five entry points.
Input schedules, traces, journals, CSVs and JSON payloads are byte-identical;
HTML matches after source-fingerprint substitution. The four synthetic risk
CSV exports and summary calculation fields also match; generated timestamps,
source identities and report artifact hashes are expected to change.
The fresh full-input reversion audit verifies 496 sessions and 476 input-ready
dates, with zero historical model fits or scores. Its controls, coverage and
synthetic prefix remain byte-identical to the earlier implementation.

This checkpoint changes internal C++ interfaces and documentation, retaining
executable options, artifact schemas and journal compatibility. The subsequent
initialization fix and native verification are recorded below. The cleanup
does not close a research or paper-execution milestone.

## Native integration verification

The MSVC 19.51.36260.0 Debug build and twenty routine groups were reported
successful on 23 October 2026 for implementation `370f274`. This includes the
explicit-initialization correction for the C4269 build warning introduced during
cleanup. The pipeline passes all 58 checks across three cases.

The [native pipeline evidence](offline_pipeline.md#native-verification) verifies
all eight artifact hashes, all 83 complete chained journal frames, eighty
independent integer-account checkpoints and all 151 source records. The three
journals, broker evidence and both signal prefixes are byte-identical to the
current Linux diagnostic; the HTML differs only in its source fingerprint.
Five CRLF variants explain the native source inventory, with no unexplained
source differences. This closes milestone 1 for the recorded implementation.

## Reversion sampling, noise and registration checkpoint

The [synthetic specification](../research/reversion_validation.md) fixes thirteen
cases before the first Monte Carlo run. `ReversionSimulator` generates known
latent paths with independent observation error, session-common anchor error,
short/long histories, transients and across-session shock dependence.
`ReversionUncertainty` reuses the authoritative estimator and whole-session block
sampler. Every refit retains its status; missing coefficients invalidate the
slope range and OU-derived ranges are explicitly conditional. An unavailable
current residual preserves history parameter ranges without fabricating a move.

The completed diagnostic retains 13,000 point fits and 1,034,800 refits, all
coverage denominators and first-trial observations. The
[findings](../research/reversion_validation_result.md) show false latent-OU
admissibility in 618/1,000 clean unit-root and 1,000/1,000 noisy unit-root trials.
Observation-noise latent-slope coverage is 0/200 at block one and 1/200 at block
five. Even clean twenty-session ranges under-cover. These are model/inference
limitations, not software-contract failures; no estimator or trading threshold
was selected to conceal them.

[Protocol revision 1](../research/intraday_reversion_protocol.md) and its
[settings](../research/intraday_reversion_protocol.json) are registered after
that synthetic evidence and before historical residuals/fits/scoring. They fix
all policies, dates, costs/delays, comparison masks, seeds and advancement rules.
Parameter ranges are descriptive and cannot filter entries or establish
stationarity/latent-OU identification. Registered document identities are:

- Protocol: `1fa24089060ccfc82963ee53216927fb0fa9e0f10f02948eae5b72b4032207b9`.
- Settings: `64640f797613466b43b6c0c8686f317aa6a5bcc5425985e931816a6910e8ff86`.

GCC 13.3 Debug Build All passes with warnings as errors. All twenty-one routine
groups pass in 19.43 seconds; the new small-workload contract takes 0.12 seconds.
The two reversion groups also pass ASan/UBSan with leak detection disabled.
The full Monte Carlo workload is a separate application rather than routine CI
work. No performance benchmark is inferred from these correctness timings.

Release evidence is retained at `scratch/runs/reversion_validation_2`; Debug at
`reversion_validation_3`. Thirteen trial files, first-trial CSV, HTML, source
inventory and all case aggregates are byte-identical between builds. Independent
60-digit Decimal arithmetic verifies all thirteen first-trial fits/variances;
the verifier checks every point aggregate, range coverage denominator, refit
status total and all sixteen artifact hashes. All HTML artifact links resolve.
Visual browser rendering was unavailable because the local browser executable
is absent; report structure and numerical content were checked directly.
The current synthetic-only preflight passes its 22 controls at
`scratch/runs/reversion_preflight_11` with `registered_unscored` status.

The Debug summary SHA-256 is
`5ffa9cbd09fcbe0f57c0b57bc284a91ab3b4a99aa339263246b8b7e2ea3d80ab`;
Release summary SHA-256 is
`bfb38fa09ac8820128f37192ba64486f56ccf7f8986e3dda07b161dfb47265d3`.
Both retain the same 163-file inventory aggregate
`c733f3bfad4967d868ca35bc6cfd755208777fb19b9c5e901ba821de024038cc`.
The repository hygiene audit reports no findings. Existing frozen studies,
policies, briefs, datasets, result records and journals retain their bytes.
No 2024-2025 prices/performance were accessed. At this checkpoint the new MSVC
build, twenty-one-group suite and native diagnostic evidence were pending.
Their subsequent verification is recorded below; the milestone-1 native baseline
remains valid for its recorded implementation.

## Native reversion diagnostic verification

On 24 October 2026 the MSVC 19.51.36260.0 Debug build and all twenty-one routine
groups were reported successful for implementation `5835606`. The supplied
console log records all thirteen synthetic cases, 1,000 point trials per case,
200 coverage trials per case and exit code zero. The uploaded report retains
zero historical model fits and zero historical strategy scores.

The native summary SHA-256 is
`23940f247b8532527042d476ba642f2dcabbb55970f1570c0e934e68a6650b6f`.
All sixteen artifact hashes verify. The independent standard-library verifier
recomputes the thirteen first-trial fits and residual variances with 60-digit
Decimal arithmetic, all 13,000 point-trial aggregates and coverage denominators.
It also checks the reported status totals covering 1,034,800 refits. All fit classifications, failure counts,
coverage counts, settings and seeds agree with the retained GCC Debug evidence.
The previously reported undercoverage and false latent-OU admissibility remain.

Raw/other numerical fields differ by at most approximately 4.33e-15 in absolute
units. The largest derived-field difference is approximately 7.26e-6 minutes in
a conditional half-life upper endpoint near 135,883.52 minutes, relative
approximately 5.34e-11. The near-unit-root transformation amplifies coefficient
roundoff; no model status or coverage decision changes. Numerical comparisons
use relative/absolute tolerances 1e-11/1e-15 for raw/other fields and
1e-9/1e-12 for equilibrium and half-life transforms. HTML text is identical after
source-fingerprint substitution; bitwise cross-compiler numerical identity is
not required.

The 163-entry native source inventory has aggregate SHA-256
`c3a0f46971892a23bc5d89ce33f9183ee620e4d002f97cdddfb82f6c79a81dff`.
Of its source records, 158 match exactly and five match exact CRLF variants:
`CMakePresets.json`, `research/intraday_continuation_protocol.md`,
`src/platform/windows/alpaca_console_credentials.cpp`,
`src/research/intraday_continuation.cpp` and its header. There are no unexplained
source differences. The registered reversion protocol and settings match exactly.

The original uploaded bytes are preserved in the isolated directory
`scratch/verification/reversion-native-xc99nthw`; comparison results are retained
in `scratch/verification/reversion-native-audit.json`. No numerical implementation
or registered research rule changes are required. This closes native verification
of the sampling/noise checkpoint, not milestone 2 or historical reversion scoring.
This documentation-only update does not require another native build.

## Registered historical reversion checkpoint

`ReversionHistory`, `ReversionStrategy` and `ReversionStudy` implement revision 1
without changing either registered document. Twenty prior eligible slots include
unavailable anchors. The current prefix never enters its own dynamics estimate;
future references affect account returns only. Fixed noon decisions feed forty
cost/delay cases and cash. All 476 dates remain in paired economic uncertainty;
forecast loss comparisons share the declared availability mask. Reduced fixture
budgets cannot pass through the authenticated historical boundary.

The [development evidence](reversion_development.md) records the complete finding:
83 primary active dates, +1.2498% gross compounding and -0.4171% primary net
compounding. The assessment is `cost_dominated`; the activity, annual/stress,
net-positive and uncertainty requirements also fail. Net increments over the
controls do not justify advancement: both lower-bound screens fail, and the
paired gross increment over sign-only is negative. Conditional forecast-loss
point contrasts favor drift; their ranges include zero. No rule is retuned.

GCC 13.3 Debug Build All and all twenty-two routine groups pass with warnings as
errors. New history/replay/report
contracts also pass ASan/UBSan with leak detection disabled. The independent
Python/NumPy 2.3.5 oracle verifies 476 rolling fits using 60-digit Decimal
arithmetic, all decisions and account paths, annual metrics, common-date outcome
resampling, 952 parameter-range records, all economic/forecast gates and hashes.
Its explicit MT19937-64 and block sampler do not call the C++ implementation.

The registered run verifies the unchanged frozen 496-session input evidence.
The final report at `scratch/runs/SPY_MR_01_development_2022-2023_4` contains
475,048 parameter refits, forty paths and ten hashed artifacts. Summary SHA-256:
`5e0eeffce064a76608ee030dac6a0ff6e7ca77dd3e96c6bd7f7e23aaed2e9cb3`.
The 171-file compiled inventory has aggregate
`184bc6f0406ce3c63a67ef97e146acf287f745ae60fb4f437ea0c1a62fa5495c`.
Repeated runs retain eight byte-identical numerical exports and identical
aggregate statistics, ranges and gates. Intervening source changes complete the
verifier, move ledger calculations into the research core and format new sources;
trading rules and numerical outputs remain unchanged. All 171 source-file hashes
in the final compiled inventory match the current source bytes. HTML links and
additional break-even arithmetic checks pass; browser rendering is unavailable.
Repository hygiene reports no findings. Frozen datasets, earlier studies,
protocol/settings and existing journals remain unchanged. No local 2024-2025
prices or candidate results were accessed.

## Next dependency

Verify the new Windows build, twenty-two-group suite and `reversion_development`
output against the retained Linux evidence, then close milestone 2. No acquisition
or rule registration needs repeating. Research completion does not require a
profitable or advancing result. Begin milestone 3 with continuation/reversion
execution adapters and explicit fractional/whole-share account parity, retaining
synthetic fill/restart cases and historical-reference semantics. Actual paper
capture, execution measurements, Release benchmarks/CI and the unified report
remain open under milestones 4-7.
