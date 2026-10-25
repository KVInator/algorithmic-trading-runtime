# Implementation and ownership

C++20 is authoritative for signals, statistical comparisons, fractional accounts,
order transitions and broker matching. Reports borrow completed results; entry
points select inputs, invoke services and display status. Python is limited to
independent references and publication rendering.

| Layer | Representative owners | Contract |
| --- | --- | --- |
| Observations/evidence | `MinuteBar`, `CalendarSession`, `DevelopmentStudyDataset`, `DailyRiskDataset` | Validated values, exact session grid, immutable manifests and explicit missingness |
| Research | `DevelopmentStudy`, `IntradayContinuationStrategy`, `ReversionHistory`, `ReversionStudy`, `DailyRiskStudy` | Completed information, prior-only estimation, frozen rules and complete dated comparisons |
| Fractional accounting | `LongCashAccount` | Cash, shares, dividend receivables/payments, splits and chronological equity |
| Order accounting | `OrderRuntime`, `JournaledRuntime` | Whole shares, integer units, reservations, stable IDs and routing gates |
| Exact message bridge | `BrokerDecimal`, `BrokerTimestamp`, `PaperExecutionState`, `JournaledPaperRuntime` | Validated external precision, durable identities and schema-3 nanodollars |
| Scheduling/matching | Strategy adapters, `OfflinePipeline`, `SimulatedBroker`, `SimulatedPaperWire`, `PaperControl` | Shared target path, finite quote budgets, independent peer state and durable control |
| Measurement | `ExecutionMeasurementStudy`, `ExecutionQuality`, `RuntimeBenchmark` | Matched declared schedules or timed calls; explicit denominators and bounds |
| Presentation | `MomentumStudyReport`, result-bound report classes, `ProjectDemo`, `ReportText` | Export/compose existing results; shared HTML/CSV encoding; no alternate ledger |
| Independent/publication | `verify_*.py`, `Evidence`, `Figures`, `Publication` | Independent numerical checks or declared display transforms on recorded evidence |

## Information and financial boundaries

Research adapters delegate to the same signal engines. Three families run
separately against one symbol; concurrent strategy ownership is outside scope.
Fractional historical accounts and integer execution accounts differ in sizing,
costs, corporate actions and execution evidence. Explicitly aligned cases check
parity; other cases retain rounding, idle cash, unfilled or rejected quantities.

The core stores neutral integer monetary units. Legacy JSON cent fields retain
their original meaning; schema 3 declares USD 1e-9. No implicit unit conversion
between journal versions is permitted. `JournalFrame` owns framing/checksums for
all schemas. `ReportText` owns its input and performs text encoding only.

## State, persistence and resources

Calls within one runtime are serialized. Candidate copies make validation atomic;
a journal owner synchronizes the candidate record before publishing state or
returning dispatch eligibility. Platform writers own handles through RAII and
exclude competing writers. Recovery validates/replays the same financial model,
then records a fresh disconnect. Contradictory snapshots never overwrite the ledger.

`SimulatedPaperWire` uses an independent broker ledger and the shared FIFO matcher.
It does not copy client balances to construct a matching snapshot. Quote sequence
budgets are finite; advancing virtual time does not refresh displayed liquidity.
`PaperControl` owns deadline/stop evidence separately from execution accounting.
Historical Windows HTTP/credentials remain separate from this offline peer.

The [runtime](execution_runtime.md), [journal](journal_recovery.md),
[paper bridge](paper_execution.md), [broker](paper_broker.md),
[strategy adapters](strategy_execution.md), [measurements](execution_measurement.md)
and [benchmark](runtime_benchmark.md) notes specify the detailed contracts.

## Source and publication provenance

CMake embeds a configure-time inventory for `src/`, `apps/`, `research/`, the root
CMake source and presets. Reports retain its aggregate and records. Source
changes alter provenance even when numerical results are unchanged. Tests and
general documentation are outside this established fingerprint scope.

The publication binds curated result summaries, runtime CSVs and saved C++ views.
Its separate manifest identifies source content, evidence and rendered outputs.
The builder's medians, unit conversions and figures are display transformations,
not another research/accounting implementation. Full run directories, build trees
and private observations remain under ignored `scratch/`.

Retired standalone probes, preliminary report exporters and proposal/checklist
files are preserved in Git history. Scientific controls and failure regressions
remain when they protect the final supported models and runtime. Verification and
roadmap acceptance are recorded in [the final evidence map](verification.md).

Six scenario/control generators used only by regressions live in `tests/support/`
and are compiled only into their consuming test targets. Production libraries
retain the final runtime, broker, research and public-demo responsibilities.
