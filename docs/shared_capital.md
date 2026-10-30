# Shared capital, ownership and recovery

M4 implements the registered `MULTI-ASSET-01` shared account. One C++
`OrderRuntime` holds one USD balance and five symbol-keyed inventories.
`SharedCapitalBook` adds strategy ownership, admission and netting;
`JournaledSharedCapital` owns durable publication. Existing single-symbol
interfaces and journal schemas 1-3 retain their contracts. Shared journals use
schema 4 and the existing checksum, storage-lock and synchronization machinery.

## Budgets and accounting

The initial balance is $100,000. The three owners per instrument are momentum,
volume-conditioned continuation and reversion. Daily risk remains a separate
experiment. All shares are whole, all cash/price/fee units are USD $10^{-9}$,
and intermediate overflow is rejected. No borrowing or short positions are
permitted.

For marked equity $V$, owner $i$ and instrument $s$, a fresh long target is

$$q^*_{i,s}=\left\lfloor\frac{\lfloor V/15\rfloor}{A_s+f}\right\rfloor,$$

where $A_s$ is the synthetic ask and $f$ is the explicit per-share fee ceiling.
Cash targets are zero. The registered admission caps are $V/15$ per owner,
$V/3$ per family, $V/5$ per instrument and $V$ gross. Exposure includes held
shares at the integer midpoint and pending buys at the greater of midpoint and
limit, plus their fee ceiling. Pending sales do not release inventory or fund
new buys. One open order per symbol and five in total are permitted.

Owner cash flows start at zero. They record signed trade cash, allocated fees
and marked internal transfers. They are attribution records, not separately
funded accounts or independent buying power. At every saved transition:

$$C=C_0+\sum_{i,s}F_{i,s},\qquad
V=C+\sum_{i,s}q_{i,s}M_s
=C_0+\sum_{i,s}(F_{i,s}+q_{i,s}M_s).$$

Market moves and fees can create marked cap breaches after admission. These
remain visible. Relevant pending buys are canceled and new buys cannot worsen
the corresponding cap. A fresh target can reduce held exposure; the system
does not pretend that market-driven breaches never occur. Cash and inventory
reservations remain hard invariants.

## Netting and asynchronous ownership

All decisions at one clock enter a canonical target batch. Each owner has a
strictly increasing decision clock; identical batch identities are idempotent,
while conflicting reuse closes routing. Every batch supplies five causal
quotes. The operational maximum quote age is two seconds and the target
dispatch window is thirty seconds. These are engineering assumptions, not
estimated market latency or optimized economic parameters.

Free inventory can cross between owners of the same symbol at the recorded
midpoint, with no global cash change, external turnover or synthetic fee. The
recipient must have owner/family budget room. Remaining sells route before
buys; instruments and owner ties use lexical order. This deterministic priority
is explicit and does not claim optimal execution.

Each dispatched net order freezes its owner claims until terminal confirmation.
New intent can request cancellation but cannot reassign a race fill to a newer
owner. Incremental shares and fees use proportional largest-remainder
allocation, with lexical owner ties. The C++ kernel avoids wide platform-specific
integers; Python arbitrary-precision `divmod` independently checks its results.
Stable execution identities prevent repeated booking. Terminal cancellation
releases only the unfilled claims; subsequent free inventory can then cross.

Expired intent authorizes no new entry. It retains held inventory and cancels
inconsistent pending quantities; expiry is not a forced liquidation. Valid
fills still book while disconnected. Recovery starts closed and requires exact
global cash, all five positions and the full open-order terms before routing.
Reconciliation compares state; it never overwrites a disagreement.

## Durable publication and bounded history

An input, outcome, dispatch authorization and full checkpoint are synchronized
before execution returns them to a caller. Append uncertainty latches that
writer unhealthy. Recovery distinguishes no append, an incomplete tail and a
complete but unacknowledged append. A verified prefix is written to a separate
destination; damaged source bytes are retained. No recovery automatically
resends an uncertain order.

Segments retain the existing bounds: 10,000 inputs, 32 MiB and 64 KiB per frame.
Reaching the record or segment-byte cap stops the writer; an oversized frame
is rejected before append. Planned handover is **flat only**:
zero held inventory, cash targets and no open orders. The predecessor is sealed
before a successor header is synchronized. The successor carries global cash,
owner attribution flows and the clock, links the predecessor's final frame
digest, and advances the order-generation namespace. Active-order recovery is
supported within a segment; active-order segment handover is rejected.
Compaction of live orders and automatic rollover are not implemented.

An uncertain successor publication leaves the predecessor sealed. A committed
successor header can be recovered with its expected generation and parent
digest. Late reports from a retired generation cannot book again and require
review/reconciliation. Exclusive storage locks prevent two writers from owning
the same file.

## Evidence and reproduction

Build Release with `py -3 -B build.py --configuration Release`, or the existing
Visual Studio **Release | x64** solution. Run `shared_capital_diagnostic.exe`.
The application prints its unique `scratch/runs/shared_capital_N` directory.
Then run, using the actual suffix:

```sh
py -3 -B scratch/reference_suite/research/verify_shared_capital.py scratch/runs/shared_capital_0 --self-check
```

The verifier writes `review.zip`, containing the manifest, interactive HTML and
small synthetic journals, including retained rejected artifacts. No private
market archive or credentials are included. The HTML has a scenario selector,
event slider, ownership bars, global cash/reservation cards and owner/order
tables tied to the audited checkpoints.

Local GNU 13.3.0 Release runs passed 182 native checks in 15 cases. Independent
verification reconstructed 163 durable transitions, checked the constructed
primary features/adapter and exact report data, and rejected seven semantic
corruptions after deliberately bypassing checksum validation. Exact allocation
checks include tied fractional claims and products exceeding 64-bit range.
The existing 259-check demo, 187-check panel suite and 46 capability checks
passed. Their independent oracles and publication checks also passed. Native
momentum, continuation, risk-measurement and daily-risk regression contracts
were rebuilt against the extended core. AddressSanitizer and
UndefinedBehaviorSanitizer passed the changed execution, codec, journal,
continuation, report and diagnostic paths; other library units were not
instrumented. LeakSanitizer was disabled because process tracing prevents its
execution in the verification environment. All 15 report selector branches and
163 event views executed; representative generated ownership charts were
rendered and inspected. A full browser layout check was unavailable locally.
Windows acceptance is recorded below. These counts describe deterministic
engineering cases, not a statistical estimate of reliability.

All quotes, reports, receipts and write faults here are constructed. A declared
twenty-session decaying-price fixture with a fixed sinusoidal perturbation
initializes the existing M3 engines; a
final dip/recovery session exercises all three families against actual owned
inventory. No historical shared-account P&L is reported and no 2024-2025
strategy input is opened. M5 supplies the separate local synthetic broker;
M6-M7 provide broader cost, execution and stability assessments.

## Windows acceptance

The Windows Release diagnostic used MSVC 19.51.36260 and passed all 182 native
checks in 15 cases on 28 October 2026. Independent review replayed all 163
durable transitions from the uploaded synthetic journals, including exact
cash, owner attribution, proportional quantities/fees, admission limits,
cancellation races, active-order recovery and linked flat handover. The
verifier also checked the constructed primary features, actual-inventory
adapter decisions, report data and seven semantic corruption rejections.

All 182 check labels, case configurations and 15 final account states matched
the independently verified GNU run exactly. The separate summary and HTML
uploads matched the review ZIP byte for byte. All 15 report selector branches,
163 event views and navigation boundaries executed successfully. This verifies
the saved-event controller and data; it is not a full browser layout check.

| Accepted record | SHA-256 |
| --- | --- |
| Uploaded review ZIP | `c6c5e3b0a436b36f1a722ebe11598311043d5c3fd94136bfffd0a840519e76c2` |
| Summary | `7ea72809504ac69d53ec7eae8d55d7f701ad58cd350e72c3a047f5649524e54b` |
| HTML report | `703348e6d15bc577d818905ed3f6869daea96056326e11988fc3b3f8efd5171f` |
| Reported native build-source inventory | `2e099d8a44cb8ecfe94b609847256268ac1f6afde059ea76d483ce25d96dd074` |

The reported source inventory identifies producer-file bytes; it is not an
independently established Git commit. The ZIP contains all 21 manifested
artifacts, including retained rejected files, so the account and journal review
does not depend on a private raw archive. Network requests and external broker
observations were zero. Historical shared-capital performance was not scored,
and reserved 2024-2025 strategy inputs remained unopened. M4 is complete.
The subsequent [M5 acceptance](process_broker.md#windows-acceptance) records the
separate synthetic broker and its lifecycle/recovery policies; M6's
[economic accounts](cost_execution.md#windows-account-acceptance) retain their
own observed-prefix coverage limits.

## Budget-limited order regression

Historical M6 integration exercises admission below an owner's complete desired
position. Such a pending order remains valid while its projected shares move
toward that intent. Cancellation checks an overshoot or reversal, alongside the
existing expiry and aggregate-limit rules; it does not require every admitted
order to complete the full target. Dispatch-time owner claims remain immutable.

The focused diagnostic holds 132 SPY shares across two owners, reprices SPY from
USD 100 to 110, and admits 52 shares of a third owner's 61-share target under
the instrument cap. Acknowledgment must not cancel that request; its fill leaves
USD 81,080 cash, 52 shares assigned to the third owner and no replacement loop.
The earlier equality-based cancellation rule fails this regression. With the
corrected rule, GNU Release passes 190 checks in sixteen cases and the independent
oracle verifies 171 durable transitions plus its seven semantic mutations.
The oracle also requires admissible routing and internal transfers to be present,
rather than only checking the terms of orders that happen to be reported.
These are local regression results; the original Windows M4 acceptance identities
and counts above remain historical evidence.
