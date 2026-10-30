# Implementation and ownership

C++20 is authoritative for signals, statistical comparisons, fractional accounts,
order transitions and broker matching. Application services compose these owners
with result-bound reports; executable entry points select inputs and display
status. Python provides build orchestration, issuer-source preparation,
independent references and publication rendering.

| Layer | Representative owners | Contract |
| --- | --- | --- |
| Observations/evidence | `MinuteBar`, `CalendarSession`, `DevelopmentStudyDataset`, `DailyRiskDataset`, `PanelReplayDataset` | Validated values, exact session grid, immutable manifests and explicit missingness |
| Research | `DevelopmentStudy`, `IntradayContinuationStrategy`, `ReversionHistory`, `ReversionStudy`, `DailyRiskStudy`, `PanelReplayState` | Completed information, prior-only estimation, frozen rules and complete dated comparisons |
| Fractional accounting | `LongCashAccount` | Cash, shares, dividend receivables/payments, splits and chronological equity |
| Economic evaluation | `CostDelayStudy`, `PanelAccountStudy` | Frozen primary decisions, distinct fractional/shared accounts, observed-prefix comparisons and explicit stops |
| Stability and sizing | `StabilityStudy`, `StabilityExtensionStudy`, `PriorMarketCondition`, `RiskMatchWindow` | One-factor parameter states, prior-only regimes, retained unit-return slots and full-calendar comparisons |
| Funded matching | `IntradayAccountLedger`, `PanelRiskLedger` | Entry/risk sizing replays through the existing fractional account; partial fills and stopped states persist |
| Application composition | `CostDelayDevelopment`, `PanelAccountDevelopment`, `StabilityExtensionDevelopment` | Choose constructed or authenticated inputs, invoke existing engines and write result-bound reports |
| Order accounting | `OrderRuntime`, `JournaledRuntime` | Whole shares, integer units, reservations, stable IDs and routing gates |
| Shared capital | `SharedCapitalBook`, `JournaledSharedCapital`, `SharedStrategyAdapter` | One global balance, five inventories, fifteen owners, netting and schema-4 recovery |
| Independent peer | `BrokerState`, `BrokerService`, `ProcessBrokerPeer`, `SharedBrokerSession` | Independent portfolio ledger, schema-5 broker history, framed child process and activity-first recovery |
| Exact message bridge | `BrokerDecimal`, `BrokerTimestamp`, `PaperExecutionState`, `JournaledPaperRuntime` | Validated external precision, durable identities and schema-3 nanodollars |
| Scheduling/matching | Strategy adapters, `OfflinePipeline`, `SimulatedBroker`, `SimulatedPaperWire`, `PaperControl` | Shared target path, finite quote budgets, independent peer state and durable control |
| Measurement | `ExecutionMeasurementStudy`, `ExecutionQuality`, `RuntimeBenchmark` | Matched declared schedules or timed calls; explicit denominators and bounds |
| Presentation | `MomentumStudyReport`, `PanelAccountReport`, `PaperBrokerReport`, `ProjectDemo`, `ReportText` | Export/compose existing results; shared HTML/CSV encoding; no alternate ledger |
| Independent/publication | `verify_*.py`, `Evidence`, `Figures`, `Publication` | Independent numerical checks or declared display transforms on recorded evidence |

## Information and financial boundaries

Research adapters delegate to the same signal engines. The published v1 runs
three families separately against one symbol. The M4 extension adds concurrent
ownership across five instruments under the [shared-capital contract](shared_capital.md).
Fractional historical accounts and integer execution accounts differ in sizing,
costs, corporate actions and execution evidence. Explicitly aligned cases check
parity; other cases retain rounding, idle cash, unfilled or rejected quantities.

The core stores neutral integer monetary units. Legacy JSON cent fields retain
their original meaning; schemas 3, 4 and 5 declare USD 1e-9. No implicit unit conversion
between journal versions is permitted. `JournalFrame` owns framing/checksums for
all schemas. `ReportText` owns its input and performs text encoding only.

`PanelAccountStudy` keeps fractional risk allocation separate from whole-share
shared capital. Required missing prices stop an account at observation, retaining
reached activity and later unavailable outcomes. Forecast-history recovery does
not restart its economic ledger. M6 shared accounts allocate frozen M3 signals;
M4's actual-inventory feedback diagnostic has a separate contract.

## Verification ownership

`PanelReplayFixture` owns one declared analytic calendar and observation stream
for the replay, cost/delay and continuous-account checks. It delegates all model
decisions to `PanelReplayState`; it is not a second strategy implementation.
`PanelReplayDiagnostic` exercises causality and failure contracts against that
source. Native diagnostic executables retain bounded fault fixtures; independent
Python oracles reconstruct their observations, numerical results and ledgers.
`PanelReplayFixture` retains its original 300-session default and offers an
explicit 600-session source for the two successive risk-history initializations.
`verify_panel_replay.py` independently checks the same configurable strategy
features. `verify_stability_extension.py` reuses that oracle and the Decimal risk
ledger audit, adding funded intraday accounting, matching-window arithmetic and
secondary report verification. It does not implement a competing strategy engine.
Pure formulas and serialization helpers remain small functions rather than
stateless classes with no domain responsibility.
The two M6 run services are defined in
`src/reporting/panel_study_application.hpp/.cpp`; shared analytic fixtures and
causality checks are in `src/research/panel_replay_diagnostic.hpp/.cpp`.

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

The [independent process peer](process_broker.md) extends the same matcher to
five symbols and explicit limit-order lifecycle policies. Its broker journal
is owned separately from the shared-client journal. The process route uses
anonymous pipes; its in-process route calls the same service directly.

The [runtime](execution_runtime.md), [journal](journal_recovery.md),
[paper bridge](paper_execution.md), [broker](paper_broker.md),
[strategy adapters](strategy_execution.md), [measurements](execution_measurement.md)
and [benchmark](runtime_benchmark.md) notes specify the detailed contracts.

## Source and publication provenance

`build.py` embeds a build-time inventory for `src/`, `apps/`, `research/`, the
compiler driver and native Visual Studio solution/project. Reports retain its
aggregate and records. Source changes alter provenance even when numerical
results are unchanged. General documentation is outside this fingerprint scope.
Original archived runs retain their original source inventories and toolchains.

The publication binds curated result summaries, runtime CSVs and saved C++ views.
Its separate manifest identifies source content, evidence and rendered outputs.
The builder's medians, unit conversions and figures are display transformations,
not another research/accounting implementation. Full run directories, build trees
and private observations remain under ignored `scratch/`.

## Integrated evaluation boundary

`PanelEvaluation` composes the existing cost/delay, account, secondary/matched
and primary-stability components. `PanelEvaluationAccess` carries calendar,
origin, unit-reference start and calibration identity; `EvaluationAuthorization`
issues later-period capabilities after checking receipts. Default readers and
strategies stay pre-2024. Histories warm independently of new evaluation capital.
The raw reader authenticates original registered observations under that scope.

`InferenceCalibration` uses the existing `FullCalendarInference` kernel and shared
deterministic normal generator. `InferenceDrawSchedule` caches only sampling
indices and their identity; moments, HAC and max-T laws are recomputed per outer
trial. Independent Python verification states its full-bootstrap reference subset
and checks all outer arithmetic. The final executable freeze binds the same
runner, source inventory and observed calibration limitations before M9.

## Final publication and verification

`Evidence` authenticates the curated manifest and reads the original and extended
result tables. `Figures` performs declared display transformations; `Publication`
renders one content model into HTML and PDF. The final report includes three
cross-period/final figures, all retained source-table identities, both saved
synthetic views and the original single-symbol benchmark scope. Original release
artifacts remain available at their immutable tag.

The final GNU 13.3.0 Release build compiles 149 units and links thirty supported
applications. Fresh independent acceptance covers the 259-check demo, the
190-check shared-capital diagnostic and the 313-check process peer. Thirty-eight
simulated build-driver discovery checks pass; they do not execute MSVC. Earlier
Windows research and frozen-run acceptance retain their own source identities.
Native financial, broker and application sources are unchanged by publication
cleanup. Frozen protocols and the independent financial oracles remain intact.

The publication uses Matplotlib 3.10.8 and ReportLab 4.4.9. Repeated rendering
produces identical manifest hashes, including the binary PDF. All nineteen PDF
pages were rendered for layout review. The publication verifier checks all
curated file hashes, stage identities, local links, static assets and saved-view
boundaries. These display checks complement the numerical verifiers; they do not
replace raw-provider or ledger authentication.
