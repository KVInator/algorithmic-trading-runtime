# Synthetic broker protocol, edition 1

Registered engineering contract for the multi-instrument execution extension.
All prices, quantities, calendar events, reports and process faults are
constructed. No exchange, external broker, network service or subscription is
used. Historical strategy results and reserved strategy inputs are outside scope.

## Independent account and matching

The fixed universe is GLD, IWM, QQQ, SPY and TLT. One broker account starts with
USD 100,000, no inventory and no orders. Money is signed representable integer
USD 1e-9; quantities are whole shares. A fixed fee of 10,000 units per filled
share is charged on both buys and sells. Multiplication is exact, without
rounding or minimum commission. This fee is an engineering assumption, not an
empirical cost estimate. Orders must declare a sufficient fee ceiling.

The broker owns its cash, holdings, reservations, orders and execution history
independently of the client. Both sides admit orders against one global cash
balance. Filled sale proceeds are immediately available on trade date. There
are no settlement receivables, leverage, shorting or borrowing. This explicit
synthetic convention does not model exchange settlement rules.

The existing FIFO matcher is authoritative. Each symbol has independent bid
and ask liquidity budgets. New accepted orders join a global arrival ordering;
only orders for a quote's symbol participate in its liquidity. Identical quote
sequences never replenish liquidity. Conflicting sequences are rejected. A new
sequence supplies a new finite displayed budget. Source and receipt clocks are
explicit; future source clocks are rejected, receipt clocks cannot regress,
and matching requires a quote no older than two seconds. Limit orders execute
at the current bid/ask only when marketable. There is no queue-position,
market-impact, exchange-priority or hidden-liquidity claim.

## Lifecycle and replacement

Only limit orders are supported. DAY orders expire at the current declared
exchange-session close before any matching at that clock. IOC orders match
once against current fresh liquidity and immediately cancel the unfilled
remainder. GTC orders retain their remaining claims across sessions and process
restarts. The declared session is validated against the retained calendar.
Only regular or early-close calendar metadata are used, not observed prices.

A cancellation request retains cash and inventory until terminal confirmation.
Quotes may fill the old order before cancellation completes. A managed
replacement consists of cancel, terminal confirmation, then a new client order
identity for the residual target. It never changes an in-flight order's owner
claims, reuses its identity or promises an atomic exchange replacement.
Rejection, cancel rejection, partial/full fill, expiration and late delivery
must preserve the same accounting identities. Broker-generated order identity,
event identity and execution identity remain distinct from client order identity.

## Durability and recovery

The broker commits the command, result and independent checkpoint before
returning a response or exposing its new activities. Command identities are
idempotent only for identical canonical commands. Conflicting reuse is denied.
Activity cursors are stable, contiguous and paged; history includes terminal
orders and fills. A client recovering from any response ambiguity performs
lookup and activity catch-up before exact reconciliation. It does not blindly
retry submission or infer a fill from a changed balance.

Client schema 4 and legacy schemas 1-3 retain their financial contracts. The
independent peer journal uses schema 5 with a named model and this policy hash.
Unsupported/mismatched journal headers are rejected, not silently migrated.
The existing exclusive storage lock, frame checksums, chain and synchronous
append boundaries are reused. An incomplete tail requires verified-prefix
recovery to a separate destination; complete corruption is rejected and never
truncated. A failed or uncertain append closes that writer. Recovery replays
committed commands without replenishing quotes or regenerating identities.

Bounds per broker segment: 1,024 retained orders, 32 open orders, 8,192
activities, 8,192 unique commands, 4,096 quotes and 32 MiB of journal bytes.
Frames and complete IPC messages are at most 64 KiB; activity pages contain at
most sixteen reports. Hitting a bound fails closed. This edition does not
compact active peer history or run without bounds.

## Local process boundary

An in-process peer and a separate native C++ peer share the service and matcher.
The process channel uses inherited anonymous pipes, a four-byte big-endian
length and a UTF-8 JSON envelope with protocol version 1 and an increasing
connection-local request ID. No shell command interpolation or sockets are used.
The parent owns child lifetime and enforces a bounded exchange deadline.
Malformed, oversized, truncated, wrong-version or mismatched replies close the
channel. Broker writes remain independent of whether a response reaches the
client. Diagnostic-only stop/pause-after-commit options make this boundary testable.

## Acceptance

The twelve scenario families cover: process/in-process parity and framing;
FIFO/partial fills/fees/quote budgets; DAY; IOC; GTC; cancel/new races;
rejections and resource admission; duplicate/gapped/reordered delivery;
ambiguous submission and lookup; client restart; broker restart; and durable
write/tail/corruption failures. Both independent ledgers, quote consumption,
activity continuity and final states are checked. Python independently rebuilds
broker accounting and the shared-client journals from saved synthetic evidence.
Windows Release acceptance follows local native, regression, sanitizer and
saved-view checks. Passing these cases establishes the declared synthetic
engineering behavior, not real-broker certification or historical profitability.
