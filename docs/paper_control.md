# Paper order controls

`PaperControl` composes the accepted schema-3 ledger and `PaperSession` without
changing their accounting or journal contracts. It owns a separate versioned
policy journal for one explicitly named owner of that execution ledger. The
composition root must retain that identity, file and ledger pairing. No native
operational launcher is introduced in this checkpoint.

## Deadlines and stopping

Submission requires a successful matching WebSocket PONG and exact snapshot
reconciliation. A future UTC cancellation deadline, at most one hour away, is
synchronized before the local order intent and POST eligibility. The transport
checks eligibility after queued messages and again after request capture,
immediately before the wire call. If a deadline/health check expires there, an
already reserved intent remains unresolved; no POST is sent. There is no automatic
resubmission or new identity after uncertainty.

A local deadline requests cancellation on the next `service()` call. It does not
expire the broker order locally or release its reservation. The broker TIF
remains regular-hours DAY; broker expiry and the application's cancellation
clock are distinct. Supported broker terminal events still govern release.

A stop is synchronized before cancellation attempts. The controller permanently
refuses new submissions and requests cancellation of tracked open orders with
known broker identities. Existing pending cancellations are never automatically
resent. An uncertain cancellation, missing identity or unmanaged active order
stops the owner for review. The returned snapshot retains open orders, pending
cancellations, exact cash/reservations and remaining inventory. No forced
liquidation or immediate-flat guarantee is made. `service()` must continue to
consume events after stopping; explicit session polling remains available.

A stop applies to this owner. Callers must route all new orders through it;
calling the lower-level session directly bypasses policy. Future native launchers
must enforce this composition and recover the existing control file, rather than
creating a fresh file to evade its stop. There is no automatic rearm API. A
reviewed new owner/closing-inventory procedure remains a launcher design item.

## Stream health and clocks

The default interval is 30 seconds and PONG budget five seconds. The session
drains queued input, sends a WebSocket PING with a fresh correlation token,
requires an identical PONG and reconciles two snapshot collections. Trade updates
arriving during this check are captured and applied. A failed response or
reconciliation stops the control. A service gap reaching two health intervals
stops it rather than silently restoring eligibility. A new process never restores
a saved monotonic health timestamp. The subscription remains active throughout.

A quiet order stream alone is not failure. This is transport PING/PONG, not an
exchange heartbeat, market-quote freshness check or proof that all earlier order
events arrived. The [heartbeat contract](websocket_heartbeat.md) records why the
repeated-listen approach was replaced and how the real socket path is tested.
The deadline is enforced on received proof; synchronous capture/network work
still makes calls serialized rather than hard real-time. Reconciliation has its
own REST phase timeouts.

UTC timestamps determine deadlines; monotonic milliseconds determine health age.
Observed backward movement in either clock durably stops the owner. Positive
wall-clock jumps can make cancellation immediately due. Calls are serialized:
there is no background timer, and a blocked process/network call cannot cancel
on time. A launcher must service frequently, bound polling and record actual
cancellation lateness. Eligibility is checked before dispatch, not retroactively
inside an already dispatched request. Market quote age must be checked by the
future quote/order composition separately.

## Persistence and recovery

The control journal uses `paper_control_stop_deadline_v2`, schema 2, separate
from execution schemas 1-3. Schema-1 control journals remain recoverable stopped. Its exact configuration header is checksum framed.
Commands chain sequence and predecessor SHA-256 and retain replayed state.
Capacity is 256 deadline identities, 2,000 commands and 32 MiB, with the shared
64-KiB frame bound. Uncertain append poisons the writer and closes the session.
Complete corruption, wrong configuration and incomplete tails are rejected;
no truncation or automatic prefix salvage is implemented for this policy file.

Recovery replays deadlines/stops, appends a stop if needed and disconnects the
session. Even an owner that was active before restart returns stopped. The
schema-3 ledger retains financial reservations independently. A crash can leave
a policy deadline without an execution intent, or an execution intent/request
without a response. These separate commits do not authorize replaying a POST.
Raw capture, policy and execution each have their own commit boundary.

## Evidence and native check

`paper_control_diagnostic` produces eight checks across three constructed cases:
deadline/cancel plus operator stop, failed WebSocket PONG, and clock
regression/recovery. The existing transport contract group also exercises
uncertain policy append, corruption/torn tails, active-owner restart, missed
service, the final dispatch guard and partial inventory retained during stop.
No network requests are sent. Counts of routine groups remain 26 full/22 quick.

The independent transport verifier also checks control-journal chains and saved
policy state when a case includes them. Run it on the printed diagnostic folder.
The HTML presents saved control and execution state; no financial logic is
reimplemented in reporting.

On Windows, Build All and run `windows-debug-quick`, then select
`paper_control_diagnostic.exe` with Ctrl+F5. Run the read-only
`paper_transport_probe.exe` again to exercise the new PING/PONG
round trip and subsequent reconciliation against the service. It still submits
and cancels no orders. Retain each complete run directory. Native acceptance
of these new controls remains pending.
