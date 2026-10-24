# Five-session paper execution capture plan

Revision 1 fixes an operational evidence workload. It does not revise any SPY
research strategy, score later-period performance or test an economic edge.
The historical studies and their failed advancement screens remain unchanged.

## Enrollment and prerequisites

Use the first five regular exchange sessions after readiness is established.
Before the first order, save all five dates in an immutable run enrollment
manifest using the broker/exchange calendar. Shortened sessions are excluded by
this rule in advance. Record every attempted/aborted session and its reason;
do not replace dates because of fills, prices or outcomes. A failed/missed date
remains incomplete evidence; any replacement enrollment requires a new revision.

Readiness requires accepted native controls, a dedicated Alpaca paper account,
exact initial reconciliation, quote/calendar access, and a reviewed launcher
that services policy, preserves existing journals and supports controlled
shutdown/restart. None of these prerequisites authorizes live endpoints.
The current read-only probe is not that launcher. Do not reset account state to
make reconciliation pass. Document existing inventory/orders and stop if the
enrolled baseline is not flat with no open orders.

## Fixed workload

Use SPY, USD, whole shares, long/cash, simple regular-hours DAY limit orders.
Limit inventory to one share and one active order. Each session has two separate
one-share entry attempts during 10:30-11:00 America/New_York: one passive buy at
the contemporaneous bid and one marketable-limit buy at the ask. Alternate their
order by session number. Never overlap attempts. No replacement/repricing after
nonfill or uncertainty is permitted. Preserve every denied/unfilled attempt.

Use observed SIP bid/ask with source/receipt timestamps and displayed sizes.
Quote/calendar adapters remain prerequisites; if access is unavailable, record
that and stop rather than silently changing feed or inventing quotes. Reject
crossed/locked, missing, nonpositive, future-dated or source-age-over-two-second
quotes.
Use cent-grid prices at or above USD 1; round buy limits down for passive bid
and up for marketable ask, with the rounding retained. These are bounded limit
policies, not guaranteed fills or measurements of live market impact.

Set each entry's local cancellation deadline to 30 seconds after eligibility.
If it fully fills, schedule one exit attempt after a 60-second holding period:
a marketable sell limit at the contemporaneous bid, rounded down to cents,
with a 30-second cancellation deadline. Do not start the next round trip until
flat with no unresolved orders. Stop after uncertain commands, inconsistent
snapshots, missing event evidence, failed quote/WebSocket PING-PONG health or failed exit.
Remaining inventory is reported and requires the reviewed closing procedure;
a stop never fabricates a flat account or sends an automatic market liquidation.

On session 3, schedule a disconnect/reconnect while flat after the first attempt.
On session 5, restart the process while flat at that point. Retain stop/recovery
state; do not automatically rearm a recovered owner. A reviewed fresh-owner
procedure must be implemented and logged before this workload can run. If the
required flat point is unavailable, record that test as unobserved. Faults and
partial-fill/cancel races that the service does not produce remain unobserved;
constructed cases supplement them without being relabeled as broker events.

## Retained evidence and interpretation

Retain the enrollment/policy/source identities, compiler/build settings,
quote/feed identity, every source/receipt clock, requested limits/quantities,
dispatch/ack/fill/cancel times, raw responses, policy/financial/capture journals,
independent account/position/open-order collections and full manifests. Archive
complete folders. Keep credentials out and sanitize account identifiers before
external publication; preserve original evidence privately.

Report coverage by planned date, attempted order and observed event type.
Include failures, censored/unfilled quantities, open reservations and residual
inventory. Replay sanitized observed messages through the same adapter and
verify exact reconciliation after disconnect/restart. Five sessions establish
only bounded operational evidence. Execution-quality estimates require the
recorded quotes and clocks; no full order book or market-wide order flow is
inferred. The later execution-measurement milestone supplies those estimates.
