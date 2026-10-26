# Multi-instrument research and execution extension

Status recorded 26 October 2026: **M0 complete; M1-M10 not started.**
This is the implementation record for the extension. The published
[v1.0.0 release](https://github.com/KVInator/algorithmic-trading-runtime/releases/tag/v1.0.0)
and its [technical report](report.pdf) retain their original scope and findings.
No expanded historical result is available yet.

## Baseline identity and preservation

| Record | Verified baseline |
| --- | --- |
| Release tag | `v1.0.0` |
| Release commit | `d5fe62a8db6db9714573f96c6223dc4ec4d3cd33` |
| Build-source inventory SHA-256 | `254f039cf6d6b7dbd18727472662bab603d7fa61ec25119345743f0893ced8aa` |
| Registered development study-manifest SHA-256 | `70fdd01093b9026cc4e673def25718e547c0052b728163bac9bdf30b9c25126e` |
| Registered daily-risk acquisition-manifest SHA-256 | `d1f500c927480e7a7013df86e476785b66afdfa5d54c2f97e757957f34ce2808` |

The build-source inventory covers `src/`, `apps/`, `research/`, `build.py`,
`Runtime.sln` and `Runtime.vcxproj`, under the existing inventory rules. It is
distinct from the Git commit, dataset identity and publication manifest.
The two acquisition identities above were checked against their registered
source records; the private raw acquisition trees were not reauthenticated at M0.

The seven frozen protocol/settings/data-policy files in `ProjectSources.frozen`
match their registered SHA-256 values. Neither those records nor the original
result files, curated evidence, report, journal schemas or release tag change
at this milestone. Subsequent code must retain the original SPY reference path;
new settings and data receive separate identities. A later result cannot
retroactively pass a failed v1 advancement screen.

## Accepted scope

- Fixed universe: **SPY, QQQ, IWM, TLT and GLD**. These are five USD-listed
  instruments, with correlated equity exposures, not five independent markets.
- Target archive: complete years **2016-2025**, subject to the M1 free-access
  and coverage assessment. Select a later common start from input availability
  alone if necessary; retain all five instruments.
- Reuse the momentum, continuation and opening-anchor reversion engines and
  retain daily risk as a separate experiment. Daily risk is not a fourth
  directional owner in the shared intraday account.
- Extend one USD account to symbol-keyed positions and explicit strategy
  ownership, target netting and aggregate cash/risk limits. Preserve unlevered
  long/cash positions and whole-share execution.
- Extend the synthetic broker into a separate local C++ process with durable
  peer state, DAY/IOC/GTC limits, managed cancel/new replacement, explicit fees,
  cash availability and activity-history recovery. Retain the fast in-process
  peer and reuse its matching implementation.
- Extend costs, execution, concentration, stability and predefined risk-matched
  comparisons. Publish coverage, exposure, turnover, drawdown and uncertainty
  beside returns. Operational correctness and economic benefit are separate
  conclusions; profitability is not a completion requirement.
- Retain historical Alpaca market-data acquisition. Purchases, paid feeds,
  latest-quote probes, real orders and native broker connectivity are outside
  the extension. A free empirical-access failure is recorded as unestimated
  evidence, not repaired with a subscription or fabricated observations.

Numerical extension settings, budgets and inference procedures remain proposed
until their governing specification is frozen. The v1 protocols remain fixed.

## Research exposure and evaluation roles

The exposure inventory records positive evidence and unknowns separately.
An absence of recorded inspection does not establish an untouched sample.

| Instrument / period | Evidence available at M0 | Treatment |
| --- | --- | --- |
| SPY, March 2022 and 2023-01-03 | Engineering pilot and diagnostic explicitly declared in the momentum protocol | Inspected; the diagnostic retains its v1 performance exclusion |
| SPY, 2022-2023 | Completed development results for all four studies | Selection-exposed reference; never relabel as held out |
| SPY, 2020 boundary / 2021 | Daily-risk calibration closes, warm-up and action evidence are registered | Previously used inputs; not untouched transfer evidence |
| SPY, 2024-2025 | The original protocol declares no use of those strategy results for selection; the retained project records say later local results remain unopened | Preserve the project reservation; this is not proof that raw prices were never viewed |
| SPY, earlier years | No complete prior-exposure declaration is retained here | Retrospective transfer; prior inspection remains unverified |
| GLD, 2004-11-18 through 2026-01-12 | Earlier daily OHLCV strategy research declares this sample and chronological 60/20/20 training/blending/testing | Known prior research exposure; do not describe GLD 2025 as globally untouched |
| QQQ, IWM and TLT, 2016-2025 | No instrument/window exposure declaration has been established by this inventory | Prior exposure unverified; no untouched-history claim |

The GLD source is the earlier study's
[versioned dataset and pipeline description](https://github.com/KVInator/CQF-Project-gld-short-horizon-trend-blending-ensemble/blob/9643fced7fc35a73d98dcf64a87869398ce25699/README.md).
Only its research metadata was inspected at M0; its price data, figures and
period-specific results were not loaded. Exact calendar split dates are not
established here. A different daily model does not make the instrument's market
history unseen. The continuation protocol also declares literature exposure
covering later years; the extended design is retrospective.

| Period | Extension role | Access boundary |
| --- | --- | --- |
| 2016, if available | Initialize past-only histories | Scoring waits for all required prior observations; no fabricated pre-archive close |
| 2017-2021, subject to warm-up | Historical transfer replay | Fixed rules evaluated retrospectively, not a prospectively selected design |
| 2022-2023 | Original inspected reference | Retain the old snapshot and attribute any provider-vintage differences |
| 2024 | Extension validation | Open at M8 after registration; any resulting design choice is recorded |
| 2025 | Locked final retrospective evaluation | Open at M9 after the M8 freeze; retain all prior-exposure qualifications |

The entire five-instrument 2025 panel cannot be described as globally untouched.
It remains reserved against selection using this extension's own outcomes.
Later raw strategy inputs, price summaries, fitted models, rankings, cost
calibration and account returns stay outside engineering inspection before
their declared stage. Automated integrity checks may expose hashes and coverage
flags. A changed model after inspection receives an exposure record, not a
restored holdout label.

The new evaluation sequence must be registered at M2. It does not override the
failed v1 escalation gates or authorize later-period runs of the old studies.
M0 performed no historical acquisition, model fitting or strategy scoring.

## Authoritative implementation and extension boundaries

Paths below are the existing implementations, not proposed duplicate engines.
The [architecture](architecture.md) and linked contract notes give their detailed
invariants.

| Responsibility | Existing source / owner | Extension boundary |
| --- | --- | --- |
| Historical HTTP and credentials | `src/alpaca/alpaca_market_data_client.hpp`; Windows implementations under `src/platform/windows/` | Preserve read-only historical transport and credential storage |
| Requests and calendar | `src/alpaca/alpaca_bar_request.cpp`, `alpaca_corporate_action_request.cpp`; `src/market/market_calendar.cpp` | Parameterize supported symbols/date plans without weakening legacy contracts |
| Acquisition and authentication | `src/storage/session_batch.cpp`, `development_study_dataset.cpp`, `risk_input_dataset.cpp` | A symbol/date panel, resumable evidence and outcome-independent masks; retain old loaders |
| Momentum | `src/research/intraday_momentum.*`, `DevelopmentStudy` | Reuse completed-prefix decisions and `evaluate_long_prices`; generalize explicit SPY restrictions |
| Continuation | `src/research/intraday_continuation.*`, `ContinuationStudy` | Per-symbol history; preserve volume as an entry filter and prior-only scales |
| Reversion | `src/research/intraday_reversion.*`, `reversion_strategy.*`, `ReversionStudy` | Preserve within-session pairs, occupied missing-history slots and frozen signals under cost stresses |
| Daily risk and fractional accounts | `src/research/daily_risk_study.*`, `daily_risk_dataset.cpp`; `src/accounting/long_cash_account.*` | Wider calendar/action evidence, explicit calibration and continuously marked accounts |
| Whole-share targets and accounting | `src/execution/momentum_order_adapter.*`, `strategy_order_adapters.*`, `order_runtime.*` | One global cash/reservation owner, symbol positions, strategy ownership and deterministic allocation |
| Durable accounting | `src/execution/journaled_runtime.*`, `paper_execution.*`, `journaled_paper_runtime.cpp`; `src/storage/journal_frame.*` | Preserve schema 1/2 cents and schema 3 USD 1e-9; version new contracts and bounded segment handover |
| Broker matching and protocol | `src/execution/simulated_broker.*`; `src/alpaca/simulated_paper_wire.*`, `paper_transport.*`, `paper_control.*` | One matcher, independent durable peer state and a bounded process adapter |
| Measurements | `src/execution/execution_measurement.*`, `runtime_benchmark.*` | Matched multi-symbol/process workloads; separate physical timings from simulated delays |
| Evidence and verification | `src/reporting/`; `research/verify_*.py`, `build_report.py` | Saved records drive views; independent numerical checks do not become another production engine |

The current `OrderRuntime` stores one symbol and one cash balance. Five separately
funded instances do not establish shared capital. `LongCashAccount` is also
single-instrument and fractional; it must not be presented as the exact integer
broker ledger. The schema-3 bridge restricts the synthetic contract to SPY DAY
limits and an explicit zero-fee policy, although the underlying matcher/core
already carry fee fields. Extending those policies requires declarations and
verification, not another matcher or silently accepted messages.

The existing broker already covers partial fills, duplicate delivery, a
cancellation race, ambiguous submission, snapshot mismatch and client restart.
It does not provide a separate operating-system process, persisted peer restart,
paginated activity recovery, strategy ownership or general cash-settlement rules.
Detailed baseline contracts remain in [broker](paper_broker.md),
[paper execution](paper_execution.md) and [journal recovery](journal_recovery.md).

## Decisions to resolve in sequence

| ID | Decision / concrete limitation | Resolution stage |
| --- | --- | --- |
| D1 | `parse_session_date` accepts only 2020-12-31 through 2025; reference calendars cover 2021 calibration and 2022-2023 development | M1: bounded wider calendar support and trusted historical calendar evidence before old-date probes |
| D2 | `CalendarSession::bar_request` and `SessionBatch` encode SPY; corporate-action requests encode SPY and process dates within 2021-March 2024 | M1: explicit five-symbol historical request/response contracts; preserve legacy defaults and identities |
| D3 | Actual free bars, distributions/payment dates, splits and quote-window coverage for all five symbols are unverified | M1: capability matrix and coverage-based common start; fixed failure/retry policy |
| D4 | Expanded dataset layout, masks, action conventions and vendor-vintage bridge are not registered | M2: immutable identities and a separate panel alongside the original inputs |
| D5 | Primary endpoint family, materiality/power design, family-aware inference and risk-matching details remain proposed | M2: finite analysis specification before expanded scoring; final executable freeze at M8 |
| D6 | Engines and aggregate runners enforce SPY, fixed histories and 2022-2023 gates | M3: parameterize authoritative engines and add the panel driver; keep the v1 reference path |
| D7 | One-symbol cash, pending-fill limits, internal ownership transfers and journal growth need a shared-account contract | M4: one global ledger, deterministic netting/allocation and bounded durable handover |
| D8 | Process framing, durable broker history, lifecycle/fee/cash policies and schema migration remain unspecified | M5: explicit bounded contracts and twelve-family fault acceptance |
| D9 | Historical executable-price references, size assumptions and source/receipt delays require separate evidence | M6: bounded quote sample and fixed synthetic stress matrix, with unestimated quantities explicit |
| D10 | Final period has known GLD research exposure and unverified exposure elsewhere | M2: carry the M0 qualifications into registration; M8/M9: preserve the project-specific lock and report labels |

M1 must establish the input route before bulk acquisition or expanded scoring.
Unsupported dates and symbols are known implementation work, not evidence of
vendor entitlement failure. No native broker service or paid access is needed.

## Milestone order and acceptance

| ID | Work package | Completion evidence | Status |
| --- | --- | --- | --- |
| M0 | Baseline and research inventory | Release identities, implementation map, exposure log, scope and decision register | Complete |
| M1 | Free data feasibility | Five-symbol capability/coverage matrix and feasible common history | Not started |
| M2 | Dataset and study contract | Authenticated panel, registered comparisons and evaluation controls | Not started |
| M3 | Multi-instrument causal replay | Original-reference parity and past-only panel records | Not started |
| M4 | Shared-capital runtime | Exact global/owner accounting, netting, limits and recovery | Not started |
| M5 | Expanded synthetic broker | Durable separate peer and lifecycle/recovery scenario acceptance | Not started |
| M6 | Cost and execution research | Matched cost, quote and synthetic execution evidence | Not started |
| M7 | Stability and fair comparisons | Declared sensitivity, concentration, risk and uncertainty views | Not started |
| M8 | Integrated validation and freeze | 2024 validation, verified analysis and final executable specification | Not started |
| M9 | Final retrospective evaluation | Locked 2025 execution with prior-exposure qualifications and retained failures | Not started |
| M10 | Publication and release | Integrated results, visual evidence, benchmarks and reproducible commands | Not started |

The initial effort range is 228-368 focused hours plus unattended work, to be
re-estimated after M1, M4 and M5. One of eleven accepted milestones is a count,
not a claim that one eleventh of the engineering effort has been spent.

## Build and verification record

`Runtime.sln` delegates to the existing `build.py` compiler driver. Both CMake
and CTest remain absent. C++ is authoritative for research, portfolio, execution
and broker behaviour. Focused native checks exercise those classes directly;
Python orchestrates executables and provides independent arithmetic/statistical
references and publication. New native checks are added with the behaviour they
verify, not as an empty framework at M0.

Ordinary edits use focused checks and small fixtures. Full acquisition audits,
historical experiments, large resampling and performance benchmarks are explicit
checkpoint workloads. [Build and reproduction](running.md) remains the command
reference. There is no continuous CI workflow.

The following checks ran against the unmodified v1 implementation on Linux with
GNU 13.3.0, C++20 and warnings as errors on 26 October 2026:

| Check | Observed result |
| --- | --- |
| Native Release `project_demo` | 259 checks, 24 cases, zero network requests; independent verifier passed |
| Native Debug `project_demo` | 259 checks, 24 cases, zero network requests; independent verifier passed |
| Independent combined-demo verification, each configuration | 350 artifact hashes, strategy states, six broker cases, integer/Decimal ledgers, captures and quote budgets verified |
| Registered identities | All seven frozen protocol/settings/data-policy records matched |
| Publication verification | 14 hashes, eight SVG figures, two static embedded views and local links verified |
| Repository hygiene audit | Filesystem/Git inspection passed with no findings |

Local run directories are `scratch/runs/project_demo_4` (Release) and
`scratch/runs/project_demo_5` (Debug). Their summary SHA-256 values are respectively
`f5b6b5881ef08c5bb140c611c6c628d9384f3a9b5be0d7df163eeaef99d51606`
and `9771b37a6356759b5d54417d81e5651e3f08c03828e63ab0df27847ec3319ad3`.
These identify local evidence, not portable fixed suffixes or reproduced Windows
performance. The [original native acceptance](https://github.com/KVInator/algorithmic-trading-runtime/actions/runs/40224499759)
records earlier Windows Release/Debug and Linux Release verification at its
stated commit. Windows, private acquisition trees and the full historical
experiments were not rerun at M0. No new benchmark claim is made.
