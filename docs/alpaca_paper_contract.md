# Alpaca paper-broker message contract

This milestone-4 substep implements a bounded request/decoder boundary. It does
not submit orders, authenticate, stream a connection or mutate `OrderRuntime`.
Constructed diagnostic messages are API-shaped fixtures, not captured broker
events. Milestone 3 and the registered strategy findings remain unchanged.

## Sources and supported scope

Official documentation was checked on 24 October 2026:

- [Paper environment](https://docs.alpaca.markets/us/docs/paper-trading): paper fills are simulated; the paper and live environments use separate endpoints.
- [Create order](https://docs.alpaca.markets/us/reference/postorder): submission fields, order classes, explicit time in force and string-valued prices/quantities.
- [Client-ID lookup](https://docs.alpaca.markets/us/reference/getorderbyclientorderid): identity lookup is distinct from submitting a replacement.
- [Cancellation](https://docs.alpaca.markets/us/reference/deleteorderbyorderid-1): HTTP 204 accepts a cancellation request; it does not prove that no fill raced it.
- [Streaming](https://docs.alpaca.markets/us/docs/websocket-streaming): authorization, subscription acknowledgement, binary paper frames, per-execution quantity/price, cumulative order fields and lifecycle events.
- [Account](https://docs.alpaca.markets/us/reference/getaccount-1) and [orders](https://github.com/alpacahq/alpaca-py/blob/master/alpaca/trading/requests.py): independent snapshot inputs.

The implemented local subset is SPY, USD, simple limit orders, whole requested
shares, limit prices of at least USD 1 on the cent grid, DAY time in force and
regular hours. Buy/sell capability describes wire
messages; inventory and reservation checks remain the runtime's responsibility.
Client IDs use at most 48 ASCII letters/digits/underscores/hyphens as a local
restriction; this is not a claim about the provider's maximum. The submitted
client identity is unchanged. Broker/execution UUIDs normalize hexadecimal case.

`AlpacaPaperRequests` accepts only the exact REST origin
`https://paper-api.alpaca.markets` and stream URL
`wss://paper-api.alpaca.markets/stream`. HTTP, live URLs, alternate ports, suffix
hosts, user-info and path variants are rejected. Request descriptions cover
submission, stable-ID lookup, broker-ID cancellation, account, positions and open
orders. There is no arbitrary-path method and no credential-bearing request in
the diagnostic. Future transport must independently enforce the same origins,
disable redirects and preserve uncertain submission outcomes.

## Exact values and broker time

`BrokerDecimal` validates plain signed decimal strings, canonicalizes redundant
zeros and preserves up to 32 whole and 32 fractional digits. This is a bounded
local representation, not a provider precision guarantee. Binary JSON numbers,
exponents, whitespace and malformed signs are rejected for financial fields.
No `double` conversion occurs. Signed ordering compares decimal digits exactly.
Explicit integer projection supports at most eighteen decimal places, checks
int64 overflow and rejects values requiring rounding. For example, USD 100.0075
is retained exactly but has no integer-cent projection. Fractional filled or
position quantities can be retained without claiming whole-share compatibility.

`BrokerTimestamp` retains the original RFC3339 representation and compares UTC
instants at nanosecond precision. Nonzero offsets, fractions and date-crossing
conversions are supported. Leap seconds, unknown `-00:00` offsets, more than nine
fractional digits and local years outside 1970-2199 are rejected. Existing
whole-second minute-bar parsing remains unchanged.

## Event interpretation

| Broker information | Boundary behavior | Later runtime responsibility |
| --- | --- | --- |
| Broker UUID and client order ID | Preserve both identities | Bind a durable client order to its broker order and reject contradictory mappings |
| Fill `execution_id`, `qty`, `price` | Retain stable execution identity and exact incremental values | Apply once and deduplicate, including replay/reconnect |
| Order `filled_qty`, `filled_avg_price` | Retain cumulative quantity and average separately | Check cumulative identities; never treat the average as a new fill |
| Authorization and listening responses | Decode status and subscribed streams independently | Require authentication, subscription, feed health and reconciliation before routing |
| `accepted`, `pending_new`, `new`, `pending_cancel` | Retain lifecycle event and order snapshot | Distinguish receipt, venue acknowledgement and pending cancellation |
| `canceled`, `expired`, `rejected`, `order_cancel_rejected` | Retain event, cumulative quantity and optional timestamp | Apply terminal/expiry rules only with consistent account evidence |
| `done_for_day`, corrections/busts, replacements and unknown events | Retain the raw stream message; mark unsupported | Close routing and reconcile rather than silently accepting a terminal interpretation |
| Fees | No fee inferred from missing fields | Establish observed fee availability and a declared accounting convention before emitting runtime reports |

A recognized event name is not permission to apply it: the decoder has no
accounting or routing side effects. Fill messages require positive incremental
quantity/price, stable execution UUID, timestamp and nonnegative resulting
position. Incremental quantity cannot exceed reported cumulative quantity.
Final fills require a fully filled order; partial fills require remaining
quantity. Unknown status strings remain visible for subsequent gating.

Account cash and average entry prices retain their exact decimal values.
Untracked/short/duplicate positions, non-USD accounts, complex/notional orders
and inconsistent quantity/average fields are rejected. Open-order identity
collisions and terminal rows are rejected. A response reaching the requested
500-order limit is rejected as potentially truncated, rather than treated as a
complete account snapshot. REST account, positions and orders are separate
observations, not an atomic snapshot; future reconciliation must account for
activity between responses.

Payload parsing is bounded to one MiB and uses the existing duplicate-key/depth
checks. The stream result owns the raw decoded server message and receipt time.
Future transport must retain bounded raw inbound evidence before decoding,
including rejected messages, while excluding credentials/authentication requests.
Only complete JSON messages enter this decoder. Binary frame assembly, codec
selection and connection health remain transport work.

## Diagnostic and remaining dependency

Build All and run `paper_contract_diagnostic` with Ctrl+F5. No credentials or
market files are required. It exports constructed messages, request descriptions,
precision examples, assertions, an HTML inspection report, source inventory and
a hashed manifest. Expected: 56 checks across forty cases, zero network requests
and zero observed broker events. Expected rejection cases pass when input is
rejected explicitly.

The existing cent ledger cannot represent every preserved broker price/cash
value. This step deliberately does not convert broker messages into
`BrokerReport`, change journal schemas 1/2, infer zero fees or round subcent cash.
The next implementation must define a versioned exact accounting/persistence
bridge, fee treatment and durable event/identity mapping while preserving legacy
replay. Transport, TIF/expiry transitions, controlled stop, raw capture, uncertain
submission lookup, reconnect reconciliation and the fixed five-session capture
plan follow. An observed paper lifecycle remains required to close milestone 4.
