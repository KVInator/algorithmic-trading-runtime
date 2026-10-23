# SPY-RISK-01: 2022–2023 development finding

The native Windows Debug run completed on 23 October 2026 with status
`not_advanced_under_registered_design`. It evaluated the [registered revision-1
protocol](daily_risk_protocol.md) on all 501 development exchange sessions,
using 252 calibration sessions in 2021. Forty account cases cover four policies,
five per-side cost rates and two morning execution delays.

The result does not support advancing intraday RV5 allocation over the daily-only
controller under the registered rules. The daily-only account has higher observed
CER and lower drawdown in the primary case. The point forecast-loss improvement
is positive, but its uncertainty intervals include zero.

## Primary comparison

Execution uses the 09:31 minute open and 1 bp per side. Returns below are
compounded over the complete 2022–2023 development period. CER is the annualized
approximation `252 × (mean daily return − 1.5 × sample variance)`, with risk
aversion three and a zero-return cash reference. Drawdown uses daily closing
equity, including dividend receivables.

| Account | Compounded return | Maximum drawdown | Annualized CER | Mean closing liquid stock weight |
| --- | ---: | ---: | ---: | ---: |
| Intraday risk | 1.7072% | 16.3524% | -0.2749% | 62.7903% |
| Daily-only risk | 4.7624% | 13.3556% | 1.2681% | 61.3035% |
| Static risk budget | 2.7920% | 19.1612% | -0.8472% | 76.8270% |
| Passive equity | 2.3777% | 24.4204% | -2.5230% | 98.6372% |

The static target is 76.9802%, fixed from the 2021 mean daily2. Passive equity
buys once and retains paid dividends in cash. Every account liquidates shares at
the registered terminal reference and retains its unpaid December entitlement.

The intraday account's CER increment is **-1.5430 percentage points** over
daily-only and **+0.5723 percentage points** over the static budget. It incurs
USD 72.3048 of fees on USD 723,047.7932 of actual traded notional. Its positive
compounded return coexists with negative CER because the registered metric
penalizes daily return variance.

## Forecast evidence and uncertainty

The known 5 June 2023 endpoint gap produces twenty subsequent daily-only fallback
dates, from 6 June through 5 July. All 501 dates remain in account returns and
block resampling. The incomplete future partition, incomplete native histories
and terminal truncated interval leave 479 common eligible forecast records.

Daily-only QLIKE less intraday QLIKE averages **0.00781307**, so the point estimate
favors intraday measurement. It is a loss difference, not a percentage accuracy
improvement. Neither block interval establishes a positive increment.

| Statistic | Point estimate | Twenty-session block 95% interval | Forty-session block 95% interval |
| --- | ---: | ---: | ---: |
| CER over daily-only, percentage points | -1.5430 | [-3.2882, 0.4183] | [-3.1541, 0.3786] |
| CER over static, percentage points | 0.5723 | [-6.3854, 7.1581] | [-5.3410, 6.2196] |
| QLIKE increment | 0.007813 | [-0.024393, 0.033742] | [-0.028631, 0.031096] |

Each interval uses 10,000 paired non-circular moving-block resamples of the full
501-date sequence, with the registered seeds. No sampled loss comparison is
undefined. All six lower confidence-bound screens fail.

Annual CER increments also lack the required consistency. Relative to daily-only,
intraday risk is -3.5011 percentage points in 2022 and +0.4292 in 2023. Relative to
static allocation, it is +2.5726 in 2022 and -1.4307 in 2023. At 2 bp per side,
the aggregate CER increment over daily-only remains negative. Ten of the fifteen
registered component screens fail.

The observed ranking is not explained by the specified fees: with zero costs,
the intraday account's CER remains 1.5582 percentage points below daily-only.
Every single-month omission retains a negative increment over daily-only.
These diagnostics describe this result; they do not modify the rules or justify
selecting a replacement strategy on the same data.

## Verification and provenance

The [curated numerical record](daily_risk_development_result.json) preserves
unrounded statistics, gates, bootstrap settings, the native report identities and
verification scope. The original report directory is
`scratch/runs/SPY_RISK_01_development_2022-2023_0` on the native machine. Its
`summary.json` SHA-256 is
`c36a5fc56f55bab1f9d7685ffd689b68d06d5cfb56b1e229475d0bbd56341af2`.

- All five uploaded HTML/CSV artifact hashes match the native summary. The
  registered protocol and three input identities match. All 274 acquisition
  evidence identities, 501 development canonical-bar identities and 24 retained
  calendar identities agree with the preceding acquisition evidence.
- The standard-library reference checks all 120 case/period summaries and 1,002
  prior-only features, including forecast masks, CER, drawdown, exposure, turnover
  and fees. Run `python research/verify_daily_risk_report.py <report-directory>`.
- An independent decimal replay reconciles all 55,760 account events. Maximum
  absolute discrepancy is 4.523e-10 USD, below the 1e-7 USD validation tolerance;
  maximum share discrepancy is 1.061e-12. Dividend entitlement, payments and
  unpaid terminal claims reconcile without duplicate income.
- The 498 locally available development-session CSVs match the native canonical
  identities. They independently verify 494 daily measurement sessions, 988
  delayed holding outcomes and 54,830 ledger price references. Missing calibration
  raw files and three shortened-session raw files limit a complete raw-price
  replay in this workspace.
- An independent MT19937-64 implementation reproduces both 10,000-resample block
  intervals, including their paired date masks. All thirty omission diagnostics
  also reproduce. The three native SVG charts were rendered and inspected.

One source-provenance comparison remains unresolved. The native report records
source digest `291931d8d07a0a992c72cf7f9223c249d20c0b7fc57509f2c1d8f524d6a3e5bc`.
The inspected pre-documentation checkout at commit `e90f64a` produces
`d6110ed23e2e5c7f5b0369462bdd668201cc85051192e0f676546406e12df6f7`.
The aggregate digest does not identify the differing files. The verified input
identities and numerical export checks do not establish byte-identical native
and local source trees; full source-byte parity is unconfirmed.

This is retrospective development evidence with hypothetical proportional costs
and minute-open execution references. It does not establish announcement-time
availability, real broker fills or performance outside the inspected sample.
The result closes revision 1 without advancement. The later-period prices and
candidate performance remain unopened.
