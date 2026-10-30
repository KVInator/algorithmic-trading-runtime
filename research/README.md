# Registered research inputs

The financial implementation is C++20 under `src/`. This directory retains the
frozen study records, issuer-data preparation and one publication builder.
Protocol files are immutable pre-scoring records; historical milestone/status
wording remains part of their provenance. Original research findings and raw-run
identities are unchanged.

| Study | Governing records | Recorded finding |
| --- | --- | --- |
| SPY-IM-01 revision 2 | [Protocol](intraday_momentum_protocol.md), [data policy](development_data_policy.json) | No demonstrated gross edge; primary net -7.8924% |
| SPY-TOD-01 revision 1 | [Protocol](intraday_continuation_protocol.md), [descriptive diagnostics](continuation_diagnostics.md) | Positive volume-variant point return, failed registered screens; volume increment unconfirmed |
| SPY-MR-01 revision 1 | [Protocol](intraday_reversion_protocol.md), [settings](intraday_reversion_protocol.json), [development result](../docs/reversion_development.md) | Cost-dominated; primary net -0.4171%; 83 active dates |
| SPY-RISK-01 revision 1 | [Protocol](daily_risk_protocol.md), [settings](daily_risk_protocol.json), [result](daily_risk_development_result.json) | No advancement; primary CER increment over daily-only -1.5430 pp/year |
| MULTI-ASSET-01 edition 1 and INPUT-01 | [Dataset/study contract](extension_protocol.md), [registered settings](extension_protocol.json), [input amendment](extension_input_policy.md) | M2 registration and M3 causal replay complete; 10,060 pre-2024 instrument-date records independently verified on Windows; original failed complete-grid assessment and explicit amendments retained |

## Retained Python utilities

| File | Responsibility |
| --- | --- |
| `prepare_panel_actions.py` | Prepare and retain canonical issuer/secondary action evidence |
| `extract_distribution_reference.py` | Issuer-workbook parsing shared by action preparation |
| `build_report.py` | Render recorded evidence into HTML, PDF and figures |
| `verify_publication.py` | Check distributed hashes, tables, links and static-view boundaries |

The root `build.py` discovers and builds the native applications. None of these
utilities implements a production signal, trading account or broker engine.
Publication dependencies remain pinned in `report_requirements.txt`.

## Independent numerical verification

The full completed suite, including source authentication, Decimal/integer ledger
oracles, causal replay, uncertainty, controlled calibration and fault mutations,
is preserved at [commit 409508fe3e90550052dc691147ffe4834b13089a](https://github.com/KVInator/algorithmic-trading-runtime/tree/409508fe3e90550052dc691147ffe4834b13089a/research).
It is removed from the current application tree, not from project history.

The [runbook](../docs/running.md#scientific-verification) gives a detached-checkout
command and runnable paths for all verifiers. Archived helper modules remain
with their callers. No numerical reference is rewritten, weakened or replaced
with the C++ implementation under test. The pinned original report remains
reproducible from that complete revision. New native builds keep the same frozen
statistical core and registered financial rules; source-inventory hashes change
when the active source file set changes.
