# SPY-RISK-01: daily risk allocation

Revision 1. Registered on 22 October 2026 before calculating candidate forecasts,
allocation paths or performance. The accompanying JSON fixes the numerical
settings and input identities. The earlier intraday studies retain their frozen
protocols and findings. The strategy brief supplies the economic motivation and
literature record; this protocol governs the implemented experiment.

## Sample and information

Use all 501 exchange sessions from 3 January 2022 through 29 December 2023,
including shortened sessions, the earlier diagnostic date and 5 June 2023.
Calibration uses the 252 sessions in 2021 and the closing boundary on 31 December
2020. The final twenty 2021 sessions initialize both rolling estimates. The input
manifests and issuer-reference digest are fixed in the JSON. Additional or altered
inputs require an explicit revision before scoring.

2022–2023 is inspected development data. The candidate was selected after earlier
strategy results and literature exposure. These confidence intervals condition on
this design and do not account for the complete research-selection process.
Local 2024–2025 prices and strategy results remain outside the experiment.
Early-2024 corporate-action processing metadata is used only to reconcile
entitlements earned before the development endpoint. Historical final bars and
present-day action records do not establish real-time availability.

## Risk measurements

Each daily reference starts with one share at the preceding final regular-session
minute close. If USD D per share goes ex during the session, the opening and all
subsequent reference values are raw price plus D. Earlier dividend claims are not
carried into this independently normalized daily reference. Thus the daily
gross factor is `(close + D) / previous_close`. This is a measurement convention,
not a claim that unpaid dividends can finance stock purchases.

The RV5 partition consists of the previous close, the session open, and the close
of each five-minute interval through the actual session close. An endpoint at
09:55 uses the close of the minute starting at 09:54. The sum of interval log
changes equals `log((close + D) / previous_close)`; RV5 is the sum of their
squares. Squaring that endpoint log return gives daily2. One-minute and fifteen-
minute partitions are measurement diagnostics, not alternative trading rules.
Any missing partition endpoint makes that partition unavailable. No interpolation,
forward fill, zero return or alternate feed is inserted.

For a development date, h_daily is the arithmetic mean of daily2 over exactly
twenty preceding consecutive exchange sessions. Native h_intraday is the mean
RV5 over those same dates only if every endpoint set is complete. The feature
cutoff is five minutes after the preceding session's actual close, ordinarily
16:05 New York and 13:05 after a shortened session. Current-session inputs cannot
change the target. Nonfinite or nonpositive risk estimates stop scoring; no
epsilon or retrospectively chosen replacement is allowed.

When native h_intraday is unavailable, the intraday-risk account uses h_daily.
The affected history dates, native availability and used estimate are exported.
The daily-only account continues to use h_daily. Missing account prices or daily
returns stop the study. Missing risk never deletes a held date's P&L. This
fallback is fixed from input coverage before observing this candidate's returns.

The target is `min(1, 0.10 / sqrt(252 * h))`, with no borrowing or shorting.
The static comparator applies that map to the full 2021 mean daily2, once before
development; it is never rescaled using development returns.

## Four accounts and event order

All accounts start with USD 100,000 cash and no shares or receivables. They use
fractional shares, raw prices, zero cash interest, immediately reusable sale
proceeds and proportional fees on actual traded notional. The target applies to
post-cost liquid wealth, excluding receivables. The existing long/cash account
solves that self-financing rebalance equation. Whole-share constraints, settlement
restrictions, taxes, market impact and broker fills are outside this account.

| Account | Morning action |
| --- | --- |
| Intraday-risk | Rebalance to the native intraday target or the logged daily-only fallback |
| Daily-only | Rebalance to the daily2 target |
| Static risk budget | Rebalance to the fixed calibration weight |
| Passive equity | Buy once at the first execution reference; retain shares and paid distributions in cash |

Orders are scheduled at 09:30. Execution uses the 09:31 minute open, with the
09:32 minute open as the fixed delay stress. Each account has ten cases: costs
of 0, 0.5, 1, 2 and 5 bp per side and both delays. The primary case is one minute
and 1 bp. There are forty paths, plus a zero-return cash reference. The zero-cost
paths provide gross comparisons rather than adding fees back to net returns.

Corporate-action events occur at the start of their actual UTC calendar date.
Accrue ex-date entitlement from shares already held, then settle all due claims.
Payment transfers a receivable into cash without recognizing income again.
For this bookkeeping event, use the preceding raw closing reference less any
new ex-date dividend; it is a theoretical continuity mark, not an observed price
or fill. Then mark at the observed session open, execute the scheduled rebalance
and mark at the final regular-session minute close. Payment-only dates between
sessions use the last available reference and zero cash interest. No trade is
scheduled on a non-session date. Account opening marks and later prices do not
feed a target already fixed from prior observations.

The acquired action history must reconcile all twelve cash distributions with
the issuer reference. Unsupported action types stop this experiment; they are
not silently approximated. The reusable account's synthetic split contracts do
not imply that a historical split ledger has been inferred from missing records.

On the final development session, liquidate shares at the minute open four
minutes before its calendar close. Include that sale's fee and retain unpaid
claims at face value in closing equity, separately from cash. No January 2024
price is needed. Passive equity also liquidates at this terminal reference.

## Returns, attribution and forecast labels

Account returns are close-to-close changes in total equity, with initial cash as
the first denominator. Overnight USD P&L is opening equity less preceding closing
equity, including ex-date entitlement. Intraday USD P&L is final equity less
opening equity and includes trading fees. Their sum reconciles to each daily
equity change. Report turnover, fees, liquid stock exposure, cash, shares,
receivables, closing equity and terminal liquidation explicitly.

Forecast outcomes run from one morning execution reference to the next. Their
partition contains the remaining session's grid endpoints and close, the next
session open, any next-session grid endpoints strictly before execution, and
the next execution open. Grid endpoints at the starting execution timestamp are
excluded. Dividend entitlement during this interval adds D to all subsequent
reference values. The squared interval log changes give the outcome proxy;
their sum reconciles to the interval endpoint log change. Both delays have
their own aligned outcomes. The final truncated holding interval stays in
account returns but is excluded from forecast evaluation.

Primary paired forecast loss uses native positive h_intraday and h_daily on a
common date mask with complete future RV5. Fallback dates remain in account
returns and are excluded from native-forecast comparison. QLIKE ranking loss is
`log(h) + y / h`; report the mean daily-only loss less intraday loss, so a positive
increment favors intraday measurement. Squared forecast error and loss against
squared endpoint returns use the same mask. Missing labels are never zero losses.

Report all-date and annual account statistics, daily-close drawdown, gross and
net CER, turnover/costs, exposure, tail losses, month omission and largest-winner
dependence. CER is `252 * (mean(r) - 3/2 * sample_variance(r))`, an approximation
rather than exact expected utility. The annualized mean/std ratio uses a zero
reference; it is not a risk-free-adjusted Sharpe ratio.

## Uncertainty and advancement

Use 10,000 non-circular moving-block resamples at lengths twenty and forty,
with the JSON seeds and the existing unbiased mt19937_64 index sampler.
Resample complete paired development-date records across the full 501-date
sequence. Compute CER on every sampled account return; compute loss increments
from eligible records within that same sample. Do not compress away unavailable
dates and thereby join unrelated blocks. Any sample with no eligible forecast
record is counted explicitly and makes that loss interval unavailable.
Use linearly interpolated 2.5% and 97.5% empirical quantiles.

Advancement requires all of the following in the primary case:

- Positive net CER increments over both daily-only and static risk budget.
- Positive paired QLIKE improvement over daily-only.
- Positive lower 95% bounds for all three increments at both block lengths.
- Positive net CER increments over both controls in each calendar year.
- Positive aggregate net CER increments over both controls at 2 bp per side,
  retaining the primary one-minute delay.

These are registered research screens, not universal laws about useful risk
management. Failure preserves the result and blocks promotion under this design.
Secondary diagnostics do not replace a failed primary gate. No threshold search,
additional strategy family or later-period evaluation is authorized by this
registration. Revision 1's forecasts, account paths and scores remain uncomputed
at publication of this protocol.
