# Simulated paper broker

The offline broker uses a deterministic, in-process broker peer. Quotes, order messages,
clock readings and failures are constructed. No network, credentials, subscription
or observed Alpaca executions are required. This demonstrates the runtime's
broker-protocol and recovery behavior under specified scenarios; it does not
establish connectivity to an external service or live execution quality.

## Ownership and protocol

`SimulatedPaperWire` implements `PaperWire` and translates canonical limit-order,
cancel, lookup, account, position and open-order requests into the existing
`SimulatedBroker`. One matching engine supplies FIFO fills against finite
bid/ask size budgets. Repeated quote sequences cannot replenish liquidity.
The peer owns broker state independently of the client ledger. Its JSON messages
use the supported [Alpaca-shaped contract](alpaca_paper_contract.md), with stable
synthetic broker/execution UUIDs, client identities, incremental fills, cumulative
quantities and terminal statuses. URL strings identify the protocol; they are
never dispatched to a network.

`PaperSession` composes that peer with durable raw capture and the
[schema-3 accounting owner](paper_execution.md). The runtime journals the order
intent before dispatch eligibility and books executions once at USD 1e-9
precision. The simulated venue quotes/fills use integer cents. Whole shares,
simple DAY limit orders, long/cash SPY and explicit zero fees define the subset.
Final average prices never supply missing executions. Descriptive average-entry
prices truncate at 1e-9 USD and do not drive cash or reconciliation.

`PaperHttpClient` permits three bounded GET attempts for rate limits, waits at
most five seconds per Retry-After and never retries POST/DELETE or follows a
redirect. Errors retain uncertain identities and reservations. Full unknown or
contradictory evidence closes routing. The fault contract group also checks
malformed messages, authentication denial, framing limits, capture failures,
quiet/missing/mismatched heartbeat responses and unstable snapshots.

## Scenario evidence

The broker portion of `project_demo` runs six reproducible cases:

| Case | Required behavior |
| --- | --- |
| `uninterrupted` | Buy ten shares in fills of four and six, deduplicate a fill, retain reservations during cancel, account for a fill winning the cancel race, then sell ten shares |
| `interrupted` | Same broker economics across disconnect/restart; refuse missing-fill reconciliation; explicitly replay fixture history before reopening routing |
| `partial_cancel` | Fill four shares and cancel six; release the remainder's reservation and retain inventory |
| `uncertain_submission` | Lose the response after acceptance; recover and look up the original client ID; cancel without a second submission |
| `snapshot_mismatch` | Refuse a one-cent cash contradiction without overwriting local balances; accept subsequent exact evidence |
| `deadline_stop` | Journal a cancellation deadline and permanent stop; wait for terminal cancellation before releasing cash; refuse later submissions |

The paired round trip starts with USD 2,000, buys four shares at USD 100.00 and
six at USD 100.01, then sells ten at USD 100.03. The prescribed final cash is
USD 2,000.24 with no inventory or reservations. This constructed USD 0.24 change
is an accounting oracle, not a strategy return. Final account, order and identity
states agree across interrupted/uninterrupted paths; journal histories differ.

History redelivery is an explicit simulator capability. It does not assert that
an external broker would replay an original WebSocket stream. Missing executions
keep routing closed until the fixture archive supplies them and exact snapshots
agree. The broker continues matching while the client is disconnected.

## Deadline, stop and health contracts

`PaperControl` journals policy separately from accounting. A future UTC deadline
is at most one hour away. Its expiry requests cancellation on `service()` and
never expires an order locally. Broker DAY expiry is a separate terminal event.
A permanent stop is synchronized before cancellation attempts and never promises
immediate liquidation. Remaining inventory and unresolved orders stay visible.
Already uncertain cancellations are not automatically resent.

Submission through the controller requires a matching simulated heartbeat token
and exact snapshot reconciliation. Defaults are a 30-second health interval and
five-second response budget. Quiet input is not a failed heartbeat. UTC controls
deadlines; monotonic milliseconds control health age. Clock regression, missed
service windows or stale round trips stop the owner. Calls are serialized without
a background timer. These are logical health tests, not physical socket timing.
The request-level diagnostic also exercises the lower session independently;
only the deadline/stop case uses the policy owner.

Policy schema 2 (`paper_control_stop_deadline_v2`) retains the existing
`websocket_ping_pong_v1` model identifier for journal compatibility. Schema-1
control journals remain readable but recover stopped. Recovery discards old
process-local health, stops an active owner and preserves financial reservations.
There is no automatic rearm. Complete corruption and incomplete policy tails are
rejected without truncating evidence. Limits are 256 deadlines, 2,000 commands,
32 MiB and the shared 64 KiB frame bound.

## Reproduction

In Visual Studio, Build All, select
`project_demo.exe` and press Ctrl+F5. The printed directory contains a `broker/` child with:

- `report.html`: position chart, expandable event chronology, cash/reservations,
  routing gates and independent broker snapshots;
- `transport_evidence.json`: checks, cases, raw synthetic trade history and saved
  frames, including source quote sequences and fill sizes;
- per-case `runtime.journal`, raw capture bodies and chained `capture.journal`;
- the deadline case's separate `control.journal`;
- `summary.json` and source inventory with artifact/source digests.

Raw bodies synchronize before capture index entries. Capture and accounting have
separate commit boundaries; a capture does not prove a ledger commit. A segment
supports 1,000 bodies, 32 MiB payload and 3 MiB index. A body is at most 1 MiB.
Uncertain writer operations close routing; evidence is never overwritten.

Independent verification:

```sh
python -B scratch/reference_suite/research/verify_paper_broker.py <printed-report-directory>
```

The verifier checks artifact/capture/journal chains and independently reconstructs
Decimal cash, positions and quote-side liquidity budgets. The C++ contract also
runs the whole schedule twice and compares all saved states and identities.
Research protocols, acquisition evidence and historical strategy results are
unchanged. Historical Alpaca data access remains supported separately.
