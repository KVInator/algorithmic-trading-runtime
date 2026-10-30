# Registered research and independent references

The published v1 [technical report](../docs/report.pdf) and
[evidence snapshot](../docs/evidence/manifest.json) consolidate the original SPY
study. Current five-instrument development results are recorded in the
[cost/execution note](../docs/cost_execution.md) and
[extension roadmap](../docs/extension_plan.md).
Protocol files are immutable pre-scoring records; their original milestone/status
wording is historical. They are retained as governing research evidence.

| Study | Governing records | Recorded finding |
| --- | --- | --- |
| SPY-IM-01 revision 2 | [Protocol](intraday_momentum_protocol.md), [data policy](development_data_policy.json) | No demonstrated gross edge; primary net -7.8924% |
| SPY-TOD-01 revision 1 | [Protocol](intraday_continuation_protocol.md), [descriptive diagnostics](continuation_diagnostics.md) | Positive volume-variant point return, failed registered screens; volume increment unconfirmed |
| SPY-MR-01 revision 1 | [Protocol](intraday_reversion_protocol.md), [settings](intraday_reversion_protocol.json), [development result](../docs/reversion_development.md) | Cost-dominated; primary net -0.4171%; 83 active dates |
| SPY-RISK-01 revision 1 | [Protocol](daily_risk_protocol.md), [settings](daily_risk_protocol.json), [result](daily_risk_development_result.json) | No advancement; primary CER increment over daily-only -1.5430 pp/year |
| MULTI-ASSET-01 edition 1 and INPUT-01 | [Dataset/study contract](extension_protocol.md), [registered settings](extension_protocol.json), [input amendment](extension_input_policy.md) | M2 registration and M3 causal replay complete; 10,060 pre-2024 instrument-date records independently verified on Windows; original failed complete-grid assessment and explicit amendments retained |

The reversion [synthetic design](reversion_validation.md) and
[sampling/noise findings](reversion_validation_result.md) explain why numerical OU
admissibility and descriptive parameter ranges cannot establish a latent model.
The [original reversion proposal](intraday_reversion_brief.md) remains because the
frozen protocol cites it as prior design exposure; its proposals do not override
the registered implementation. Other superseded selection briefs are in Git history.

## Independent checks

`verify_project_demo.py` composes the saved-strategy and simulated-broker oracles.
The other `verify_*.py` entry points check their corresponding historical study,
synthetic validation, exact paper bridge, raw transport capture, execution
measurement or Release benchmark outputs. They preserve Decimal accounting/fits,
explicit bootstrap mappings, quote budgets, clocks and artifact identities where
applicable. They are references, not alternate production engines.

`build_report.py` reads the curated snapshot and produces the standalone HTML,
PDF and figures. `verify_publication.py` checks publication/evidence identities,
links and embedded-view boundaries. Publication dependencies are pinned in
`report_requirements.txt`. `extract_distribution_reference.py` supports the
[issuer-distribution reference](spy_distribution_reference.json) used by the
registered risk accounting audit.

`verify_historical_panel.py` authenticates the extension archive and produces
price-free coverage evidence. `prepare_panel_actions.py` reuses the issuer
workbook reader and extracts the Invesco/iShares cash tables. It retains a separate
resolved reference using issuer cash authority, GLD issuer evidence and full-horizon
secondary event responses. The source-resolution policy and original failure
assessment remain separately identified. `extension_precision.py` supplies a
controlled analytical/Monte Carlo planning reference, separate from market
outcomes and final block-bootstrap validation.

`verify_panel_replay.py` independently reconstructs the M3 primary features,
fixed-slot histories, pooled AR(1) fit, cash/long targets, delayed reference
returns and lagged risk estimates. Its historical route authenticates source
pages through the existing panel oracle and rejects reserved-year access.
The analytic fixture is explicitly synthetic. Replay methods and boundaries
are described in [causal panel replay](../docs/panel_replay.md).
`PanelReplayFixture` supplies one native analytic observation stream to the
causal replay and M6 checks; the Python oracle reconstructs it independently.
`CostDelayDevelopment` and `PanelAccountDevelopment` compose registered inputs,
the existing numerical/accounting owners and result-bound reports. Their
executables only select the constructed or authenticated-data route and print
the resulting paths.

`verify_shared_capital.py` reconstructs the schema-4 global cash ledger, owner
attribution, exact fill/fee allocations, admission budgets and linked recovery
segments using Python integers. It also checks constructed primary-engine
features and actual-inventory adapter decisions. The self-check option rejects
deliberate semantic corruption without relying on checksums. The
[shared-capital note](../docs/shared_capital.md) defines the accounting contract
and its synthetic scope.

`synthetic_broker_protocol.md` registers the independent peer's account, fees,
cash availability, lifecycle, process framing and recovery bounds before its
acceptance run. `verify_process_broker.py` independently rebuilds FIFO matching,
quote consumption, broker cash/inventory, terminal history and paged reads,
then checks shared-client attribution against those independently generated
reports. Its semantic checks deliberately alter fees, liquidity, lifecycle,
schema and owner attribution without relying on checksums. Process methods and
acceptance commands are in [independent broker process](../docs/process_broker.md).

`cost_execution_protocol.md` registers the M6 cost/delay grid, bounded old SIP
quote sample, synthetic execution matrix and continuous-account missing-price
rule. `verify_cost_quotes.py` authenticates every retained response and page
continuation, reconstructs the fixed sample from the accepted input masks,
and independently checks latest-quote eligibility, units, age and spreads.
It also verifies the static report's coverage cells, status bars, scatter
coordinates and complete window tables. `--report-only` checks those saved
representations without accessing or authenticating the raw response archive.
For cached condition diagnostics, the full verifier reconstructs latest-record
condition/tape frequencies and price/size/metadata conflicts from those same
raw receipts; the primary classifications must remain unchanged.
`verify_cost_delay.py` reuses the independent M3 source/feature/history checks,
then reconstructs both execution delays and the five charges with 50-digit
Decimal cash/share arithmetic. It verifies all 400 instrument/policy cases,
240 paired comparisons and static report curves/tables. Its `--self-check`
rejects semantic corruption independently of hashes. The native evaluator
changes execution references and accounting without refitting signals.
The [M6 implementation note](../docs/cost_execution.md#windows-costdelay-acceptance)
records accepted Windows development results: all 400 series and 240 contrasts
use 1,682 common dates per instrument, with full source/Decimal verification on
the producing machine and separate compact review. Positive paired point
estimates remain distinct from candidate profitability and statistical
superiority. The [quote-reference comparison](../docs/cost_execution.md#windows-quote-reference-acceptance)
passed Windows raw-archive verification and separate uploaded-artifact review.
Continuous/shared accounts have the separate accepted evaluator below;
M7 primary diagnostics have Windows acceptance. `StabilityExtensionDevelopment`
implements the fourteen registered one-factor variants and 50 funded risk-matched
accounts, with 45 genuine unit accounts for prior volatility sizing. Its independent
`verify_stability_extension.py` oracle authenticates source observations, configurable
strategy decisions, Decimal cash/fee ledgers, 252-slot sizing, attribution, joint
uncertainty and static plot marks. Windows constructed/historical execution and
independent uploaded-evidence review completed on 30 October 2026. M7 is complete
with input-limited matched comparisons and no strategy advancement;
[the acceptance record](../docs/stability.md#windows-secondary-and-risk-matching-acceptance)
binds all 345 period summaries and 24 endpoint/block records to the historical
summary. The v1 results retain their original sample and identities.

`verify_cost_execution.py` independently rebuilds all 680 registered synthetic
cases, broker matching, client receipt-time accounting and fee-inclusive
shortfall. It reuses the M5 broker oracle and checks 510 paired contrasts,
policy denominators and all static heatmap values. Its self-check rejects
semantic corruption without relying on artifact hashes. The study is in-process
engineering evidence; it does not load historical strategy prices or extend
the M5 process/recovery claim. Windows Release acceptance and full-bundle
independent verification passed on 29 October 2026; all 680 native case traces
match the GNU reference byte for byte. The
[acceptance record](../docs/cost_execution.md#windows-synthetic-execution-acceptance)
retains the policy results and evidence identities.

`verify_panel_accounts.py` authenticates the executable account plan without
rewriting its native number tokens, then reuses the M3 causal source/history
oracle. It reconstructs the 200 fractional risk accounts with 50-digit Decimal
cash/share/receivable arithmetic and the 50 shared-capital accounts through the
independent M4 integer ownership/admission oracle. Expected event clocks,
missing-reference stops, exact owner attribution, all 190 common-prefix
contrasts, forecast coverage, 25 static curves and 279 table rows are verified.
Its self-check rejects twelve semantic mutations independently of artifact
hashes. Constructed bundles include generated daily paths; provider bundles
contain summary, report and plan only, with full source authentication performed
locally against the retained registered panel. No 2024-2025 strategy observations
are opened by this evaluator. Its [methodology and acceptance scope](../docs/cost_execution.md#continuous-risk-and-shared-capital-accounts)
keep forecasts, continuously funded accounts and conditional reference returns
distinct. Windows historical verification and uploaded-output review completed
on 29 October 2026. Of 352,000 risk records and 88,000 shared account days,
162,040 and 2,750 respectively have observed returns; the others retain explicit
unavailability. The [account acceptance record](../docs/cost_execution.md#windows-account-acceptance)
binds the curated 250-case and 190-contrast tables, stop coverage and primary
point estimates to their historical summary. M6 is complete with those coverage
qualifications; no full-horizon shared-capital performance is inferred.

The 2022-2023 period remains inspected development data, with prior pilot,
research and literature exposure. Local 2024-2025 strategy prices and candidate
performance remain unopened. Failed gates authorize neither a new search nor
later-period access. Full historical reproduction requires the retained
acquisition and corporate-action evidence described in the [runbook](../docs/running.md).

`verify_stability.py` is the standard-library oracle for primary saved-export
stability analysis. It authenticates the original M6 records when available and
independently recomputes causal conditions, pooling, concentration and all four
10,000-draw block analyses, including fixed original-sample HAC studentizers and
the six-endpoint max-T family. Static report marks are checked against those
results. Its shared `block_sampling.py` primitive also supplies the existing
reversion oracle; production calculation remains C++. See
[stability methods](../docs/stability.md) and [commands](../docs/running.md#primary-stability-and-uncertainty-m7).

## Registered validation (M8)

`inference_calibration` reuses the registered full-family kernel on ten controlled
stationary Gaussian AR(1) scenarios. `validation_protocol.json` fixes outer trials,
resampling, truth, mask, effect shifts and failure treatment. The independent
oracle reconstructs every outer sample, estimate, studentizer, bound and aggregate,
and fully replays first/last trials per case/block, forty full bootstrap references.
Other native critical values are authenticated outputs, not independently rerun
laws. Native sampling-index caching leaves every trial's moments and covariance
independent. The shared deterministic normal generator preserves the existing
reversion simulation mapping.

Full GNU Release calibration has independent acceptance: 20,000 outer families,
forty reference bootstrap laws and six semantic corruptions. Size excess appears
in 19 of twenty cells. The [acceptance record](../docs/validation.md#controlled-calibration-acceptance-gnu-release)
retains every cell and the verified native-coordinate SVG. The procedure stays
fixed; supported inference claims are withheld. This is not Windows/provider
2024 acceptance.

`panel_evaluation` composes the existing primary, secondary, continuous risk and
shared-capital engines. Its typed capability binds origin and year, while default
readers retain their pre-2024 boundary. Provider validation requires the full
independent calibration receipt. Features warm from 2016; 2024 economic accounts
start fresh, and unit references start exactly 252 prior exchange sessions.
Stopped ledgers never restart. All fourteen variants remain present without
selection. The last forecast stays unavailable if its target would require 2025.

The analytic check uses all 2022-2024 exchange sessions, the existing bar formula
and four missing-minute faults, with QQQ cash events at source slots 268 and 600
paid three sessions later. Both measurements and ledgers consume the declared
actions. `verify_panel_evaluation.py` independently reconstructs sources, histories,
Decimal fractional ledgers, integer shared accounts, sizing, concentration and
joint inference under the same explicit cohort. Its parent report links four
complete child reports rather than replacing their evidence with summary counts.

A verified 2024 evaluation freezes a copy of the exact runner with source,
registration, calibration and validation hashes. The explicit final branch is
already included so the binary can be frozen before M9; it is not run during M8.
Constructed receipts cannot authorize provider 2025 prices. See the [methods](../docs/validation.md)
and [guarded M8 commands](../docs/running.md#integrated-validation-and-freeze-m8).
