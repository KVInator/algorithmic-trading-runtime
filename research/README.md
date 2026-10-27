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
| MULTI-ASSET-01 edition 1 and INPUT-01 | [Dataset/study contract](extension_protocol.md), [registered settings](extension_protocol.json), [input amendment](extension_input_policy.md) | Parent complete-grid assessment retained; operational/feature masks and exact-time mark recovery implemented; action evidence and final registration pending |

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
workbook reader and extracts the Invesco/iShares cash tables; it leaves
unverified GLD/split coverage incomplete. `extension_precision.py` supplies a
controlled analytical/Monte Carlo planning reference, separate from market
outcomes and final block-bootstrap validation.

The 2022-2023 period remains inspected development data, with prior pilot,
research and literature exposure. Local 2024-2025 strategy prices and candidate
performance remain unopened. Failed gates authorize neither a new search nor
later-period access. Full historical reproduction requires the retained
acquisition and corporate-action evidence described in the [runbook](../docs/running.md).
