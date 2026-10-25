# SPY-TOD-01 development diagnostics

These analyses follow inspection of the revision-1 development result. They
explain the 2022-2023 evidence and propose falsifiable mechanisms; they are not
registered selection tests or independent confirmation. The existing protocol,
twenty cost/delay cases, sample eligibility and continuation gates are unchanged.
Local 2024-2025 candidate performance remains unopened.

The implementation uses the existing frozen snapshot and primary one-minute,
1 bp-per-side trade paths. It never selects a new threshold, trading clock,
instrument or holding period. Generated evidence belongs under `scratch/runs/`.

## Questions and measurements

### What does volume remove?

Both variants use the same price exit. The volume condition gates fresh entries
and therefore removes either an entire price-only trade or its early portion.
Each volume trade must be contained within exactly one price trade and share
its exit. The diagnostic rejects unmatched exposure or inconsistent prices and
returns rather than treating the strategies as independent portfolios.

For proportional one-way cost `c`, each round trip has constant log cost drag
`k = log(1+c) - log(1-c)`. With full reinvestment:

`log(W_volume / W_price) = gross log increment + saved round trips * k`.

For a skipped trade, the removed gross log return is `log(exit / price_entry)`.
For a delayed entry, it is `log(volume_entry / price_entry)`. The gross increment
is the negative of the removed return. Shared trades contribute zero. These
log contributions reconcile exactly across trades and days, subject to floating
point tolerance. Multiplying a log contribution by 100 gives log percentage
points, not a simple or compounded percentage return.

The report also preserves the arithmetic daily-return decomposition: paired net
mean equals paired gross mean plus the reduction in gross-to-net cost drag.
This decomposition uses the original daily returns, including cash days.

### How much depends on exceptional days?

The registered five-winner gate remains authoritative. A separate descriptive
stress replaces the volume variant's largest 1, 3, 5 and 10 winning dates with
cash in **both** variants. Ranking is descending primary net return, with date
ascending for ties. The same 476-day timeline and denominator remain in place.
Cash dates are exported. These counterfactuals use knowledge of later outcomes
and are not implementable filters.

Leave-one-month-out results remove each of the 23 scored calendar months in
turn and recalculate both variants on the remaining dates. Their denominator
changes. Neither this exercise nor winner removal creates a new held-out sample
or justifies choosing the most attractive subset.

### Are scheduled announcements associated with the result?

The checked-in calendar contains every CPI release, Employment Situation release
and scheduled FOMC policy statement during 2022-2023: 24, 24 and 16 records.
Dates and local release times come from the BLS annual schedules and Federal
Reserve statements. All scored dates receive a label before subgroup summaries
are calculated; overlapping release types form an explicit combined group.
An employment release on the closed Good Friday, 7 April 2023, stays in the
calendar and creates no observation. January 2022 releases belong to warm-up.

Calendar classes are disjoint. The report shows counts, active days, gross and
net means and paired differences. Small groups, different risk and post-result
classification limit inference. A date with no selected release can contain
other announcements, speeches, earnings or geopolitical news. This is not a
complete news calendar, an announcement-surprise dataset or a causal event study.
No event-day trading rule is inferred from a subgroup's return.

### Is volume identifying price magnitude or risk?

Among positive price conditions, compare high and other relative-volume groups
at each of the twelve decision clocks. Export the raw open-to-decision move,
move divided by its historical same-clock scale, and completed-prefix realized
volatility. The last is the square root of the sum of squared one-minute log
close changes, beginning with the current session open. It excludes the
overnight return, uses no subsequent bars and is not annualized.

These are covariate summaries, not a regression or causal adjustment. Repeated
clocks on one day are dependent observations. Relative share volume measures
activity; it does not reveal signed order flow, dealer inventory or options
gamma. A high-volume return difference can reflect price magnitude, risk,
announcement exposure or common shocks.

### What do the entry references conceal?

For each primary trade, export the delayed entry open relative to the last
completed decision close. This is the intervening market movement, not measured
slippage. The entry minute's high-low range divided by its open and its total
shares are ex-post context only. They are not known at entry and do not estimate
the bid-ask spread, order capacity, queue position or attainable fill price.
No account size or order size is assumed.

## Research context

| Source | Inspected evidence | Relevance and boundary |
| --- | --- | --- |
| [Gao et al., Market Intraday Momentum](https://profiles.wustl.edu/en/publications/market-intraday-momentum/) | Author-institution abstract | Reports first-to-last-half-hour predictability, stronger in high-volume, volatile and selected macro conditions. Its opening measure includes the previous close. SPY-TOD-01 is a different design, so the published result cannot validate this implementation. |
| [Baltussen et al., Hedging Demand and Market Intraday Momentum](https://papers.ssrn.com/sol3/papers.cfm?abstract_id=3760365) | Author manuscript abstract | Gamma hedging offers a possible continuation mechanism. The current price/volume bars contain no direct measurement of hedging demand. |
| [Lucca and Moench, The Pre-FOMC Announcement Drift](https://www.newyorkfed.org/research/staff_reports/sr512.html) | Federal Reserve research summary | Scheduled policy events can have distinct return patterns. Their pre-announcement window differs from an intraday reaction strategy. |
| [Moreira and Muir, Volatility-Managed Portfolios](https://www.nber.org/papers/w22208) | Working-paper abstract | Risk conditioning can affect portfolio outcomes. It motivates separating risk measurement from entry prediction and retaining a daily allocation alternative. It does not establish high-volume alpha. |
| [BLS 2022](https://www.bls.gov/schedule/2022/home.htm), [2023](https://www.bls.gov/schedule/2023/home.htm) | Complete release dates and times for the selected series | Calendar labels only, without revised macro values or surprise estimates. |
| [Federal Reserve calendars](https://www.federalreserve.gov/monetarypolicy/fomccalendars.htm) and linked statements | All sixteen scheduled statement dates; statement release times | The policy statement date is the final meeting day. Speeches and minutes releases are different events. |
| [Alpaca real-time stock data](https://docs.alpaca.markets/us/docs/real-time-stock-pricing-data) | Bar and updated-bar specifications | Late trades can revise a minute's close and volume. Historical final bars are not a complete point-in-time record of information received by a live process. |

The mechanism tests should distinguish persistence after information arrival,
temporary inventory pressure and exposure to volatile market states. Current
bars can describe their associations but cannot identify the mechanism alone.
Evidence required for a new candidate includes an explicit information cutoff,
observable pre-decision features, a matched benchmark and an execution model
appropriate to its turnover. Later testing requires a separately frozen design;
repeatedly inspecting development subsets does not replenish independent data.

## Interpretation boundaries

Removing a few positive-tail days is deliberately demanding. Trend-like payoff
distributions can depend on rare moves, so a failed concentration screen is not
a theorem that the economic mechanism is absent. The appropriate response is
to report that dependence and preserve the screen, not silently relax it.

The separate registered daily-risk study supplies overnight corporate-action
accounting and static/passive controls. Its objective differs from this intraday
entry comparison; neither study changes the other's recorded gates.

## Findings on the frozen development sample

The C++ diagnostics use input manifest
`70fdd01093b9026cc4e673def25718e547c0052b728163bac9bdf30b9c25126e`.
The original daily returns, feature windows, trades, case statistics, bootstrap
results and assessments remain unchanged. The additional calculations were
independently checked against retained bars and the original exported trades.

| Primary comparison | Measurement |
| --- | --- |
| Net volume increment | 1.493531 bp per scored day |
| Gross volume increment | 0.758595 bp per scored day |
| Reduction in cost drag | 0.734936 bp per scored day, 49.208% of the net increment |
| Shared price trades | 162 |
| Delayed volume entries | 8; seven remove a positive early segment, one a negative segment |
| Skipped price trades | 175; 112 gross losers and 63 gross winners |

The skipped trades contribute +6.184420 gross log percentage points and
+3.500000 saved-cost log percentage points. Delayed entries contribute
-2.485569 gross log percentage points and save no round trips. Together they
produce a +7.198851 log percentage-point wealth-ratio increment. These are
additive log attributions, not additive compounded percentages.

Volume does not uniformly improve entry timing. Most retained trades are
identical to price-only; delayed entry is detrimental in this sample. The
measured improvement comes from the collection of whole trades skipped and
the associated lower turnover. This is stronger evidence about what the rule
does than a comparison of headline portfolio returns alone.

### Tail dependence and calendar stability

Replacing the volume variant's largest five winning dates with cash in both
variants reduces its compounded return from +11.965217% to -0.429211%. Its own
registered arithmetic sum after removing five winners is -0.261113 percentage
points. The simple-sum screen and the compounded counterfactual measure
different quantities; both show reliance on a small positive tail.

The same ten largest volume-winning dates have identical primary returns in
the price-only strategy. Consequently the paired arithmetic increment remains
+1.493531 bp/day in all four common-date stresses. Tail dependence of absolute
profit and concentration of the incremental volume effect are different
questions. The original gate addresses the former.

The volume variant's mean remains positive in all 23 month-omission samples,
ranging from +1.360502 to +3.026911 bp/day. Its paired advantage remains positive,
from +1.296144 to +1.728637 bp/day. These overlapping samples indicate that no
single month accounts for the improvement; they provide no independent
statistical confirmation. The original paired bootstrap intervals still include
zero, and both variants still fail their registered development screens.

### Scheduled-release association

| Scored-day class | Sessions | Price net mean, bp/day | Volume net mean, bp/day | Paired increment, bp/day |
| --- | ---: | ---: | ---: | ---: |
| No selected release | 416 | 0.492402 | 1.573403 | 1.081001 |
| CPI | 23 | 2.667410 | 2.312859 | -0.354551 |
| Employment Situation | 22 | 9.404294 | 13.483845 | 4.079551 |
| FOMC policy statement | 15 | -1.352202 | 10.623070 | 11.975271 |

The CPI group does not show an incremental benefit from volume. Employment
and FOMC groups have larger positive point differences but few independent
sessions. The 416 remaining dates still have a positive paired difference:
selected releases do not account for the entire improvement. These differences
do not justify adding an event filter after inspecting the result.

The calendar deliberately classifies 30 November 2022 as having no selected
release. A [Powell speech](https://www.federalreserve.gov/newsevents/speech/powell20221130a.htm)
occurred that day, demonstrating why this label cannot be interpreted as an
absence of news. A date annotation alone does not attribute the strategy return
to that speech.

### Price magnitude, risk and execution

At every decision clock, the high-volume group has a higher mean normalized
price move and higher mean prefix realized volatility than the other-volume
group. At the first decision, those means are approximately 1.917 versus 1.526
for normalized move and 37.621 versus 27.608 bp for prefix volatility. The
pattern persists across clocks. A volume threshold may be separating stronger
price moves or different risk states; these summaries cannot distinguish those
possibilities from additional information in volume itself.

Mean absolute decision-close-to-entry-reference movement is 4.682342 bp for
price-only and 5.395342 bp with volume. This movement is already reflected in
the replay prices. It must not be charged again as estimated transaction cost.
Neither the movement nor entry-minute range estimates a bid-ask spread. A
quote-based execution audit and a record of bar arrival/revision times would
be required before making live-execution claims.

### Research implication

The volume rule is a plausible activity filter with positive development point
estimates, fewer trades and an increment spread across months. It remains
unconfirmed, depends on a positive return tail and is confounded with price
magnitude and realized risk. The evidence supports retaining the hypothesis for
comparison, without promotion to later testing or alteration of its gates.

The subsequent registered reversion and daily-risk studies are reported in the
[research index](README.md). Their results do not alter these diagnostics.
