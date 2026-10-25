# Research and execution completion plan

Revision 3, 25 October 2026. Original implementation baseline: `aa58bf6`.
Milestones 4 and 5 use a deterministic simulated broker and synthetic quote/event
evidence. Paid feeds, native broker operation and multi-session live capture are
outside the completion scope. Historical Alpaca acquisition remains supported.

The [milestone ledger](progress.md) records subsequent acceptance evidence and
open dependencies. This scope remains the governing completion plan.

## Objective

Complete a bounded C++20 study of how specified intraday strategies translate
into auditable orders and positions under execution uncertainty. The completed
work must combine mathematical strategy definitions, historical comparisons,
synthetic broker-protocol events, deterministic recovery checks and measured
implementation costs in one reproducible technical report.

The scope contains two trend-oriented designs and one nontrivial reversion
design, with common execution interfaces and explicit differences in research
conventions. The completed daily-risk experiment remains a separate investigation
of forecast quality and allocation value. It does not replace a reversion design.
Research completion does not require a strategy to pass its advancement screens.

This plan governs remaining implementation priorities. Earlier candidate briefs
and checkpoint notes retain their dated scope. Frozen research protocols,
acquisition identities, existing journals and published results remain unchanged.

## Assumptions and interpretation

Separate numerical validity, forecast evidence, economic value, robustness and
execution feasibility in milestone reviews and the final report. A registered
advancement decision applies to its declared experiment; it is not a verdict on
the entire project or a prerequisite for testing execution infrastructure.

Maintain an explicit account of consequential design assumptions: hypothetical
per-side costs, execution delays, instrument/period coverage, long/cash scope,
observation/reference construction, estimation window/model, fractional sizing,
cash interest, controls and statistical screens. Distinguish assumptions informed
by observed evidence from simplifying conventions and project judgment. The
100-active-date cutoff is a coverage convention, not a statistical law; discuss
sampling dependence and precision alongside it. Confidence levels, block lengths,
annual consistency and concentration screens also require stated justification.

Use existing registered sensitivity cases to explain their effects. Distinguish
recosting fixed entries from changing a cost-aware entry rule. Preserve original
results and criteria; any future changed design must be declared separately
before scoring, with prior sample/selection exposure retained. Causality, data
integrity, accounting conservation and durable recovery invariants remain required.
No additional strategy search is added to the completion scope.

Synthetic quote and execution scenarios explain declared cost conventions. Their
fills cannot estimate live spreads, impact, latency slippage or queue-position
effects. Changes in assumptions do not automatically improve results, and existing simplifications can be optimistic as well as conservative.

## Existing evidence

- SPY-IM-01 has a completed development result with no demonstrated gross edge.
- Both SPY-TOD-01 variants fail their directional screens; a volume increment is
  not established. They are variants within one continuation design.
- SPY-RISK-01 revision 1 completes forty full-calendar account cases and does not
  advance over the registered controls.
- The execution core checks reservations, fills, cancellations, deduplication
  and exact account/open-order reconciliation.
- Synchronized journals retain committed inputs and gate a recovered writer.
  Abrupt-exit contracts cover the append/publication boundary.
- The offline momentum pipeline passes 58 checks in three synthetic cases on
  Linux. Its Windows execution remains pending. Nineteen routine contract groups
  pass on Linux; native verification of the earlier eighteen-group milestone is
  recorded separately.

The existing execution and fractional research accounts have different units,
sizing and corporate-action conventions. Their current results are not
interchangeable.

## Remaining work and acceptance evidence

### 1. Close the current integration checkpoint

Build and run the current routine Windows preset and
`pipeline_diagnostic`. Retain the native evidence and compare numerical traces,
journals and source identities with the Linux run. Resolve introduced compiler
or replay discrepancies and retain legacy replay checks during later changes.

Acceptance: the current milestone has verified native output with explained
platform differences. Unresolved discrepancies remain open. A successful build
alone is insufficient.

### 2. Complete the strategy research specification

Retain SPY-IM-01 and SPY-TOD-01 as the two explicitly described trend-oriented
designs. Explain their distinct decisions, information sets and holding windows;
do not count the price/volume variants as additional strategy families.

Add one bounded, long/cash intraday reversion investigation. The intended family
uses a causally constructed intraday price residual, estimated reversion dynamics
and a holding-horizon/cost-aware decision. A rolling price Z-score alone does not
define the contribution. Before scoring, specify the residual reference,
estimation window, reversion-speed/half-life interpretation, parameter failures,
decision and exit clocks, costs, controls and uncertainty calculations.

Residual stationarity or economically useful reversion must be investigated,
not assumed from the transformation. Check estimation against constructed
mean-reverting and non-reverting controls, quantify parameter uncertainty and
retain unstable or unsupported dates. Failed evidence can produce a cash
decision and a completed negative study. Register the actual design before
historical evaluation; do not freeze numerical thresholds through this roadmap.

Use the inspected 2022-2023 data only in its declared development role. Preserve
the trial history and distinguish descriptive diagnostics from confirmatory
claims. Local 2024-2025 prices and candidate results remain unopened. Any later
evaluation requires a separate design and period-role decision in advance.

Acceptance: three complete strategy specifications, implemented causal signal
calculations, appropriate controls and reproducible results. Unresolved data or
method feasibility remains open; a scored failed gate is a result. The
risk-allocation study is reported alongside these designs under its own objective.

### 3. Connect research decisions to the execution account

Extend the existing strategy adapters and broker boundary only where required
to reuse the same decision-to-order path. Add continuation and reversion adapters
without copying their signal calculations. Run the three designs separately
against one single-symbol account; concurrent strategy ownership is outside scope.

Retain decision timestamps, completed input boundaries, share targets, order
eligibility, fills and final positions in a common evidence view. Distinguish
delayed bar-price research returns from quote-based simulated or paper fills.
Bar OHLCV cannot establish bid/ask liquidity or an intraminute execution path.

Start the interactive execution replay in this milestone, alongside the adapters.
Use retained synthetic traces and journals to support an event-time playhead,
order/target inspection, partial-fill/cancellation chronology and the corresponding
cash, reserved cash, positions, open orders and routing gates. Include a comparison
of interrupted and uninterrupted paths. Bind the viewer to authoritative saved
results rather than duplicating financial or state-transition logic. Clearly label
synthetic events and simulated timing. Add research decision/failure attribution
and views of existing cost/delay cases where they explain the same replay.

Verify simplified parity cases between fractional research accounting and the
whole-share execution account under explicitly aligned sizing, costs and action
assumptions. Report lot rounding, idle cash, rejected/unfilled orders and action
coverage as differences where parity is not expected. Do not replace the frozen
historical account results with newly sized execution results.

Acceptance: positive, cash, incomplete-fill and restart paths for each adapter,
plus one historical-bar replay with clearly labelled reference-price semantics.
The replay must expose event details and account/routing state, including restart
behavior. Quote/fill evidence is required for execution-quality conclusions.

### 4. Demonstrate a simulated paper-broker protocol

Compose the existing matching engine with a deterministic `PaperWire` peer, the
paper-message decoder, schema-3 ledger, durable capture and policy controller.
Retain independent broker account/position/open-order state, stable client and
broker identities, incremental/cumulative fill distinctions and exact money.
No native broker connection, paid access or current market-data query is required.

Acceptance: partial/full fills, confirmed cancellation, fill/cancel races,
duplicate delivery, uncertain submission resolved by stable-ID lookup without
resend, disconnect/restart, missing-fill refusal, exact reconciliation,
deadlines and permanent stop. Compare interrupted/uninterrupted final ledgers,
retain raw synthetic messages and verify accounting independently. Repeated runs
must reproduce saved states. Native Windows build/tests/demo remain a verification
checkpoint. Describe the evidence as synthetic protocol testing, never as an
observed external-broker lifecycle. Details are in [the broker contract](paper_broker.md).

### 5. Measure simulated execution behavior

On declared synthetic quote/delivery schedules, record decision, dispatch,
acknowledgment, incremental fill and receipt times. Retain bid/ask, displayed
sizes, intended quantities, fill prices and source quote sequences. Compare the
supported limit/cancellation policies on matched schedules; record unfilled
quantities, censored outcomes, sample counts and zero-fee assumptions.

Compute signed arrival-midpoint shortfall, spread-related costs, fill fraction
and simulated acknowledgment/fill delays where inputs support them. Show how
spread, size, delivery delay and cancellation deadline assumptions affect these
measurements. Synthetic prices are not historical SIP observations. Do not infer
market impact or actual broker latency from constructed timestamps.

Acceptance: reproducible scenario tables and quote-versus-fill plots, independent
arithmetic, liquidity-budget checks and explicit assumption attribution. Extend
the saved event viewer with these measurements. This milestone remains distinct
from software processing-time benchmarks and historical strategy performance.

### 6. Measure C++ implementation costs and verification portability

Specify the order-state transition rules and accounting invariants, including
reservation conservation, execution deduplication and the durable commit boundary.
Explain the conditions for once-only accounting and why uncertain external
dispatch does not provide an end-to-end exactly-once guarantee.

Benchmark Release builds on matched replay workloads: core event processing,
journal append/synchronization and restart replay. Report workload sizes,
hardware/compiler settings, repeated-run variability, latency quantiles,
throughput and memory use. Distinguish local processing time from network and
simulated delivery delays. Explicitly measure the current copy-per-input design and journal
limits. Optimize only if a measured cost obstructs the declared workload; record
correctness parity for any optimization.

Add reproducible Linux and Windows CI builds for offline contracts, with pinned
dependencies and no required data credentials. Include sanitizer coverage where
supported and a clean-checkout constructed-fixture demonstration. Keep service-
dependent checks separate and preserve independent numerical references.

Acceptance: measured performance evidence, passing portable offline checks and
documented resource limits. No speedup or low-latency claim is assumed in advance.

### 7. Complete the technical report and repository presentation

Write one standalone analytical report, available as HTML and a paginated PDF,
covering strategy mathematics, chronological information sets, statistical
methods, historical findings, account conventions, broker semantics, recovery,
execution measurements and implementation performance. Include a compact map
from each contribution to its supporting figure/table and reproduction command.
Integrate the replay and research visuals developed through earlier milestones,
with the latency/throughput/recovery benchmark views from milestone 6.

Curate only permitted, sanitized fixtures and small result tables. Preserve bulk
inputs and routine run output under ignored `scratch/`. Make an offline demo
reproducible without private datasets; describe additional requirements for full
historical reproduction. Rewrite the README around the finished
contributions and measured findings, with detailed run procedures linked out.

Acceptance: the report, README and retained outputs agree; supported conclusions
and remaining limitations are clear. Check branch contents, default-branch
presentation and reviewer access before describing the repository as a finished
showcase. Merges and visibility changes remain separate publication operations.

## Completion boundary

Follow the milestone dependencies while retaining the native checkpoint as
an explicit final verification requirement. Research and Linux-verified
implementation can continue while native output is pending. Complete the synthetic
broker checkpoint, then measure declared execution scenarios before preparing
the performance study and final report. No service entitlement or external order
history is a dependency.

No additional strategy search, multi-venue router, market-making engine, Level-II
reconstruction, learned strategy, distributed service or database platform is
required. The project remains independent of other numerical-solver work. The
final report can recommend specialist extensions without implementing them.

The completion decision requires the promised scope, traceable evidence and a
reproducible report. Profitability, guaranteed execution quality, universal
generalization and production readiness are not completion criteria. Persistent
limitations are documented rather than removed by changing research gates.

## Broker semantics references

- Alpaca, [Paper Trading](https://docs.alpaca.markets/docs/paper-trading).
- Alpaca, [Working with Orders](https://docs.alpaca.markets/docs/working-with-orders).
- Alpaca, [Websocket Streaming](https://docs.alpaca.markets/docs/websocket-streaming).

These references describe the emulated message subset. Offline tests do not
verify the current behavior or availability of those external services.
