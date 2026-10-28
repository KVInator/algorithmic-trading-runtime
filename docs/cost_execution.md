# Cost and execution research

M6 is in progress. Its first implemented checkpoint registers the comparisons
and acquires a bounded historical quote sample. Historical account and execution
results are not available at this checkpoint. The v1 findings and M2-M5 evidence
retain their identities.

## Registered comparisons

The [governing contract](../research/cost_execution_protocol.md) fixes three
separate evidence layers:

| Layer | Registered design | Implementation status |
| --- | --- | --- |
| Bar references | 0, 0.5, 1, 2 and 5 bp per side, crossed with one-/two-minute delay; primary signals and entry hurdles fixed | Extended comparisons remain open |
| Historical SIP quotes | Seven input-selected dates, five instruments, thirteen decision clocks and two delays: 910 one-second windows | Windows acquisition complete; 232 eligible windows, all in 2022-2023 |
| Synthetic execution | 170 matched one-factor workloads crossed with four limit/deadline policies: 680 cases | Workload plan registered and checked; execution evaluation remains open |

Daily risk remains a separate experiment with intraday-risk, daily-only,
static-risk-budget and passive accounts. It will reuse the existing lagged
estimator and dividend-aware fractional account. If a required actual price is
unavailable, the continuous account stops; subsequent outcomes remain unavailable.
The three registered missing 2019 closes cannot be filled, bridged or repaired
with a capital reset. Recovery of a forecast history is a different operation.

## Historical quote sample

For each year 2017-2023, the plan takes the earliest regular date on which all five
instruments have operational inputs in the accepted M2 masks. No strategy return
or quote is consulted for selection. It samples decision offsets 30, 60, ..., 360
and 385 minutes from the open, followed by the two registered delays. Each query
covers the inclusive second ending at its reference instant. This is a small
descriptive sample of 35 instrument-days; it does not establish typical spreads
across years or market conditions.

The sampler follows continuation tokens even after an empty page. A continued
sixteenth page leaves the window incomplete. It selects the latest timestamp
only after completing pagination, then checks geometry, exchange fields, sizes
and regular condition flags. Invalid latest quotes are not replaced with older
favorable records. Conflicting latest records are ambiguous; exact duplicates
remain eligible. Nanosecond clocks and the pre-2024 round-lot size convention
are retained.

For eligible bid $b$ and ask $a$, the report derives

$$
\text{spread}_{bp}=10^4\frac{a-b}{(a+b)/2},\qquad
\text{half-spread}_{bp}=\frac{\text{spread}_{bp}}{2}.
$$

Quoted crossing costs replace an all-in charge in their own comparison; adding
both would double count. Quotes do not establish actual fills, queue priority,
replenishment, market impact or historical client receipt latency. The report
shows coverage, source age, displayed shares and sampled half-spreads by symbol,
year and clock. Ineligible windows stay visible as unestimated observations.

## Archive and verification

`CostQuotePlan` authenticates the accepted M2 study and mask identities without
loading strategy prices. `CostQuoteSample` uses the same `MarketPageArchive`
as historical panel acquisition. Raw HTTP byte envelopes are retained before
decoding; their format is unchanged. A fixed plan and exclusive writer lock
protect resumptions. Completed pages are reused. Retry receipts remain contiguous
and bounded, with the existing pacing policy.

The default quote archive is
`scratch/datasets/cost_quote_sample_2017_2023`. Each invocation creates a separate
`scratch/runs/cost_quotes_<suffix>` report. A transport interruption retains its
completed pages; rerunning resumes that archive. A retained 401/403 stops further
requests and produces unestimated coverage. It does not prompt a purchase, change
feed or repeat the denied request. A new archive path is an explicit new provider
vintage; old evidence remains intact.

`cost_execution_check` exercises twelve constructed acquisition cases plus
future-clock and checksum rejection. `verify_cost_quotes.py` independently reads
the raw byte envelopes, authenticates their identities and reconstructs request
paths, page chains, latest-record selection, exact price/share units, source age
and spread arithmetic. The provider route also checks the original M2 masks on
the producing machine. The compact review bundle contains summaries and HTML;
it is not a replacement for that local raw-evidence verification.

Local verification on 28 October 2026 used GNU 13.3.0, C++20 Release and warnings
as errors. The quote diagnostic passed 330 checks in twelve cases, including
the authenticated accepted-M2 mask plan. The independent oracle verified 1,560
constructed windows and rejected six deliberately altered arithmetic/coverage
summaries. Address/undefined-behavior instrumentation of the changed acquisition
path passed both the 330-check quote diagnostic and the 187-check panel suite.
The original 259-check demo, 182-check shared-capital and 313-check process-broker
diagnostics and their independent verifiers also passed. All thirteen registered
protocol identities and the original publication hashes remain valid.

## Windows quote acceptance and coverage

Windows Release execution on 28 October 2026 passed all 330 native checks and
the local raw-archive verifier. The compact native review contains twelve cases
and 1,560 windows matching the GNU reference decisions and measurements.
Historical acquisition retained 1,322 HTTP responses for the 910 planned windows.
The producer's independent local verifier passed. Compact review checked saved
arithmetic, coverage and displayed records; raw response bodies were not uploaded
and were not reauthenticated during that review.

| Window disposition | Count |
| --- | ---: |
| Eligible | 232 |
| Failed registered condition rule (`nonregular`) | 520 |
| Ambiguous latest timestamp | 76 |
| Empty | 80 |
| Locked | 2 |

The [instrument/date coverage table](evidence/m6_quote_coverage.csv) retains all
35 sampled instrument-days. Every 2017-2021 group has zero eligible observations;
2022 contributes 107 and 2023 contributes 125. The sample therefore does not
establish eligible spread evidence across all seven years. A `nonregular` result
records failure of the fixed condition rule; it does not establish that market
trading itself was abnormal. No eligibility rule or sample was changed after
inspection.

| Retained review identity | SHA-256 |
| --- | --- |
| Provider summary | `89a35fc123c43923f573e9f1809aa5fdd2391a3e60baf9ee8bea77d02f89424f` |
| Provider review ZIP | `38eba47ca73bd16165bbe36c8a32f1356c00b0f205ef8fceee3ce312e8c0958c` |
| Native check summary | `51cf72ebc6643d3eaa18292d74c0b388ab7134d9d0e5fe5dcf22b8f58fce13b4` |
| Native review ZIP | `e15550f24185a5fc58bc0af009f42bbfb1ae84de479aaef503ba3776e2f5df10` |

## Static plots and saved-report rendering

The report embeds coverage/status SVGs and every instrument/year's spread plot
and table directly in the HTML. They do not depend on JavaScript calculations.
The initial detail is the first group with eligible observations in plan order;
it is SPY 2022 for the accepted sample. JavaScript only changes the visible
group. Empty groups display an explicit unavailable message, with no invented
zero spread or axis estimate. Script-disabled viewers retain the overview and
initial detail.

`cost_quote_acquire --render <saved-summary>` creates a new view directory,
preserves the original summary bytes and does not access credentials, masks,
raw archives or the network. The renderer source identity is separate from
the original producer identity. `verify_cost_quotes.py --report-only` checks
the embedded records, coverage cells, status widths, scatter coordinates and
table labels without claiming to authenticate raw HTTP. The default verifier
still performs its full local raw-archive audit and also checks these views.

The [runbook](running.md#cost-and-execution-inputs-m6) gives the Windows commands.
The provider sampler uses only the existing historical-data credential route.
The M6 input checkpoint is accepted with the recorded coverage limitation.
M6 still requires the cost, synthetic execution and daily-risk evaluations. M7 adds stability,
concentration, risk matching and uncertainty; M8-M9 evaluate the reserved periods;
M10 consolidates publication and release.
