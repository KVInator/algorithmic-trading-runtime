# Opening-range continuation

Candidate: `SPY-ORC-01`. Status: proposed research design, not a registered
experiment or an implemented strategy. No candidate returns have been computed.
The [candidate comparison](strategy_candidates.md) records the research decision,
evidence boundaries and source inspection.

## Economic question

Does acceptance above the first half-hour's price range predict positive returns
after the first hour, beyond a simple same-clock momentum condition?

An opening range can describe the initial response to information and liquidity
demand. A subsequent completed price above that range may indicate persistent
buying demand. This is a hypothesis about price discovery, not an observation of
institutional orders or dealer inventory. Temporary liquidity pressure could
instead reverse; a higher price does not establish informed demand.

This proposal isolates one decision and one holding window. Compared with
SPY-TOD-01 it changes the information set and removes repeated entry decisions.
It is a new development trial, not a revision that rescues the failed candidate.

## Proposed specification

All clocks are New York exchange time. Use the existing verified 496-session
development snapshot and its five declared exclusions. The first twenty eligible
sessions initialize the reference control; the following 476 dates form the
common candidate/control sample. Original evidence and protocols remain intact.

| Item | Proposed rule |
| --- | --- |
| Instrument | SPY; unlevered long or cash |
| Opening reference | Highest recorded high from bars starting 09:30 through 09:59 |
| Decision | 10:30, after the 10:29 minute completes |
| Condition | Last completed close is strictly above the opening reference |
| Entry | 10:31 minute open, one minute after the decision |
| Exit decision | 15:55, independent of the subsequent signal |
| Exit reference | 15:56 minute open |
| Exposure | Full continuous research notional on an entry; otherwise cash |
| Trades | At most one round trip per scored session |

The alternative delay is two minutes at both ends. Costs remain 0, 0.5, 1, 2
and 5 bp per side, with one minute and 1 bp primary. Equality at the range high
does not enter. A touch or an intraminute high above the range is insufficient.
There is no stop, target, short position, entry-time scan, leverage or volume
entry filter. Consequently OHLC does not need to resolve a stop-versus-target
path. The delayed opens still remain price proxies rather than executable quotes.

Thirty minutes for the reference range and sixty for confirmation are fixed
design conventions: initial price discovery followed by one additional half-hour
of observation. They are not claimed to be optimal. Testing several range lengths
or moving the decision after inspecting results would create additional trials.

## Comparisons that can reject the idea

All market exposures share the 10:31-15:56 primary reference window, costs,
sample and one-round-trip limit:

1. The opening-range candidate.
2. A normalized-momentum control: at 10:30, enter when the open-to-completed-close
   move exceeds the mean absolute same-clock move in twenty earlier eligible
   sessions. Exit at the same scheduled reference. This is a newly declared
   single-decision control, not SPY-TOD-01's published multi-decision result.
3. Always long over that same intraday window.
4. Cash, earning zero under the current intraday convention.

Report every cost/delay case for each market exposure, not the winning matrix
cell. There are ten cases per market exposure, thirty in total, plus cash.
Different fractions of active days make these exposure-sensitive comparisons;
they are not automatically risk-matched or comparable to overnight buy-and-hold.

Use the same delayed gross forward return for every scored day, including days
on which either rule stays in cash. Report the four intersections of the range
and momentum conditions, their counts, mean normalized moves, prefix realized
volatility and forward returns. This exposes common support and risk differences
without fitting a new selection rule. An empty or very small intersection limits
the comparison; it is not filled with synthetic observations.

The directional contrast is mean forward return on range-positive days minus
that on other days. The incremental comparison is the paired candidate-minus-
momentum daily net return, with its gross counterpart. A strategy that benefits
only because of fewer cost-incurring days has not demonstrated additional gross
return information. Gross contrasts can still reflect different risk states.

Volume remains a diagnostic covariate using the existing causal same-clock
feature. Its association with stronger moves and risk in SPY-TOD-01 is a reason
to investigate confounding, not permission to add a volume threshold here.
Scheduled announcements remain labels rather than trading filters.

## Proposed advancement conditions

Before any scoring, register the exact rules, controls and these project screens:

- At least 100 active candidate days; positive gross and primary net daily means;
  positive primary net means in each year and at both existing delay/cost stresses.
- The existing concentration conditions: the largest five winners contribute at
  most half the positive-return sum, and the arithmetic net sum remains positive
  after removing them. Tail dependence remains an explicit project constraint.
- Positive range-direction contrast and positive paired net increment over the
  momentum control, with nonnegative paired gross increment.
- Positive lower 95% bounds for the primary net mean, directional contrast and
  paired net increment using both five- and ten-session moving blocks, 10,000
  paired resamples and a protocol-recorded seed. Undefined contrasts block the
  corresponding conclusion.

These proposed conditions are demanding and do not guarantee tradability. An
insufficient active sample or failed comparison is a stopping result. Changing
the time, range, filters or screening rule requires a new recorded trial before
calculation. Existing SPY-TOD-01 gates remain unchanged.

## Implementation and learning value

The present minute data, causal feature history, delayed return kernel, paired
statistics and reporting can be reused. A single scheduled decision should use
the same price-reference and cost implementation; a copied backtester is
unnecessary. The additional domain responsibility is an opening-range feature
with explicit completed-prefix boundaries.

Useful contracts include range construction, exclusion of later highs,
strict-threshold equality, a single entry, identical control windows, the
two-minute stress and compounding parity with `evaluate_long_prices`.
The main learning is information-set design and fair signal comparison. The
incremental C++ architecture work is smaller than for an overnight account.

## Reasons to defer

This is another SPY continuation hypothesis selected after inspecting two earlier
studies on the same development years. An attractive result would remain
selection-exposed. It cannot identify a dealer-hedging mechanism from prices and
unsigned volume. Published leveraged ETF and cross-sectional stock results are
not validation of this unlevered SPY rule. Its advantage is a bounded, testable
question and low additional data burden, not an assured edge.
