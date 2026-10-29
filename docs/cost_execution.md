# Cost and execution research

M6 is in progress. Historical quote acquisition and cached diagnosis are accepted.
Windows historical cost/delay evaluation is accepted as a development comparison.
Continuous-account and synthetic execution results remain separate work. The v1
findings and M2-M5 evidence retain their identities.

## Registered comparisons

The [governing contract](../research/cost_execution_protocol.md) fixes three
separate evidence layers:

| Layer | Registered design | Implementation status |
| --- | --- | --- |
| Bar references | 0, 0.5, 1, 2 and 5 bp per side, crossed with one-/two-minute delay; primary signals and entry hurdles fixed | Windows development comparison and independent local verification accepted: 400 series, 240 paired contrasts |
| Historical SIP quotes | Seven input-selected dates, five instruments, thirteen decision clocks and two delays: 910 one-second windows | Windows acquisition complete; 232 eligible windows, all in 2022-2023 |
| Synthetic execution | 170 matched one-factor workloads crossed with four limit/deadline policies: 680 cases | Workload plan registered and checked; execution evaluation remains open |

Daily risk remains a separate experiment with intraday-risk, daily-only,
static-risk-budget and passive accounts. It will reuse the existing lagged
estimator and dividend-aware fractional account. If a required actual price is
unavailable, the continuous account stops; subsequent outcomes remain unavailable.
The three registered missing 2019 closes cannot be filled, bridged or repaired
with a capital reset. Recovery of a forecast history is a different operation.

## Frozen-decision cost and delay evaluation

`cost_delay_development` reads the accepted M2 panel through the existing
authenticated pre-2024 reader. `PanelReplayState` produces each day's primary
features, fits and decisions once. The same loaded observations then supply
execution opens for both delays; no second strategy or history implementation
is introduced. Costs never alter the reversion entry hurdle or continuation
targets. At one minute and one basis point, every primary trade and daily return
must match the existing M3 reference path within its numerical tolerance.

Eight policies comprise momentum/always-long, continuation price/volume, and
reversion candidate/sign-only/drift/always-long. Five instruments, eight policies,
five charges and two delays produce 400 series. Four candidate-minus-control
contrasts are retained for each instrument and for an equal-weight average of
the five paired daily differences: 240 comparisons. No case is chosen as a new
primary policy after scoring. Shared-capital portfolio performance is not
inferred by adding these separately normalized series.

For entry/exit raw opens $B_j,S_j$ and the same per-side charge $c$ on all trades,

$$
1+r_{gross}=\prod_j\frac{S_j}{B_j},\qquad
1+r_{net}=\prod_j\frac{S_j(1-c)}{B_j(1+c)},\qquad
c=10^{-4}\,\mathrm{cost}_{bp}.
$$

The existing fractional `LongCashAccount`, initialized with one unit of cash per
daily reference, independently checks the return kernel through actual buy/sell
rebalances. Turnover is the sum of purchased and sold notional divided by that
initial equity; paid cost is $c$ times turnover. Holding time uses executed
reference clocks. Cash days have observed zero return and zero turnover, while
unavailable dates have NA outcomes. The common operational mask covers both
delays and all five instruments and is applied after decisions. It does not
authorize trading or change an earlier signal. Initialization and shortened
sessions remain on the original calendar with no scored intraday return.

The report's means, active dates, turnover and cost totals use the same eligible
dates for every case. Return products and drawdowns are explicitly conditional
on those dates; they do not represent a continuous funded account across excluded
dates. Continuous daily-risk accounts retain the separate missing-mark stop rule.
The equal-weight paired curve averages five same-date return differences; it is
a research contrast, not an execution or shared-cash ledger.

`verify_cost_delay.py` reuses the independent M3 feature/history oracle, then
reconstructs delayed opens and fractional cash/share arithmetic with 50-digit
Decimal calculations. It checks every recorded path, the 400-series and
240-comparison matrices, calendar denominators, static chart points/lines and
all displayed table values. Its semantic corruption checks bypass hashes to
exercise financial verification. A provider `review.zip` contains the summary
and HTML; full local verification additionally requires the five path files and
original panel. Synthetic review bundles include their small generated paths.

SVG line coordinates are serialized to six decimal places. Verification bounds
each coordinate's error by half that last place plus a $10^{-9}$ canvas-unit
floating-point allowance. One-to-one line matching retains duplicates and
rejects missing, extra, reordered or altered vertices. Comparing coarser rounded
buckets can falsely reject valid coordinates at a bucket boundary; the Windows
historical report reproduced two such cases on 29 October 2026. This correction
changes only SVG verification, with all financial tolerances and saved artifacts
unchanged. Regression checks retain both observed boundary cases and malformed
line/coordinate rejection.

The HTML embeds all 44 plots and 400 case rows. JavaScript only selects among
the forty instrument/policy panels; all plots remain available without scripts.
No acquisition, credentials, quote-code changes or later-period prices are
needed. The [runbook](running.md#historical-cost-and-delay-evaluation-m6) gives
the constructed and historical commands. Windows acceptance is recorded below.

Local verification used GNU 13.3.0, C++20 Release and warnings as errors. The
constructed evaluator passed 32 numerical/failure checks and 300 five-instrument
calendar dates. Its 47 common development dates per instrument produced 18,800
eligible daily cost-case outcomes; initialization and the excluded date remained
explicit. The independent oracle verified all causal primary records, priced
paths, summary matrices and report marks, and rejected six semantic corruptions
without relying on hashes. The panel suite passed 190 checks, including the
market-aware reader and its unchanged original record interface; its source and
300-date replay oracles also passed. AddressSanitizer/UndefinedBehaviorSanitizer
passed the changed cost, report, dataset-reader and fractional-account units
with the constructed run. Other archive units were not instrumented and
LeakSanitizer was disabled in the traced verification environment. All forty
report-selector branches executed; static overview and detail layouts were
rendered and inspected. These are constructed checks, not historical findings.

## Windows cost/delay acceptance

Windows Release evaluation completed on 29 October 2026 with MSVC 19.51.36260.
The reader retained 2,012 calendar records per instrument, including 252
initialization dates in 2016 and 1,760 development dates in 2017-2023. All cases
use the same 1,682 common operational development dates per instrument, or 95.57%
of the development exchange calendar. The 400 series contain 672,800 eligible
daily cost-case outcomes; the other dates remain unavailable rather than zero.
Primary-reference parity passed and reserved strategy inputs remained unopened.
Network requests and observed broker events were zero.

The producing machine's full independent verifier authenticated the source
observations, causal histories/decisions, delayed references and Decimal cash
arithmetic. It passed six semantic corruption rejections and fifteen SVG
serialization checks. Compact review independently checked both summary matrices,
paid-cost/notional identities, invariant activation across all cases, decreasing
returns with increasing charges, each paired/equal-weight mean, and all static
curves and table values. Raw observations and the five large daily path files
remain local and were not reauthenticated during compact review.

At the registered primary one-minute delay and one basis point per side, the
equal-weight arithmetic averages of instrument-level daily means are:

| Candidate / control | Candidate net bp/day | Control net bp/day | Paired increment bp/day |
| --- | ---: | ---: | ---: |
| Momentum / same-window always-long | -1.0621 | -2.4308 | +1.3687 |
| Volume continuation / price-only continuation | +0.2770 | +0.0358 | +0.2412 |
| Reversion candidate / sign-only | -0.3864 | -1.0028 | +0.6165 |
| Reversion candidate / drift | -0.3864 | -0.3894 | +0.0031 |

All means include eligible cash days. Positive paired differences do not imply
positive candidate returns: momentum and reversion remain negative after the
primary charge. Volume continuation has a positive pooled net mean, but its
increment over price-only changes from -0.2589 bp/day at zero charge to +0.2412
bp/day at the primary charge. Lower turnover therefore matters to the relative
result; this does not establish improved gross signal quality. The primary
continuation increment is positive for SPY, IWM, TLT and GLD and negative for QQQ
(-0.9501 bp/day). At five basis points per side and the one-minute delay, both
continuation means are negative. The near-zero reversion/drift contrast also
changes sign across the declared cost/delay grid.

These are retrospective development point estimates, without completed stability,
risk matching or simultaneous uncertainty analysis. They authorize no replacement
primary parameters, claim of statistical superiority or later-period advancement.
The pooled contrast is not a shared-capital account. Eligible-date products and
drawdowns retain their conditional interpretation above.

The curated [400-series table](evidence/m6_cost_delay_series.csv) and
[240 paired contrasts](evidence/m6_cost_delay_paired.csv) are direct projections
of the accepted summary. Return, cost and drawdown fields use fractions, not
percentages or basis points; `cost_bps` alone is the per-side input charge in
basis points. `equal_weight` in the paired table denotes the arithmetic mean of
five instrument-level contrasts on identical dates. No raw quote or daily path
prices are included.

| Accepted cost/delay record | SHA-256 |
| --- | --- |
| Historical summary | `2ddfe103241f261ffde9f8329f6ef912b4129434b0154a11a8cfa3b795ce411c` |
| Historical HTML report | `02d3f96a3146eba9710aa3880b2a7b45dc09f7b6206b07dc7efddfe16e6ea1b0` |
| Historical review ZIP | `3d91d73db2d33f3e3fc5fe0e2162fd15861d99a86e4eb6c61d767a80727dde76` |
| Reported native source inventory | `6c91270bfd5cf11ce5f73fb11eff23c910aa6f5751fa9e49d6e3cd346cd9de6b` |

The source inventory identifies producer-file bytes, not an independently
established Git commit. Original artifacts were preserved through the SVG
verifier correction. The Windows constructed run separately passed 32 native
checks. Its uploaded bundle passed independent verification of all 300
five-instrument dates, 400 series, 240 contrasts, six financial corruption
rejections and fifteen SVG serialization checks.

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

`cost_execution_check` exercises twelve acquisition fixtures and five additional
metadata/ambiguity fixtures, plus future-clock and checksum rejection.
`verify_cost_quotes.py` independently reads
the raw byte envelopes, authenticates their identities and reconstructs request
paths, page chains, latest-record selection, exact price/share units, source age
and spread arithmetic. The provider route also checks the original M2 masks on
the producing machine. The compact review bundle contains summaries and HTML;
it is not a replacement for that local raw-evidence verification.

Initial verification on 28 October 2026 used GNU 13.3.0, C++20 Release and warnings
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
35 sampled instrument-days, with the cached condition and conflict counts below.
Its `latest_condition` column stores the single returned flag; `nonempty_windows`
and `latest_records` distinguish window coverage from tied-record counts.
Every 2017-2021 group has zero eligible observations; 2022 contributes 107 and
2023 contributes 125. The sample therefore does not
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

## Cached condition and timestamp diagnosis

`cost_quote_acquire --inspect <saved-summary>` replays the retained response
archive without a fetch callback. It authenticates the saved plan and every
response, then requires the original window classifications, measurements,
page receipts and retained-attempt count to match exactly. The original summary
and response files are not rewritten. Provider inspection also authenticates
the accepted M2 masks; no historical strategy prices are loaded.

The diagnostic counts condition-array/tape patterns at each window's latest
source timestamp. Empty arrays, blank strings, nulls and absent fields remain
distinct. A pattern contributes at most once to a window count, while exact
duplicates remain visible in record counts. Different patterns can occur within
one window, so those window counts must not be summed as disjoint coverage.
Partial or capped windows do not enter the complete-window frequency table.
Metadata values over 512 serialized bytes are represented by type and digest;
this export bound does not change quote eligibility.

The original ambiguous windows are separated into price conflicts, size
conflicts at identical prices, metadata-only differences at identical prices
and sizes, and unsupported fields. The registered classifier supplies the same
exact price/share projections used by acquisition. Metadata-only disagreement
does not establish message order, regular conditions or an executable quote.
No ambiguous window is automatically admitted. Removing only the condition
test is reported as a hypothetical count, not a revised policy.

The independent verifier reauthenticates the original raw archive, reconstructs
all latest records and checks every diagnostic observation, frequency count,
static chart and table. Its compact `review.zip` contains diagnostic counts,
metadata and HTML, without quote prices or response bodies. `--report-only`
checks compact representation without claiming raw authentication.

Local constructed verification covers seventeen sample cases and 2,210 windows,
including price/size/metadata conflicts, missing fields, blank/empty/mixed
condition arrays, duplicate records, incomplete pages and oversized metadata.
The native total is 355 checks with accepted-M2 metadata, or 354 without it.

Windows cached inspection completed on 28 October 2026 with MSVC 19.51.36260
Release. It reproduced all 910 original classifications and measurements with
zero network requests. The producing machine's independent verifier authenticated
the raw responses and primary replay. Review of the uploaded compact bundle
verified the 910 diagnostic records, aggregate counts, static charts and table;
raw responses remain local and were not reauthenticated during compact review.

| Sampled period | Nonempty windows | Latest condition array | Eligible under the registered rule |
| --- | ---: | --- | ---: |
| 2017-2021 | 584 of 650 | `["?"]` in every nonempty window | 0 |
| 2022-2023 | 246 of 260 | `["R"]` in every nonempty window | 232 |

The returned `?` flags explain all 520 `nonregular` classifications. These are
literal provider metadata, not inferred from the absence of eligible quotes.
The other 64 nonempty pre-2022 windows are 62 ambiguous and two locked windows.
The observations establish the pattern on seven sampled dates, not the date or
cause of a provider-wide metadata change. No authenticated mapping establishing
that `?` means regular is retained. Alpaca documents a
[tape-specific condition-code mapping](https://docs.alpaca.markets/us/reference/stockmetaconditions-1);
the tape is `B` for SPY/IWM/GLD and `C` for QQQ/TLT in this sample. Missing code
semantics are not repaired by treating `?` as `R`.

| Latest-timestamp disagreement | Windows | Interpretation |
| --- | ---: | --- |
| Size conflict | 67 | Identical prices; displayed quantities differ |
| Price conflict | 3 | At least one bid or ask price differs |
| Metadata only | 6 | Identical prices and quantities; only `ax` or `bx` differs |

All 76 windows remain ambiguous. Only one of the six metadata-only cases carries
`["R"]`; the other five carry `["?"]`. A price-only statistic could use different
tie rules, but the registered joint price/size/exchange reference cannot identify
a unique latest record from these timestamps. Removing only the condition test
would increase the passing count from 232 to 752, leaving the 76 ambiguous,
80 empty and two locked windows excluded. This is a diagnostic counterfactual,
not an adopted eligibility change or evidence that all 752 quotes were executable.

| Cached diagnostic review identity | SHA-256 |
| --- | --- |
| Diagnostic summary | `1820012968c459df0f1aad9ac29fcf9ccffda82d691127d927341b25961b7ada` |
| Diagnostic report | `4df5eb524eab3e6d3d582b5e3a657cd7502431488886862e7e5a398a3093887f` |
| Diagnostic review ZIP | `5052ad4756b9a188c8b270d0a54cbab0406eae67e78b324995fd9f3bb3a5a107` |

This completes the registered quote-input diagnosis. Eligible descriptive spread
evidence remains limited to 2022-2023. The fixed bar-reference cost/delay grid and
synthetic execution matrix do not require quote eligibility in earlier years;
their evaluation can proceed with this limitation visible. No further quote
acquisition or change to the primary strategy results follows from this diagnosis.

The [runbook](running.md#cost-and-execution-inputs-m6) gives the Windows commands.
The provider sampler uses only the existing historical-data credential route.
The M6 input checkpoint is accepted with the recorded coverage limitation.
M6 still requires quote-reference comparisons, synthetic execution, continuous
daily-risk accounts and shared-account economic integration. M7 adds stability,
concentration, risk matching and uncertainty; M8-M9 evaluate the reserved periods;
M10 consolidates publication and release.
