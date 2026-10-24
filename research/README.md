# Research specifications and evidence

The [completion plan](../docs/completion_plan.md) governs active remaining work.
This index separates registered experiments, implemented proposals and dated
selection history. Earlier briefs retain their original wording and bytes;
their recommendations do not supersede the current plan.

## Registered development studies

| Study | Governing record | Finding |
| --- | --- | --- |
| SPY-IM-01 | [Momentum protocol](intraday_momentum_protocol.md) and [data policy](development_data_policy.json) | `no_gross_edge`; no advancement |
| SPY-TOD-01 | [Continuation protocol](intraday_continuation_protocol.md) | Both variants fail; incremental volume benefit is unproven |
| SPY-RISK-01 revision 1 | [Protocol](daily_risk_protocol.md), [settings](daily_risk_protocol.json) and [result](daily_risk_development_result.md) | Forty account cases complete; no advancement |

The baseline report retained at
`scratch/runs/SPY_IM_01_development_2022-2023_0/summary.json` contains 268 active
trades and the following fixed cases:

| Delay and per-side cost | Compounded return | Daily-close maximum drawdown |
| --- | ---: | ---: |
| One minute; zero cost | -2.8207% | 7.8018% |
| One minute; 1 bp | -7.8924% | 11.8412% |
| Two minutes; 1 bp | -9.8039% | 13.5161% |

Primary annual returns are -3.1999% in 2022 and -4.8475% in 2023. Both
five-/ten-session bootstrap mean-return intervals include zero. These comparisons
use continuous notional, delayed minute opens and hypothetical costs. The
independent baseline arithmetic/bootstrap reference remains locally under
`scratch/verification/`, outside source distribution.

[Continuation diagnostics](continuation_diagnostics.md) retain post-result
exposure/cost attribution, common-date winner removal, month omissions,
scheduled-release groups and entry context. They do not change registered rules.
The [scheduled-release reference](scheduled_releases_2022_2023.csv) retains sources.

The daily-risk result records exact metrics, gates, artifact identities,
independent checks and the explained native/local source-fingerprint difference.
Its primary intraday-risk CER increment over daily-only is -1.5430 percentage
points. Point forecast-loss improvement fails registered uncertainty requirements.

## Registered reversion design

[SPY-MR-01 revision 1](intraday_reversion_protocol.md) and its
[settings](intraday_reversion_protocol.json) are registered before historical
fitting/scoring. The [synthetic specification](reversion_validation.md) and
[evidence](reversion_validation_result.md) quantify finite-sample error,
observation/anchor noise, false OU admissibility and range undercoverage.
Causal measurements, pooled OLS, whole-session ranges and input coverage are
implemented. Prior-only history, four policy accounts, cash reference and paired historical
comparison are implemented. The [implementation and development evidence](../docs/reversion_development.md)
records reproduction, gates and native verification status. Parameter ranges are descriptive and cannot filter
entries or establish stationarity. The [original brief](intraday_reversion_brief.md)
remains unchanged as selection history.

## Execution integration

The [strategy execution contract](../docs/strategy_execution.md) connects these
fixed research engines to whole-share orders, journals, reconciliation and a
saved-event viewer. Its constructed account comparisons and first-date reference
replay are separate from the registered historical strategy findings.
`verify_strategy_execution.py` checks the exported accounting, signals and
journal/frame evidence independently of the C++ engines.

## Selection and prerequisite history

- [Candidate comparison](strategy_candidates.md): dated opening-range/daily-risk comparison.
- [Opening-range brief](opening_range_continuation_brief.md): retained proposal outside active remaining strategy scope.
- [Intraday-risk allocation brief](intraday_risk_allocation_brief.md): rationale subsequently governed by the registered daily-risk protocol.
- [Daily-risk preflight](daily_risk_preflight.md): input/accounting prerequisites and bounded acquisition.
- [Issuer distribution reference](spy_distribution_reference.json): reconciliation reference; extraction/verification scripts remain separate from authoritative C++ engines.

The 2022-2023 sample remains inspected development data. These records do not
authorize access to local 2024-2025 prices or candidate performance. A new design
does not create an untouched test from previously inspected observations.
