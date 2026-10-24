# Exact paper execution and recovery

The schema-3 paper bridge reuses `OrderRuntime` for reservations, whole-share
inventory, incremental fills, cancellation and exact reconciliation. C++ monetary
fields now use neutral `*_units` names. Legacy owners still supply cents;
schema-1/2 JSON keys, financial meaning and replay remain unchanged. There is one
order/accounting algorithm, rather than a second paper ledger.

## Financial and identity contract

`JournaledPaperRuntime` declares `paper_whole_share_nanodollar_v1`, USD 1e-9
integer monetary units, SPY simple DAY regular-hours orders, whole executed shares
and `paper_zero_fees_v1` in its immutable journal header. Initial cash must be
positive and exactly representable in int64 nanodollars. The supported range is
bounded by int64 arithmetic, not an arbitrary-precision accounting claim.

Submit intents retain cent-valued limits and fee ceilings for compatibility with
the existing strategy/request adapters. The bridge validates that request subset
and scales monetary terms once into nanodollars. Actual fill prices and broker
cash project directly from exact decimal text to nine places. Finer nonzero
precision, fractional executions and overflow are rejected without rounding.
Zero fees are an explicit paper experiment convention, not an inference from
missing broker fields or an observed fee result. Different fees require a
separate declared policy; no unknown-fee live accounting is provided.

Client order IDs bind one-to-one to broker UUIDs only when symbol, side, requested
quantity and limit match an existing durable intent. REST order snapshots can
resolve identities but cannot create fills from cumulative averages. A snapshot
showing missing executions closes routing until execution evidence and exact
reconciliation resolve the gap. Duplicate execution identities preserve once-only
accounting; changed economics, cumulative quantities, resulting position or fill
time under an existing execution ID are contradictory evidence. Different broker
event IDs may carry the same execution. Identical events and late acknowledgements
cannot regress order state. `accepted`/`pending_new` retain broker receipt without
marking venue acceptance; `new` acknowledges venue acceptance. A rejection after
broker receipt can therefore release an unfilled reservation.

For a new execution, incremental quantity/price are authoritative. After applying
it to a candidate core, cumulative order quantity and account position must match
the broker message. Missing/out-of-order execution evidence is not filled in.
Cumulative average prices are retained in raw input; averages may have their own
rounding and do not establish incremental cash flows.

## Gates and terminal events

Creation and recovery persist a disconnect barrier. Authorization and explicit
trade-update subscription precede reconciliation; neither response alone opens
routing. Reconciliation compares account identity/status, exact cash, whole-share
position and the complete known open-order terms/quantities. Mismatched balances
are never adopted. A temporary snapshot mismatch can be resolved by later exact
evidence. Subcent ledger cash versus cent-rounded broker cash remains a mismatch;
settlement/rounding conventions need observed evidence before any new policy.

Unknown status/events, conflicting identities, invalid precision and inconsistent
execution data preserve financial state/reservations, record the raw input and
latch unresolved evidence. A matching snapshot alone cannot clear this latch,
including after restart. Review/resolution of such evidence is a remaining
operational dependency; no automatic adjustment or reset is implemented.

Cancellation reserves resources until a terminal message matches received fills.
Expiry releases the remainder through the same terminal-cancellation accounting
transition; the raw `expired` event remains explicit in the journal. A local
cancellation deadline, broker DAY expiry and controlled stop are separate future
transport policies. Late cancellation rejection of a terminal order is obsolete.
No execution-quality or operational-completion claim follows from these fixtures.

## Durable schema 3

`JournalFrame` is the shared SHA-256/tab/LF codec used by legacy and paper journals.
Schema 3 records each command, raw inbound body and receipt timestamp where
available, outcome/reason, monetary account checkpoint and a digest of the full
order/identity/deduplication state. Replay evaluates commands through the same
bridge/core and verifies every resulting checkpoint, sequence and predecessor.
It does not load serialized financial state as an alternative ledger.

A candidate bridge is evaluated before append. Synchronization succeeds before
state publication or dispatch eligibility returns. Any uncertain write poisons
the writer; state queries and further commands are refused until closure and
recovery. A complete durable intent survives a failed caller acknowledgement
without automatic resend. An incomplete tail requires verified-prefix recovery
into a separate empty file, retaining the original. Complete corruption and
forged replay checkpoints are rejected. Schemas 1/2 and 3 require separate
readers and are never silently upgraded.

The shared limits are 64 KiB per complete frame, 32 MiB per journal and 10,000
commands. The paper owner additionally caps configured open orders at 100.
A body fitting the decoder's one-MiB limit may exceed the journal frame limit;
such an unrecordable command poisons this writer. Transport must archive bounded
raw messages before decode/journaling, stop routing on storage failure and omit
credential-bearing outbound authentication messages. Rotation and archival
transport are not implemented. State copies and full-state digest calculation
are correctness mechanisms pending the milestone-6 benchmarks.

## Diagnostic and verification

`paper_execution_diagnostic` writes three journals, saved frames, HTML, source
inventory and an artifact manifest. Forty-two assertions cover continuous and
restarted execution, exact balances, duplicate redelivery, cancellation races,
temporary snapshot mismatch and quarantined excessive precision. Both valid
paths finish flat at USD 1,999.9875 under the declared zero-fee convention. The
six-share checkpoint retains USD 1,399.965; replacing it with USD 1,399.96 is refused.
The paired final order/identity/account states agree; restart journals correctly
retain different command histories.

The test group additionally injects failures before append, during a partial
append and after a complete append but before success returns. It checks poisoned
writers, retained uncertain intents, prefix recovery, conflicting executions,
wrong identities, unsupported/fractional/over-limit fills, malformed cumulative
values, expiry, late acknowledgements, model/configuration mismatch, forged
checkpoints and rejection between legacy/paper readers. Existing abrupt-process
exit tests still cover the shared storage boundary; these new injected faults
are not a physical power-loss test.

`research/verify_paper_execution.py` independently reconstructs Decimal cash,
position and reservations from the recorded commands/outcomes, checks duplicate
execution economics and verifies journal chains, saved state digests and artifact
identities. It does not claim an independent classifier for every broker event.

Native acceptance for this substep remains pending. Paper transport, consistent
multi-response snapshot collection, fee/cash-precision observation, explicit
expiry/stop control and the declared five-session operational capture remain
before milestone 4 can close. Minute-bar study inputs and registered results
are not used or changed by this diagnostic.
