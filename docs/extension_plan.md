# Study design and evidence

The completed study covers prior-only research, funded accounting, synthetic
execution and frozen retrospective evaluation across five instruments. The
[report](report.pdf) presents the consolidated results; detailed numerical and
operational contracts remain beside their implementations. The registered
original SPY study and its release retain their original identities.

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

- Fixed universe: SPY, QQQ, IWM, TLT and GLD. Equity exposures are correlated;
  five instruments do not imply five independent markets.
- Archive: 2016-2025. Features initialize from available earlier observations;
  development is 2017-2023, validation is 2024 and final evaluation is 2025.
- Three intraday directional owners per symbol share one whole-share USD book.
  Daily-risk allocation is a separate fractional-account experiment.
- Existing signal, account and matching engines remain authoritative. Independent
  Python numerical oracles are retained for verification.
- Costs, delays, control accounts, fourteen secondary cases and six primary
  endpoints are registered before later-period evaluation. Missing evidence,
  stopped ledgers and failed screens remain in the output.
- Broker events are synthetic. Historical Alpaca acquisition is retained; paid
  feeds, latest-quote probes and actual broker orders are outside the scope.

## Research exposure and evaluation roles

The exposure inventory records positive evidence and unknowns separately.
An absence of recorded inspection does not establish an untouched sample.

| Instrument / period | Evidence available at M0 | Treatment |
| --- | --- | --- |
| SPY, March 2022 and 2023-01-03 | Engineering pilot and diagnostic explicitly declared in the momentum protocol | Inspected; the diagnostic retains its v1 performance exclusion |
| SPY, 2022-2023 | Completed development results for all four studies | Selection-exposed reference; never relabel as held out |
| SPY, 2020 boundary / 2021 | Daily-risk calibration closes, warm-up and action evidence are registered | Previously used inputs; not untouched transfer evidence |
| SPY, 2024-2025 | The original protocol excluded later-year selection; the extension later opened them under its registered validation/final sequence | Frozen retrospective evaluation; no proof that raw prices were never viewed |
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

## Registered inputs and source resolution

The registered study manifest is
`edeee4275c9a01df3859f6c5090073c2b496df31e625e3bf6eed79bee7a8715c`.
Its acquisition archive has 50,350 retained HTTP attempts over 120 months and
2,514 exchange sessions. The input/action policies and source amendments are
build-pinned alongside the unchanged parent protocol. No parent failed gate is
relabeled as passed under its original definition.

### Coverage and operational masks

The original complete-minute intersection qualifies only four development years
under the five-year requirement. High aggregate minute coverage is distinct from
complete common dates. The [annual input table](evidence/m2_coverage.csv) retains
that assessment. The [input amendment](../research/extension_input_policy.md)
keeps the numerical five-year/200-date/80% requirements but applies them to common
operational dates, with feature availability reported separately. It derives
33 required execution slots from registered schedules, delays and holdings.
Momentum/reversion observations and complete continuation-volume prefixes retain
their own feature rules. The [policy coverage table](evidence/m2_policy_coverage.csv)
records the changed input definition and its denominator.

### Missing closes and corporate actions

A separate recovery returned zero bars for the three missing SPY/IWM/GLD closes
on 2019-08-12. The [source resolution](../research/extension_source_resolution.json)
permits only those registered exceptions. Their daily returns remain unavailable
and consecutive close histories reset; no interpolation or return bridge is
introduced. Other required endpoint failures remain blocking.

Canonical issuer cash evidence supplies one event per symbol/ex-date. Provider
conflicts remain recorded and duplicate announcements are not summed. Payment
availability follows the first exchange session on or after the declared date.
Five full-horizon secondary event responses cross-check split absence. GLD cash
absence combines issuer, secondary and provider evidence. These are declared
cross-provider completeness assumptions, not infallibility claims. Conflicting
or nonempty split evidence remains blocking.

The original SPY snapshot and a provider-vintage bridge retain their independent
identities. The extension cannot overwrite original two-year results. The
[precision record](evidence/m2_precision.csv) describes the controlled planning
experiment; it is not the later full-family calibration.

## Evaluation and inference

Development replays, costs and continuously funded accounts retain each path's
actual coverage. Required missing execution marks stop capital paths; recovery
of a forecast history cannot restart them. The [cost/account note](cost_execution.md)
and [stability specification](stability.md) define accounting, concentration,
prior-only labels, sensitivity and funded exposure matching.

The controlled family calibration runs 20,000 outer trials, detects size excess
in 19/20 case/block cells and withholds supported nominal simultaneous-confidence
claims. It does not tune another bootstrap procedure after seeing that result.
Validation/final capital starts fresh, while model histories warm from earlier
observations. Forty-five unit references support fifty funded matched accounts.
The complete endpoint family remains present when a member is unavailable.

The original M8 executable could not parse a payable date outside the price
calendar. Its replacement reproduced all 26 financial artifact identities and
four 2024 scientific summaries before a new freeze authorized final evaluation.
Original and replacement freezes, validation records and calibration remain local.
The [validation record](validation.md) owns accepted hashes, the maintenance
lineage and final results. The corrected date parser admits payment metadata,
not 2026 prices or market sessions.

## Milestone order and acceptance

All eleven registered work packages are complete. This counts scope acceptance,
not economic success or a measured fraction of engineering time.

| Work package | Retained completion evidence |
| --- | --- |
| M0: baseline and exposure inventory | Original release/source identities and retrospective-study declarations |
| M1: free historical feasibility | Five-symbol capability and coverage records; no paid access |
| M2: inputs and contract | Registered raw/archive identities, masks, actions, source amendments and vintage bridge |
| M3: causal replay | 10,060 development symbol/date records; 187 native acquisition/replay checks and independent reconstruction |
| M4: shared capital | Windows acceptance: 182 checks in fifteen cases; exact global/owner accounting and 163 durable transitions |
| M5: independent synthetic broker | Windows acceptance: 313 checks, twelve families and 21 native child processes |
| M6: cost and execution research | Historical quote references, 400 cost/delay series, 680 synthetic cases, funded risk/shared accounts |
| M7: stability and exposure comparisons | Primary attribution, fourteen sensitivities, concentration and fifty funded matched accounts |
| M8: calibration and validation | Full controlled calibration, 252-date Windows validation and verified executable freeze |
| M9: frozen final evaluation | 250-date Windows final run, complete economic cases, independent receipt and derived-bundle review |
| M10: publication and cleanup | Consolidated report/figures, source-bound evidence, reproducible commands and final code/document review |

Economic conclusions retain their failed or unavailable assessments. No setting
is selected or advanced from final results. The archived Release benchmark
measures the original single-symbol workload; no expanded-runtime performance
claim is inferred. Source, publication and original frozen-run identities remain
distinct. Full local raw-source verification and remote derived-artifact review
are explicitly separated in the acceptance records.
