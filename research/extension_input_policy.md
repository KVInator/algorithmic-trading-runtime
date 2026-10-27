# Input availability amendment

Registration: **MULTI-ASSET-01-INPUT-01**, 27 October 2026, before expanded
historical strategy replay. The [registered JSON](extension_input_policy.json)
is build-pinned separately from the unchanged [parent protocol](extension_protocol.md).
The original acquisition, its complete-grid masks and the failed four-year
assessment remain intact. This amendment changes the definition of input
adequacy; it does not establish that the original definition passed.

## Reason and evidence boundary

The full acquisition has high aggregate minute coverage but sparse gaps across
TLT and GLD remove many dates from a five-instrument complete-session intersection.
The detailed development review contains 1,500 missing intervals, including
1,431 one-minute intervals. It also contains longer unexplained holes and
gaps on the four March 2020 market-wide circuit-breaker dates documented by
[NYSE](https://www.nyse.com/article/assessing-nyse-model-performance).
The halt dates provide context, not proof of the cause of an individual gap.
The provider's [aggregation rules](https://docs.alpaca.markets/us/docs/market-data-faq)
also permit absent bars when no eligible trades update their fields.

The amendment is informed by input coverage and the formulas already specified
for the strategies. No expanded historical model, return, cost estimate or
advancement result was evaluated to choose it. Reserved-year inputs may still
be authenticated automatically and summarized only through availability counts.
The public price reader remains closed for 2024-2025.

## Operational dates and feature availability

An operational date has observed reference bars at every potential scheduled
execution time in the registered primary and holding-period comparisons.
This is distinct from the availability of a feature, fit or forecast. An
operational date is not necessarily an active date or an admissible model date.

Number minute slots from zero at 09:30 New York. With delays
$D=\{1,2\}$ minutes, the required reference set is

\[
E=\{0\}\cup\{t+d:t\in T,\ d\in D\},
\]

where $T$ contains momentum entry at 360 and exits at 370/380/385, continuation
decisions at 30,60,...,360 and liquidation at 385, and reversion entry at 150
and exits at 165/180/210. The union has 33 slots. These times follow the
registered one/two-minute delays and holding periods; they are not selected to
maximize the number of retained dates. The opening bar is additionally required
as a common session reference.

Full regular sessions and the original SPY diagnostic exclusion remain required.
Common operational dates use the intersection of all five instruments. The
numerical adequacy requirements remain five development years, at least 200
common operational dates per year and at least 80% of the regular calendar.
The changed availability definition must accompany every reported adequacy count.
Original complete-grid adequacy remains a separate sensitivity assessment.

| Family | Actual observations required for a current feature |
| --- | --- |
| Momentum | Opening price and the completed minute-359 close |
| Reversion | All first-30-minute closes/volumes for the anchor, plus exact closes at slots 29,34,...,149 for the primary five-minute grid |
| Continuation | Every actual minute in the completed prefix of each scheduled decision, including observed volumes |

One- and fifteen-minute reversion neighborhoods require their own exact fixed
grid observations. A missing minute outside a mathematical feature's support
does not make that feature unavailable. Missing anchor volume is not inferred
as zero, and cumulative volume is unavailable whenever any required prefix
minute is missing. No sequence is compressed and no observation is moved to
another clock time.

The twenty prior continuation sessions retain the parent complete-session
history rule. Reversion retains twenty prior regular calendar slots without
replacement, with completeness determined from the required anchor/grid
observations. All histories remain prior-only. Positive anchor volume, numerical
fit admissibility and forecast uncertainty are later model checks, not availability
flags. Daily-return and risk initialization rules remain unchanged.

## Replay obligations

Operational coverage is an ex-post research outcome-availability mask, never
a signal input or advance knowledge supplied to an order decision. A missing
future execution reference cannot alter an earlier feature. Unobserved execution
outcomes remain unavailable on the original calendar; they are not zero returns.
Coverage-conditioned results must report their missingness and selection limits.

Within the observable comparison calendar, unavailable required features produce
an explicit cash target for all members of that paired comparison. For an existing
continuation position, feature unavailability requires liquidation at the next
registered executable decision; it does not retrospectively cancel an earlier
fill. The strategy adapters must implement and verify this rule causally at M3.
An absent required execution reference leaves the affected outcome unavailable,
rather than creating a fill at a different delay. A continuous account path
cannot be asserted across an unresolved executed order or valuation mark.

The final dataset records feature masks separately from execution observability.
Availability counts do not demonstrate sufficient activation, power, profitable
exposure or complete continuous-account evidence. M3 must verify the causal
missing-input behavior and original-reference parity before expanded scoring.
Both the parent and amendment identities are required in the new study manifest.

## Exact-time closing recovery

The parent daily-mark rule still requires an actual observation at the required
minute. The amendment authorizes one separate recovery vintage for SPY, IWM and
GLD on 2019-08-12, each requesting only the 15:59-16:00 New York minute with the
original SIP/raw/USD conventions. No daily aggregate substitutes for that minute.
The three requests retain raw response bytes, retrieval times, request identities
and bounded retry histories under `resolutions/closing_marks/`.

The recovery manifest binds the original acquisition and this amendment. Original
missing flags remain visible. A recovered close may initialize the daily account
only after its raw page is authenticated; it cannot repair other missing intraday
slots. Empty or invalid responses supply no price. A successful empty response
is retained and is not repeatedly requested. Corporate-action reconciliation,
daily marking and the vintage bridge remain independent registration gates.
