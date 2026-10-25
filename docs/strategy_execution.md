# Strategy-to-order integration

The three strategy families use the same whole-share target/order path. Adapters
call the authoritative research engines; they do not refit signals, select
thresholds or inspect future bars. Each design owns one SPY account independently.
Concurrent strategies sharing inventory are outside scope.

`MomentumOrderAdapter`, continuation and reversion adapters consume completed
information and actual executed inventory. `OfflinePipeline` persists targets
before submit/cancel eligibility and matches only against declared later quotes.
It preserves partial fills, pending cancellation, finite displayed-size budgets
and explicit routing gates. Durable uncertain intent is not automatically resent.
The exact paper peer/control layer additionally covers message conversion,
stable broker IDs, deadlines and permanent stop.

## Account parity and model differences

Constructed aligned cases use common prices, integer-compatible sizes and costs.
They reproduce fractional/whole-share equity under those conventions. A rounding
case retains idle cash and an expected difference. Generic equality is not claimed:
research uses fractional notional and delayed trade-price references; execution
uses whole shares, quote-side matching, explicit fees, rejected/unfilled quantities
and broker reports. Historical risk accounts additionally retain corporate actions.
Frozen strategy results are never overwritten with execution-sized returns.

## Offline replay

Run `project_demo` for eighteen constructed strategy cases and six broker cases.
The strategy part retains 224 checks, targets, 216 saved frames, fills, independent
broker snapshots, orders, reservations, routing gates and fractional-account
comparisons. Each CSS radio button selects a saved checkpoint; no JavaScript or
financial calculation runs in the viewer. The C++ report binds const results.
Keyboard focus selects the same events as clicking the playhead.

The `strategies/` child directory includes the report, inputs, saved cases,
journals, source inventory and manifest. Its independent verifier reconstructs
integer accounts and strategy directions and checks aligned/rounding parity.
The combined verifier also checks the broker child:

```sh
python -B research/verify_project_demo.py <printed-demo-directory>
```

## Bounded historical reference replay

`strategy_reference_replay` authenticates the unchanged protocol and frozen
2022-2023 dataset, then uses the first eligible scored date after the common
warm-up. It adds four explicitly labelled historical-bar proxy cases. Quotes
constructed from bar prices are not observed bid/ask liquidity. The account is
reconciled at the end; any unfilled inventory remains visible.

The original native reference replay passes 296 assertions in twenty-two cases.
This verifies adapters, causal boundaries and account state on one chosen date,
not historical execution quality or another strategy performance evaluation.
The final [verification map](verification.md) separates those claims from the
registered research and synthetic measurements.
