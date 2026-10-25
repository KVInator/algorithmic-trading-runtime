# Simulated execution measurements

The measurement study compares fixed limit and cancellation policies on synthetic quotes.
The question is how price, completion and receipt timing change under declared
spread, liquidity and delay assumptions. This is a deterministic sensitivity
experiment, not an estimate of trading costs or a strategy performance study.
Historical Alpaca inputs and frozen research results are unchanged.

## Design and information set

The full runner contains 36 workloads and four policies, giving 144 independent
accounts, 108 paired differences against `cross_11s` and 120 one-factor
differences against the corresponding base workload under the same policy. Each policy sees the
same quotes, initial inventory convention and ten-share measured order within a
workload. Separate accounts prevent one policy from consuming another's budget.

Workloads combine three paths with buy/sell direction and six settings:

| Setting | Spread, cents | Shares per quote side | Outbound delay, s | Order-message delivery delay, s | Cancel processing, s |
| --- | ---: | ---: | ---: | ---: | ---: |
| Base | 2 | 4 | 0 | 0 | 1 |
| Wider spread | 6 | 4 | 0 | 0 | 1 |
| Thinner size | 2 | 2 | 0 | 0 | 1 |
| Outbound delay | 2 | 4 | 2 | 0 | 1 |
| Delivery delay | 2 | 4 | 0 | 2 | 1 |
| Cancel delay | 2 | 4 | 0 | 0 | 3 |

These are one-factor changes around a baseline, not a full factorial experiment.
Interactions between changed factors are not identified. No probabilities or
sampling uncertainty are attached to scenario averages.

The buy-side midpoint begins at USD 100.00. At virtual seconds
10/12/15/18/21/24/30, midpoint changes in cents are:

| Path | Changes from initial midpoint, cents |
| --- | --- |
| Steady | 0, 0, 0, 0, 0, 0, 0 |
| Reverting | 0, 0, -2, -4, 0, 2, 0 |
| Adverse | 0, 2, 4, 6, 8, 10, 12 |

Sell paths mirror those changes. Each account starts with USD 10,000. Sell cases
first acquire ten shares at USD 100.00 in a separately journaled setup fill;
setup events are excluded from execution metrics and included in ledger checks.

The decision occurs at second 10 and client dispatch at second 11. Cross limits
use the opposite touch of the decision quote; passive limits use the same-side
touch. Neither is repriced. Cancellation is requested five or eleven seconds
after dispatch if the client has not yet received full execution. Broker DAY TIF
and the local cancellation deadline are separate concepts. All requests and
terminal confirmations finish within the common second-30 observation horizon.

## Matching and clocks

`MeasurementReplay` composes `SimulatedPaperWire`, the existing FIFO
`SimulatedBroker`, schema-3 `JournaledPaperRuntime`, canonical request factories,
message decoding and durable `PaperCapture`. The existing session initializes
and finally reconciles the account. The measurement coordinator schedules
outbound requests and inbound evidence; it does not implement another matcher
or order/account transition engine. The operational deadline/stop controller
is verified by the broker contract; this experiment specifies cancellation timing
as a fixed scenario policy.

Quotes have finite independent bid/ask budgets. Matching uses the current
opposite touch when the limit permits it. A resting same-side limit receives
no queue-position or maker-fill inference. Such a limit fills only when a later
opposite quote crosses its fixed limit. Quote updates reset budgets; advancing
the clock without a quote does not. Quotes remain immediately observable to the
client; delivery-delay settings apply to order messages and HTTP responses.

All clocks are integer virtual seconds from the synthetic origin
2023-01-03 14:30:00 UTC. Source time and client receipt time are distinct values
on this same axis. No wall-clock sleep or external service timing is measured.

At a tied second, quotes precede broker acceptance or cancellation confirmation.
Due messages are delivered before the local cancellation decision. Source-order
trade updates precede HTTP responses produced at that same source time; fills
may therefore arrive before the submission response. Cancellation requests with
zero outbound delay execute in that same second. Reservations remain until the
existing ledger receives terminal evidence. Every measured raw stream message
is captured and journaled before metrics are derived from its decoded content.

## Metrics and exact cost decomposition

Let `s` be +1 for a buy and -1 for a sell, `Q` the requested shares, `F` the
filled shares, `m0` the client-dispatch midpoint, `mT` the common terminal midpoint,
and `(qi, pi, mi)` each incremental fill's quantity, price and source midpoint.
All economic sums are retained as signed integer half-cents, avoiding midpoint
rounding. Ratios are exported as floating-point descriptive values.

- Executed shortfall: `s * sum(qi * (pi - m0))`.
- Spread component: `s * sum(qi * (pi - mi))`.
- Timing component: `s * sum(qi * (mi - m0))`.
- Unfilled opportunity: `s * (Q - F) * (mT - m0)`.
- Total shortfall: executed shortfall plus unfilled opportunity; explicit fees zero.

Executed shortfall equals spread plus timing exactly. Positive means a cost
relative to the benchmark; negative means a benefit. Executed-only bps use
`F * m0` and are unavailable when `F=0`. Total bps use requested notional `Q * m0`.
The terminal midpoint is an explicit mark for incomplete orders, not an actual
catch-up fill or realized cost. It can make unfilled shares appear beneficial
or costly depending on the path, so fill coverage and separate components remain
visible beside total shortfall. The timing component does not identify impact.

Acknowledgment, first-fill and completion delays are reported from dispatch in
both source and receipt time. An incomplete order has null completion time. Its source/receipt observation
interval stops at terminal cancellation, or at the common horizon if no
cancellation was observed. Completed orders stop at their full fill. The separate
horizon is 19 seconds from dispatch. Censoring reasons distinguish completion,
terminal cancellation and observation end; no incomplete order receives a zero
completion delay. Cancellation is policy-dependent, informative censoring, so
these records are not used to fit an independent-censoring survival estimator.
Policy means for completion delay include completed cases only, with completed
and censored counts. Policy shortfall aggregates divide summed costs by summed
requested benchmark notionals. Cancellation counts distinguish confirmed cancel,
rejection after fill and no cancellation request.

## Reproduction and evidence

In Visual Studio, Build All, run `windows-debug-quick`, select
`execution_measurement.exe` and press Ctrl+F5. No credentials, data download or
subscription is needed. The full 144-case experiment runs only in this executable;
routine contracts use a small matched subset and analytical metric controls.

The printed directory contains `report.html`, `execution_metrics.csv`, complete
`measurement_evidence.json`, per-case runtime/capture journals and raw bodies,
source identities and `summary.json` artifact digests. The report contains a
policy cost-component figure, expandable bid/ask/limit/fill plots, source/receipt
chronology and saved client cash, reservations and order state. No JavaScript or
external assets are required.

```sh
python -B research/verify_execution_measurement.py <printed-report-directory>
```

The independent verifier reimplements finite-budget matching and signed costs
using integers/Decimal, checks timestamps against raw journal/capture bytes,
reconstructs all ledgers, validates censoring and policy summaries, and verifies
paired workloads, CSV and artifact hashes. Analytical C++ controls cover buy/sell
symmetry, half-cent midpoints, incomplete orders, limits and invalid timing.
The repeated-run contract compares complete saved states and metrics.
