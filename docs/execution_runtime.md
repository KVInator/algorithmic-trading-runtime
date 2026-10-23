# Deterministic execution runtime

The execution core maintains limit-order state, cash, positions and resource
reservations under scripted broker-message delivery. Its contracts concern
execution correctness independently of the economic results of the historical
SPY studies. `execution_diagnostic` runs offline with synthetic prices and fills.

## Model and ownership

`trading::execution::OrderRuntime` owns a single symbol, integer USD cents,
whole-share inventory, orders, cancellation identities, processed event and
execution identities, and a sequential trace. Calls are serialized in arrival
order. The initial diagnostic account has USD 10,000, zero shares, a twenty-share
position limit and a four-open-order limit. Each scenario starts a fresh account.

This is an explicit execution model. The existing `LongCashAccount` remains the
authoritative fractional, target-weight research account with dividend and split
handling. Historical rebalances do not become broker fills. The execution model
does not implement corporate actions, fractional shares, shorting, leverage,
interest, settlement delays, order replacement or execution corrections/busts.

The order interface accepts limit orders with an explicit per-share fee ceiling.
The ceiling must be nonnegative and smaller than the positive limit price.
Prices, quantities, fees and intermediate products must fit signed 64-bit units.
Overflow is rejected before the candidate state is committed.

## Reservation and message contracts

| Event | State and accounting effect |
| --- | --- |
| Submit | Reserve remaining buy shares times (limit price plus fee ceiling), or remaining sell shares, before dispatch |
| New acknowledgement | Advance `pending_new` to `working`; delayed acknowledgements retain later fill/cancel states |
| Incremental fill | Apply quantity times actual price and explicit fee once; reduce the remaining reservation; preserve a pending cancel unless fully filled |
| Cancel request | Enter `pending_cancel` with a unique cancel ID; retain every remaining reservation |
| Cancel rejection | Restore working/partial state only for the currently pending cancel ID; older cancel rejections are obsolete |
| Cancel confirmation | Require cumulative filled shares to equal the received execution total before releasing the remainder |
| New-order rejection | Release resources only if neither a new acknowledgement nor any execution was previously accepted |
| Disconnect/reconnect | Retain resources and close the entry gate; reconnect alone does not reopen it |
| Reconciliation | Compare cash, position and the complete set of open-order terms and filled quantities; clear the gate only on an exact connected match |

Position limits include current holdings plus all outstanding buys. Pending
sales do not offset potential purchases. Sell reservations cannot exceed current
holdings; buy reservations cannot exceed cash. Buy fills cannot exceed their
limit price, sell fills cannot fall below it, and each fill's fee cannot exceed
its quantity times the declared fee ceiling. Every active order counts toward
the open-order limit, including unacknowledged and pending-cancel orders.

Event IDs identify deliveries; execution IDs independently identify fills.
An identical event is idempotent. A fill repeated under a different event ID is
also idempotent when its execution ID and economic fields match. Conflicting
reuse, unknown orders, impossible quantities, invalid terms and contradictory
terminal reports are quarantined. IDs remain reserved for the runtime's lifetime.

Each broker report is evaluated on a candidate copy of the state. A contract
failure leaves orders, cash, inventory, deduplication identities and reservations
unchanged, adds a reason to the trace and latches the entry gate. Broker deliveries
and connected cancellation requests remain available while entry is gated.
Quarantined messages are not marked processed: missing executions can arrive,
the terminal message can be retried, and reconciliation can then confirm the
account. Matching snapshots confirm state; they never overwrite discrepancies.

An adapter must provide a coherent broker snapshot after the relevant execution
stream has been drained. This diagnostic supplies explicit snapshots; it does
not establish an external sequence barrier or automatically repair discrepancies.
Undelivered commands, externally canceled orders and missing terminal events
retain reservations until explicit evidence is delivered. Late new fills after
an accepted terminal cancellation require investigation and are quarantined.

## Diagnostic and evidence

Build all targets, run the routine test preset, then run
`execution_diagnostic.exe` with **Ctrl+F5** in Visual Studio. Linux:

```sh
cmake --build --preset linux-debug
ctest --preset linux-debug
./scratch/build-linux/execution_diagnostic
```

Six schedules cover partial fills, repeated event and execution IDs, delayed
acknowledgements, cancellation races and stale rejection IDs, missing executions,
disconnect/reconnect, resource limits, partial sales and inconsistent reports.
Every expected transition and final integer balance is checked. Replaying each
schedule from a new runtime must produce an identical complete trace.

Each fresh `scratch/runs/execution_diagnostic_*` directory contains:

| File | Purpose |
| --- | --- |
| `report.html` | Six event timelines, account states, reservations, gate changes and fault reasons |
| `inputs.json` | All submitted commands, delivered reports, snapshots and expected outcomes |
| `trace.csv` | One post-input state per logical sequence number |
| `source_inventory.txt` | Exact configure-time per-file hashes used for the aggregate source identity |
| `summary.json` | Expected/final balances, replay status, build identity and artifact hashes |

The inventory retains the existing source-fingerprint scope: `src/` and `apps/`
C++ files, `research/` files, `CMakeLists.txt` and `CMakePresets.json`. It is
embedded in the binary together with its aggregate SHA-256; its file hash equals
that aggregate. CMake also writes the inventory in the build directory. Tests
and general documentation are outside this established fingerprint scope.
Inventories must be compared for the same intended source revision. The first
native inventory also explains the earlier SPY-RISK-01 aggregate difference;
the [result record](../research/daily_risk_development_result.md#verification-and-provenance)
retains the reconstruction and its limits.

## Native verification

The native MSVC 19.51.36260.0 Debug diagnostic for source revision `f5992df`
completed with exit code zero on 23 October 2026. The accompanying build and all
seventeen routine test groups were reported successful. The uploaded manifest's
SHA-256 is `84a8fdeceeb61107c588d53e65710ae6cab4563eee6cf793ecb94d6a012559bc`;
all four artifact hashes match the manifest's recorded identities.

The complete `inputs.json` and 69-row `trace.csv` are byte-identical to the Linux
diagnostic. The HTML differs only in its displayed source digest. An independent
integer ledger reproduces cash, positions, both reservation balances,
outstanding buys and open-order counts after every input. All six scenarios end
with their expected balances, no open orders or reservations, and an open entry
gate. Ten quarantined reports and seven denied commands are expected fault
cases; their complete traces match across the two compilers.

The native source inventory contains 116 entries: 108 exact local byte matches
and eight exact matches after LF-to-CRLF conversion. All execution-core files
match without conversion. The raw platform fingerprints differ, while the
inventory accounts for every file within the declared source scope. The eight
paths and the reconstruction of the earlier risk-study aggregate are retained in
the [curated provenance record](../research/daily_risk_development_result.json).

## Remaining scope

Runtime contracts additionally check acknowledgement/fill delivery permutations,
exact integer cash identities, reservation retention, full-order snapshot
comparison, numerical overflow and invalid inputs. The order core retains an
in-memory trace and copies account state per report to make validation atomic.
The [journaled wrapper](journal_recovery.md) adds synchronized input persistence,
verified replay, a restart gate and separate recovery of an incomplete tail.
Broker transport, automatic resend, market matching/liquidity, strategy-to-order
integration and throughput targets remain outside the implemented scope.
Passing these synthetic checks establishes neither executable strategy returns
nor live-trading readiness.
