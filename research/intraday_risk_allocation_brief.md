# Daily exposure informed by intraday risk

Candidate: `SPY-RISK-01`. Status: proposed research design, subject to data and
accounting feasibility. No candidate returns or forecast scores have been
computed. The [candidate comparison](strategy_candidates.md) records the
research decision and inspected sources.

## Economic question

Do intraday risk measurements improve a daily SPY allocation compared with an
otherwise identical controller using only daily returns, after turnover and cash
are accounted for?

The mechanism is persistence in risk, combined with an expected return that need
not rise proportionately with conditional variance. Better measurement could
allow more useful exposure decisions. Neither persistence nor a more accurate
forecast guarantees better portfolio outcomes. Reducing exposure can protect
capital during turbulent periods and miss subsequent recoveries.

This is a portfolio risk-management strategy. It does not forecast the next
return's sign. A positive absolute return can simply reflect SPY exposure; the
research claim must concern incremental forecast and economic value against
credible controls. In particular, lower realized risk than 100% SPY is not enough.

## Proposed observations and chronology

The proposed development period remains 2022-2023. A new dataset version must
retain every exchange session, including early closes and all five dates excluded
from the earlier intraday study. A separate 2021 calibration segment would fix
the static comparator and provide leading risk history. Obtaining those inputs
does not authorize later-period performance access.

For completed session j, define an action-consistent passive unit-wealth path
from the preceding session's closing reference through j's opening reference,
five-minute intraday references and closing reference. The closing reference is
the final regular-session minute's close, explicitly a proxy rather than an
official auction close. Early closes use their actual exchange endpoint.

From identical interval endpoints, construct:

```text
daily_move[j] = log(unit_wealth_at_close[j] / unit_wealth_at_prior_close)
RV5[j]       = sum(squared log changes along the reference partition)
daily2[j]    = daily_move[j]^2
```

RV5 includes the overnight jump and regular-session observations. It does not
annualize a shortened session to 390 minutes or turn a weekend into additional
exchange sessions. The unit path accounts for splits and cash distributions, so
an ex-dividend price drop is not mistaken for an unoffset investor loss.
The sum of partition log changes must equal the endpoint log change.

The proposed primary risk estimate for trading session d is:

```text
h_intraday[d] = mean(RV5[j])    over twenty earlier consecutive exchange sessions
h_daily[d]    = mean(daily2[j]) over those same sessions
annual_risk  = sqrt(252 * h)
target       = min(1, 0.10 / annual_risk)
```

The forecast must be finite and positive. A missing endpoint is not a zero
return; a zero estimate is not replaced with an arbitrary epsilon. Missing-data
policy must be resolved before registration. These are second-moment risk
estimates; interpreting them as conditional variances uses the short-horizon
zero-conditional-mean approximation.

Both estimates use information available by 16:05 after the preceding session's
regular close. The target is fixed then, the order decision is scheduled for
09:30 on d, and the primary reference is d's 09:31 minute open. Current-session
prices or volume cannot alter the target. Two minutes is the fixed execution-
delay stress. There is one scheduled rebalance per session and positions may
remain open overnight. No within-day risk adjustment or return-sign filter is
added. Ten percent is a declared annual risk budget and 100% is a leverage cap,
not fitted performance parameters.

The forecast target for loss evaluation is the subsequent reference-to-reference
holding interval. Its five-minute realized-variation partition includes the
regular-session tail, overnight jump and the next opening tail, ending at the
next execution reference. This outcome is available only after that interval
ends. The historical feature windows end at prior closes, while the evaluated
holding intervals run between morning references; that transfer of risk
information is part of the hypothesis and may fail.

## Controls and account convention

Use the same calendar, prices, corporate actions, cash convention and cost model
for all four accounts:

| Account | Allocation rule |
| --- | --- |
| Intraday-risk controller | Target from h_intraday |
| Daily-only controller | Identical target map from h_daily |
| Static risk-budget comparator | Daily rebalance to a fixed weight from the full 2021 daily2 calibration average and the same risk budget/cap |
| Passive equity comparator | Buy and hold SPY with distributions retained in the account |

The static weight is frozen before development and never rescaled to the
realized 2022-2023 volatility. It is an ex-ante risk-budget comparator, not a
claim of identical realized risk. Passive buy-and-hold drifts with prices and
distributions; it is not rebalanced to a constant weight each day. Cash is an
additional reference.

The research account holds fractional shares, cash and dividend receivables.
It has nonnegative cash and shares and no borrowing or short positions. Targets
apply to liquid wealth; unpaid receivables count toward total equity but cannot
fund purchases. Entitlement is established by the position held across the
relevant ex-date. Recognize the receivable on the ex-date and transfer it into
cash on the payment date, without recognizing the same income twice. Split
events alter shares and price units while preserving wealth absent a market move.

Use raw transaction-reference prices plus the corporate-action ledger. Combining
dividend-adjusted prices with explicit cash dividends would double-count income.
Primary idle cash earns zero, an explicit analytical convention for this study.
An investable cash-return sensitivity requires a separately sourced, causal rate
and crediting convention before registration; overnight calendar days must then
be counted explicitly. Risk-free-adjusted Sharpe must not be claimed for a
zero-cash-return account without an appropriate reference.

Charge 0, 0.5, 1, 2 and 5 bp on actual traded notional, with 1 bp primary. Both
delay references are retained, giving ten cases per account and forty account
paths in total, plus cash. Holding an unchanged number of shares incurs no
turnover charge; an unchanged target weight can still require a trade after
prices move. Compute the rebalance against post-cost liquid wealth rather than
using a fully invested round-trip formula for partial positions. Model sale
proceeds as immediately reusable research cash. Settlement restrictions,
integer lots, taxes, market impact and broker fills are outside that convention.

Account returns and annual results use daily closing-reference equity, including
overnight moves, distributions and trading costs. Forecast-loss records use their
own morning-to-morning interval labels. Crossing a year boundary must not assign
all overnight profit to the year of the entry decision. At the development end,
liquidate at the final session's declared pre-close reference. That truncated
last holding interval remains in account P&L and is excluded from full-horizon
forecast scoring. Any unpaid dividend receivable remains at its declared face
value in terminal equity, separately from cash. No January 2024 price or later
strategy return is needed to terminate the study.

## Data feasibility before implementation

The current 496 complete intraday sessions are reusable evidence, not a complete
overnight dataset. The 24 retained calendar-response bodies, checked against
their saved hashes, contain 501 unique exchange dates and three shortened
sessions. That count is the starting coverage requirement for the new account.

- Early-close sessions need valid opening, intraday and closing references.
  The old 3 January 2023 diagnostic may supply data, but its prior use is disclosed.
- The retained 5 June 2023 record lacks four minutes, including the 09:55
  five-minute endpoint. Existing evidence therefore cannot establish complete
  RV5 for that date. Full account valuations still require that day's marks.
- Dividend amounts, ex-dates, payment dates and split events require verified
  coverage for calibration, warm-up and development. API access and historical
  completeness must be checked, not inferred from successful bar requests.
- The existing calendar conversion is bounded to 2022-2025 and its independent
  development calendar to 2022-2023. A 2021 segment requires a deliberate calendar
  extension and verification, not a fabricated list of weekdays.
- Historical final bars and present corporate-action records do not prove what
  arrived by the original decision cutoff. Source timestamps, revisions and
  announcement availability remain a point-in-time evidence limitation.

A missing risk feature cannot justify deleting a held session's P&L. The
preflight must either obtain a verified observation or propose a causal,
explicitly logged data-failure policy before scoring. Forward filling a missing
price, substituting a different feed silently or choosing a policy after seeing
its P&L is unacceptable. Unresolved feasibility leaves this proposal unregistered.

## Forecast and economic tests

Forecast evaluation compares both estimates on the same future holding intervals
and the same outcome proxy. Primary loss is `log(h) + y/h`, the QLIKE ranking
form up to a common outcome-only term; h is positive and y is the subsequent
realized-variation proxy. Squared forecast error is a secondary diagnostic.
Also report results against squared endpoint returns, which are noisier labels.
QLIKE does not cure a biased or incorrectly timed variance proxy. Five-minute
sampling is primary; one-minute and fifteen-minute measurement differences are
noise/aggregation diagnostics rather than a search for the winning strategy.

For economic value, report account growth, returns, risk, turnover, time-varying
exposure, daily-close drawdown, tail losses, overnight/intraday attribution and
results by year. The proposed primary utility statistic is the mean-variance
certainty-equivalent approximation, with risk aversion gamma = 3:

```text
CER = 252 * (mean(daily_net_return) - gamma/2 * sample_variance(daily_net_return))
```

This is a decision criterion, not an exact utility certainty equivalent for a
non-Gaussian wealth distribution. Report paired CER differences versus both the
daily-only and static risk-budget accounts, together with gross counterparts.
Neither higher Sharpe nor a smaller drawdown alone establishes value from the
intraday measurements. A higher total return than buy-and-hold is not required
for a risk-management benefit, and must not be substituted for its registered
economic objective after inspection.

The proposed advancement requirements are positive net CER increments over
both primary controls, positive forecast-loss improvement over daily-only,
positive lower 95% bounds for those three differences at twenty- and forty-
session block lengths, and positive net CER increments in each year and at the
2-bp cost stress. Use 10,000 resamples and a protocol-recorded seed. Resample
whole paired dates or full paired forecast-interval records as appropriate to
each statistic; their different clocks must remain explicit. These intervals
condition on the fixed research design and do not recreate model selection.

Twenty-day measurement windows induce persistent weights. The longer block
lengths reflect that dependence, with substantial uncertainty remaining in a
two-year sample. Single-month removal and extreme-day dependence are diagnostic
tables, not alternative ways to pass a failed primary test. These are proposed
screens for a new economic question; SPY-TOD-01's failed gates are unchanged.

## Implementation and learning value

The main reusable addition is an authoritative long/cash account and corporate-
action ledger, composed with a risk-measurement component, a target-weight policy
and the existing deterministic price-reference adapter. Strategy decisions,
account state, execution costs and reporting remain separate responsibilities.
No general-purpose broker framework or machine-learning stack is needed.

Analytical contracts should cover dividend price-drop/receivable continuity,
payment without duplicate income, split invariance, partial rebalance costs,
liquid-wealth purchase limits, overnight attribution, early closes and terminal
liquidation. A zero-action, fully invested round trip should agree with the
existing return kernel. Flat paths, offsetting intraday moves and strictly
lagged windows provide risk-estimator limits. The core remains C++20.

This direction adds account accounting, event ordering, numerical risk measures
and fair portfolio comparison. Its larger implementation burden is justified
only if the data preflight succeeds and the incremental economic question stays
central. It does not warrant building two complete strategy families at once.
