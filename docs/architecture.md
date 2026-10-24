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
| Research | Strategy/history classes, `OpeningAnchorMeasurement`, `PooledReversionEstimator`, `ReversionUncertainty`, `ReversionHistory`, `ReversionStrategy`, study engines | Completed information, prior-only estimates and explicit failures |
| Fractional accounting | `LongCashAccount` | Cash, holdings, claims/payments and chronological ledger |
| Order accounting | `OrderRuntime`, `JournaledRuntime` | Owner-declared integer units, reservations, deduplication, durability and routing gates |
| Paper API boundary | `BrokerDecimal`, `BrokerTimestamp`, `AlpacaPaperRequests`, `AlpacaPaperDecoder` | Exact external values and paper-only request descriptions; no cent-ledger mutation or network transport |
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
and independently reported fills. The [execution integration cases](strategy_execution.md) align sizing and costs
explicitly for simple comparisons and retain an expected whole-share rounding
difference. No general equivalence is inferred. Strategy adapters share target
construction while delegating decisions to their research engines. The saved
frame viewer reads runtime and broker snapshots without recalculating balances.

The journaled runtime records an accepted input before external dispatch
eligibility. Recovery creates a fresh client with routing closed. Reports can
repair missing client knowledge; a complete matching snapshot is required before
routing resumes. Durable uncertain orders retain their identity and are never
automatically resent.

The [runtime](execution_runtime.md), [journal](journal_recovery.md) and
[pipeline](offline_pipeline.md) contracts define transitions, fault handling,
versions and resource limits. Native paper connection acceptance and observed
execution measurements remain open.

The [paper-broker contract](alpaca_paper_contract.md) preserves broker precision
without changing the current cent ledger or journal schemas. The versioned exact
accounting bridge is described below; observed transport/reconciliation remains open.

The [paper execution owner](paper_execution.md) composes that same order core
with exact broker conversion, durable identity mapping and a schema-3 journal.
`JournalFrame` provides one framing/checksum implementation for all schemas.

The [paper transport](paper_transport.md) composes `PaperSession`,
`PaperHttpClient`, `PaperCapture` and a `PaperWire` substitution boundary.
The Windows wire owns credentials and WinHTTP handles; the session owns protocol
ordering and calls the same journaled bridge. Capture bodies/indexes and runtime
inputs have separate durable boundaries. The Windows probe is read-only;
operational policies and observed session capture remain open.

`PaperControl` composes the session and schema-3 ledger with a separate policy
journal. Deadline and permanent-stop state are durable; process-local health
age is discarded at restart. The transport invokes its eligibility callback
after capture and immediately before dispatch. The [control contract](paper_control.md)
records these separate commit boundaries and serialized servicing requirements.
