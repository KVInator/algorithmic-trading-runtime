# Algorithmic Trading Runtime

C++20 research and execution software for single-symbol SPY strategies. The
system preserves market-data evidence, evaluates specified trading rules and
connects causal strategy decisions to a journaled order runtime and deterministic
broker simulator.

The project is in progress. Native paper-transport acceptance, observed execution
measurements, performance benchmarks and the final analytical report remain open
in the [completion plan](docs/completion_plan.md) and [milestone ledger](docs/progress.md).

## Research findings

| Investigation | Current evidence |
| --- | --- |
| SPY-IM-01 intraday momentum | The 496-session development study has 268 active trades and fails its gross-edge screen. The primary one-minute-delay, 1 bp-per-side compounded return is -7.8924%. |
| SPY-TOD-01 time-of-day continuation | Both price-only and volume-conditioned variants fail their advancement screens. An incremental benefit from volume is not established. |
| SPY-RISK-01 daily risk allocation | Forty account cases cover all 501 development exchange sessions. The primary intraday-risk CER increment over daily-only is -1.5430 percentage points; revision 1 does not advance. |
| SPY-MR-01 opening-anchor reversion | The registered 476-date, forty-account comparison is cost-dominated: 83 active dates; primary compounded net return -0.4171%. Parameter ranges remain descriptive. |

The [research index](research/README.md) identifies governing protocols, retained
findings and earlier proposals. These are development findings from inspected
data. Local 2024-2025 prices and candidate performance remain unopened. Passing
software checks does not establish a profitable strategy.

## Execution and recovery

The execution core maintains integer-cent cash, whole-share positions, order
reservations, execution identities and cancellation state. Broker snapshots gate
routing after contradictions, disconnects and restart. A synchronized journal
records accepted inputs before dispatch eligibility; uncertain durable orders
are never automatically resent.

The shared pipeline connects momentum, both continuation variants and reversion.
Eighteen constructed cases cover cash/positive decisions, partial fills,
cancellation races, restart and fractional/whole-share comparisons. A bounded
historical replay uses explicitly constructed bar-price quotes. Its outcomes
remain separate from frozen research performance. The [interactive event replay](docs/strategy_execution.md)
exposes saved client/broker state, reservations, fills and routing gates.
The [paper API boundary](docs/alpaca_paper_contract.md) adds exact decimal/time
decoding and paper-only request construction. The [schema-3 bridge](docs/paper_execution.md)
adds exact nanodollar accounting and durable broker identities using the same
order core. The [paper transport](docs/paper_transport.md) adds bounded HTTP,
authenticated streaming, durable raw capture and deterministic faults; native
read-only connection acceptance is recorded. The [control owner](docs/paper_control.md)
adds durable deadlines/stopping and subscription-response health checks; native
control acceptance and operational order capture remain open.
The [runtime](docs/execution_runtime.md), [journal](docs/journal_recovery.md)
and [pipeline](docs/offline_pipeline.md) contracts define the execution invariants.

## Build and run

Windows requires Visual Studio 2026 with MSVC, a Windows SDK and C++ CMake tools.
CMake 3.25 or later pins nlohmann/json 3.12.0 by SHA-256. Initial configuration
requires access to that dependency. Project warnings are treated as errors.

1. Open the repository with **File > Open > Folder**.
2. Select configure preset **windows-x64** and build preset **windows-debug**.
3. Select **Build > Build All**.
4. For everyday edits, run **Test > Run Test Preset > windows-debug-quick**
   (twenty-two groups). Use **windows-debug** for all twenty-six groups at milestones
   and affected extended groups when changing study/storage code.
5. Select **strategy_execution_diagnostic.exe** and press **Ctrl+F5** for an offline example
   requiring neither market data nor credentials.
6. Open the printed `report.html` and retain its evidence directory.

The [runbook](docs/running.md) lists targets, prerequisites, artifact files, Linux
commands, Release presets and acquisition procedures. Full input verification can
take several minutes in Windows Debug; measured test timings are in the ledger.

## Implementation structure

Domain classes implement numerical and account behavior. Diagnostic services
compose those classes into reproducible scenarios. Report objects bind results
for export without recalculating signals or statistics. Entry points select
inputs, invoke those objects and display paths/status.

| Location | Responsibility |
| --- | --- |
| `src/market/`, `src/alpaca/` | Bar/calendar invariants, request contracts and response decoding |
| `src/storage/`, `src/platform/` | Evidence integrity, acquisition, durable files, Windows HTTP/credentials and hashing |
| `src/research/`, `src/accounting/` | Causal measurements, study engines, statistical comparisons and fractional long/cash accounting |
| `src/execution/` | Whole-share order state, reconciliation, journals and broker simulation |
| `src/reporting/` | Result-bound HTML/SVG, CSV and JSON exports |
| `apps/`, `tests/` | Runnable composition roots, constructed contracts and optional integration checks |
| `research/`, `docs/` | Protocols, evidence records, operational contracts and completion status |

The [architecture note](docs/architecture.md) describes ownership and layers.
Builds, raw data, test output and routine reports stay under ignored `scratch/`.
Expensive acquisition evidence is retained locally outside source distribution.

## Verification and reproduction limits

The routine suite uses constructed observations, including full-calendar study
fixtures and crash/recovery cases. Historical reproduction additionally requires
the retained acquisition datasets; paper operation requires separate service
access and observed-event evidence. GitHub CI is not configured yet.

The frozen intraday sample contains 496 complete sessions and 193,440 bars. Its
[data policy](research/development_data_policy.json) preserves all five excluded
dates and reasons. Finalization and loading verify responses, checkpoints,
calendars and immutable manifests. Missing bars are not filled, and excluded
sessions are not inserted as synthetic cash returns.

Research comparisons retain declared costs, clocks, uncertainty methods and
failure outcomes. Corporate-action-aware fractional accounts and whole-share
execution accounts have different conventions. No final-period strategy
evaluation or real-money order routing is implemented.
