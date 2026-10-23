# Implementation responsibilities

The C++20 implementation has one authoritative calculation for each signal,
account and runtime transition. Applications compose domain services; reports
consume their results. Classes own substantive workflows and state. Value
records hold inputs and results; local numerical/conversion helpers do not own
application state.

## Composition and ownership

| Responsibility | Representative types | Boundary |
| --- | --- | --- |
| Market observations | `MinuteBar`, `HistoricalBarRequest`, `CalendarSession` | Validated prices, quantities, UTC intervals and exchange clocks |
| Input evidence | `SessionDownloader`, `DevelopmentStudyDataset`, `RiskInputDataset`, `DailyRiskDataset` | Bounded requests, checkpoint identities and immutable-study verification |
| Research | Strategy/history classes, `OpeningAnchorMeasurement`, `PooledReversionEstimator`, `ReversionUncertainty`, study engines | Completed information, prior-only estimates and explicit failures |
| Fractional accounting | `LongCashAccount` | Cash, holdings, claims/payments and chronological ledger |
| Order accounting | `OrderRuntime`, `JournaledRuntime` | Integer units, reservations, deduplication, durability and routing gates |
| Broker coordination | `MomentumOrderAdapter`, `OfflinePipeline`, `SimulatedBroker` | Targets, independent quote budgets, events and reconciliation |
| Diagnostics | `ExecutionDiagnostic`, `RecoveryDiagnostic`, `PipelineDiagnostic`, `ReversionPreflight`, `ReversionValidationStudy` | Constructed scenarios composed from existing engines |
| Presentation | Study/diagnostic report classes, `ReportText` | Result-bound exports and shared text encoding |

Entry points select data/configuration, invoke these objects and display status
and paths. They do not contain an alternative signal, return calculation, ledger
or broker state. Platform resources use RAII in storage/transport implementations.

## Reports and text encoding

Report objects borrow const references to completed results. Results must outlive
their reports. Export does not mutate research/runtime state. Continuation study
tables and descriptive diagnostic tables bind their respective result records.

`ReportText` owns a copy of its input. HTML text/double-quoted attribute encoding
and quoted CSV-field encoding share one implementation. It does not interpret
markup, normalize observations or calculate financial values. Numerical
precision and unavailable-value conventions stay explicit in each report.

Reports retain configure-time source/build identities. A cleanup changes source
provenance even when numerical evidence is identical. Existing reports and
journals remain retained with their original identities.

## Account conventions and recovery

Historical studies retain registered delayed minute-open references, fractional
sizing and corporate-action rules. Execution uses whole shares, integer money
and independently reported fills. Future parity work must align sizing, costs,
rounding and actions explicitly.

The journaled runtime records an accepted input before external dispatch
eligibility. Recovery creates a fresh client with routing closed. Reports can
repair missing client knowledge; a complete matching snapshot is required before
routing resumes. Durable uncertain orders retain their identity and are never
automatically resent.

The [runtime](execution_runtime.md), [journal](journal_recovery.md) and
[pipeline](offline_pipeline.md) contracts define transitions, fault handling,
versions and resource limits. Actual paper transport and observed execution
measurements remain open.
