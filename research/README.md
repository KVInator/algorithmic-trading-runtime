# Registered research and independent references

The final [technical report](../docs/report.pdf) and
[evidence snapshot](../docs/evidence/manifest.json) consolidate methods and findings.
Protocol files are immutable pre-scoring records; their original milestone/status
wording is historical. The report describes the completed implementations and evidence.

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
superiority. Quote-reference, account/execution and stability comparisons remain
open; the v1 results retain their original sample and identities.

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

The 2022-2023 period remains inspected development data, with prior pilot,
research and literature exposure. Local 2024-2025 strategy prices and candidate
performance remain unopened. Failed gates authorize neither a new search nor
later-period access. Full historical reproduction requires the retained
acquisition and corporate-action evidence described in the [runbook](../docs/running.md).
