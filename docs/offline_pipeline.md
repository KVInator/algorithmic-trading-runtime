# Offline strategy, broker and recovery pipeline

The pipeline connects the existing `IntradayMomentumStrategy` to the journaled
whole-share execution runtime and an independent simulated broker. Its purpose
is to verify the complete decision-to-order-to-fill path across client restart.
Only constructed minute bars and top-of-book quotes are used. The fixed SPY
research protocols and their failed advancement decisions remain unchanged.

## Signal and execution policy

`MomentumOrderAdapter` calls the existing strategy with exactly 360 completed
minute bars at its scheduled entry decision. A positive signal requests a fixed
ten-share target; otherwise the target is zero. Scheduled liquidation requests
zero shares. This explicit integer-share policy differs from the historical
continuous-notional return proxy and is not a revised investment experiment.

Target IDs combine the strategy name and decision timestamp. Each target records
the symbol, decision time, latest new-order dispatch time, share target, buy/sell
limit prices and per-share fee ceiling. The diagnostic uses the causal quote's
ask and bid, a two-second quote-age bound and a thirty-second dispatch window.
Future or stale signal quotes, incomplete prefixes and clocks before the broker
or strategy decision are rejected. Later observations never enter the signal.

The runtime persists every accepted target, including cash targets that produce
no order. Identical IDs/payloads are idempotent; conflicting reuse is quarantined.
New target timestamps must strictly advance. Replaying an older accepted target
cannot supersede the latest objective. The latest target and all accepted IDs
are reconstructed through journal replay after a fresh client is created.

## Routing and resource ownership

`OfflinePipeline` coordinates one strategy owning one symbol/account. Its runtime
and simulated broker references must outlive it, and their symbols must match.

1. Reconnect, redeliver the broker's report history, and require its complete
   cash/position/open-order snapshot to match the client.
2. Persist the target before attempting any command. If an older order remains
   open, persist and dispatch a correlated cancellation while retaining resources.
3. Wait for all old orders to become terminal, including fills that arrive during
   cancellation. Compute the new quantity from the actual executed inventory.
4. Require a current causal quote and an unexpired new-order dispatch window.
   Persist the limit order, apply the existing cash/inventory/position/order-count
   checks, and dispatch only after the journaled call returns `applied`.
5. Route at most one accepted order per target. Existing active orders are waited
   on; a terminal rejected/canceled order is not automatically replaced. A fresh
   strategy decision is required for another order if its target was not reached.

The dispatch window controls new-order creation, not broker time-in-force. An
already accepted limit order can remain active beyond that window. A later target
may cancel it; cancellation does not require a fresh quote. Broker errors after
a durable command append a disconnect barrier and propagate the error. Uncertain
dispatches retain their IDs and reservations.

If a client exits after recording an order but before sending it, the broker may
have no corresponding order. Reconciliation then remains blocked. The pipeline
does not infer rejection from absence or blindly resend the command. A durable
target with no order command can resume routing after exact reconciliation.

## Independent broker model

`SimulatedBroker` owns accepted orders, pending cancels, quote budgets, executions
and generated reports. Its cash and inventory are independently summed from its
actual execution ledger. It does not read the client's financial state.

Each quote has a unique sequence, timestamp, bid/ask prices and nonnegative
displayed share sizes. Buys execute at the ask when their limit permits; sells
execute at the bid. Accepted client orders consume a shared per-side size budget
in FIFO order. Matching can occur when an order arrives or a new quote arrives.
Repeated identical quote sequences do not replenish liquidity; conflicting or
unknown out-of-order sequences are rejected. Public broker mutations validate a
candidate copy so invalid fills cannot leave partial state changes.

Fees are one integer cent per executed share in the diagnostic, within the
client's ten-cent ceiling. The broker rejects an order whose declared fee ceiling
cannot cover its configured fee. Pending cancellation becomes effective only
when the scenario processes the cancellation queue. A quote delivered first can
therefore produce a legitimate final partial fill before cancellation confirms
the cumulative quantity. Report generation and client delivery are separate:
acknowledgements can be delayed and reports can be replayed or duplicated.

This is a deterministic liquidity-budget fixture. It has no calibrated queue
position, venue competition, hidden liquidity, impact or stochastic fill model.
The broker represents an external service retaining its state while the client
is reconstructed. Broker-process persistence and real transport are not part of
this milestone. Source quotes, execution records and client journals are retained
to reproduce each case.

## Verification and native workflow

Build All, run the `windows-debug` preset's nineteen groups, then select
`pipeline_diagnostic.exe` and **Ctrl+F5**. No dataset or credentials are needed.
The diagnostic checks 58 assertions across:

- An uninterrupted client using the real momentum signal on a rising prefix.
- The same path with two fresh-client restarts while broker fills are undelivered.
- A falling-prefix cash signal, including restart and decision redelivery.

The paired active paths must produce exactly the same broker reports and five
executions for two orders. The entry receives four shares, then two, then one
during cancellation. Liquidation sells the actual seven shares in parts of three
and four. The last fill receives bid-price improvement. Client and broker finish
flat with the same integer cash balance; these constructed cash movements are
not a performance estimate. The cash case produces no order or fill.

Routine contracts additionally test shared quote-size budgets, FIFO priority,
limit prices, identity conflicts, schema-1 compatibility, durable target/command
boundaries, stale/future observations, clock regression, risk rejection, broker
rejection without retry and external broker discrepancies. The prior journal
group retains its four actual abrupt-exit child-process tests.

The Linux Debug build passes all nineteen routine groups with warnings as errors.
The order, pipeline and journal groups, plus the pipeline entry point, also pass
AddressSanitizer/UndefinedBehaviorSanitizer with leak detection disabled. An
independent ledger check reproduces all eighty pipeline input checkpoints and
both broker accounts, checks per-quote liquidity budgets and validates all eight
artifact hashes. The original execution inputs/trace and all four legacy recovery
journals plus their check table remain byte-identical to the verified native
baseline. The new pipeline's native Windows run remains pending.

Each `scratch/runs/pipeline_diagnostic_*` directory contains:

| File | Evidence |
| --- | --- |
| `report.html` | Three case summaries, expandable client event traces and all checks |
| `pipeline_evidence.json` | Quotes, targets, independent broker fills/reports, client traces and final states |
| `signal_rising.csv`, `signal_falling.csv` | The exact completed prefixes supplied to the strategy |
| `uninterrupted.journal`, `interrupted.journal`, `cash-signal.journal` | Schema-2 inputs, targets and replay checkpoints |
| `source_inventory.txt` | Configure-time source records embedded in the binary |
| `summary.json` | Compiler/configuration, 58-check/three-case counts and eight artifact hashes |

Legacy schema-1 journals remain readable and keep their version. Schema 2 adds
target inputs while retaining the existing financial model and framing. New
target records cannot be appended to schema-1 files. No journal, historical
input or result is migrated in place.
