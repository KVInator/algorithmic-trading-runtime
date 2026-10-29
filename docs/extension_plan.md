# Multi-instrument research and execution extension

Status recorded 29 October 2026: **M0-M6 complete; M7-M10 not started.**
This is the accepted scope and evidence record for the extension. The published
[v1.0.0 release](https://github.com/KVInator/algorithmic-trading-runtime/releases/tag/v1.0.0)
and its [technical report](report.pdf) retain their original scope and findings.
Development replay, shared accounting and cost/execution comparisons are accepted
with explicit coverage qualifications. Remaining inference and evaluation stages
are listed below; detailed ownership is in the [architecture](architecture.md).

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

The seven original frozen protocol/settings/data-policy files were authenticated
at M0; the current build pins thirteen research records. Original v1 inputs,
findings, publication and release tag retain their identities. Subsequent code
retains the original SPY reference path; new settings and data have separate
identities. No extension result can retroactively pass a failed v1 screen.

## Accepted scope

- Fixed universe: **SPY, QQQ, IWM, TLT and GLD**. These are five USD-listed
  instruments, with correlated equity exposures, not five independent markets.
- Registered archive: **2016-2025**. The accepted M2 coverage, source amendments
  and first-history dates below govern access; all five instruments are retained.
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

The study, input amendments and M5/M6 execution policies are frozen. M7 analysis
and M8 executable inference validation must follow those registered records.
The v1 protocols and outcomes retain their original identities.

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

The evaluation sequence is registered by the M2 study contract. It does not
override failed v1 escalation gates or authorize later-period runs of the old studies.
M0 performed no historical acquisition, model fitting or strategy scoring.

## Milestone order and acceptance

| ID | Work package | Completion evidence | Status |
| --- | --- | --- | --- |
| M0 | Baseline and research inventory | Release identities, implementation map, exposure log, scope and decision register | Complete |
| M1 | Free data feasibility | Five-symbol capability/coverage matrix and feasible common history | Complete as a sampled access assessment; coverage/action limitations recorded below |
| M2 | Dataset and study contract | Authenticated panel, registered comparisons and evaluation controls | Complete: Windows Release inputs registered; source actions, amendments, masks and original-SPY vintage bridge bound to the study manifest |
| M3 | Multi-instrument causal replay | Original-reference parity and past-only panel records | Complete: Windows Release replay, 187 native checks, original-SPY parity and independent verification of all 10,060 records |
| M4 | Shared-capital runtime | Exact global/owner accounting, netting, limits and recovery | Complete: Windows Release passed 182 checks in 15 cases; 163 durable transitions independently verified |
| M5 | Expanded synthetic broker | Durable separate peer and lifecycle/recovery scenario acceptance | Complete: Windows Release passed 313 checks in twelve families; independent broker/client replay and GNU final-state parity passed |
| M6 | Cost and execution research | Matched cost, quote and synthetic execution evidence | Complete with coverage qualifications: quote-reference, historical cost/delay, 680 synthetic execution cases, 200 risk and 50 shared accounts accepted; stopped economic paths remain unavailable |
| M7 | Stability and fair comparisons | Declared sensitivity, concentration, risk and uncertainty views | Not started |
| M8 | Integrated validation and freeze | 2024 validation, verified analysis and final executable specification | Not started |
| M9 | Final retrospective evaluation | Locked 2025 execution with prior-exposure qualifications and retained failures | Not started |
| M10 | Publication and release | Integrated results, visual evidence, benchmarks and reproducible commands | Not started |

The initial planning range was 228-368 focused hours plus unattended work;
it is not a measured completion-time forecast. Seven completed milestones count
work packages, not a fraction of engineering effort. M1 required no paid access;
M2 registered full-panel coverage, gap masks and action reconciliation.
The remaining scope is M7-M10. Build commands are in the [runbook](running.md).

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

M1 is complete as a feasibility assessment. M2 subsequently completed these
prerequisites before expanded scoring:

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
were available. Under the parent rule, these findings blocked the intended
continuous daily accounts and the development-year gate.

All 55 action process-date intervals are represented, including the five
January-March 2026 tails. The provider summary reports 243 cash-dividend rows,
153 with complete payment fields, no split rows and no GLD action rows. Thus
90 cash records need independent date evidence. Empty split/GLD responses
do not establish that no actions occurred. Aggregate counts cannot resolve the
previously identified QQQ amount and same-ex-date discrepancies.

At this coverage-review checkpoint, the acquisition was complete according to
the independent export, but M2 remained input-limited. Source evidence for gaps
and actions still required resolution. Any change to eligibility required an
explicit protocol amendment before replay,
with the original contract and its failed input assessment retained. The
coverage review authorizes neither a silent threshold change nor historical
strategy evaluation. The original-SPY vintage bridge was still pending at that
checkpoint; its completed comparison is recorded below.
Reserved-year review used coverage metadata only; no prices, fitted models,
returns or spread estimates were inspected.

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
records each recovered daily-close source separately. The actual provider
recovery returned no bars; its disposition is recorded below.

| Record | SHA-256 |
| --- | --- |
| Input amendment | `af45ca4552ef58d5de5d9305b140c57eecd42ffc02453867b127d2f6886ad99e` |
| Supplied detailed readiness summary | `5a2b674c446eb7624a66b71893bc2b4c3c18d0d1fdb2fe3100053d8a72cf6b98` |
| Derived operational comparison table | `1ae006196b7074a1737c172fcf06850a4f292bee6bd433d936e96781ab3798b0` |

The input amendment changes only input eligibility. It relaxes no issuer-coverage
flag, amount tolerance or action-disagreement gate. Source resolution and final
registration are recorded below; original v1 snapshots are preserved.

## Source resolution after the provider recovery

The subsequent Windows recovery made three requests and returned zero closing
bars. Its retained manifest is
`8372e7ddf07e50f14ff44d08a0aaedd89fc696693748f0b975b91924de2a051b`.
Repeating the same successful empty requests cannot supply the observations.
[MULTI-ASSET-01-SOURCE-01](../research/extension_source_resolution.json) therefore
registers two bounded changes before expanded scoring:

- Only the three SPY/IWM/GLD closes on 2019-08-12 may remain unavailable after
  authenticated recovery. Their daily outcomes remain NA and consecutive close
  history resets; no valuation or return bridges the gap. Other endpoint gaps
  still block registration. Parent coverage failures remain in the manifest.
- The retained issuer cash table determines one canonical distribution per
  symbol/ex-date. Provider disagreements are retained as resolved conflicts;
  missing issuer events still block, and duplicate announcements are never
  summed. Declared issuer payment dates remain unchanged; cash availability is
  the first exchange session on or after the declared date.

Split coverage uses five retained secondary event responses requesting the full
2016-2025 horizon, each validated against all 120 monthly coverage timestamps.
No split events were returned. GLD cash absence combines the issuer's
[no-income statement](https://www.ssga.com/us/en/individual/etfs/spdr-gold-shares-gld),
zero secondary cash events and zero provider cash events. These are explicitly
cross-provider completeness assumptions, replacing the unresolved requirement
for an independent issuer split history. They do not prove that either feed is
infallible. Nonempty or conflicting split evidence remains blocking.

The preparation command writes a separate resolved reference and retains every
old file. The finalizer binds both amendments, canonical actions, missing masks
and the original SPY vintage comparison. Windows registration subsequently
completed as recorded below. No v1 result or reserved-price observation is
changed by the source resolution.


| Source-resolution record | SHA-256 |
| --- | --- |
| Source resolution | `9237188b4ac24282a4296e2622948666e8d8da0a78c8be103453658093d9d734` |
| Supplied post-recovery readiness summary | `15424df8eca18e4defc394a4f3038f700a0a31bfd4d02f03f1a56e14804bf346` |

## M2 Windows registration completed

The Windows Release finalizer completed with `Status: registered_inputs_ready`
on 28 October 2026. Its study manifest has `research_inputs_ready=true`,
`strategy_scored=false` and `reserved_strategy_inputs_opened=false`. The native
run authenticated the private acquisition and original SPY inputs before
publishing the registration. The pre-registration readiness fields remain a
diagnostic snapshot; its nested `registration` record and the separate study
manifest identify the completed registration.

| Registration evidence | Observed result |
| --- | --- |
| Historical panel | Five instruments, 2016-2025; 2,514 exchange dates and 12,570 symbol-days |
| Operational development coverage | Seven qualified years, 2017-2023, under the registered input amendment |
| Parent complete-grid assessment | Four qualified years; the original failed assessment is retained |
| Canonical corporate actions | 241 issuer cash events; six provider conflict records retained; no action blockers |
| Split coverage | Zero returned split events under the declared secondary-feed cross-check assumption |
| Original SPY comparison | 496 development windows plus 256 calibration/supplement windows; all 202,103 original bars present and unchanged |
| Eligibility masks | 12,570 rows; exact original missing flags retained; three unavailable closes remain NA with daily history resets |
| Native build | MSVC 19.51.36260, Release |
| Expanded research | No fitted model, strategy outcome or reserved-price evaluation |

The review matched the uploaded study manifest to the readiness record, both
uploaded artifact hashes, all three protocol/amendment identities, action
provenance, recovery identity and the report's embedded JSON. Independent
metadata checks reproduced the history/readiness rules across all 12,570 masks,
including the three close-gap resets. The raw private archive was authenticated
by the native Windows run; it was not supplied for a new independent raw-price
replay during this registration review. The earlier independent acquisition
review remains tied to the same unchanged acquisition-manifest identity.

| Registered artifact | SHA-256 |
| --- | --- |
| Study manifest | `edeee4275c9a01df3859f6c5090073c2b496df31e625e3bf6eed79bee7a8715c` |
| Readiness summary | `bd224533f1d84023c7e35a2b556577bf23735f4b3cddba7cfc89abe210f9f81b` |
| Vintage bridge | `58c63e9e7a96235bf92c02e41da7890b9758b68beec9ff6f00dd362da9563b2b` |
| Eligibility masks | `29413ab872c317cbef3724e7bdf25e5000a3c12ee536163ddc6592168129c173` |
| Canonical accounting actions | `f61d80f8908f0c5436002ee75de9ee464ec3bf48b08cba6dc7e1ff6c96de1558` |
| Issuer/source reference | `56a3c07e114239e029b521d7997492c329d0c9c7ce86c776af135b21bb848867` |
| Windows build-source inventory | `d5f8e21919f6456275f4c70b7a286add91d63a6fa8a165be5543813f47a043cc` |

The source inventory is a build identity, not an independently established Git
commit. M2 is complete. M3 subsequently verified prior-only replay and original-reference
parity; M4-M6 completed shared capital, the synthetic peer and cost/execution
evaluation. Stability, reserved periods and publication remain M7-M10. Milestone counts
describe accepted work packages, not the fraction of engineering effort spent.

## M3 Windows acceptance completed

The Windows Release run used MSVC 19.51.36260 and passed all 187 native panel
checks. Original-SPY parity passed on 496 momentum dates, 476 continuation
dates, 476 reversion dates and 733 daily-risk estimator windows, using identical
original inputs and histories. The original numerical comparison tolerance was
`1e-12`.

The independent verifier at revision `670070a` passed its 31 constructed
numerical checks and the full 2,012-date, five-instrument provider replay.
It checked source observations, histories, fits, targets, arithmetic and saved
display records. Ten equilibrium differences required the condition-aware
identity check described in the [methods note](panel_replay.md). Primitive OLS
tolerances, exact fit-status comparisons and exact target comparisons remained
in force. The native implementation, registered rules and saved outcomes were
unchanged by that verifier correction.

| Instrument | Calendar records, 2016-2023 | Development dates, 2017-2023 | Common operational development dates | Prior-only daily-risk forecasts |
| --- | --- | --- | --- | --- |
| SPY | 2,012 | 1,760 | 1,682 | 1,506 |
| QQQ | 2,012 | 1,760 | 1,682 | 1,759 |
| IWM | 2,012 | 1,760 | 1,682 | 1,506 |
| TLT | 2,012 | 1,760 | 1,682 | 1,759 |
| GLD | 2,012 | 1,760 | 1,682 | 1,506 |

The 10,060 instrument-date records occupy five JSONL streams totaling
117,376,080 bytes. The lower forecast counts for SPY, IWM and GLD follow the
registered consecutive-history resets after their unavailable 2019 closing
mark. Model activity and economic benefit are separate from these coverage and
forecast-availability counts.

| Accepted record | SHA-256 |
| --- | --- |
| Review ZIP | `fcf75a798ee6c2098b589e4f834f4e2fa9bf27e9bcd0ff6360c9348581262d2c` |
| Replay summary | `09b716d3c94a01ea21b961698c1adbd1b6e2f3556d81e8431871a48ba486eda7` |
| HTML report | `dfe73e2588bdca29553bb9d4039d4fca30ce6a6c7931ff9cf942575b524e1a0f` |
| Reported native build-source inventory | `3312eca5d12a846d7006b34fbfbbe668f6476bf03ff52035d7aa248a2f9a4f95` |

The native source inventory identifies producer-file bytes; it is not an
independently established Git commit. The review ZIP contains the summary and
HTML, including five saved development-date target traces. Its report hash,
embedded metadata, input identities and all selector branches were checked; the
separate HTML upload matched the ZIP exactly. Full raw inputs and JSONL streams
remain local and were checked by the independent verifier on Windows.

The accepted study identity remains
`edeee4275c9a01df3859f6c5090073c2b496df31e625e3bf6eed79bee7a8715c`.
Network requests and broker events were zero. Strategy inputs for 2024 and
2025 remain unopened. Shared capital, extended daily-risk account ledgers and
simultaneous inference were not executed by this milestone. M3 is complete;
The accepted M4 shared-capital ledger supplies ownership, target netting,
aggregate limits and recovery for subsequent account integration.

## Shared-capital and broker acceptance

M4's [shared-capital acceptance](shared_capital.md#windows-acceptance) records
one global cash ledger, five inventories and fifteen owners. Windows Release
passed 182 checks in fifteen cases; 163 durable transitions were independently
reconstructed. The M6 budget-limited-order regression extended local coverage
to 190 checks in sixteen cases and 171 transitions. Original M4 hashes retain
their original counts.

M5's [independent broker acceptance](process_broker.md#windows-acceptance)
records 313 Windows checks in twelve families across twenty-one native child
processes. Independent reconstruction checked 139 broker commits, 98 client
inputs and 294 logical peer calls. Broker history and client attribution have
separate durable owners. All broker events are synthetic.

## M6 development and execution acceptance

The [cost/execution note](cost_execution.md) is the authoritative method and
result record for the frozen cost/delay grid, historical quote references,
680-case synthetic matrix and funded accounts. It retains accepted hashes,
case tables and the distinction between producer-machine raw-source verification
and compact uploaded-artifact review.

The [account acceptance](cost_execution.md#windows-account-acceptance) records
352,000 emitted risk records and 88,000 shared account days, of which 162,040 and
2,750 respectively have observed returns. All shared paths stop after 65
sessions at one-minute delay or 45 at two minutes. All active risk paths also
stop; only passive QQQ/TLT reach December 2023. Stopped accounts do not restart;
forecast-history recovery does not restore capital paths. These are material
economic coverage limits, not a seven-year shared-capital result.

M6 is complete with those qualifications. M7 must retain each comparison's
actual coverage and unavailable outcomes while assessing concentration,
stability, risk matching and uncertainty. M8-M10 remain gated as specified above.
