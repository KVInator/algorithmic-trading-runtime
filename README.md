# Algorithmic Trading Runtime

C++20 market-data ingestion, SPY strategy research and a deterministic execution runtime.
The implemented workflow acquires Alpaca SIP minute bars, preserves the original
responses, verifies saved inputs and evaluates fixed intraday trading rules
with delayed price references, explicit costs and block-bootstrap uncertainty.
The [execution core](docs/execution_runtime.md) separately checks order lifecycle,
cash and whole-share positions under partial fills, duplicate messages,
cancellation races, disconnects and exact broker-snapshot reconciliation.

The baseline research design and continuation gates are defined in the
[SPY-IM-01 protocol](research/intraday_momentum_protocol.md). The
[development data policy](research/development_data_policy.json) records its
revision-2 data exclusion. No validation or final-period strategy evaluation is
implemented in the current workflow.

The continuation candidate is [SPY-TOD-01](research/intraday_continuation_protocol.md):
time-of-day continuation with an explicit price-only versus volume-conditioned
comparison. Its 20-session causal feature history, half-hour state machine and
delayed replay feed a paired historical study with twenty fixed cost/delay cases,
annual returns, exposure and turnover, winner concentration, daily-close drawdown
and shared-session moving-block uncertainty. Results are generated as HTML/SVG,
CSV and JSON; the primary volume-conditioned variant retains a separate
incremental-information comparison with price-only.

## Project layout

| Directory | Responsibility |
| --- | --- |
| `apps/` | Acquisition, offline finalization and study entry points |
| `apps/diagnostics/` | Optional live gap investigation |
| `src/market/` | Minute-bar invariants, UTC timestamps and session calendar |
| `src/accounting/` | Fractional long/cash account, corporate actions and chronological ledger |
| `src/execution/` | Limit-order lifecycle, integer cash/inventory, reservations and synthetic message replay |
| `src/alpaca/` | Request, response, JSON and rate-limit contracts |
| `src/storage/` | Raw evidence, checkpoints, integrity and dataset finalization |
| `src/research/` | Signal/replay, return statistics and moving-block bootstrap |
| `src/reporting/` | HTML/SVG study reports, daily/trade/feature tables and synthetic traces |
| `src/platform/` | Windows HTTP/credential access and platform SHA-256 backends |
| `tests/` | Synthetic contract fixtures and optional full-period integration |
| `research/` | Economic protocol and declared data policy |
| `scratch/datasets/` | Acquired data and immutable dataset evidence |
| `scratch/runs/` | Acquisition audits, diagnostic evidence and research reports |

Generated builds, test output and local experiments belong under the ignored
`scratch/` directory. Visual Studio regenerates ignored `.vs/` state. Neither is
part of source delivery. Raw datasets and run evidence are local research
artifacts and remain excluded from version control.

## Visual Studio workflow

Requirements: Visual Studio 2026 with MSVC, a Windows SDK and C++ CMake tools.
CMake pins nlohmann/json 3.12.0 by SHA-256. The first clean configuration needs
internet access to obtain that dependency. The core uses C++20; project warnings
are treated as errors.

1. Open the project folder with **File > Open > Folder**.
2. Select configure preset **windows-x64** and build preset **windows-debug**.
3. Select **Build > Build All**.
4. Select **Test > Run Test Preset > windows-debug** for seventeen routine groups.
5. Select **continuation_diagnostic.exe**, then **Ctrl+F5**.
6. Open the printed `trace.html` path for the price/volume plots and paired
   decision table. `decisions.csv` retains unrounded features and UTC clocks.

The diagnostic uses constructed observations, requires no API credentials and
does not score historical strategy performance. Its chart compares the signal
with the same-clock historical scale and shows the volume entry condition.

For the [daily-risk feasibility checkpoint](research/daily_risk_preflight.md),
select **risk_preflight.exe** and **Ctrl+F5** after the routine tests pass. It
verifies the frozen development inputs, audits all 501 exchange sessions and
checks the new long/cash account against constructed dividend, payment and split
identities. On Windows it also makes three bounded read-only corporate-action
access requests using the existing credential store. It writes an HTML coverage
chart, accounting tables, CSV evidence and a hashed JSON manifest. Open the
printed `report.html` path. The current protocol is registered separately;
`input_audit_only` identifies this target's coverage/accounting scope. Missing
references remain explicit. This target does not score a strategy or alter inputs.

For the [bounded risk-input acquisition](research/daily_risk_preflight.md#bounded-input-acquisition),
select **risk_acquire.exe** and **Ctrl+F5**. It acquires the additional calibration
and shortened-session references into a separate dataset, paginates corporate
actions through the early-2024 processing boundary and compares cash dividends
with the retained issuer reference. Completed evidence is verified and reused on
restart. Open the printed `report.html`; input gaps and unresolved conventions
remain explicit. This target does not request 2024–2025 prices or strategy returns.

For the [registered daily-risk experiment](research/daily_risk_protocol.md),
select **risk_development.exe** and **Ctrl+F5** after the routine tests pass.
The completed `risk_acquire` dataset and existing frozen development snapshot
are required; acquisition does not need repeating. This offline target verifies
the registered protocol, acquisition and issuer identities before calculating
forecasts. The retained 2023-01-03 diagnostic bars must also match the completed
coverage audit. It evaluates all 501 exchange sessions, preserving the known
June 2023 gap through the registered daily-only fallback. Local 2024–2025 prices
and results remain unopened.

The four accounts are intraday risk, daily-only risk, a fixed 2021-calibrated risk
budget and passive equity. Each has five cost cases and two execution delays.
The shared account core handles ex-date claims, actual payment dates,
post-cost liquid targets and terminal liquidation. The runner reports its
registered screens, rather than treating successful execution as advancement.
The immutable acquisition inventory retains its original registration-time note;
the registered study separately verifies that required evidence is complete.

For the next runtime milestone, select **execution_diagnostic.exe** and
**Ctrl+F5** after building and running the routine tests. It needs no dataset or
credentials. Six constructed broker-message schedules check 69 transitions,
final integer balances and identical traces on repeated replay. Open the printed
`report.html`; expected quarantined rows identify injected faults, retained
reservations and the entry gate. The accompanying input journal, CSV trace and
hashed manifest retain the evidence. A per-file source inventory accompanies the
compiled aggregate fingerprint. The [runtime contracts and limits](docs/execution_runtime.md)
distinguish this in-memory execution stage from durable recovery, broker transport
and strategy integration, which remain pending.

Each `scratch/runs/SPY_RISK_01_development_2022-2023_*` directory contains:

| File | Evidence |
| --- | --- |
| `report.html` | Equity, drawdown, exposure, annual comparisons, forty cost/delay cases, forecast losses, uncertainty and gates |
| `measurements.csv` | 2021 calibration and development daily2, RV1, RV5 and RV15, with missing partitions blank |
| `features.csv` | Prior twenty-session histories, availability, fallback, targets and execution-aligned future labels |
| `daily_accounts.csv` | Forty complete paths, overnight/intraday attribution, fees, turnover, cash and receivables |
| `account_ledger.csv` | Chronological marks, corporate-action events, morning rebalances and terminal sales |
| `summary.json` | All-date/year statistics, paired block intervals, omissions, gates, source/build and input/artifact identities |

An independent standard-library arithmetic reference can check a generated run:
`python research/verify_daily_risk_report.py <report-directory>`. It checks the
exports' identities, lagged estimates, masks, CER, drawdown, tails, exposure and
costs. The synthetic contracts exercise a full calendar but do not establish a
historical investment result. Historical candidate scoring must run against the
retained acquisition evidence; no historical score is committed with the engine.

For the historical continuation study, select **continuation_development.exe**
and **Ctrl+F5** after the routine tests pass. This offline target verifies the
existing frozen snapshot and replays 476 scored sessions after twenty warm-up
sessions. No data acquisition or finalization rerun is required when that
snapshot is already present. Progress covers verification, replay and both
10,000-resample bootstraps. Open the printed `report.html` path in a browser.

Each new `scratch/runs/SPY_TOD_01_development_2022-2023_*` directory contains:

| File | Evidence |
| --- | --- |
| `report.html` | Equity, daily-close drawdown, cost/delay curves, annual tables, clock groups, uncertainty and gates |
| `daily_returns.csv` | All scored dates and twenty cases, including cash days, exposure and turnover |
| `features.csv` | Prior-history identities, completed features, paired actions and both delayed forward windows |
| `trades.csv` | Completed strategy round trips with decisions, reference clocks/prices and costs |
| `removed_exposure.csv` | Shared trades, delayed entries and skipped trades with additive log-return attribution |
| `opportunity_diagnostics.csv` | Calendar labels, normalized price moves and completed-prefix realized volatility |
| `entry_timing.csv` | Decision-to-reference gaps and explicitly ex-post entry-minute context |
| `scheduled_releases.csv` | Complete 2022-2023 CPI, employment and FOMC statement calendar with official sources |
| `summary.json` | Full statistics, bootstrap configuration, screens, input/feature/source hashes and build identity |

The first twenty eligible sessions are never counted as zero-return days. Both
variants and the always-long control use the same scored dates. Bootstrap indices
resample their complete paired session records; missing diagnostic groups remain
explicitly undefined. Source artifacts, raw pages and the frozen study manifest
are read-only. This target does not access 2024-2025 data or submit orders.

The same report includes [development diagnostics](research/continuation_diagnostics.md):
exact exposure and cost attribution, common-date winner removal, monthly omission
tests, scheduled-release groups, price/risk covariates and entry timing. These are
post-result descriptive analyses. They do not revise strategy gates or authorize
later-period evaluation. `summary.json` schema version 2 retains the diagnostic
statistics and the hashes of all eight report artifacts.

The [strategy candidate comparison](research/strategy_candidates.md) evaluates
opening-range continuation and daily exposure informed by intraday risk. The
daily-risk candidate is now registered in revision 1 and implemented by
`risk_development`; its completed development run fails the registered
advancement screens. The opening-range alternative remains a proposal.

The existing baseline is run through **development_finalize.exe** followed by
**development_replay.exe**, each with **Ctrl+F5**, when offline evidence
verification or baseline reproduction is required.

`windows-debug-full` adds the 497-session synthetic disk integration test. It is
optional for routine changes and can take several minutes in Windows Debug.
The regular preset excludes this test. These two test presets explicitly select `Debug`.

For optimized offline runs, select build preset **windows-release**, rebuild,
then run the targets from `scratch/build/Release/` through the Visual Studio
startup-item selector. The matching **windows-release** test preset runs the
seventeen routine groups in Release. Domain and artifact validation remain enabled.

Finalization and replay are offline. Their paths are anchored to the source
folder, independently of the IDE working directory. Repeated finalization
verifies the existing frozen study without overwriting it. Each replay writes
a new report directory. Both apps print a flushed startup message before
verification, elapsed-time progress for each month and the final manifest status.
Replay also reports its calculation and bootstrap stages. A month or bootstrap
stage may take time in Debug; progress messages mark work boundaries rather than
a periodic heartbeat. Run finalization to completion before starting replay.

## Acquisition and credentials

`alpaca_development.exe` collects only the fixed 2022–2023 development period.
It reuses the original March 2022 pilot and verifies calendar dates, daylight
saving conversion, all completed checkpoints and retained incomplete sessions.
Coverage gaps are inventoried while later sessions are collected. HTTP, schema,
calendar, integrity and filesystem errors stop the run. Exit code 2 means the
collection audit finished with unresolved sessions; it does not authorize replay.

Credentials are read through Windows Credential Manager from the generic entry
`algorithmic-trading-runtime/alpaca-paper`: the user-name field holds the paper
API key and the password field its secret. A missing entry triggers hidden
console prompts. The app reads only this named credential and never writes it
to project files. HTTP uses WinHTTP with certificate checks, bounded responses,
timeouts, redirects disabled and the existing bounded HTTP-429 policy.

The optional `alpaca_gap_probe.exe` target is enabled by the CMake cache option
`TRADING_BUILD_DIAGNOSTICS=ON`. It retains diagnostic responses separately and
does not modify dataset checkpoints or data policy. Completed setup/pilot
launchers are retired; their real data and reports remain under `scratch/`.

## Frozen development sample

The original acquisition inventory contains 497 eligible sessions after three
early closes and the 2023-01-03 diagnostic fixture are excluded. One session,
2023-06-05, lacks four minute bars in repeated and targeted Alpaca responses.
Protocol revision 2 excludes that entire session before full-period evaluation.

The study contains **496 sessions and 193,440 bars**, with 250 sessions in 2022
and 246 in 2023. All five excluded dates and their reasons are reported. Missing
bars are not filled, omitted dates are not synthetic cash returns, and no feed
is substituted. The original March manifest retains SHA-256 identity
`a8d9489284efe604ab8350f945e0e5393ae769c6bc3a0350450a2328759416e5`.

`development_finalize` verifies the policy, acquisition audit, diagnostic probe,
raw responses, CSV values, calendar snapshots and selected checkpoints before
publishing `study_manifest.json` beside the original data. The original
acquisition manifest remains absent because its 497-session contract was not
met. The separate study manifest identifies the revised evaluation sample.
A changed policy, altered evidence or unexpected date blocks finalization/load.

`development_replay` verifies that study manifest and applies the unchanged ten
cost/delay cases. Its `daily_returns.csv` and `summary.json` retain input and
protocol hashes, annual results, daily-close drawdown, winner concentration,
five-/ten-day moving-block bootstrap intervals and the continuation decision.
The protocol defines every financial convention and statistic.

These results use continuous-notional delayed minute-open price proxies and
hypothetical two-sided costs. They do not establish broker fills, market impact
or executable account profit. Advancement to later evaluation follows the
predeclared gates.

## Development finding

The locally retained development report at
`scratch/runs/SPY_IM_01_development_2022-2023_0/summary.json`
contains 268 active trades. The unchanged candidate fails its development gate
with `no_gross_edge`; validation and final evaluation remain unopened.

| Fixed case | Compounded strategy return | Daily-close maximum drawdown |
| --- | ---: | ---: |
| One-minute delay, zero cost | -2.8207% | 7.8018% |
| One-minute delay, 1 bp per side | -7.8924% | 11.8412% |
| Two-minute delay, 1 bp per side | -9.8039% | 13.5161% |

Both annual primary-case returns are negative: -3.1999% in 2022 and -4.8475%
in 2023. The five-/ten-day bootstrap mean-return intervals include zero. These
observations do not support advancing this adaptation as a profitable strategy.
The report's manifest and daily-return digests identify the exact inputs and
outputs; the original engineering-pilot result is retained separately.

An optional Python standard-library reference check is retained under
`scratch/verification/`. It independently recomputes raw-price returns, period
statistics, concentration and both bootstrap calculations. Its recorded
verification identifies the same frozen manifest and daily-return digest.
The application workflow and numerical engine are C++.

## Daily-risk development finding

The completed [SPY-RISK-01 result](research/daily_risk_development_result.md)
uses all 501 development exchange sessions and forty fixed account cases. In the
primary one-minute-delay, 1 bp-per-side case:

| Account | Compounded 2022–2023 return | Daily-close maximum drawdown | Annualized CER |
| --- | ---: | ---: | ---: |
| Intraday risk | 1.7072% | 16.3524% | -0.2749% |
| Daily-only risk | 4.7624% | 13.3556% | 1.2681% |
| Static risk budget | 2.7920% | 19.1612% | -0.8472% |
| Passive equity | 2.3777% | 24.4204% | -2.5230% |

The intraday account's CER increment over daily-only is -1.5430 percentage points.
Its point QLIKE improvement is positive, but both registered confidence intervals
include zero. Annual consistency and confidence-bound requirements fail; revision
1 does not advance. The result record preserves exact metrics, artifact identities,
independent numerical checks and the native/local source-fingerprint comparison.
The subsequent per-file inventory explains that difference through eight CRLF
files and exactly reproduces the earlier native aggregate. No later-period
strategy evaluation follows from this result.

## Version control

Source commits contain the CMake configuration, application/core code, tests and
research specifications. Raw responses, local reports, credentials and generated
build state remain outside source control. The existing baseline and new
candidate have separate protocols and reviewable milestones.

Development changes use task branches in the established GitHub repository.
Visual Studio's **Git > Fetch** retrieves branch updates; checking out the
corresponding remote branch creates a local tracking branch. Subsequent updates
use **Git > Pull** on that branch. The existing local data directory remains in
place. Native MSVC builds and the routine test preset verify each milestone
before integration into the default branch.
