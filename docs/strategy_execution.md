# Strategy execution integration

Milestone 3 connects the three strategy designs to the same journaled execution
pipeline. Momentum, price-only continuation, volume-conditioned continuation and
opening-anchor reversion run as separate cases. The registered research rules,
fractional historical accounts and published findings are preserved.

## Responsibilities and target policy

`MomentumOrderAdapter`, `ContinuationOrderAdapter` and `ReversionOrderAdapter`
call the existing causal strategy engines. They do not refit alternative models
or select thresholds. Continuation receives actual executed inventory when
classifying enter/hold/exit actions; a partial holding retains the declared long
target. The volume condition applies to new entries, as in the research engine.
Reversion uses the registered candidate action and twenty completed prior sessions.

`WholeShareTargetPolicy` constructs a stable strategy-and-decision-time identity,
a fixed ten-share long target or a zero-share cash target, quote-derived buy/sell
limits, a fee ceiling and a dispatch window. It rejects stale, future, crossed
or invalid quotes. Defaults allow two seconds of quote age and thirty seconds
for dispatch. This window does not expire a resting broker order. Broker TIF and
expiry remain milestone 4 work. The original momentum identities are retained.

`ScenarioRunner` composes `JournaledRuntime`, `OfflinePipeline` and the independent
`SimulatedBroker`. It retains client and broker checkpoints before and after
message delivery. Restart rebuilds from the same journal and closes routing
until reconciliation; it does not create a replacement external submission.

## Constructed acceptance cases

The default application produces eighteen cases and 224 assertions:

- Each of four variants has a filled positive signal, a cash signal, an
  incomplete-fill/cancellation-race path and its interrupted/restarted pair.
- Partial paths buy four, then two, then one share during a cancellation race;
  liquidation sells three and four. One-cent-per-share fees are charged by the
  broker. Interrupted and uninterrupted fills, reports and final accounts agree.
- A separate flat-price fee case aligns fractional and integer cost arithmetic.
  A separate rounding case retains the difference caused by whole-share sizing.

The aligned zero-fee cases buy ten shares at USD 100 and sell at USD 101 from
USD 1,000. Both accounts finish at USD 1,010. In the fee case USD 1,000.10 funds
ten shares at USD 100 plus one-cent-per-share fees; both finish at USD 999.90.
The rounding case starts with USD 1,000.50: the integer account keeps USD 0.50
idle and finishes at USD 1,010.50; fractional exposure finishes at USD 1,010.505.
The USD -0.005 difference is expected, rather than an accounting failure.
These deliberately aligned cases do not establish universal parity: partial
fills, fee rounding, different sizing, corporate actions, cash treatment and
calendar scope require separate interpretation. The frozen research accounts
are not overwritten by execution-sized results.

## Bounded historical reference replay

`strategy_reference_replay` first runs the same constructed cases, authenticates
the registered protocols and verifies all 496 frozen development sessions. It
then uses the first eligible scored date after twenty warm-up sessions, without
choosing a date from its return or fill outcome. Four additional cases produce
296 assertions in total. `strategy_execution_diagnostic --historical` is equivalent.

At each decision, the target uses only the completed bar prefix. A zero-spread,
zero-size decision quote is constructed from the last completed close, rounded
to cents. The next minute open becomes a new quote sixty seconds later with
assumed ten-share availability. A limit set at the earlier close fills only if
that later quote permits it. This avoids granting the decision access to a future
open or inventing an immediate fill. Final bar revision times are unavailable.
All continuation decision clocks are replayed. Tracked orders are cancelled and
reconciled at the end; any remaining inventory is reported rather than forcibly
sold at an invented price.

In the recorded first-date run all four historical cases have zero fills and
unchanged final cash. A positive momentum signal leaves an unfilled limit;
positive signals therefore do not imply executions. The selected date is retained.
These quotes are constructed price references, not observed bid/ask, market depth,
latency, or executable liquidity. This small replay tests integration and causal
order construction. It neither measures execution quality nor rescored strategy
profitability. No 2024-2025 price or performance data are accessed.

## Interactive evidence

The offline HTML report uses native HTML radio controls and CSS to select saved
frames. Expand a case, then select numbered events or previous/next. The view
shows the quote path and already-generated fills, client/broker balances, cash
and share reservations, target and order states, the latest broker report and
client transition, routing gates and restart count. Client/broker differences
before report delivery are intentional. The raw-frame inspector exposes the
underlying values. Quote sizes and time are synthetic or constructed, and do
not represent market-wide order flow or measured network latency.

`StrategyExecutionReport` reads const results from the C++ execution core. No
signal, fill or account engine runs in the presentation. Exports comprise the
HTML viewer, exact input bars, case/frame/fill/report evidence, chained journals,
source inventory and a hashed artifact manifest. The independent Python checker
verifies journal chains, reconstructs client reservation/account checkpoints,
checks broker fill arithmetic and report prefixes, and recomputes signal
conditions and Decimal account comparisons from retained inputs:

```sh
python research/verify_strategy_execution.py scratch/runs/strategy_execution_N
```

Linux verification is recorded in the [milestone ledger](progress.md). Native
MSVC Build All, twenty-three routine groups and both executable outputs remain
the final acceptance dependency for milestone 3. Paper transport, observed quotes
and fills, execution measurements, benchmarks/CI and the unified technical report
remain milestones 4-7.
