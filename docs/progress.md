# Completion milestone ledger

Updated 24 October 2026. The [completion plan](completion_plan.md) governs scope
and acceptance. This ledger distinguishes implemented foundations, completed
substeps and open end-to-end milestones; contract counts are verification
evidence rather than a completion percentage.

| Milestone | Status | Evidence and remaining acceptance |
| --- | --- | --- |
| 1. Native integration checkpoint | Complete for `370f274` | MSVC build and twenty routine groups reported successful. All eight pipeline artifacts, 83 complete journal frames, eighty account checkpoints and the 151-file source inventory are verified against current Linux evidence. |
| 2. Strategy research | Complete for `05aec9d` | Three strategy specifications have reproducible historical comparisons. Native MSVC build and all twenty-two groups are reported successful. SPY-MR-01 retains 476 dates, forty market cases and its cost-dominated finding; all ten artifacts and 171 source records verify, with identical native/Linux account paths. |
| 3. Shared execution path | Complete for `9aeffa4` | MSVC build and routine test preset reported successful. Native eighteen-/twenty-two-case runs pass 224/296 checks; saved evidence, inputs and HTML match Linux byte-for-byte. Source differences are five explained CRLF variants. Journal manifest digests match the checked Linux bodies; native bodies were not supplied. |
| 4. Paper broker | In progress: message/precision boundary | Exact decimal/time decoding, a bounded SPY/DAY-limit contract and paper-only request descriptions are implemented. Versioned accounting/identity mapping, transport, TIF/expiry, stop/capture and observed reconciliation remain. |
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

## Assumption interpretation and visual delivery guidance

Completion-plan revision 2, dated 24 October 2026, separates model validity,
forecast evidence, economics, robustness and execution feasibility. It requires
explicit justification of consequential assumptions and project-selected screens,
while preserving registered studies and their findings. Milestone 3 now explicitly
starts an interactive trace-backed execution replay and recovery/account view;
paper quote/fill visuals follow in milestones 4-5, benchmark views in milestone 6
and final integration in milestone 7. Depth-data visualizations remain optional
and cannot be fabricated from minute bars. This documentation-only clarification
does not change the current historical runner or require a repeat build. Native milestone-2 verification was the next acceptance item; its completion
is recorded below.

## Native historical acceptance and milestone 2 completion

On 24 October 2026 the MSVC 19.51.36260.0 Debug build and all twenty-two routine
groups were reported successful. The historical study exits zero with 476 scored
dates, forty market-account cases, 83 primary active dates and 262 common-mask
forecast dates. Its source inventory matches implementation `05aec9d`, including
the unchanged registered protocol/settings and frozen dataset identity.

The uploaded summary SHA-256 is
`05132a4ab4df954ac81ef13a5acac2bb5f99a9c6e8f8ba3051dbdb0f4c95ad84`.
All ten artifact hashes verify. The independent Decimal/NumPy verifier passes
all 476 rolling fits/decisions, account and annual calculations, both outcome
bootstraps, 952 parameter-range records, economic/forecast gates and identities.
Four market-account CSVs (19,040 rows total) and the 476-row cash reference are
byte-identical to the retained Linux report. All classifications, reason codes,
coverage counts, settings and gates agree; HTML matches after source-fingerprint
substitution. The registered `cost_dominated` finding remains unchanged.

The largest raw/other numerical difference is approximately 1.71e-13 USD in an
opening-anchor price near USD 431.04. The largest derived difference is about
4.34e-6 minutes in a conditional half-life upper endpoint near 103,454.29 minutes.
Small coefficient differences are amplified near a unit root; all comparisons
pass relative/absolute tolerances 1e-11/1e-14 for raw/other numerical fields and
1e-9/1e-12 for equilibrium/half-life transforms. No decision or interval-coverage
classification changes.

The 171-entry native inventory aggregate is
`763c5b2475ff4f6c978d8b5165bed2ecd3666b9109fd86eff9df4ab66a78de1f`.
There are 166 exact matches and five exact CRLF variants: `CMakePresets.json`,
`research/intraday_continuation_protocol.md`,
`src/platform/windows/alpaca_console_credentials.cpp`,
`src/research/intraday_continuation.cpp` and its header. No source differences
remain unexplained. Original uploaded bytes are retained under
`scratch/verification/reversion-historical-native-kpu85l1u`, with comparison
results in `scratch/verification/reversion-historical-native-audit.json`.

This closes milestone 2 for `05aec9d`: strategy specifications, causal engines,
controls, registered comparisons and cross-platform reproduction evidence are
complete. Failed economic/forecast screens remain findings. The daily-risk
experiment retains its separate objective. No research rule changes or new
historical scores are introduced by this documentation-only acceptance update.

## Shared execution integration checkpoint

The [execution integration contract](strategy_execution.md) records the new
continuation/reversion adapters, shared whole-share target policy, eighteen
constructed cases, four bounded historical reference cases and saved-event
viewer. The original momentum target identities and journal schema are preserved.
The fixed first eligible historical date produces no fills under the constructed
quotes and limit rules; no alternative date is selected. Fractional and integer
accounts agree when conventions align; an explicit rounding case retains its
USD -0.005 difference. Research rules and findings remain unchanged.

GCC 13.3 Debug Build All passes with warnings as errors. All twenty-three routine
groups pass in 24.43 seconds. The new strategy-execution group and the existing
offline-pipeline group pass ASan/UBSan with leak detection disabled. The original
pipeline's six numerical/input/journal artifacts remain byte-identical to its
retained baseline. Repository hygiene and diff checks pass.

Constructed Debug evidence is retained at `scratch/runs/strategy_execution_4`;
Release at `strategy_execution_5`. All twenty-two exported artifacts, including
the HTML, eighteen journals, inputs, evidence and 181-file source inventory, are
byte-identical between builds. The manifest identifies compiler/configuration
separately. The independent Python checker verifies eighteen cases, 216 saved
frames and 398 chained journal inputs, reconstructing account reservations and
broker balances and checking target directions and Decimal account comparisons.

The Release reference replay at `scratch/runs/strategy_execution_6` verifies all
496 input sessions and then replays 1 February 2022 using only the preceding
twenty sessions for history. Its twenty-two cases and 296 assertions pass. The
independent checker verifies 408 saved frames and 454 journal inputs, all artifact
hashes and the same source inventory. The first eighteen case records equal the
constructed run. All four reference cases have zero fills, with tracked orders
resolved and no remaining inventory; these are integration outcomes, not new
strategy performance results.

All event selectors, label targets and artifact links are checked structurally.
Selected entry/restart layouts are rendered and visually inspected; they expose
the different broker/client balances before report delivery and the closed restart
gate. Actual browser clicking remains unverified because a browser renderer was
unavailable. Subsequent native acceptance is recorded below. A successful
synthetic replay does not establish paper execution quality.

## Native shared execution verification

On 24 October 2026 the MSVC 19.51.36260.0 Debug build and routine test preset
were reported successful for implementation `9aeffa4`. The preset configures
twenty-three groups; a separate per-test console transcript was not supplied.
Both executable logs record exit code zero. The constructed run passes 224
checks across eighteen cases. The reference replay verifies 496 sessions and
passes 296 checks across twenty-two cases.

All four uploaded artifact hashes per run verify: execution evidence, input
bars, HTML and source inventory. Saved evidence, inputs and HTML are byte-identical
to the retained Linux runs. These include 216 constructed frames and 408 frames
in the extended reference run. Independent target-direction and Decimal parity
checks pass on the native inputs; frame balances and reservations agree with
the independently reconstructed matching Linux journal prefixes. Broker fill
arithmetic is rechecked from each native frame's executed-fill prefix.

All eighteen/twenty-two journal digests listed in the native manifests match the
independently checked Linux journal files. Native journal bodies were not supplied,
so their actual uploaded bytes, chain parsing and storage behavior are not
independently reverified by this attachment audit. This distinction is retained
rather than treating reference journal files as native uploads.

The native 181-entry source inventory aggregate is
`589019ec621628aa5e8593150493d3358145a1c6eebf8d643f76a295a116fb05`.
There are 176 exact source matches and five exact CRLF variants:
`CMakePresets.json`, `research/intraday_continuation_protocol.md`,
`src/platform/windows/alpaca_console_credentials.cpp`,
`src/research/intraday_continuation.cpp` and its header. There are no unexplained
source differences. Native summary identities are:

- Constructed: `245773d8d4155699f85822b0e0229135fb3c7f46ae39b5fe5cd2b4fd237c74a4`.
- Reference: `4fdaca0c381d6a8784d70b5b6c77db391179286444967a7280bdb4ad20046cca`.

Uploaded originals remain unchanged. Named verification copies and audit results
are retained under `scratch/verification/execution-native-20261024`. The four
historical reference cases retain zero fills and flat final inventory. The
reported build/test success and verified cross-platform integration outputs
close milestone 3 for its declared acceptance scope. Actual browser clicking
remains unverified; observed paper events and execution-quality evidence remain
separate dependencies. This acceptance record changes documentation only.

## Paper API contract checkpoint

The [paper-broker contract](alpaca_paper_contract.md) records a bounded first
milestone-4 substep: exact broker decimals/timestamps, immutable paper-origin
selection, request descriptions and REST/stream decoders. Forty constructed
cases cover authorization/subscription responses, incremental versus cumulative
fills, subcent values, unsupported events, invalid origins/values, missing fields,
open-order identity collisions and potential truncation. The diagnostic passes
56 checks and retains raw constructed evidence with an HTML report.

No network requests or orders are sent. No credentials, market data or historical
strategy results are loaded. Existing cent-ledger behavior and journal schemas
1/2 remain unchanged. Broker fee availability and subcent accounting require an
explicit versioned bridge before runtime reports can be emitted. This substep
does not establish an observed paper lifecycle or close milestone 4.

GCC 13.3 Debug Build All and all twenty-four routine groups pass with warnings
as errors in 22.35 seconds. The new API contract group passes ASan/UBSan with leak
detection disabled. An independent fixed-seed Python Decimal/UTC oracle agrees
with 4,000 exact decimal canonicalization/comparison/projection cases and 1,000
nanosecond timestamp cases. The local audit is retained at
`scratch/verification/paper-contract`.

Debug evidence is retained at `scratch/runs/paper_contract_3`; Release at
`paper_contract_4`. Constructed evidence, source inventory and HTML are byte-identical
between configurations. All three artifact hashes, all 190 configure-time source
records, exact Decimal examples, request scope, incremental/cumulative examples
and report links verify. The report is statically rendered and visually inspected;
actual browser interaction remains unverified. Repository hygiene and diff checks
pass. Native MSVC acceptance is recorded below.

## Native paper contract acceptance and test selection

For `7f218b0`, the MSVC 19.51.36260.0 Debug build is reported successful and the
supplied transcript records all twenty-four groups passing in 342.68 seconds.
`alpaca_paper_contracts` takes 0.40 seconds. The application passes 56 checks
across forty cases, sends zero requests and observes zero broker events.

All three uploaded artifact hashes verify. Constructed evidence and HTML are
byte-identical to Linux. The 190-file source inventory has 185 exact matches and
five exact CRLF variants: `CMakePresets.json`, the continuation protocol,
`alpaca_console_credentials.cpp`, and the continuation engine/header. There are
no unexplained differences. The native inventory aggregate is
`5e3832f4062fabfd7dd21a25c19eaa765ba1a04f5c48fdf4980f981e93c0471b`;
the summary hash is
`af54242dbdf39676e33cf5b61f19b6aa37c190f29a0f262698b79778257c0a79`.
Original uploads remain unchanged; verification copies and audit results are
retained under `scratch/verification/paper-contract-native`. This accepts the
message-contract substep, not the entire paper-broker milestone.

Four existing groups account for 333.21 seconds, or about 97.2% of the supplied
full-suite time. New quick presets exclude those `extended` groups and optional
`integration`, retaining twenty groups including paper-message, order, journal,
pipeline and recovery checks. The existing full presets remain unchanged in
coverage at twenty-four groups. No assertions or numerical workloads are reduced.
The [runbook](running.md) records the exact groups and when full/affected checks
are required. CTest selection checks confirm full=24, quick=20 and all=25
including optional integration, with exactly the four declared groups excluded.
The Linux quick run passes all twenty groups in 2.66 seconds. Windows quick-preset
timing remains unmeasured. CMake configuration, preset inheritance, diff and
repository hygiene checks pass; no C++ implementation changes require rebuilding
the diagnostic for this selection update.

## Next dependency

Implement the versioned exact accounting and durable broker-identity/event
bridge, then connect paper transport, TIF/expiry and controlled-stop paths.
Declare the five-session capture plan before an observed operational run.
Milestones 1-3 remain closed; 4 is in progress; execution measurements, Release
benchmarks/portable CI and the final report remain milestones 5-7. Test counts
do not define a completion percentage.
