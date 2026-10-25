# Runtime costs and portable verification

`runtime_benchmark` measures local C++ calls on constructed round trips. It does
not measure network latency, historical execution quality or strategy returns.
Release builds provide the performance reference; Debug and sanitized runs only
check correctness. No optimization or throughput target is assumed.

## Workload and parity

The default grid has 8, 32 and 128 round trips, five fresh-state repetitions and
eight modes: 120 result rows. Each trial starts with USD 100,000 and no inventory.
A round trip submits one buy share at USD 100, accepts and fills it, redelivers
the identical fill, then submits, accepts and fills one sell share at USD 100.01.
Fees are zero. The final oracle is starting cash plus one cent per round trip,
zero inventory/reservations/open orders and two filled orders per round trip.
Every applied/duplicate outcome and final order identity is checked.

Typed workloads include reconnect and exact reconciliation: `7n + 2` timed inputs.
Paper workloads include authorization, subscription and reconciliation: `7n + 3`.
They retain API-shaped raw JSON, stable client/broker/execution IDs and schema-3
nanodollar accounting. Typed journals use schema 2 and cents. Workload sizes and
terminal economics match; decoding, identity handling and payloads differ, so
cross-schema differences do not isolate one implementation feature.

| Mode | Timed operation |
| --- | --- |
| `core` | Apply each prebuilt typed input directly to `OrderRuntime` |
| `copy_candidate` | Copy the runtime, apply the same input and publish by move, reproducing the journal owner's additional candidate copy |
| `journal_memory` | Schema-2 candidate, serialization, framing, SHA-256 and append to volatile string storage |
| `journal_sync` | Same schema-2 calls through the exclusive platform file writer and real synchronization |
| `journal_recovery` | Open, read, validate configuration/checksums, replay and synchronize a new disconnect record |
| `paper_memory` | Schema-3 paper calls, decoder/identity conversion and volatile journal storage |
| `paper_sync` | Same schema-3 calls through the synchronized file writer |
| `paper_recovery` | Schema-3 open, validation, raw-command replay and synchronized disconnect |

The core already copies internal state transactionally; `copy_candidate` adds the
outer owner copy. No claims about another architecture follow from this control.
Volatile storage has no durability guarantee and is used only for measurement.
The synchronized adapter's nested append durations include its write/flush call,
but exclude framing and the outer owner copy. Differences between sequential
trials include scheduling/cache variation and are not pure hardware flush costs.

One untimed two-round-trip warm-up covers every mode and retains its journals.
Each repetition alternates core/copy order and the two schema blocks. Recovery
follows its synchronized source, copied to a fresh file outside the timer. These
are warm-cache, same-process replays, not cold boots or crash-time measurements.
Crash boundaries remain covered by the existing abrupt-exit contracts.

## Timing, resources and limits

`std::chrono::steady_clock` brackets individual calls. Retained raw nanoseconds
support nearest-rank p50/p95/p99, minimum, maximum and summed call time. Reported
throughput is timed calls divided by that sum; it excludes between-call checks,
workload construction, initial file creation, final validation and report export.
It is not end-to-end application throughput. Recovery rows contain one whole
operation per trial; their per-row percentiles are therefore identical. The
report also shows minimum/median/maximum across repeated trial p50 values.

A 1,000-sample empty-clock probe exposes instrumentation scale; its duration is
not subtracted. Nested storage timing adds overhead. Event-type and state-growth
effects are pooled within each input distribution; raw ordered samples and
workloads permit inspection. Five trials do not establish tail guarantees or a
confidence interval. CPU frequency, competing load, virtualization, filesystem,
cache state and antivirus are uncontrolled host conditions.

Linux reads process RSS/high-water memory from `/proc/self/status`; Windows reads
working-set/peak from `GetProcessMemoryInfo`. Sampling occurs outside timed calls.
These are process-wide sequential observations, including fixtures, evidence and
allocator retention; they do not isolate per-case allocations or transient copies.
Unavailable readings remain null. The report retains OS, CPU descriptor, logical
processor count, compiler, build configuration and source inventory.
The manifest/evidence also retain common and configuration-specific CMake C++
flags, the C++20 standard and the warnings-as-errors setting. Target warning
options remain specified by the retained root CMake source. Compiler/linker
defaults and externally injected launcher/environment options are not a captured
toolchain image.

The existing journal limits remain 10,000 inputs, 32 MiB per file and 64 KiB per
frame. Exhaustion refuses append/restart rather than rotating or compacting.
The benchmark deliberately caps each size at 128 round trips, four sizes and ten
repetitions; it records actual byte/record/frame occupancy. It does not establish
throughput at the journal caps. Retained orders, identity maps, traces and
candidate copies grow with input history. See the [journal contract](journal_recovery.md)
for corruption, incomplete tails and device-level durability limitations.

## Accounting and commit boundary

The [runtime transition table](execution_runtime.md#reservation-and-message-contracts)
defines submit, acknowledgement, incremental fill, cancellation, rejection and
reconciliation behavior. Buy reservations cover remaining quantity times limit
plus fee ceiling; sell reservations cannot exceed holdings. Fills conserve exact
cash/position units and release only the corresponding remaining reservation.
Pending cancellation retains resources until validated terminal evidence.

Once-only accounting requires serialized calls, stable nonconflicting execution
identities, validated quantities and replay through the same financial contract.
Duplicate delivery and duplicate execution IDs cannot book an extra fill.
Conflicting identities or incomplete/contradictory terminal evidence close entry.
The journal owner synchronizes the candidate input/checkpoint before publishing
state or returning dispatch eligibility. Recovery validates every complete frame
and durably closes routing until fresh connection and exact reconciliation.
An external send can succeed before its acknowledgement is received; durable
intent does not prove delivery. No end-to-end exactly-once dispatch guarantee or
automatic resend follows from local once-only accounting.

## Reproduction and artifacts

In Visual Studio, pull the task branch, select **windows-release**, **Build All**,
then **Test > Run Test Preset > windows-release-quick**. Select
**runtime_benchmark.exe** and use **Ctrl+F5**. Confirm the console says `Release`
and 120 rows. Retain the complete printed directory; timings vary across hosts.
For milestone regression use the full **windows-release** test preset once.
The quick benchmark contract uses two round trips and eight modes, rather than
running the full performance grid on every edit. No data or credentials are needed.

Linux:

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure
scratch/build-linux-release/runtime_benchmark
python -B research/verify_runtime_benchmark.py scratch/runs/runtime_benchmark_0
```

Use the directory actually printed; each run receives a new suffix.
The HTML includes state-size latency and whole-recovery figures, per-trial
throughput/resource tables and expandable evidence. `benchmark_metrics.csv`
retains portable summary rows. `benchmark_evidence.json` retains all timed inputs,
samples, expected orders and environment metadata. Mode journals, warm-up files,
`source_inventory.txt` and hashed `summary.json` complete the evidence.
The Python verifier checks checksums/chains, journal replay, analytical balances,
duplicates, terminal order parity, recovery closure, distributions/rates, nested
append timings and CSV agreement. It cannot independently reproduce elapsed
time or OS memory observations.

## Offline CI

`.github/workflows/offline-ci.yml` runs on pushes to `master` and
`research/continuation-study`, or manual dispatch. Standard `ubuntu-24.04` and
`windows-2025` runners build Release with warnings as errors. General documentation-
only pushes under `docs/`, `README.md` and `AGENTS.md` are skipped; research
protocols are outside that exclusion. The jobs run the full offline
routine contracts, then create and independently verify a constructed strategy/broker
demo from the clean checkout. The small benchmark contract's exported evidence
also receives independent verification. No private dataset or Alpaca secret is supplied.
An Ubuntu ASan/UBSan job covers the order, journal, exact paper, simulated broker,
measurement and benchmark contract groups. It enables leak detection on the hosted
runner. Integration-labelled full-period disk runs stay separate.

nlohmann/json 3.12.0 remains pinned by archive SHA-256. Actions are pinned to full
commit identities using Node 24, with read-only workflow permissions. Superseded runs on the
same branch are canceled. Test logs are retained for three days. No benchmark
speed threshold runs in CI: shared-host timing is unsuitable for such a gate.
Hosted-runner availability and the account's included quota remain external
constraints; this workflow adds no paid runner, service or subscription.

References: [GitHub workflow security](https://docs.github.com/en/actions/reference/security/secure-use),
[steady clock](https://en.cppreference.com/w/cpp/chrono/steady_clock),
[Microsoft process memory counters](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters).
