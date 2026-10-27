# Multi-instrument research and execution extension

Status recorded 27 October 2026: **M0-M1 complete; M2 input-limited; M3-M10 not started.**
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

The seven original frozen protocol/settings/data-policy files in `ProjectSources.frozen`
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
| D1 | At v1, calendar conversion starts at 2020-12-31; reference calendars cover 2021 calibration and 2022-2023 development | M1 extends conversion to 2016-2025; all 20 provider calendar samples passed; old reference calendars retain their bounds |
| D2 | At v1, calendar bar requests, session batches and corporate-action requests encode SPY | M1 adds explicit-symbol requests and a separate capability runner; legacy defaults/loaders remain; panel acquisition follows at M2 |
| D3 | Free historical access is demonstrated, with four sampled missing minutes and 90 dividend records lacking payment/record dates; complete action history and quote windows remain unverified | M1 assessment below; M2 authenticates the full panel and action sources; M6 evaluates complete declared quote windows |
| D4 | Parent complete-grid coverage is input-limited; a registered operational/feature amendment and exact-time recovery are implemented; actual recovery and action evidence remain open | M2: authenticate recovery responses and issuer evidence before freezing masks and vintage identities alongside the original inputs |
| D5 | Six endpoints, materiality, precision plan and risk matching are registered; final max-T coverage/size verification remains open | M2: controlled precision checks; M8: validate inference and freeze the executable before final evaluation |
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
| M1 | Free data feasibility | Five-symbol capability/coverage matrix and feasible common history | Complete as a sampled access assessment; coverage/action limitations recorded below |
| M2 | Dataset and study contract | Authenticated panel, registered comparisons and evaluation controls | Full panel reviewed; input amendment and exact-time recovery implemented; actual recovery receipts, issuer reconciliation and final registration pending |
| M3 | Multi-instrument causal replay | Original-reference parity and past-only panel records | Not started |
| M4 | Shared-capital runtime | Exact global/owner accounting, netting, limits and recovery | Not started |
| M5 | Expanded synthetic broker | Durable separate peer and lifecycle/recovery scenario acceptance | Not started |
| M6 | Cost and execution research | Matched cost, quote and synthetic execution evidence | Not started |
| M7 | Stability and fair comparisons | Declared sensitivity, concentration, risk and uncertainty views | Not started |
| M8 | Integrated validation and freeze | 2024 validation, verified analysis and final executable specification | Not started |
| M9 | Final retrospective evaluation | Locked 2025 execution with prior-exposure qualifications and retained failures | Not started |
| M10 | Publication and release | Integrated results, visual evidence, benchmarks and reproducible commands | Not started |

The initial effort range is 228-368 focused hours plus unattended work, to be
re-estimated after M1, M4 and M5. Two of eleven accepted milestones is a count,
not a claim that two elevenths of the engineering effort have been spent. M1
required no paid access; full-panel volume, gap masks and action reconciliation
must be measured at M2 before revising the remaining effort range.

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

## M1 implementation

The capability runner and its native/independent checks are implemented. Calendar
conversion covers 2016-2025. Explicit-symbol bar/action/tick requests reuse the
historical read-only transport. The old SPY action constructor, study loaders,
reference calendars, protocols and default demo retain their original contracts.
Full-panel acquisition is not implemented at this stage.

The runner samples two full sessions per symbol/year, a first page of historical
quotes/trades, and paginated process-date actions. Its report separates
accessible, empty, incomplete-field, gapped, forbidden and invalid evidence.
A sampled bar route is not full-history readiness. Action completeness,
payment-date/issuer agreement and announcement availability are not inferred
from successful HTTP or an empty response.

Every HTTP attempt is archived before decoding. Later-period responses remain
under a local `sealed/` boundary. The review bundle contains counts, flags,
request metadata and hashes. No strategy fitting, scoring, returns, spread
calibration or price summaries are produced. The Windows provider run below
establishes sampled access, rather than complete historical coverage.

Run and sharing instructions are in
[Extension data capability](running.md#extension-data-capability).

Local GNU 13.3.0 Release and Debug runs passed 46 native checks each. The
standard-library oracle independently verified all five constructed evidence
bundles in both configurations and rejected altered raw evidence, false
completion claims and an unexpected raw file in the review boundary. The
existing Release demo retained its 259 checks across 24 cases and passed its
independent accounting verification. The seven frozen identities and released
publication/result evidence remain unchanged. These are software checks, not
provider-access or full historical-coverage evidence.

## M1 provider evidence and disposition

The Windows Release run on 26 October 2026 used MSVC 19.51.36260. Its 570
read-only requests, between 10:24:15 and 10:36:23 UTC, all returned HTTP 200.
The fixed universe and all ten sampled years were retained. The sanitized
[50-row capability matrix](evidence/m1_capability.csv) contains no prices,
credentials or strategy outcomes.

| Evidence | Observed result | Scope |
| --- | --- | --- |
| Calendar | 20 valid January/December samples | Two months per year; not a full calendar audit |
| SIP minute bars | 97 complete and three gapped symbol-days; 38,996 of 39,000 expected observations | Two fixed sessions per symbol/year |
| SIP quotes | All 50 symbol/year samples accessible; 5,000 usable records | First page of one fixed minute; no full-window or execution claim |
| SIP trades | All 50 samples accessible; 4,816 usable records | Same first-page sampling boundary |
| Corporate actions | 238 cash-dividend records; 148 with complete payment/record fields; no returned split records | Process-date query results; issuer agreement and ex-date completeness unverified |
| Native Windows verification | 46 constructed checks completed, zero network requests | All five completed synthetic bundles also passed the independent verifier during review |

The sampled gaps are GLD on 2016-01-04 (two minutes), TLT on 2017-12-29
(one minute), and GLD on 2024-12-31 (one minute). Gaps remain missing; no
replacement date or fabricated observation is introduced. Ninety dividend
records in 2016-2019 lack both record and payable dates. The 2020-2025 samples
have complete fields under the declared schema checks, which does not establish
that all distributions were returned. GLD's empty action responses likewise
do not establish an absence of distributions or splits.

The saved status `input_access_review_required` is expected for the three
gapped bar samples. It is not an HTTP, credential or subscription failure.
The reported `provisional_common_price_start` of 2025 means only that the
sampled 2025 sessions form the earliest remaining suffix without a sampled
bar gap. A single 2024 gap removes every earlier suffix under that statistic.
It does not date the start of available history or establish a feasible scored
period. The 2016-2025 target archive and all five instruments remain in scope;
M2 must establish the usable calendar and initialization from full coverage.

M1 is complete as a feasibility assessment. No entitlement workaround, paid
feed or repeat of the same probe is warranted by this evidence. The following
are M2 acceptance requirements before expanded scoring:

- Authenticate the full archive, retain each missing interval, and register
  outcome-independent session eligibility and common comparison masks.
- Reconcile distributions and splits against independent source records,
  including missing early payment/record dates and process-date boundary tails.
  Unresolved fields cannot become assumed cash payments or zero actions.
- Establish prior-history availability, the first eligible scored date and the
  bridge to the original SPY acquisition vintage. Preserve the original inputs.
- Register the finite comparisons, costs, uncertainty rules and 2024/2025 access
  boundaries before model fitting or strategy evaluation.

The supplied `review.zip` exactly matches the provider run's root and `review/`
files. Its CSV and HTML hashes match the summary. Automated byte/hash and index
verification passed for all 570 archived responses, including 114 reserved
responses; reserved response bodies were not decoded during this review.
Only pre-2024 action fields were inspected to diagnose the missing dates.
No reserved prices, models, returns or spread estimates were inspected.
The local verifier creates `review.zip` only after its raw-evidence checks pass;
the supplied bundle follows that export contract.

| Evidence identity | SHA-256 |
| --- | --- |
| Provider and synthetic-run build-source inventory | `357a615910cf57518d927a4eeb0908c1159d9e5a844c3675e89df60340d34ce9` |
| Provider review summary | `47e5e1d2c1c8d25c0fd4e4b82e9b00c52dccecef45a90e34ce998a0e9af17298` |
| Capability CSV, retained unchanged | `84a41b853a3dc09bf5a68e7eb0b3d768951c24543dc215408ffe650015b8abdc` |
| Provider review HTML | `3fa04ce963cfaea8074afe0b0bb0fe4ea9900e2adf0b6a7dda038b47d7ab385c` |

The Windows source inventory is a run identity, not a verified Git commit:
Visual Studio had expanded the local project items before compilation.
Bulk/raw evidence stays outside version control. The v1 protocols, results and
publication identities remain unchanged.

## M2 implementation checkpoint

The [registered study contract](../research/extension_protocol.md) fixes the
dataset, six primary endpoints, materiality, pooling, uncertainty design,
secondary neighborhood and access controls before expanded scoring. The
JSON identity is pinned by the existing Python build driver. No CMake/CTest or
hosted workflow is introduced.

`panel_check`, `panel_acquire` and `panel_finalize` call cohesive native owners
for resumable page acquisition, eligibility, action reconciliation and the
original 496-date SPY vintage bridge. The independent Python panel verifier
checks raw hashes, request chains, a separate fixed-date calendar oracle,
minute grids and masks. The public price reader requires an acquisition
identity and rejects reserved years before lookup.

The initial controlled precision design checks nine Gaussian AR(1) cases with
2,000 trials each against analytical mean variance and power. Its retained
[table](evidence/m2_precision.csv) is a planning calculation, not an estimate
from market outcomes. It does not establish final max-T coverage or market power.

Free source extraction is implemented for SPY, QQQ, IWM and TLT distributions.
The reviewed source snapshots supplied 40, 41, 40 and 120 cash records,
respectively, in the ex-date horizon. Those counts are not a substitute for
reconciling the complete provider archive. GLD and independent split-history
coverage remain unresolved; the source preparer records them as incomplete
and cannot publish ready research inputs.

A pre-2024-only comparison with M1 evidence also found QQQ amount disagreements
on 2018-06-18 and 2018-12-24, plus distinct provider records with the same
2022-09-19 ex-date and different payable dates. These are retained source
discrepancies, not rounding/zero-payment assumptions. The complete acquisition
must establish their current-vintage status and an explicit source resolution
before accounting registration. No reserved price data were inspected.

M2 acceptance requires complete action-source review, resolution of the full-panel
input limitations recorded below, the vintage bridge and final study manifest.
The v1 results and seven original frozen identities remain unchanged.
No expanded strategy has been fitted or scored. Run/sharing instructions are in
[panel acquisition](running.md#extension-panel-acquisition-and-registration).

The final local checkpoint used GNU 13.3.0 on Linux:

| Verification | Observed result |
| --- | --- |
| Release build | All 16 Linux applications built with warnings as errors |
| Native panel checks | 91 passed in Release and Debug; no network requests |
| Independent panel oracle | Four constructed raw archives verified in each configuration, including interrupted/outage recovery and history masks |
| Existing capability contracts | 46 passed; five constructed bundles independently verified |
| Existing offline demo | 259 checks / 24 cases passed; independent 350-artifact accounting/replay verification passed |
| Controlled precision design | Nine cases, 2,000 trials each; analytical variance/power comparisons passed |
| Legacy issuer extractor | Original twelve SPY financial records unchanged |
| Frozen identities/publication | Seven original and one extension identity matched; v1 publication hashes/links verified |
| Repository hygiene | Passed with no findings |

Panel evidence directories are `scratch/runs/panel_check_6` (Release) and
`scratch/runs/panel_check_7` (Debug), under source-inventory SHA-256
`fb194e3307078fc0727e400e008d46e951aa3cf115ddc1e07b1aa3fc77efcc07`.
These are local run identities, not provider coverage evidence. Native MSVC and
the full private panel have not been verified by this checkpoint.

## Full-panel acquisition review

The 27 October 2026 coverage-only review reports 50,350 retained HTTP attempts
verified locally across 120 months, 2,514 exchange dates and 12,570 symbol-days.
The submitted ZIP contains only `summary.json` and `coverage.csv`; both match the
separately retained files byte for byte. All CSV rows match the summary, and a
separate fixed-date calendar check reproduces dates, session lengths and the
registered eligibility flags. Raw responses were not supplied for this review,
so their local verification is reported rather than independently repeated.
The export does not retain compiler or native-check evidence.

The panel contains 4,881,323 of 4,883,400 expected minute observations. The 2,077
missing observations are 0.0425% of the expected grid. Their cause cannot be
established from coverage flags alone: an absent minute does not prove either
a failed download or a no-trade interval. No interpolation or zero-volume bar
was introduced. TLT accounts for 1,196 missing minutes and GLD for 555; the
other three instruments account for 326 combined.

The [annual coverage table](evidence/m2_coverage.csv) is derived by grouping
the review's coverage rows by calendar year. Regular/common counts use one
SPY row per date; missing-minute and endpoint counts sum all five instruments.
Development-year qualification requires both 200 common dates and 80% of the
regular calendar, excludes the 2016 initialization year and does not use
strategy outcomes or warm-up/model availability.

| Development year | Common eligible dates / regular dates | Coverage | Qualifies |
| --- | --- | --- | --- |
| 2017 | 92 / 249 | 36.95% | No |
| 2018 | 111 / 248 | 44.76% | No |
| 2019 | 153 / 249 | 61.45% | No |
| 2020 | 221 / 251 | 88.05% | Yes |
| 2021 | 209 / 251 | 83.27% | Yes |
| 2022 | 227 / 250 | 90.80% | Yes |
| 2023 | 208 / 248 | 83.87% | Yes |

Only four development years qualify; five are required. Common coverage is
the intersection of five complete-session grids, so high aggregate minute
coverage does not imply sufficient common dates. These are input dates, not
active dates or performance observations. There are also three unavailable
required closing minutes, all on 2019-08-12, for SPY, IWM and GLD. All opens
are available. The current finalizer therefore cannot register the intended
continuous daily accounts or pass the development-year gate.

All 55 action process-date intervals are represented, including the five
January-March 2026 tails. The provider summary reports 243 cash-dividend rows,
153 with complete payment fields, no split rows and no GLD action rows. Thus
90 cash records need independent date evidence. Empty split/GLD responses
do not establish that no actions occurred. Aggregate counts cannot resolve the
previously identified QQQ amount and same-ex-date discrepancies.

The acquisition is complete according to the independent export, but M2 remains
input-limited. Further work must resolve source evidence for gaps and actions;
any change to eligibility must be an explicit protocol amendment before replay,
with the original contract and its failed input assessment retained. The
coverage review authorizes neither a silent threshold change nor historical
strategy evaluation. The original-SPY vintage bridge is still pending.
Reserved-year review used coverage metadata only; no prices, fitted models,
returns or spread estimates were inspected.

The readiness workflow now reports every source-coverage declaration alongside
coverage gates and actual pre-2024 gap intervals. Incomplete split coverage no
longer prevents cash amount/date comparisons from being reported. Duplicate
provider records remain explicit discrepancies and cannot publish canonical
income. Detailed reserved gaps and reserved cash discrepancy amounts are
excluded from the review export. No eligibility rule or pinned protocol was
changed. [Run instructions](running.md#extension-panel-acquisition-and-registration)
describe the free source preparation and existing finalizer entry point.

The implementation checkpoint built all 16 Linux Release applications with
GNU 13.3.0 and warnings as errors. All 105 native panel checks passed, including
missing references, grouped gaps, duplicate cash actions and blocked registration.
The independent oracle verified five constructed archives and both readiness
reports, including exact gap intervals; four altered-report cases were rejected.
The source inventory is
`cc8bf7d8edf338e87fda49b3e2beaccf0ef42ad04dffedcacfb795a4e3ed750a`.
This verifies the software, not the outstanding provider discrepancies. The
private Windows archive must produce the detailed readiness report before
source resolutions or an eligibility amendment can be assessed.

| Evidence identity | SHA-256 |
| --- | --- |
| Registered extension protocol | `269d6e677a3e62a21ede6e88659edb2f7a998bfe0137e62b13458e411f0c55ee` |
| Acquisition manifest reported by local verification | `ebf148ee10d8997ef9b259aad5af1f9e4894e4875824dbcaf4203beb3538b1d2` |
| Submitted review summary | `b30355d563e63d09fbb62e3f7aade96018f88d03a714bc5fbf12da5af21d9ca9` |
| Submitted daily coverage CSV | `2b579f1dbf80edc8338343bd4039d799924b45732d879c19fb2ab52b3fe4fc94` |
| Derived annual coverage CSV | `10378dcca03a994db6c31e8de0bf0fd9b474d93d8200ca1e59073ed6d717b031` |

## Input-policy amendment and closing recovery

[MULTI-ASSET-01-INPUT-01](../research/extension_input_policy.md) registers the
distinction between execution observability and feature availability before
expanded replay. Its numerical authority is build-pinned separately from the
unchanged parent. The parent assessment remains failed under its own definition.
The amendment retains the five-year/200-date/80% numerical requirements but
applies them to common operational dates, with feature availability reported
separately. This is a changed input definition, not a pass under the parent rule.

The native policy derives 33 required execution-reference slots from the already
registered schedules, delays and holding periods. It preserves exact momentum
and reversion observations, complete continuation volume prefixes, prior-only
history dates and explicit unavailable-feature behavior. Operational coverage
cannot establish model availability or activation. The M3 replay must verify
causal missing-input behavior and cannot use future outcome availability as a
signal or silently turn missing outcomes into zero returns.

An [eight-row metadata comparison](evidence/m2_policy_coverage.csv) was reproduced
with the native policy and separate Python calendar/set arithmetic from the
supplied readiness summary. Operational counts for 2017-2023 are respectively
222, 229, 242, 249, 250, 247 and 243. All seven years meet the amended operational
criterion; only four meet the parent complete-grid criterion. These counts are
availability evidence, not scored or active dates, returns, power estimates or
proof of an unrestricted continuous account. No reserved-price values were used.

The closing-recovery collector requests only the registered three missing
2019-08-12 minute windows, retains a separate immutable response vintage and
binds it to the original acquisition and amendment. It accepts actual bars at
the exact original clock. Empty successful responses remain unavailable and are
not repeatedly requested. Finalization preserves original missing flags and
records each recovered daily-close source separately. No actual provider recovery
response is retained at this implementation checkpoint; the constructed success
cases do not establish that the provider can supply the missing observations.

Verification used GNU 13.3.0 Release with warnings as errors: all 16 applications
built, 135 native panel checks passed, and the independent verifier checked seven
constructed archives and four readiness reports. Five altered-count, slot,
identity, endpoint and raw-receipt cases were rejected. Existing capability
checks (46), five capability reference bundles, the offline demo (259 checks /
24 cases), independent 350-artifact accounting/replay verification and publication
hash/link checks passed. All nine pinned identities match. The final source
inventory is `533f57c37a21cb8c335b4c69c342de6e23f626df09e04a9ef807385eabad0ad6`.

| Record | SHA-256 |
| --- | --- |
| Input amendment | `af45ca4552ef58d5de5d9305b140c57eecd42ffc02453867b127d2f6886ad99e` |
| Supplied detailed readiness summary | `5a2b674c446eb7624a66b71893bc2b4c3c18d0d1fdb2fe3100053d8a72cf6b98` |
| Derived operational comparison table | `1ae006196b7074a1737c172fcf06850a4f292bee6bd433d936e96781ab3798b0` |

M2 remains open. Actual closing evidence, QQQ cash/date resolutions, independent
GLD/split coverage and the final vintage bridge are still required. No issuer
coverage flag, amount tolerance or action-disagreement gate was relaxed by this
input amendment. The original v1 results and acquisition snapshots are preserved.
