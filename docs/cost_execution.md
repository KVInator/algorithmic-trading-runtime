# Cost and execution research

M6 is complete as a development and synthetic evidence package. Historical quote
acquisition and cached diagnosis are accepted.
The cached quote-reference comparison has passed Windows verification and
independent review of the uploaded sample, costs and report.
Windows historical cost/delay evaluation is accepted as a development comparison.
The 680-case synthetic execution evaluator has passed Windows Release acceptance
and full-bundle independent review. Continuous risk and shared-capital accounts
passed Windows verification and uploaded-output review, with the stopped paths
reported in the [account acceptance record](#windows-account-acceptance).
The v1 findings and M2-M5 evidence retain their identities.

## Registered comparisons

The [governing contract](../research/cost_execution_protocol.md) fixes three
separate evidence layers:

| Layer | Registered design | Implementation status |
| --- | --- | --- |
| Bar references | 0, 0.5, 1, 2 and 5 bp per side, crossed with one-/two-minute delay; primary signals and entry hurdles fixed | Windows development comparison and independent local verification accepted: 400 series, 240 paired contrasts |
| Historical SIP quotes | Seven input-selected dates, five instruments, thirteen decision clocks and two delays: 910 one-second windows | Windows acquisition and crossing-cost comparison accepted; 232 eligible windows, all in 2022-2023; 108 delay pairs jointly eligible |
| Synthetic execution | 170 matched one-factor workloads crossed with four limit/deadline policies: 680 cases | Windows Release accepted; complete case traces, 510 contrasts and report marks independently verified |

Daily risk remains a separate experiment with intraday-risk, daily-only,
static-risk-budget and passive accounts. The account evaluator reuses the existing lagged
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

## Registered synthetic execution study

`cost_execution_study` executes the registered 170 workloads against four
policies: aggressive/passive fixed DAY limits, each with five-/eleven-second
cancellation deadlines. Every workload is retained. Three policies are compared
with the fixed aggressive/five-second reference on the same workload, giving
510 paired contrasts. No best-policy selection or historical strategy scoring
is performed.

The runner reuses `BrokerState`, the five-symbol FIFO matcher, `OrderRuntime`
and the existing execution-quality kernel. Broker and client start with
separate USD 100,000 ledgers and integer USD $10^{-9}$ units. Fee per filled
share is USD 0.00001. No fees or liquidity parameters are estimated from the
historical quote sample. Client cash/inventory updates only on delivered
broker reports and must reconcile exactly after all terminal deliveries.
Command/reply/checkpoint records and client receipt states are retained for
independent reconstruction. This study uses the in-process broker model;
M5 retains the separate-process, durable journal and restart evidence.

The implementation makes these clock/path conventions explicit in its hashed
plan before running cases. They complete the engineering specification of the
fixed matrix; they are not empirical estimates or historical validation:

- The constructed regular session is 2023-01-03. Decision and dispatch are at
  second 10 after the open; all cases end at the common second-30 horizon.
- Quotes arrive at seconds 10, 12, 15, 18, 21, 24 and 30. Midpoint starts at
  USD 100. The steady path has zero moves; adverse moves are 0, 2, 4, 6, 8, 10,
  12 cents; reverting moves are 0, 0, -2, -4, 0, 2, 0 cents. Moves are signed
  against the buy/sell direction. This path generator is shared with v1.
- An aggressive buy/sell uses the decision ask/bid as its fixed limit; passive
  uses the decision bid/ask. Subsequent source quotes never revise the limit.
- Outbound delay applies to both submission and cancellation. Receipt delay
  applies to broker activities. Source quote age is separate and the existing
  two-second freshness rule applies at actual broker processing time.
- At a tied clock: advance the broker, process quotes, submit arrivals, process
  due cancel arrivals/completions, deliver due reports, then check the client
  deadline. Zero-delay cancellation generated at the deadline is processed
  immediately afterward. Quotes can therefore fill before same-clock terminal
  cancellation. Cancellation requests retain reservations until confirmation.
- Sell cases first buy their requested inventory at second 1 and USD 100, with
  the explicit fee. Setup fills and fees remain in both ledgers but are excluded
  from measured execution costs. No shorting or assumed initial inventory is used.

For direction $s=+1$ for buys and $-1$ for sells, requested shares $Q$, filled
shares $q_i$, fill prices $P_i$, fill-time midpoints $M_i$, common arrival
midpoint $M_0$ and terminal midpoint $M_T$, monetary shortfall is

$$
IS=s\sum_i q_i(P_i-M_0)+s\left(Q-\sum_iq_i\right)(M_T-M_0)+\sum_i f_i.
$$

The filled component is also decomposed into spread
$s\sum_iq_i(P_i-M_i)$ and timing $s\sum_iq_i(M_i-M_0)$. Doubled integer units
preserve half-unit midpoints exactly. Total shortfall basis points divide by
$QM_0$, retaining unfilled opportunity cost and explicit fees. The filled-only
shortfall excludes fees and is NA when no shares fill. Fees remain separately
reported; total shortfall includes them. Source and receipt delays, first fill,
completion and cancellation censoring remain distinct. No-fill cases do not
receive an invented completion time or a filled-only cost estimate.

The static report embeds ten instrument/side matrices, all 680 cell widths and
labels, and four policy summaries. JavaScript only changes panel visibility.
Policy summaries show both equal-case and quantity/notional-weighted measures.
All five symbols share identical constructed paths; label repetition checks
routing symmetry, not five independent samples of market liquidity. The fixed
one-factor matrix does not identify interactions among adverse paths, liquidity
and delays. A passive no-fill case on a steady midpoint has zero opportunity
cost under this benchmark; its zero fill fraction and censored completion remain
visible. Lower shortfall alone is not a successful-execution criterion.

`verify_cost_execution.py` rebuilds the entire plan independently, reuses the
M5 Python broker oracle for matching and fees, reconstructs a separate client
ledger, then checks every command checkpoint, report delivery, baseline/terminal
account, fill, cost decomposition, matched comparison and plotted value. Integer
money is exact; Decimal calculations supply an independent ratio reference.
`--self-check` rejects deliberately corrupted financial, timing and source
records without relying on hashes. The self-contained `review.zip` contains
summary, HTML and all synthetic cases; it needs no private market data.
The [runbook](running.md#synthetic-execution-study-m6) gives the commands.
Windows Release acceptance is recorded below. The original historical reports
retain their original executed-case counts and identities.

Local GNU 13.3.0 Release verification passed all 680 terminal reconciliations,
510 paired contrasts and 680 plotted cells. The independent oracle rejected six
semantic corruptions, including altered fees, client cash and report clocks.
The original 144-case execution study also passed its independent Decimal ledger
and 108-comparison checks after the shared measurement-kernel change.
AddressSanitizer/UndefinedBehaviorSanitizer passed the new study, report and entry
point plus the changed measurement unit across the full matrix; the remaining
archive units were not instrumented and LeakSanitizer was disabled in the traced
environment. All ten report-selector branches executed, and static overview,
detail and print layouts were rendered and inspected. These checks establish
the constructed implementation's behavior, not empirical execution performance.

## Windows synthetic execution acceptance

Windows Release execution completed on 29 October 2026 with MSVC 19.51.36260.
All 680 cases ended with exact broker/client reconciliation. The complete
uploaded bundle passed independent reconstruction of broker commands, client
receipts and ledgers, fills, fees, costs, 510 matched contrasts and 680 report
cells. Six semantic corruption checks passed without relying on artifact hashes.
The separately uploaded summary and HTML are byte-identical to their bundle
members. All 680 native case traces are byte-identical to the GNU Release
reference; plan and paired comparisons agree exactly. Policy counts agree
exactly, with floating-point mean differences below $10^{-12}$.

Each policy covers the same 170 workloads and 2,510 requested shares. Case-mean
fill fraction gives each workload equal weight; share-weighted fill fraction
divides total filled shares by total requested shares. Both cost columns include
fees and terminal opportunity cost for unfilled shares. The notional-weighted
column divides total shortfall by total arrival notional.

| Policy | Case-mean fill % | Share-weighted fill % | Censored cases | Mean shortfall bp | Notional-weighted shortfall bp |
| --- | ---: | ---: | ---: | ---: | ---: |
| Aggressive, 5 s | 80.0000 | 61.3546 | 40 | 0.977271 | 0.733681 |
| Aggressive, 11 s | 82.3529 | 70.1195 | 40 | 1.000824 | 0.821418 |
| Passive, 5 s | 11.7647 | 7.9681 | 150 | 0.647176 | 0.438327 |
| Passive, 11 s | 11.7647 | 7.9681 | 150 | 0.647176 | 0.438327 |

The longer aggressive deadline fills 1,760 shares versus 1,540 at five seconds,
with higher aggregate shortfall. Passive policies each fill only 200 shares and
have censored completion in 150 of 170 cases. Their lower shortfall therefore
does not establish better execution. Both passive deadlines give identical
outcomes under this fixed one-factor matrix. These results characterize the
declared model; repeated symbol paths supply no independent market samples.
Network requests, external broker events and child processes were zero.
Historical strategy inputs, including reserved years, were not opened.

| Accepted synthetic execution record | SHA-256 |
| --- | --- |
| Windows summary | `d0b48e01ddaf158ae4f5e87aea1760b85c3e6b700d575aeafdd04c3867fcf742` |
| Windows HTML report | `56b48b5a21474209db977d96755507fa7ebe4a4c0d44f2a12df5967e5f4adb4f` |
| Complete case traces | `2a3b5332dd2bb4d65607a4835727aa4f7b3e59bd16a22883dda4c81d1cd45f66` |
| Uploaded review ZIP | `048a9e98dd5fed9b4336baed731730f67b3ed63f7bacd8b04096469e39cda67e` |
| Execution plan | `8b0e297326cbf0a89ae23c0ad30f971e46b61193cc11ead7e05ff4ef9316c4bd` |
| Reported native source inventory | `c6562df01544a0ee63276a3dcd2a8eb38c416f58855b446e0f0290be1e7c2c3b` |

The source inventory identifies producer-file bytes, not an independently
established Git commit. The original uploaded files are preserved; verification
does not alter the accepted historical cost/delay evidence or registered rules.

## Historical quote sample

### Crossing-cost reference comparison

`quote_reference_development` authenticates the saved sample through the existing
read-only raw-response replay. It compares each eligible quoted half-spread
with the five registered all-in per-side charges. For bid $B$ and ask $A$,
the hypothetical one-sided crossing cost relative to the contemporaneous
midpoint is $10^4(A-B)/(A+B)$ basis points, for either buy or sell direction.
It replaces the fixed charge in this descriptive comparison; the two charges
are never added. No strategy P&L or empirical fill estimate is produced.

All planned windows retain their original status. Year/instrument groups without
eligible observations display NA. Paired one-/two-minute windows compare spread
variation only, without interpreting the midpoint move, retrieval time or
displayed size as realized execution cost or latency. Earlier-year eligibility
remains unchanged. The independent verifier reauthenticates raw quotes, checks
Decimal arithmetic, every group and delay pair, and all static plot coordinates.
The constructed run passed 130 windows, 65 delay pairs and two semantic
corruption checks. Windows provider-run acceptance is recorded below.

The saved sample identity hashes the native compact JSON serialization. Its
pretty-printed `sample.json` retains the same tokens; verification removes only
JSON whitespace outside strings before hashing. Re-encoding parsed values in
Python can change decimal spellings for the same binary floating-point value.
The retained provider sample reproduced that mismatch. Sixteen identity
regression checks cover the observed decimal, exponents, large integers,
escaped paths, Unicode and altered-content rejection. Raw-response authentication
and financial tolerances are unchanged. Existing comparison folders require only
the Python verifier to be rerun; acquisition and native reports remain unchanged.

```powershell
py -3 -B build.py --configuration Release --target quote_reference_development
& .\scratch\native\Release\quote_reference_development.exe --check
py -3 -B research/verify_quote_reference.py scratch/runs/quote_reference_0 --self-check
& .\scratch\native\Release\quote_reference_development.exe scratch/runs/cost_quotes_0/summary.json
py -3 -B research/verify_quote_reference.py scratch/runs/quote_reference_1 --self-check
```

Use the actual printed suffixes. The provider command uses existing retained
quotes and sends no requests. Share each generated `review.zip`; provider raw
quotes remain local. Continuous risk and shared-capital economic verification
remain in progress.

### Windows quote-reference acceptance

Windows Release execution with MSVC 19.51.36260 and full local verification
completed on 29 October 2026. The producing machine reauthenticated the cached
raw HTTP evidence and passed all 910 windows, 455 paired delay records, static
marks, sixteen identity checks and two financial corruption rejections. Network
requests were zero. The comparison sample, quote eligibility and original
reports were preserved through the serialization-verifier fix.

Independent review checked the uploaded summary and HTML against their ZIP
members, authenticated the separate sample against its compact native identity,
and verified all Decimal costs, group statistics, reference thresholds, delay
differences and plotted coordinates. The sample's file hash matches the bound
original acquisition summary, and its bytes match the earlier accepted sample.
Raw HTTP archives remained on the producer machine and were not reauthenticated
during this uploaded-artifact review.

Of 910 planned windows, 232 are eligible, all in 2022-2023. The remaining
classifications are unchanged: 520 nonregular, 76 ambiguous, 80 empty and two
locked. Only 108 of 455 delay pairs have eligible quotes at both delays; other
paired differences remain unavailable. The following pooled means give each
eligible window equal weight across the two sampled dates:

| Instrument | Eligible windows | Mean half-spread bp | Minimum bp | Maximum bp |
| --- | ---: | ---: | ---: | ---: |
| SPY | 52 | 0.186983 | 0.104744 | 0.654048 |
| QQQ | 44 | 0.263581 | 0.124538 | 0.751456 |
| IWM | 51 | 0.340602 | 0.220755 | 1.142204 |
| TLT | 42 | 0.433681 | 0.341355 | 0.975895 |
| GLD | 43 | 0.421103 | 0.289176 | 1.161170 |

Half-spread is at or below 0.5 bp in 201 eligible windows and at or below 1 bp
in 230. These counts describe the sampled spread component. They exclude
implementation shortfall from price moves before execution, market impact,
unfilled orders and other charges. They do not establish a complete all-in cost
estimate or authorize changing the registered cost grid. The seven selected
dates are not a representative liquidity sample; earlier-year spread evidence
remains unavailable under the unchanged condition rule.

| Accepted quote-reference record | SHA-256 |
| --- | --- |
| Windows comparison summary | `44d222ebc3dea8c6f5d9859eb53ced2276878d0db9fd585523f2bd8cb0e076c7` |
| Windows HTML report | `17e21aa78c9ba7319e25bc9c04227927acf01d98c0629a93b755728d1395579b` |
| Uploaded review ZIP | `a8eb1214313bdc728fe7f4874f18204afd840e4308966e972c4109c05dc2f295` |
| Saved sample / original acquisition summary | `89a35fc123c43923f573e9f1809aa5fdd2391a3e60baf9ee8bea77d02f89424f` |
| Compact native sample identity | `525e9e28a8ad5424e544fd2ec6dd85d3b6190536426d51597505ab9879a68a39` |
| Reported native source inventory | `5ff405e260af4ed4a556a46566cc8834fcc654fa712baa895b61d23948041046` |

The source inventory identifies producer-file bytes rather than an independently
established Git commit. Historical strategy results, frozen rules and reserved
2024/2025 strategy-input boundaries retain their original scope and identities.

### Sampling and eligibility

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
Continuous daily-risk and shared-capital economic integration are accepted
below with their observed-prefix limits. M7 adds stability,
concentration, risk matching and uncertainty; M8-M9 evaluate the reserved periods;
M10 consolidates publication and release.

## Continuous risk and shared-capital accounts

`panel_account_development` completes the remaining M6 implementation using one
causal replay of the accepted M2 panel. `PanelReplayDataset` exposes its already
authenticated cash actions and recovered closing marks; it adds no acquisition,
price substitution or later-year access. `PanelAccountStudy` consumes the existing
M3 decisions and lagged risk estimates. Accounting is delegated to the existing
`LongCashAccount` and `SharedCapitalBook`, with separate financial conventions.
The executable plan is retained as `plan.json` and authenticated by its native
compact-token SHA-256. The plan specifies initial capital, clocks, units,
rounding, budgets, actions, missing-reference behaviour and fill assumptions.

### Continuously funded risk accounts

The matrix contains five instruments, four accounts, two delays and five
per-side charges: **200 accounts**. Each starts with USD 100,000, no debt or
short positions, and zero cash interest. The accounts are intraday-risk,
daily-only, static-risk-budget and passive equity. All four wait in cash until
the first development date with all three prior-only risk weights available.
The 252 calibration closes provide only 251 close-to-close returns, so that
start need not be the first session of 2017. Calibration cash is not scored;
observed development cash days have zero returns.

Risk targets are the unchanged M3 estimates. Each active account is marked at
the actual session opening, rebalanced at the actual open one or two minutes
later, then marked at the actual final-minute close. Passive equity buys once
at the common start. A still-observed account liquidates at the actual open
four minutes before the final development session's close. Shortened exchange
sessions are retained. Missing intraday risk uses the registered daily fallback
when available; missing a required sizing estimate stops an active allocation
account. The passive account needs no subsequent risk estimate.

For cash $C$, shares $q$, raw price $P$, liquid wealth $L=C+qP$, target weight
$w$ and per-side rate $c$, the charged signed notional is

$$
D=wL-qP,\qquad N=\frac{D}{1+\operatorname{sgn}(D)cw},\qquad
C'=C-N-c|N|,\qquad q'=q+N/P.
$$

This solves the post-cost stock-weight constraint. Dividend receivables enter
equity but do not fund purchases. On each reached ex-date, the previous raw
mark less the cash amount supplies the existing accounting continuity
convention; it is not an executable price. Entitlement uses shares held across
the ex-date. Payment transfers the claim to cash once, including when payment
falls between exchange sessions. Terminal unpaid claims remain in equity.
Canonical split coverage is the registered M2 no-split basis, not a new split
assumption introduced by this evaluator.

The first required missing opening, trade or closing reference stops the
account at that observation. Activity earlier that day remains in the ledger,
but the day's return and subsequent account returns are NA. The last observed
close remains the final point in the equity curve. No earlier liquidation,
interpolation, return bridge, capital reset or silent restart is introduced.
The three unavailable 2019 closes therefore remain consequential for the
corresponding continuously held accounts. Forecast-history recovery cannot
restore those accounts.

Holding-risk measurements are reported separately from account availability.
Each frozen prior estimate is compared with the next-session dividend-aware
morning-to-morning endpoint square and RV5, when the necessary observations
exist. QLIKE uses $\log h+RV5/h$; positive control-minus-candidate loss favours
the intraday estimate. The final forecast horizon is NA. Forecast outcomes
never feed an earlier allocation. The ten instrument/delay coverage rows remain
visible even after an economic account stops.

### Historical shared capital

The shared matrix contains **50 accounts**: five portfolio variants, two delays
and five charges. Each variant starts with one USD 100,000 balance across
fifteen strategy owners. The candidate combines momentum, volume-conditioned
continuation and reversion on all five instruments. Four controls replace just
one family at a time: always-long momentum, price-only continuation, sign-only
reversion or drift reversion. Daily-risk allocation remains a separate experiment.

This is a frozen-signal economic comparison. Continuation intent retains the
M3 reference path's held-state convention; an admission shortfall does not
regenerate that signal from shared-account inventory. The actual-inventory
feedback adapter remains covered by M4's separate integration diagnostic.
Consequently these paths isolate allocation and charging of the registered
signals, rather than establishing the behaviour of an inventory-adaptive
deployed strategy.

The existing ownership, target-netting, proportional admission and integer cash
rules apply. Budgets are equity/15 per owner, equity/3 per family, equity/5 per
instrument and equity in total, with whole shares and no borrowing. Raw opens
are rounded to the nearest nanodollar. For registered cost $b$ in bp, the
per-share crossing charge in nanodollars is

$$
a=\left\lceil\frac{P_{nano}(2b)}{20000}\right\rceil,\qquad
P_{bid}=P_{nano}-a,\quad P_{ask}=P_{nano}+a.
$$

Both sides of the constructed quote are supplied to the existing allocator.
No additional broker fee is added to this cost grid. Notional and crossing
charges use exact integers; owner cash flows and internal transfers reconcile
to the global account. A valid pending order admitted below its desired target
because of a budget cap is retained. Cancellation follows an overshooting or
reversed intent, expiry, or an admission-limit violation, rather than requiring
one order to reach the complete target.

Each frozen decision arrives after the selected delay. Sizing uses the current
arrival-clock prices and equity. All five actual opens are required at each
scheduled batch to mark aggregate admission limits. The common full-day
operational mask is not used to avoid a later missing price before it is
observed. An unavailable target or quote stops the account at that clock,
retaining earlier transitions and inventory with the remaining outcome NA.
Known unavailable strategy features retain their registered cash intent.

Acknowledgments, full fills and cancellation responses are constructed,
synchronous reference events at the cost-shifted raw opens. They establish
funded accounting and allocation behaviour, not available liquidity, realized
broker execution, queue priority or measured network latency. Event processing
is iterative and bounded. Every successful day must be flat and sealed; its
checkpoint carries the same cash and owner attribution into the next day,
without resetting capital. A stopped account never resumes. Separate-process
and durable restart evidence remains the accepted M5 diagnostic.

### Comparison, reporting and verification

The manifest contains 150 risk and 40 shared candidate/control contrasts.
Comparisons use each pair's common observed prefix. Mean daily increments and
annual CER increments retain that denominator; CER is
$252(\bar r-\tfrac{3}{2}s_r^2)$ with sample variance. Tables also report observed
coverage, last observed equity, drawdown, traded notional divided by initial
capital, paid costs and exposure. Risk exposure is the observed close stock
fraction of liquid wealth; shared exposure is the maximum gross fraction over
that day's reached transitions. The two exposure summaries have different
sampling conventions. Costs and turnover include reached activity on the
stopping date even though no complete return exists for that date.

Six static figures show all 25 primary account paths at one minute and one bp
per side. Each uses the full development calendar axis and ends stopped curves
at the last observed close. All 250 case rows, ten forecast-coverage rows and
nineteen primary paired rows are embedded without requiring JavaScript. Complete
cost/delay contrasts and daily ledgers remain in the saved artifacts.

`verify_panel_accounts.py` first reuses the independent M3 source/history/fit
oracle. It then reconstructs fractional cash, shares and receivables with
50-digit Decimal arithmetic, expected event clocks and action entitlements.
The independent M4 integer oracle reconstructs every shared input, ownership
allocation, order, acknowledgment, fill, cancellation and complete-state digest.
It checks exact matrices, coverage, summaries, all 190 contrasts, forecasts,
25 plotted curves and 279 displayed table rows. Native compact JSON tokens are
preserved for plan identity checks; decoded floats are not reserialized to
validate an identity. Twelve semantic mutations exercise cash, charges, missing
events, restarts, future measurements, aggregates and chart coordinates without
relying on artifact-hash failure.

The 300-date analytic fixture passed GNU Release and independent verification:
9,600 risk records and 2,400 shared account days across 48 development sessions.
It includes a cash distribution, missing close, missing intraday reference,
terminal liquidation and stopped-account continuation. These results establish
constructed arithmetic and failure behaviour, not historical performance.
The [runbook](running.md#continuous-risk-and-shared-capital-evaluation-m6) gives the
Windows acceptance commands. Historical verification requires the retained
registered panel; provider review bundles omit raw prices and daily transcripts.
The v1 findings and accepted M2-M6 input/results identities remain unchanged.
Windows historical verification and compact review are recorded below.
Stability, concentration, predefined risk matching and uncertainty
remain M7; validation, final evaluation and publication remain M8-M10.

Local regression review also passed 190 shared-capital checks in sixteen cases
and independently reconstructed 171 durable transitions. The focused
budget-limited acknowledgment test fails under the earlier cancellation rule.
AddressSanitizer and UndefinedBehaviorSanitizer passed the complete constructed
account calendar with the study, report, application, shared-capital core and
fractional-account units instrumented. Other library units were not instrumented;
LeakSanitizer was disabled because tracing prevents it in this environment.
The static HTML was rendered and inspected, including equity axes and complete
account tables. The retained v1 publication hashes, figures and local links pass.

### Windows account acceptance

Windows Release execution and the full independent verifier completed on
29 October 2026 with MSVC 19.51.36260. The historical replay contains 2,012
calendar dates, including 252 initialization sessions and 1,760 development
sessions from 3 January 2017 through 29 December 2023. It emits **352,000 risk
records and 88,000 shared account days** across the registered 200 and 50 cases.
These counts include unavailable outcomes after a stop: only 162,040 risk
records and 2,750 shared account days have observed returns. Network requests
were zero and reserved 2024/2025 strategy inputs remained unopened.

The producing machine's verifier authenticated retained source observations,
causal histories, risk estimates and the full daily ledgers. Decimal fractional
accounting and integer shared ownership/admission reconstruction passed, as did
all twelve semantic corruption checks. The complete constructed Windows bundle
also passed the full verifier during independent review; its financial summaries
and forecasts agree with the GNU reference within existing tolerances.

Historical compact review authenticated the saved native plan, report hash and
separately supplied summary/report against their ZIP members. It reconstructed
all 250 reported equity-prefix means, CERs and drawdowns, all 190 paired contrasts,
25 plotted curves and 279 displayed table rows. The report was rendered to
seventeen pages and visually inspected. Raw provider prices and six daily
transcripts remain on the producing machine and were not reauthenticated during
compact review. The summary binds the producer's reported artifact identities.

#### Observed account coverage

Every cost case retains the same stopping rule. No stopped path restarts,
bridges a missing mark or becomes an observed cash return on subsequent dates.
The first observed cash session is included in the risk-prefix counts.

| Accounts | Cases | Observed sessions per case | Last observed close | First unavailable date and cause |
| --- | ---: | ---: | --- | --- |
| All four risk accounts on SPY, IWM and GLD | 120 | 655 | 2019-08-09 | 2019-08-12: required closing mark unavailable |
| Intraday, daily-only and static-risk accounts on QQQ and TLT | 60 | 804 | 2020-03-13 | 2020-03-16: delayed execution open unavailable |
| Passive QQQ and TLT | 20 | 1,760 | 2023-12-29 | None; terminal liquidation reached |
| All five shared variants, one-minute delay | 25 | 65 | 2017-04-05 | 2017-04-06: required shared execution quote unavailable |
| All five shared variants, two-minute delay | 25 | 45 | 2017-03-08 | 2017-03-09: required shared execution quote unavailable |

The missing shared reference is the required raw minute-open input to the
constructed bid/ask, not a failure to acquire a live quote or a subscription
restriction. Each scheduled batch needs all five instruments for aggregate
marking and admission. These early stops therefore prevent a seven-year
shared-capital performance conclusion. The eligible-date cost/delay experiment
uses a different denominator and does not repair these continuous accounts.

#### Primary development results

At one-minute delay and one basis point per side, intraday-risk versus daily-only
comparisons use the same observed prefix within each instrument. Annual CER
increments are percentage points per year under the stated mean-variance
definition, not compounded annual growth rates.

| Instrument | Common sessions | Intraday-risk last observed equity USD | Intraday-risk prefix drawdown % | CER increment over daily-only pp/year |
| --- | ---: | ---: | ---: | ---: |
| SPY | 655 | 128,215.87 | 12.2495 | +0.1998 |
| QQQ | 804 | 159,293.81 | 11.4156 | +1.4064 |
| IWM | 655 | 106,251.92 | 16.9402 | -0.2886 |
| TLT | 804 | 141,659.66 | 10.8448 | -0.1313 |
| GLD | 655 | 124,445.46 | 13.4738 | +0.0925 |

The primary shared candidate's last observed equity is USD 100,072.60 after
65 sessions, with a 0.2425% prefix drawdown and USD 456.03 in reached crossing
charges. Candidate-minus-control mean increments are -0.0439 bp/day versus the
momentum replacement, +0.1880 versus price-only continuation, +0.2817 versus
sign-only reversion and +0.2648 versus drift reversion. They describe only that
short common prefix. The two-minute candidate has 45 observed sessions and last
observed equity of USD 99,893.13, so delay comparisons also have unequal coverage.

Holding-risk forecast measurements continue separately after account stops.
At one-minute delay, all five mean control-minus-intraday QLIKE differences are
positive, with unequal outcome availability:

| Instrument | RV5 outcomes | Common QLIKE dates | Mean control-minus-intraday QLIKE |
| --- | ---: | ---: | ---: |
| SPY | 1,751 | 1,462 | +0.021243 |
| QQQ | 1,754 | 1,730 | +0.037817 |
| IWM | 1,751 | 1,462 | +0.011896 |
| TLT | 1,600 | 877 | +0.033708 |
| GLD | 1,696 | 817 | +0.042884 |

These are development point estimates with different exposure and observation
prefixes. They establish neither uniform benefit nor statistical superiority.
No risk-matched or uncertainty result is implied. M7 must preserve unavailable
comparisons and report each actual denominator; completion of M6 does not turn
stopped paths into full-horizon results or authorize reserved-year access.

The curated [250-case summary](evidence/m6_panel_accounts_series.csv) and
[190 paired contrasts](evidence/m6_panel_accounts_paired.csv) are direct
projections of the accepted historical summary. `layer` distinguishes fractional
risk from whole-share shared accounts. Return, CER, exposure and drawdown fields
are fractions; `cost_bps` is the per-side charge in basis points. Equity, traded
notional and paid-cost fields are USD. `last_observed_date` and `stopped`
distinguish the final complete close from the first unavailable date. Costs and turnover include
reached activity on that stopping date. No raw prices or daily transcripts are
distributed in these tables.

| Accepted account record | SHA-256 |
| --- | --- |
| Historical Windows summary | `592c8ab3a99afd2d8adc59d8b7c504d95d5609d0962224e5acce494f1c4993bc` |
| Historical HTML report | `2128c09f6a7ca2f551c53b426201ba759351ce28c898d7e54225392c53c72a77` |
| Historical review ZIP | `863417257ae469f382023104c396b58e0f08843f1fbbfbef51df9d3a534680a6` |
| Saved plan file | `be75730ffc8bb8723abb3ea19266e7a2693c67e1c6ef8b5388250559405124eb` |
| Compact native plan identity | `8f7382e7cce46af6f328b5e267e64784d2ab91a75b3957343677ee450dcbb4b6` |
| Producer daily account transcript | `283babc25e25d3840002a361d54f7a8ed5c81666175c8c7163c646a3e4078743` |
| Complete constructed Windows review ZIP | `0fad28890145465d4a74cb9808c6cef37d973116e142f8f29f738831dd1a4998` |
| Reported native source inventory | `1d876707890c1f60a245d17609c4cf2c8cf031c7fedc76e607f1ea973ad89553` |

The source inventory identifies producer-file bytes, not an independently
established Git commit. The original uploads are preserved. M6 acceptance is
complete with these coverage qualifications; v1 findings and frozen inputs are
unchanged. M7-M10 remain open.
