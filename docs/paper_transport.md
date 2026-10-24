# Alpaca paper transport

The transport connects the accepted schema-3 paper bridge to fixed Alpaca paper
endpoints. The offline diagnostic substitutes deterministic wire behavior. The
Windows application is a read-only account/stream probe. No operational order
launcher or completed paper-session sample is provided by this checkpoint.

## Ownership and endpoints

`PaperSession` coordinates durable order intents, authenticated subscription,
raw capture, order lookup and independent reconciliation. `PaperHttpClient`
provides the bounded HTTP policy, including bootstrap queries before a runtime
exists. `PaperWire` is the substitution boundary for the native WinHTTP adapter
and deterministic faults. Calculations and state transitions remain in
`JournaledPaperRuntime` and its shared whole-share order engine.

The credential-bearing WinHTTP adapter independently validates each request.
Only `https://paper-api.alpaca.markets` is permitted: account, positions, the
canonical bounded open-order query, stable client-ID lookup, supported simple
SPY DAY limit orders and cancellation by validated broker UUID. Redirects are
disabled. Default certificate checks remain enabled. Live origins, alternate
ports, unrecognized paths and unsupported order fields are rejected before
credentials are attached. The stream is fixed to
`wss://paper-api.alpaca.markets/stream`.

The native probe reuses the existing Windows Credential Manager/hidden-console
credential loader. Credentials and outbound stream authentication messages are
excluded from captures. Returned bodies/selected headers containing an exact
credential key or secret are refused before archival. Raw responses otherwise
retain account IDs and financial information in ignored local run directories;
they require deliberate sanitization before external distribution.

## HTTP and stream policy

GET rate limits permit at most three attempts. Numeric `Retry-After` values
between zero and five seconds are supported; a missing value uses one second.
HTTP-date values and longer waits stop the operation. Other statuses and
transport exceptions are not retried. POST and DELETE are never retried,
including after 429, connection loss or an ambiguous response. A successful
POST must return 200; cancellation acceptance must return 204. Neither response
substitutes for incremental execution evidence or cancellation confirmation.

REST uses 10-second resolve/connect/send and 20-second receive timeouts, plus
a 20-second response-body budget after headers. These are separate phase
bounds, not one total request deadline. Bodies are limited to one MiB. Only
bounded printable `Retry-After` and request-ID response headers are retained.

WebSocket upgrade requires HTTP 101. Async WinHTTP sends and receives retain
callback contexts/buffers through the final handle-closing callback. Handshake
send and headers each have a 20-second application wait; outbound stream sends
have ten seconds. A pending receive can survive a quiet poll without issuing a
second concurrent read. Binary and UTF-8 fragments are assembled, with a one-MiB
message limit and 4,096-fragment bound. Mixed types, malformed payloads, peer
closure and API errors close routing. No market-data stream is added.

An authorized response must precede a `trade_updates` listening acknowledgment.
Authorization waits up to ten seconds; subscription permits up to 32 one-second
polls with queued trade updates retained until acknowledgment. An empty poll
during that handshake stops it. An empty ordinary poll means only that its wait
budget expired. It does not establish feed health or trigger a reconnect.
A durable stop policy, active stale-feed deadline and operational heartbeat
acceptance remain open; WinHTTP keepalive behavior alone is not such evidence.

## Capture and commit boundaries

Each run creates a fresh capture segment. Every bounded response/message body
is written to an exclusively created file and synchronized before its index
entry. The SHA-256-framed index chains sequence, predecessor digest, raw-body
name/hash, local UTC nanosecond-format receipt, steady-clock milliseconds,
message kind, selected headers and HTTP status. Outbound noncredential REST
requests are captured before dispatch. Captures hold at most 1,000 records,
32 MiB of bodies and a three-MiB index; exhaustion stops the operation.

A stream receipt records when the complete message is delivered to the session,
not the kernel's first byte or exchange time. REST response receipt follows full
body collection. These clocks cannot establish network or market latency. The
offline diagnostic uses a declared constructed clock. Within a process,
steady-clock values order receipts; they are not comparable across restarts.

Raw capture and the accounting journal have separate synchronization boundaries.
A captured response can exist without a committed accounting input. A reserved
intent can exist without a request or response capture. An orphan body from an
interrupted capture is preserved; creation refuses to replace it. Capture/write
failure stops dispatch and closes the session. Resume uses a new capture segment
and verified journal recovery, never reuse/truncation of damaged evidence.
Malformed messages remain captured and are submitted to the bridge for durable
quarantine. Reconnection alone cannot clear that unresolved-evidence latch.

## Reconciliation and uncertain commands

After connection/restart, authorization and subscription leave reconciliation
required. Synchronization drains queued events, independently fetches account,
positions and open orders twice, and drains between/after collections. A changed
semantic projection or intervening trade update repeats the complete pair, at
most three times. Equal collections are submitted to the bridge's exact
reconciliation. Snapshot values never replace local balances or infer executions
from cumulative average prices.

This collection is not an atomic broker snapshot. Delayed/missing stream messages
and changes outside the observed windows can escape the equality check. The
adapter therefore does not establish a general consistency guarantee. A dedicated
paper account and a declared operational policy are required for later runs.
Unsupported/missing executions and incompatible cash precision remain unresolved
and keep routing closed. No automatic replay of missed executions is claimed.

Submission commits the local identity/reservation before a captured POST can be
dispatched. Any uncertainty preserves both. Recovery authenticates anew, looks
up that identity and reconciles independently. A 404 is not proof of rejection
and never authorizes another POST. Observed incremental fills are still needed
if an order executed during disconnection. DELETE 204 retains the remaining
reservation until a supported terminal event. The current library can request
these operations, but the native probe has no submit/cancel call path.

## Running and evidence

Run `paper_transport_diagnostic` offline. Expected evidence is 24 checks and seven
cases: partial-fill/cancel lifecycle, uncertain POST/404, bounded rate limits,
authorization denial, peer closure, changing snapshots and malformed stream
capture. Routine contracts add boundary sizes, error statuses, subscription
interleaving, capture-before-dispatch failure and stable-ID restart recovery.
All events in this diagnostic are constructed; no network requests are sent.

On Windows, run `paper_transport_probe` with Ctrl+F5. It reads the paper account,
authenticates/subscribes, and collects the two sets of independent snapshots.
It creates a new flat schema-3 ledger using the initial reported cash. Existing
positions/orders, blocked accounts, unsupported precision or inconsistent
snapshots stop the baseline; nothing is canceled, liquidated or reset. A failed
connection/probe retains available evidence and exits nonzero. It does not
produce an observed order lifecycle or a five-session record.

Both applications print new report/manifest paths. `summary.json` hashes the
raw bodies, capture indexes, runtime journals, source inventory, case evidence
and HTML. Native network/event counts are null because the report does not
measure them; zero real order submissions follows from the read-only entry point.
The report's expandable state inspection contains saved values only.

An independent Python check verifies capture chains/body hashes, artifact paths,
saved state identities and exact Decimal cash/reservations from journal inputs:

```sh
python research/verify_paper_transport.py scratch/runs/paper_transport_0
```

Native MSVC compilation and the read-only broker probe remain acceptance work.
Before operational orders, finish durable expiry/stop and feed-health policy,
fix the five-session capture plan, and retain observed lifecycle/reconnect/restart
evidence. Milestone 4 remains in progress.

## Primary API references

- [Alpaca streaming](https://docs.alpaca.markets/us/docs/websocket-streaming):
  paper binary frames, authorization, listening and trade updates.
- [Alpaca client-ID order lookup](https://docs.alpaca.markets/us/reference/getorderbyclientorderid).
- [WinHTTP callbacks](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nc-winhttp-winhttp_status_callback):
  completion payloads, reentrancy and final handle closure.
- [WinHTTP concurrency](https://learn.microsoft.com/en-us/windows/win32/winhttp/concurrency-in-winhttp):
  asynchronous cancellation and handle lifetime.
- [WebSocket upgrade](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpwebsocketcompleteupgrade).
