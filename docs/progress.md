# Milestone ledger

Current scope: [completion plan revision 3](completion_plan.md), 25 October 2026.
Completion follows acceptance evidence, not test counts or strategy profitability.

| Milestone | Status | Remaining dependency |
| --- | --- | --- |
| 1. Native integration checkpoint | Complete | Preserve legacy journal/pipeline contracts |
| 2. Strategy research | Complete | Preserve registered findings and unopened later-period performance |
| 3. Strategy/execution integration | Complete | Preserve adapters and historical reference-price distinctions |
| 4. Simulated broker protocol | Complete | Native diagnostic independently verified; Windows build and tests reported successful |
| 5. Simulated execution measurements | Complete | Native archive independently verified; Windows build and tests reported successful |
| 6. Implementation benchmarks and CI | In progress | Release benchmark implemented and Linux verified; hosted CI and native Release measurements pending |
| 7. Unified technical report | Open | Standalone HTML/PDF, integrated figures, curated evidence and final repository presentation |

## Retained research findings

- SPY-IM-01: 496 development sessions, 268 active trades, no demonstrated gross
  edge. Primary one-minute-delay/1 bp-per-side compounded return: -7.8924%.
- SPY-TOD-01: both continuation variants fail advancement screens; a volume
  increment is not established. See [development evidence](continuation_development.md).
- SPY-RISK-01: 501 exchange sessions, forty cases, 479 eligible primary forecast
  dates. Registered screens fail. See the [registered result](../research/daily_risk_development_result.md).
- SPY-MR-01: 476 scored dates, forty cases, 83 primary active dates and 262
  common-mask forecast dates; cost-dominated, primary net return -0.4171%.
  See [reversion development](reversion_development.md).

These findings complete research questions without establishing a tradable edge.
Protocols, acquisition identities and result records remain unchanged. Local
2024-2025 strategy prices and candidate performance remain unopened.

## Accepted execution foundations

Native `pipeline_diagnostic` passes 58 checks in three constructed cases;
interrupted and uninterrupted broker paths agree. Its build and twenty routine
groups were reported successful. Legacy runtime/journal contracts cover exclusive
writers, abrupt-exit recovery, corruption and independent account reconciliation.

Milestone 3 native acceptance for `9aeffa4` includes 224 checks/eighteen cases in
`strategy_execution_diagnostic`, and 296 checks/twenty-two cases with the bounded
historical reference replay. Four supplied artifacts per run match Linux evidence;
native journal bodies were not supplied. Bar-price references remain constructed
quotes, separate from historical study results.

The native schema-3 diagnostic passes 42 checks/three cases with exact subcent
cash and durable identities. The native message diagnostic passes 56 checks/forty
cases. Their accounting, decoder and persistence contracts remain in use.

## Simulated broker checkpoint, 25 October 2026

`SimulatedPaperWire` translates the existing matching simulator into the bounded
paper-message contract. `PaperSession`, durable raw capture, schema-3 accounting
and `PaperControl` remain authoritative. Six synthetic cases cover partial/full
fills, cancel races, duplicate deliveries, finite quote budgets, uncertain POST
response, stable-ID lookup, missing-fill refusal, paired restart, snapshot
contradiction, deadlines and permanent stopping. The prescribed round trip ends
flat at USD 2,000.24 from USD 2,000, an accounting oracle rather than performance.

The repeated-run contract compares all saved states and identity maps. The
independent Python verifier checks artifact/capture/journal integrity, Decimal
cash/positions and quote-side size budgets. The expandable report shows saved
event checkpoints, positions, reservations, routing and independent broker state.
See [the broker contract](paper_broker.md) for reproduction and boundaries.

Native broker wire, libcurl/WebSocket integration, current SIP quote/calendar
probes and the five-session service capture plan are retired. Their earlier
read-only captures established connectivity/calendar/heartbeat evidence and a
latest-SIP entitlement denial, not an observed order lifecycle. They are no longer
completion dependencies. Existing local evidence remains retained; Git history
preserves superseded source and investigation notes. Historical Alpaca download,
credentials, raw bar decoding and corporate-action acquisition remain supported.

No paid feed, subscription or broker operation is required for the remaining
completion plan. Synthetic messages are never described as broker-observed fills.

## Verification boundary and next work

GCC Debug builds with warnings as errors. All 27 routine groups pass in 20.31
seconds, and all 23 quick groups pass in 4.41 seconds on this environment. The
broker diagnostic passes 35 assertions in six cases. Independent verification
checks 325 artifact hashes, 309 capture records, six Decimal ledgers, 100 journal
inputs, seven incremental fills and finite quote budgets. ASan/UBSan pass the
broker, transport and accounting groups; LeakSanitizer is unavailable under this
environment's process inspection restrictions, so that run disables leak detection.
Eight Windows-target syntax checks pass, including the preserved historical
WinHTTP client. Repository hygiene and diff checks pass.

The complete native `paper_broker_0` archive for `5ba59d0` verifies with MSVC
19.51.36260.0 Debug. All 325 artifact hashes, 309 capture records, six Decimal
ledgers, 100 journal inputs, seven incremental fills and quote-size budgets pass
independent checks. Of the retained artifacts, 324 are byte-identical to the Linux
run; only the source inventory differs. Its 216 source records have 212 exact
matches and four explained CRLF variants, with no unexplained source differences.
The paired paths end at USD 2,000.24, flat with no open orders or reservations.
The partial-cancel case retains four shares and USD 1,600; uncertain-submission,
snapshot-mismatch and deadline/stop cases preserve USD 2,000 and zero inventory.
The archive and console establish a successful native diagnostic. The subsequent
Windows build and tests were reported successful on 25 October 2026, completing
the native checkpoint. No additional native test log or timing was supplied.

The native summary SHA-256 is
`951aea03d3b5ed206ed04ae99b06c14feb43f522030df0fe2cd07a53b403c978`.
Its source-inventory identity is
`75e42c73dda639c76638b64d8aad5f7c2ab38862b24e2382f67675e90b1cf172`.
The complete evidence and comparison record are retained under
`scratch/verification/native-broker-20261025/`. Existing data and run directories
remain retained. The accepted diagnostic need not be repeated unless its
implementation changes.

Milestone 4 is complete under the revised offline scope.
Milestone 5 evidence follows below. No additional strategy search or service-
access investigation is needed.

## Execution measurement checkpoint, 25 October 2026

The [measurement contract](execution_measurement.md) defines 36 synthetic
workloads, four fixed limit/deadline policies and 144 independent account cases.
The report retains 108 matched policy comparisons and 120 one-factor assumption
comparisons. Source and receipt clocks are integer virtual seconds; processing
latency benchmarks remain milestone 6. Advancing source time does not refresh
quote liquidity. All matching, account transitions and persistence reuse the
existing broker, decoder and schema-3 ledger.

Economic sums retain exact signed half-cents. Executed shortfall decomposes into
spread and timing; unfilled shares receive a separate common-terminal-midpoint
opportunity mark. Incomplete orders retain null completion times and an explicit
censoring reason/interval ending at cancellation confirmation or the observation
horizon. Policy-driven cancellation is informative censoring; no independent-
censoring survival estimate is fitted.

| Policy | Filled shares / requested | Completed cases / 36 | Total shortfall, bps of requested notional |
| --- | ---: | ---: | ---: |
| Cross limit, 5-second cancel | 72.22% | 20 | 3.5889 |
| Cross limit, 11-second cancel | 76.67% | 24 | 3.5889 |
| Same-side limit, 5-second cancel | 14.44% | 0 | 3.7667 |
| Same-side limit, 11-second cancel | 20.00% | 0 | 3.6000 |

These are equally weighted deterministic workload summaries, not empirical fill
probabilities or universal policy rankings. The larger fill coverage under the
longer cross deadline does not change its total marked shortfall in this grid.
Passive limits trade less, with substantial incomplete quantities. The report
shows canceled/rejected counts, conditional timing sample counts and cost
components beside these aggregates.

The independent verifier checks all 6,294 artifact hashes, 6,002 captured records,
144 Decimal ledgers, 2,078 journal inputs, 206 measured fills, finite quote budgets,
source/receipt times against raw bytes, censoring endpoints, paired comparisons,
assumption sensitivities and CSV/summary arithmetic. Analytical C++ controls cover
signed costs, buy/sell symmetry, half-cent midpoints, no-fill observations, clock
validation and finite liquidity across clock advancement. The short contract
runs sixteen cases twice and compares saved states and metrics; it does not run
the full 144-case demonstration on every edit.

The report adds a policy cost-component figure, 144 bid/ask/limit/fill plots and
expandable source/receipt and client-state tables. SVG charts are structurally
validated and representative charts rendered for inspection. Headless browser
interaction testing is unavailable because its browser executable is absent.
No new production dependency or JavaScript is introduced.

Linux Debug builds with warnings as errors. All 28 routine groups pass in
22.71 seconds and all 24 quick groups in 6.03 seconds in this environment.
Affected ASan/UBSan groups and six Windows-target syntax checks pass. Independent
checks also reject forged censoring intervals and opportunity-cost values.
Repository hygiene and diff checks pass. Leak detection remains unavailable in
this environment.

The uploaded MSVC 19.51.36260.0 Debug `execution_measurement_0` archive for
`e2062a1` passes the full independent verifier. All 6,294 artifact hashes and all
matching, ledger, timing, censoring and sensitivity checks pass. Of its artifacts,
6,293 are byte-identical to the Linux run; only the source inventory differs.
Its 222 records contain 218 exact source matches and four explained CRLF
variants, without unexplained differences. The Windows build and tests were
reported successful. Native summary SHA-256:
`7c0a07c2f8bf6bf719d1d3c8f73c4683f34b47a1ee7250027a318574951368aa`.
Verification evidence remains under `scratch/verification/native-measurement-20261025/`.

The runner now flushes a start message, account progress every sixteen cases and
a report-writing message. Progress callbacks do not change calculations or saved
case results; contracts compare runs with and without an observer.

Milestones 1-5 are complete. Milestone-6 implementation and verification follow.
The integrated HTML/PDF report and final repository presentation remain milestone 7.

## Implementation benchmark checkpoint, 25 October 2026

`RuntimeBenchmark` composes typed/core and schema-3 paper inputs with the existing
owners and platform storage. Three sizes, five fresh-state repetitions and eight
modes produce 120 Release rows. Every accepted row passes the analytical
round-trip balance, terminal-order and duplicate/recovery checks. The independent
verifier checks all 100 artifact hashes, fifteen complete mode groups, raw
quantiles/rates, nested append timings, journals and CSV arithmetic. Recomputed-
hash forgeries of a quantile, final cash and recovery gate are rejected.

The first Linux Release run uses GCC 13.3.0, an OS-reported AMD EPYC 9V74 and nine
logical processors in this hosted environment. At 128 round trips:

| Mode | Median trial p50 | Trial p50 range | Median timed-call rate |
| --- | ---: | ---: | ---: |
| Core | 24.818 us | 23.105-38.248 us | 34,471 inputs/s |
| Additional candidate copy | 97.787 us | 87.643-103.306 us | 9,577 inputs/s |
| Schema-2 volatile journal | 110.517 us | 104.317-113.732 us | 8,740 inputs/s |
| Schema-2 synchronized journal | 421.287 us | 343.199-479.530 us | 2,298 inputs/s |
| Schema-3 volatile paper journal | 1.282 ms | 1.220-1.358 ms | 793 inputs/s |
| Schema-3 synchronized paper journal | 1.682 ms | 1.600-1.805 ms | 584 inputs/s |
| Schema-2 warm recovery | 51.745 ms | 50.219-54.156 ms | 19.3 recoveries/s |
| Schema-3 warm recovery | 1.167 s | 1.054-1.233 s | 0.86 recoveries/s |

Typed/paper synchronized journals occupy 650,192/976,805 bytes before recovery,
with largest frames 752/1,294 bytes. The process-wide peak observed across this
run is 15.918 MiB; it is not an isolated runtime allocation measurement. Clock
probe p50/p99 is 20/31 ns. Raw timings, environment and build/source identities
remain in `scratch/runs/runtime_benchmark_0/`. Copying and schema-3 state handling
have visible costs; no low-latency claim, cap-scale extrapolation or optimization
is made. The [benchmark contract](runtime_benchmark.md) defines exact boundaries,
resource limits, once-only accounting conditions and external dispatch ambiguity.

Linux Debug passes all 29 routine groups in 24.82 seconds and 25 quick groups in
6.69 seconds. Six affected ASan/UBSan groups pass in 7.58 seconds with local leak
detection disabled under the existing process-inspection restriction. Five
Windows-target syntax checks pass. Two SVG figures parse and render correctly.
The Release full suite passes all 29 groups in 6.53 seconds. Repository hygiene
and diff checks pass. Clean-checkout hosted workflow outcomes remain pending;
configuration alone does not establish passing CI. Native workstation Release benchmark evidence
remains an explicit checkpoint.
