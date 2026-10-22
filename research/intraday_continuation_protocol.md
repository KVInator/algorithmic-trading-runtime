# Intraday continuation and volume protocol

Protocol: `SPY-TOD-01`, revision 1, recorded 2026-10-21.

Status: specification and deterministic engine milestone. No historical strategy
result has been computed for this candidate. Aggregate scoring is a subsequent
milestone, after the implementation contracts pass. SPY-IM-01 and its rejected
development finding retain their original identities and conventions.

## Question and scope

Does an unusually positive SPY move relative to its historical time-of-day scale
predict subsequent intraday returns? Does an entry condition based on unusually
high cumulative share volume improve the same strategy?

Persistent information processing or trading demand motivates continuation.
Unsigned share volume measures activity, not buyer-initiated order flow. Its
incremental information is an empirical question. This is an adaptation inspired
by the cited literature, not a replication of its published returns or rules.

The primary variant is volume-conditioned; price-only is its declared reference.
The two variants are price-only and volume-conditioned. Both are
unlevered long/cash rules with sparse decisions, identical exits and no stop,
profit target, VWAP exit, fitted coefficient or parameter optimization. Overnight
strategies remain eligible for future research; this candidate closes each day.

## Input identity and chronology

Use the verified 496-session 2022-2023 SPY SIP development snapshot identified by
study-manifest SHA-256
`70fdd01093b9026cc4e673def25718e547c0052b728163bac9bdf30b9c25126e`.
The March pilot and all retained source evidence remain unchanged. Apply the
existing five exclusions to both feature history and scoring: 2022-11-25,
2023-01-03, 2023-06-05, 2023-07-03 and 2023-11-24.

All selected sessions require a complete 390-minute 09:30-16:00 New York grid.
The calendar supplies each date's UTC window. A fixed year-round UTC offset is
invalid. Missing bars, unordered/duplicate timestamps, wrong symbols or malformed
values fail; they are not repaired or converted to cash decisions.

The first 20 eligible complete sessions are warm-up only. They are not scored or
represented as zero-return observations. The remaining 476 sessions are the
planned development scoring sample. Each forecast uses the latest 20 strictly
earlier eligible sessions. Holidays and excluded sessions are skipped explicitly;
20 eligible sessions are not described as 20 consecutive calendar dates.

No cross-day raw-price ratio is used. Share volume remains in raw shares; any
split or known within-session adjustment anomaly requires a new declared data
policy rather than an ad hoc price/volume repair. Exact trade-weighted VWAP and
trade count are not inputs to this candidate.

The dataset wrapper must verify the expected study-manifest identity, retain
input/feature/protocol hashes and append a current session to history only after
its complete replay. The feature engine itself validates sessions and chronology;
it does not authenticate files or infer exchange trading dates.

## Causal feature definitions

All times below are America/New_York on the supplied regular session.
At elapsed minute m, use bars with starts 0 through m-1. Their OHLC and volume
are known only after the last interval completes.

For current session d, let O[d] be its 09:30 bar open and C[d,m] the close of the
bar ending at elapsed minute m. Define

```text
move[d,m] = (C[d,m] - O[d]) / O[d]
scale[d,m] = mean(abs(move[j,m])) over the 20 earlier eligible sessions j
cum_volume[d,m] = sum(volume[d,i]) for i = 0,...,m-1
normal_volume[d,m] = median(cum_volume[j,m]) over those same 20 sessions
relative_volume[d,m] = cum_volume[d,m] / normal_volume[d,m]
price_condition[d,m] = move[d,m] > scale[d,m]
volume_condition[d,m] = relative_volume[d,m] > 1
```

The median of 20 observations is the arithmetic midpoint of the tenth and
eleventh sorted values. Twenty sessions provide one fixed approximately monthly
reference window for both features. The volume threshold of one means activity
above the historical same-clock median. These are design choices, not estimates
of optimal parameters. Equality does not pass either condition.

A zero historical median volume makes the feature undefined and blocks that
evaluation. It does not imply zero relative volume or authorize a fabricated
cash day. Finite arithmetic overflow fails explicitly. A zero current cumulative
volume yields zero relative volume when the historical denominator is positive.
No same-day final volume, future interval or later history enters a decision.

## Decisions and execution

Decisions occur at elapsed minutes 30, 60, ..., 360: 10:00 through 15:30.
The terminal liquidation decision is at elapsed minute 385, or 15:55.

| Existing position | Price condition | Variant / volume condition | Decision |
| --- | --- | --- | --- |
| Cash | False | Either | Remain in cash |
| Cash | True | Price-only | Enter long |
| Cash | True | Volume-conditioned, volume condition true | Enter long |
| Cash | True | Volume-conditioned, volume condition false | Remain in cash |
| Long | True | Either | Hold long |
| Long | False | Either | Exit to cash |
| Either | Any | Terminal 15:55 decision | Exit any position; remain in cash otherwise |

Volume restricts fresh entries. A later decline in relative volume does not
change the exit rule. Re-entry is allowed at a subsequent scheduled decision.
There are no pyramided positions, overlapping orders or intraminute triggers.

Use one-minute delay as the primary case and two-minute delay as the fixed
stress. An instruction at elapsed m uses the bar open at m+delay, after the
instruction. The old position remains effective until this reference time.
Since both delays are shorter than the decision spacing, a reference occurs
before the next strategy decision. Liquidation references are 15:56 or 15:57.

The always-long control enters at 10:00+delay and exits at 15:55+delay on every
scored session. Cash earns zero in every case. A bar open remains a trade-price
proxy, not an observed executable bid or ask. Quotes, integer sizing, broker
fees and paper reconciliation are required before tradability claims.

## Return convention and fixed comparisons

Each entry allocates the then-available continuous-notional equity. For entry
reference E, exit reference X and one-way cost c in return units:

```text
trade_gross = X / E - 1
trade_net = X * (1-c) / (E * (1+c)) - 1
daily_net = product(1 + trade_net) over closed trades - 1
```

Reinvestment compounds within the session; returns are not summed. A cash-only
session returns zero and incurs no trade costs. A carried position is not charged
again at each hold decision. Both variants and the control use the authoritative
`evaluate_long_prices` calculation.

Report both delays and costs of 0, 0.5, 1, 2 and 5 bp per side for both variants.
One-minute delay / 1 bp per side is primary. These are hypothetical all-in costs,
not observed fee/spread estimates. Do not add a second spread/slippage charge.
The fixed matrix is sensitivity analysis, not permission to select its winner.

## Statistical scoring contract

The aggregate milestone must report daily gross/net returns including genuine
cash days, completed round trips, active days, time in market, turnover, annual
results, compounding, daily-close drawdown and five-largest-winner concentration.
Daily-close drawdown is not intraday worst-case loss. A zero standard deviation
gives undefined Sharpe. Capital is committed account equity rather than only
capital employed in winning trades.

The primary volume comparison is the paired daily net difference between the
volume-conditioned and price-only variants. Retain the paired gross difference
as well: transaction-cost savings alone do not establish return predictability.
Compare both variants with the identical-clock always-long control and cash;
report their different exposures rather than calling them risk-matched.

For a directional-information diagnostic, calculate a delayed forward return at
each of the 12 decision clocks, ending at the next decision's delayed open, or
the terminal delayed open for the final clock. At each clock, contrast mean
gross forward returns for true versus false price conditions. The volume
contrast compares high versus other volume among true price conditions. Average
the 12 within-clock differences with equal weights. If either group is absent
at any clock, the overall contrast is undefined and its group counts are shown.

Use 10,000 non-circular moving-block resamples of whole ordered scored-session
records, five-day primary blocks and ten-day sensitivity blocks, seed 20261021.
All variants, controls and opportunity records share the sampled indices.
Report 95% percentile intervals for each primary daily net mean, the paired net
and gross volume increments and both within-clock contrasts. Use the existing
unsigned rejection/modulo sampling and interpolated-percentile conventions.
Any undefined contrast replicate makes that interval undefined; report counts
instead of dropping or replacing failed resamples. These intervals do not
correct for all previous or future strategy-selection trials.

## Continuation gates

These are project screens, not universal standards. Assess price-only and
volume-conditioned results separately; never silently relabel a failed primary.

For either variant to advance from development, require at least 100 active
days, a positive gross daily mean, positive primary net mean, positive net means
at both the two-minute/1-bp and one-minute/2-bp stresses, and positive primary net
means in each calendar year. Its price-information contrast must be defined and
positive. A non-positive gross mean stops that candidate; a positive gross mean
with a non-positive primary net mean is cost-dominated.

Before evaluating later performance periods, require strictly positive lower
95% bounds for its primary daily net mean and price-information contrast at both
block lengths. Otherwise the development result remains inconclusive for that
escalation, even when its point-estimate screen passes.

The five largest positive daily net returns must contribute no more than 50% of
the positive-return sum, and removing those five contributions must leave a
positive arithmetic net sum. Report the actual drawdown and concentration even
when a prior gate fails. Positive point estimates with uncertain intervals are
development evidence, not confirmed alpha.

Claim incremental volume value only when paired primary net improvement is
positive, paired gross improvement is non-negative and the volume-information
contrast is defined and positive. Before later escalation, require strictly
positive lower 95% bounds for the paired net increment and volume contrast at
both block lengths. Otherwise retain the finding as unproven or adverse. A
supported price-only result can be considered on its own declared gates, with
the failed volume comparison visible.

## Later evidence and implementation milestones

2022-2023 remains development, including previously inspected prices and the
March pilot. Local 2024-2025 candidate results remain uncomputed. The literature
review includes papers using 2024-2026 observations, so local held-out historical
evaluation is not wholly independent of external strategy selection. Record any
later historical test once, its prior exposure and frozen configuration. A
genuinely prospective interval begins after design and implementation freeze.

1. This milestone: causal history, features, strategy state transitions,
   deterministic delayed replay, synthetic contract tests and a labelled visual
   diagnostic. No historical aggregate scoring or broker submission.
2. Next: verified frozen-dataset traversal, paired daily/opportunity records,
   the declared uncertainty/statistics, and generated visual development output.
3. Only after the screens: declared later evaluation, historical quote checks
   and an account/paper adapter appropriate to the supported strategy.

## References

- Zarattini, Aziz and Barbon, *Beat the Market: An Effective Intraday Momentum
  Strategy for S&P500 ETF (SPY)*, version 2025-02-03.
  <https://alexandria.unisg.ch/bitstreams/a99aba00-f967-49b3-aceb-f544dc386e0b/download>
- Baltussen, Da, Lammers and Martens, *Hedging demand and market intraday
  momentum* (2021). <https://academicweb.nd.edu/~zda/intramom.pdf>
- Alpaca, *Historical bars*. <https://docs.alpaca.markets/us/reference/stockbars>
