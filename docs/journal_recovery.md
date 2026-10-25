# Durable journal and restart recovery

`JournaledRuntime` wraps the existing whole-share, integer-cent `OrderRuntime`.
The separate [schema-3 paper owner](paper_execution.md) reuses the same order
engine with a declared finer unit and shares `JournalFrame` encoding.
Every input is evaluated on a candidate core, framed with its resulting trace,
appended and synchronized before the candidate becomes visible or an outcome
returns. Future transport code may dispatch an applied submit/cancel only after
that successful return. External dispatch belongs to the coordinating broker/session layer.

## Commit and recovery contract

1. Open a new file without replacement, or acquire exclusive writer ownership of
   an existing file. Parents must already exist; a final journal file that is a
   symbolic/reparse link is refused.
2. Persist a versioned header containing the symbol, starting cash and limits.
   Recovery requires the caller's expected configuration to match exactly.
3. Encode each typed input and the core's complete resulting trace entry. Each
   record contains a sequence number and the preceding record's SHA-256.
4. Append the JSON payload, a tab, its SHA-256 and an LF terminator. Synchronize
   the file, then publish the candidate state by a nonthrowing move.
5. On restart, validate every complete frame and replay every input through the
   same core. Require the resulting checkpoint, sequence and chain to match.
6. Append and synchronize a new disconnect input before returning the recovered
   runtime. New orders remain blocked until reconnection and an exact broker
   snapshot match. Historical reconciliation cannot reopen the restart gate.

New files also begin with a durable disconnect. Event IDs, execution IDs,
cancellation correlation, terminal orders, reservations, denied commands and
quarantined reports are reconstructed from the input sequence. The original
six delivery schedules replay with identical pre-restart traces through this
wrapper. No second order/accounting implementation or serialized-state shortcut
is used. JSON decoding rejects duplicate keys, unknown shapes, excessive nesting,
fractional quantities and unsupported model/schema versions.

Schema 1 remains the default for the original order/recovery diagnostic. The
[offline pipeline](strategy_execution.md) explicitly creates schema-2 journals,
which additionally persist whole-share strategy targets. The financial model
contract remains `whole_share_limit_v1`. Recovery accepts both schemas and keeps
the original schema when appending its restart barrier. A schema-1 writer refuses
target inputs before I/O; records are never silently upgraded or rewritten.

The platform storage contract returns from an append only after synchronization.
Windows uses `CreateFileW` with read sharing and exclusive writer access,
`WriteFile`, and `FlushFileBuffers`. POSIX uses `O_APPEND`, a nonblocking exclusive
`flock`, complete write loops, and `fsync`; a newly created journal's parent
directory is synchronized as well. The writer owns its handle until destruction.
Calls within one runtime must remain serialized.

## Interrupted and uncertain writes

| Observed file state | Recovery behavior |
| --- | --- |
| Complete valid frames | Replay all frames and persist a fresh disconnect barrier |
| Final frame lacks an LF terminator | Ordinary recovery refuses to append; inspection reports the complete prefix and remaining byte count |
| Explicit verified-prefix recovery | Copy only validated complete bytes into a new empty file, synchronize, then append the restart barrier; retain the original file unchanged |
| Any complete frame has invalid framing, checksum, schema, chain, sequence or replay checkpoint | Reject recovery, including prefix recovery; never skip the frame |
| Header is absent or incomplete | Reject recovery; never guess initial state |
| Append or synchronization throws | Poison the writer; refuse further inputs and state queries until the handle is closed and the journal is recovered |

A complete frame can survive even when the caller never receives success, for
example after a flush followed by process termination. Recovery retains that
input. An uncertain order intent therefore retains its order ID and reservation;
it is not automatically sent again. An uncertain fill is booked once on replay
and its execution ID remains idempotent on broker redelivery. If no complete
frame survived, only the preceding valid state is recovered. Reconciliation,
rather than blind retry, resolves dispatch ambiguity.

The file is bounded to 32 MiB, each frame to 64 KiB and the journal to 10,000 input
records. Exhaustion stops further appends, including a restart barrier. Rotation,
compaction and snapshots are not implemented. These bounds and per-input core
copies suit the current correctness diagnostic, not an established throughput
target. Checksums detect corruption and chain discontinuities; they are not
authentication and cannot detect removal of an entire valid suffix without an
external trusted tip. Storage guarantees depend on the filesystem and device;
the tests cover process termination and injected I/O failures, not physical
power loss or controller failure.

## Verification

`journaled_runtime_contracts` runs injected I/O faults and four child processes
that terminate with `std::_Exit`, bypassing C++ destruction. Cases stop before
append, after synchronization but before publication, after a partial frame and
after return. Contracts verify poison handling, exclusive writers, configuration
mismatch, chain/sequence/checkpoint corruption, incomplete headers and legacy
schema replay. The tests preserve the original damaged evidence.

The optional regression suite includes the abrupt-exit and corruption cases.
`project_demo` exposes the paired broker restart/reconciliation path. Build and
verification procedures are in the [runbook](running.md).

## Platform references

- Microsoft: [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
  and [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/flushfilebuffers).
- Linux: [fsync](https://man7.org/linux/man-pages/man2/fsync.2.html)
  and [flock](https://man7.org/linux/man-pages/man2/flock.2.html).
