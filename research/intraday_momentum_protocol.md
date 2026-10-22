# Intraday momentum research protocol

Protocol: `SPY-IM-01`, revision 2, recorded 2026-10-21.
Revision 1 was recorded on 2026-10-20; its economic rule and decision gates are unchanged.

Status: 2022–2023 acquisition has finished with 496 complete sessions and one
incomplete session, 2023-06-05. Revision 2 excludes that entire session on the
recorded data-quality evidence before the full development evaluation. The
excluded 2023-01-03 diagnostic and March 2022 engineering pilot have previously
been inspected. Validation and final evaluation remain unopened.

March pilot input-manifest SHA-256:
`a8d9489284efe604ab8350f945e0e5393ae769c6bc3a0350450a2328759416e5`.
The development dataset must retain this existing snapshot unchanged. Its
reported positive result and concentration in a few trades do not change the
signal, clocks, cost grid, exclusions or continuation criteria below.

## Question and motivation

Does a positive SPY return from the regular-session open to 15:30 New York time
identify late-session long trades with positive returns after execution delay
and trading costs? Does the conditioning signal add information beyond simply
holding SPY during the same late-session window?

Gao et al. (2018) document a relationship between early and late SPY returns.
Baltussen et al. (2021) study broader intraday continuation and hedging demand.
These results motivate a test; they are not performance evidence for this
implementation. The protocol uses the same day's market open as its reference,
one ETF, long-or-cash positions, delayed minute-open price proxies and a
scheduled exit before the closing auction. The cited studies use different
signals, instruments, price conventions and samples. In particular, their
previous-close-based predictors include overnight information that this signal
omits. A failure here rejects this adaptation, not the wider literature.

The initial rule has no fitted coefficients, parameter search, volatility
target, leverage, stop-loss, profit target or machine-learning model. Signal
strength filters and short positions are outside this experiment.

## Data and session conventions

| Item | Fixed convention |
| --- | --- |
| Instrument | SPY only |
| Bars | Alpaca historical SIP, 1Min, raw, USD, ascending, `asof=-` |
| Session | Exchange-calendar sessions opening at 09:30 and closing at 16:00 America/New_York |
| Early closes | Excluded by the calendar before any return calculation; retained in the acquisition log |
| UTC conversion | Per session date, accounting for daylight saving; no fixed year-round UTC offset |
| Bar timestamps | Interval starts; close/high/low/volume become available only when that interval finishes |
| Bar quality | Exactly one validated bar at each expected minute start; no sorting, deduplication or filling |
| Corporate actions | No cross-day price ratios or overnight holdings; any known within-session adjustment anomaly blocks the affected session |

Raw response pages, calendar responses, requests, retrieval timestamps and data
audit results must be retained. A frozen dataset manifest must identify the
exact input files and their SHA-256 digests before strategy evaluation. A later
provider revision creates a new dataset snapshot rather than silently changing
the old evidence.

An unresolved missing or invalid session blocks evaluation of the affected
period. It is not counted as a no-trade day and is not silently removed.
Acquisition failures are repaired from the same source before scoring. Any
unavoidable data exclusion requires an explicit protocol revision, its reason,
and a count of affected sessions before performance is recomputed.

The already inspected 2023-01-03 session is an ingestion/engineering fixture.
It remains available for replay checks but is excluded from all reported
strategy and control performance. It is not an independent evaluation sample.

## Revision 2: development data exclusion

The completed acquisition inventory contains 497 originally eligible sessions:
496 complete sessions and one unresolved session on 2023-06-05. That session
contains 386 of 390 expected bars. The missing UTC interval starts are 13:52,
13:53, 13:54 and 13:55, corresponding to 09:52–09:55 America/New_York. A second
full-session request returned the identical CSV. A surrounding-window probe and
four individual-minute requests returned no missing bars; neighbouring bars
matched. These observations establish a persistent absence in the supplied bar
responses. They do not establish the reason for that absence.

The complete 2023-06-05 session is excluded from strategy returns, controls,
annual statistics, concentration calculations and bootstrap records. No price
is filled, no feed is substituted and no synthetic cash day is inserted. The
retained 386 bars, both full-session attempts and all probe responses remain
outside the evaluation sample. This decision precedes inspection of that day's
strategy result or full-period performance. It changes the original sample;
it is not represented as an exclusion declared in revision 1.

The resulting sample has 496 sessions and 193,440 bars: 250 sessions in 2022
and 246 in 2023. There are five exclusions among 501 exchange-calendar
sessions: three early closes, the diagnostic session and this one data-quality
exclusion. Removing 1/497 originally eligible sessions does not establish a
negligible performance effect or eliminate missing-data selection risk.

[Development data policy](development_data_policy.json) is the authoritative
machine-readable exclusion record and binds the acquisition audit, raw session
files and diagnostic probe by SHA-256. Offline finalization verifies those
identities and all selected checkpoints before publishing `study_manifest.json`.
It preserves the original monthly manifests and frozen March data. Any later
data revision or additional exclusion requires a new declared policy and a
new frozen study snapshot. The trading rule, clocks, costs, metrics, bootstrap
settings and continuation gates below retain their revision-1 definitions.

## Trading rule and event timing

All clock times below are America/New_York on an eligible session date.

Let `O_09:30` be the opening price of the 09:30 bar and `C_15:29` the closing
price of the bar covering `[15:29, 15:30)`. The signal is

$$
m_d = \frac{C_{d,15:29}}{O_{d,09:30}} - 1.
$$

| Event | Rule |
| --- | --- |
| Signal availability | At 15:30, after the 15:29 bar has completed |
| Entry decision | At 15:30, enter long if and only if `m_d > 0` |
| Zero or negative signal | Remain in cash for the entire session |
| Primary entry price reference | Open of the 15:31 bar |
| Exit decision | At 15:55, submit a full exit if a position exists |
| Primary exit price reference | Open of the 15:56 bar |
| Frequency | At most one entry and one exit per session; no re-entry |
| End-of-day state | Flat, with no pending strategy order |

The primary replay assumes a fixed one-minute delay from each order decision
to its price reference. The predeclared delay stress is two minutes for both
orders: entry at 15:32 and exit at 15:57. The signal and decision times do not
change between cases. Delay does not grant access to future bar high, low,
close or volume.

For example, on the diagnostic winter session an entry decision at 20:30 UTC
uses the close of the bar labelled 20:29 UTC. Its primary price reference is
the 20:31 UTC bar open. The exit decision is at 20:55 UTC and its primary
reference is the 20:56 UTC bar open. Summer sessions use a different UTC offset.

A minute-bar open is a trade-price proxy, not an observed executable bid or ask
for an arriving order. This replay cannot establish exact fills, queue position,
partial execution or market impact. A positive result advances to quote-based
execution validation before any tradability claim.

The first economic calculation uses continuous notional, fully allocated on
active days, without leverage. This separates the edge calculation from share
rounding and account size. It does not claim an executable account P&L. Cash
earns zero in the strategy and all controls. Integer quantities, account cash,
broker fees and reconciliation belong to the later execution/accounting stage.

## Costs and return calculation

Let `P_e` and `P_x` be the delayed entry and exit price references. For an
assumed one-way all-in cost of `k` basis points, set `c = k / 10000`:

$$
P_{buy}=P_e(1+c), \qquad P_{sell}=P_x(1-c),
\qquad R_d(k)=\frac{P_{sell}}{P_{buy}}-1.
$$

On cash days, `R_d(k)=0`. A round trip charges both sides. The cost grid is
`0, 0.5, 1, 2, 5` basis points per side. One basis point per side and the
one-minute delay form the primary case. Two basis points per side and a
two-minute delay are separately reported stresses. Zero cost is diagnostic.

These are explicit hypothetical all-in scenarios, not estimates measured from
Alpaca quotes, a broker fee schedule or actual fills. Spread, slippage and
fees must not be charged again on top of the same all-in assumption.

For the equal-notional active-trade mean, let `g` be the mean gross return at
a fixed delay. The non-negative break-even cost per side, when `g > 0`, is

$$
k^* = 10000\frac{g}{2+g}.
$$

This follows from the same cost formula, not a linear approximation. If
`g <= 0`, no non-negative cost produces a positive mean; report that condition
rather than a misleading positive cost allowance. Break-even cost is not an
estimate of available real-world execution costs or capacity.

## Chronological evaluation

| Period | Purpose | Allowed use |
| --- | --- | --- |
| March 2022 | First calendar/batch and replay check | Engineering only initially; later included in development, never described as held out |
| 2022-01-01 through 2023-12-31 | Development | Test the fixed hypothesis, inspect diagnostics and establish feasibility |
| 2024-01-01 through 2024-12-31 | Validation | One evaluation of the unchanged candidate after development passes |
| 2025-01-01 through 2025-12-31 | Reserved final evaluation | Access strategy results only after the preceding gates pass |

No feature window, signal threshold, direction, trading clock or cost case is
selected by its ranking on validation or final-evaluation performance. This is
one candidate with a fixed sensitivity matrix, not ten strategies from which
to select a winner. Any change after results are inspected is a new experiment
and must disclose the earlier trials. A failed validation is not followed by
repeated adjustments against the same validation year.

Prior-use declaration recorded 2026-10-20: SPY strategy/backtest results from
2024 and 2025 have not previously been used for strategy judgment or selection.
The planned validation and final-evaluation roles are therefore retained.
This declaration concerns previous strategy selection, not a claim that raw
prices have never been viewed. Subsequent access to strategy results must
follow the sequence above; future exposure changes must be recorded.

## Controls, metrics and uncertainty

All controls use the identical eligible dates and delayed entry/exit window.

1. Cash: zero return, under the stated zero-interest convention.
2. Always-long late window: trade every eligible day, with the same costs and
   delay. Report separately; its larger exposure makes a direct total-return
   ranking insufficient to establish timing skill.
3. Signal contrast: mean gross late-window return on positive-signal days minus
   its mean on zero/negative-signal days. The control window return is computed
   on both groups even when the strategy stays in cash.

Report the number of calendar sessions, exclusions, unresolved data failures,
active days, fraction active, gross and net mean basis points per active trade,
daily mean including cash days, compounded return and daily-close maximum
drawdown. Report daily Sharpe descriptively as `sqrt(252) * mean / sample_std`
with zero cash interest; zero variance yields an undefined Sharpe, not zero.
Daily-close drawdown does not capture intraday adverse excursions. Report
results by calendar year and the contribution of the five largest winning
trades without using those diagnostics to remove inconvenient observations.

Use 10,000 moving-block bootstrap resamples of the ordered eligible-day records,
five trading days per block, with a fixed seed of `20261020`. Sample complete
day records so returns, signal groups and cash days stay linked. Choose block
starts uniformly from `0` through `N-L`, concatenate blocks and truncate to
`N` days; do not wrap the final date onto the first. Report 95% percentile
intervals for primary mean net daily return and the signal contrast. Repeat
with ten-day blocks as an uncertainty sensitivity. If a resample lacks either
signal group, the contrast is undefined for that resample; report the count
and do not claim a reliable contrast interval if such resamples occur.
These intervals assess this specified candidate, not a large unreported search.

## Statistical implementation conventions

Implementation notes recorded 2026-10-20 before the full development run:

- Daily-close drawdown includes initial wealth of one and is reported as a
  nonnegative loss fraction. Annual statistics start a separate wealth path
  for each calendar year. Intraday OHLC excursions are not used as drawdowns.
- Standard deviation uses N-1. A constant return series has zero variance and
  an undefined Sharpe. No annualized return forecast is inferred from the
  descriptive daily Sharpe.
- The five largest winners are positive active-trade net returns at each
  declared case. Their dates and arithmetic return contributions are retained.
  Shares of total arithmetic net and of positive-return sums have distinct
  denominators; zero denominators produce null. Ties use ascending dates.
- The bootstrap engine is `mt19937_64`. Uniform block starts use unsigned
  rejection followed by modulo, avoiding standard-library distribution
  differences. The engine resets to seed `20261020` for each block length.
  Primary net means and gross signal contrasts use the same sampled complete
  day records. The final partial block is truncated; there is no circular wrap.
- Percentile endpoints interpolate linearly at `(M-1)*p`, with p equal to
  0.025 and 0.975. If any replicate lacks either signal group, its undefined
  contrast is counted and the contrast interval is null. No replacement or
  zero-valued contrast is inserted.
- Bootstrap records contain the ordered eligible sessions after all declared
  exclusions. Calendar holidays and excluded sessions are not synthetic cash
  observations. A genuine strategy cash decision on an eligible date remains
  a zero-return observation in both point estimates and resampling.

These specify numerical conventions; the economic rule and gates are unchanged.

## Continuation decisions

These are project screening rules chosen before strategy results, not universal
statistical or commercial standards.

Development requires at least 100 active days. Below that count, the result is
insufficient for this screen. Otherwise:

- Non-positive gross mean at the primary delay: no gross edge demonstrated;
  stop escalation of this candidate.
- Positive gross mean but non-positive primary net mean: cost-dominated at the
  declared cost; stop escalation of this candidate.
- Positive primary net mean, positive net mean at the two-minute delay with
  the same one-basis-point cost, and positive signal contrast: eligible for
  unchanged validation. Uncertainty intervals remain visible even if they
  include zero. Failure of these conditions does not advance the candidate.

Validation requires at least 50 active days, the same positive mean and signal
contrast conditions, and a non-negative net mean at two basis points per side
with the primary delay. For both block lengths, the primary net-mean and signal-
contrast 95% interval lower bounds must exceed zero before final evaluation
is opened. Positive estimates with intervals crossing zero are inconclusive,
not confirmed profitable. Preserve failed and inconclusive outcomes.

Final evaluation reports the complete predeclared matrix once. A negative or
inconclusive final result cannot be repaired by changing the rule and keeping
the label of an untouched final test. A promising final result advances to
historical quotes, actual fee assumptions, executable order sizing and paper
execution checks. It does not establish live profitability.

## C++ implementation sequence

1. Implemented: `IntradayMomentumStrategy` consumes completed `MinuteBar`
   history, and `DelayedBarReplay` applies fixed timing and two-sided cost
   assumptions through `evaluate_long_prices`. Synthetic fixtures cover
   no-trade, positive/zero signals, delayed price references and analytical
   return examples. Missing required timestamps fail explicitly. The diagnostic
   `session_replay` target reads only the excluded diagnostic session; its
   explicit UTC window is not a substitute for calendar verification.
2. Implemented: calendar retrieval, a bounded March 2022 batch, request pacing,
   explicit rate-limit handling and resumable completed sessions reuse the
   existing transport, downloader, audit and storage. Immutable calendar and
   session checkpoints feed a SHA-256 dataset manifest. Offline reconstruction
   checks retained raw pages against CSV and metadata. Synthetic fixtures cover
   both sides of daylight-saving changes, shortened sessions, interrupted
   downloads, missing bars and modified files. The pilot replay saves the
   fixed cost/delay matrix with input provenance; all runtime output is under
   `scratch/`. The March pilot has completed live Windows calendar/batch retrieval.
3. Implemented: the fixed 24-month development dataset imports the frozen March
   pilot and excludes three shortened sessions and the diagnostic fixture.
   Full-period and annual risk statistics, concentration, paired block-bootstrap
   intervals and development screening reuse the same return and statistical
   kernels. Run the full development study and review its continuation decision
   before acquiring or scoring later performance periods.

No order-submission endpoint is required by these steps.

## References

- Gao, L., Han, Y., Li, S. Z. and Zhou, G. (2018). *Market intraday momentum*.
  Journal of Financial Economics 129(2), 394-414.
  <https://doi.org/10.1016/j.jfineco.2018.05.009>
- Baltussen, G., Da, Z., Lammers, S. and Martens, M. (2021). *Hedging demand and
  market intraday momentum*. Journal of Financial Economics 142(1), 377-403.
  <https://academicweb.nd.edu/~zda/intramom.pdf>
- Alpaca, *Historical bars*: inclusive provider bounds, pagination, feed and
  adjustment conventions.
  <https://docs.alpaca.markets/us/reference/stockbars>
- Alpaca, *Get Market Calendar info*: trading dates and session open/close
  times, including shortened sessions.
  <https://docs.alpaca.markets/us/v1.1/reference/getcalendar-1>
