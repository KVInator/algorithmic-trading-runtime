# Primary stability and uncertainty

M7 begins with diagnostics of the authenticated M6 development exports. This
checkpoint implements the primary attribution, concentration and six-endpoint
uncertainty analysis. The fourteen registered secondary cases and risk-matched
ledger replays remain open. It does not select a strategy, modify the registered
rules, advance a model or open 2024–2025 strategy inputs.

## Inputs and interpretation

`stability_development` reads a completed cost/delay directory and a completed
continuous-account directory. Both must use the frozen extension and M6 policies,
matching primary decision records, calendar and study identities. File sizes,
SHA-256 identities, report identities and the account plan are checked. These are
saved M6 exports, whose ledgers have their own independent verifier. This analysis
does not reacquire prices or reauthenticate the raw provider archive.

The primary charge is one basis point per side, with a one-minute reference delay.
The 77 series comprise 48 intraday series (eight policies across five instruments
and an equal-weight index), 24 continuous-risk series (four accounts on the same
six views), and five shared-capital accounts. Equal-weight returns are formed only
when all five component returns are observed. They are research indices, not new
funded portfolio ledgers. Their costs, turnover, holding time and exposure are
component averages. Shared-capital accounts retain the M6 global ledger.

Cash is an observed zero return. A missing or stopped outcome remains unavailable.
Costs and turnover incurred before an incomplete account day remain in their
accounting totals; an unavailable daily return is not inferred from them. Risk
exposure uses the retained mark; shared exposure uses the day's observed maximum.
The accepted shared-account prefixes are particularly short: 65 development dates
at one-minute delay and 45 at two minutes. Cross-instrument primary risk-account
returns also stop before the end of development. No missing tail is interpolated,
restarted or presented as an eight-year account path.

## Descriptive stability

Every series is attributed by year and prior-only volatility condition. Intraday
series are also attributed by total minutes invested per session: cash, (0, 15],
(15, 30], (30, 60], (60, 120] and (120, 390]. These are session-level holding-time
bands, not individual trade-duration or overnight classifications.

At date t, volatility is the sample standard deviation of the 60 consecutive
prior dividend-aware log returns, annualized by sqrt(252). Its cutoffs are the
one-third and two-thirds quantiles of 252 prior calendar volatility slots. All
slots must be observed; missing slots are retained. The current return and current
volatility enter their respective histories after today's classification. Linear
quantiles use index (n − 1)p. Values at or below the lower cutoff are low; values
above the upper cutoff are high; the remainder are middle. Until both histories
are available, the label is unavailable. The pooled descriptive condition uses
the arithmetic mean of the five log returns on all-five observed dates. It is a
descriptive market index, not a funded portfolio return or a trading filter.

Each group reports its selected and observed dates, active dates, mean return,
exposure, turnover, costs, selected-observation return product and drawdown. A
product or drawdown within a selected group is not a continuously funded account
with other dates removed. No group-level significance claim is made.

Concentration removes the largest 1, 5 and 10 positive date contributions, and the
ceiling of 1% of positive dates, by zeroing those contributions while preserving
the original valid-date denominator. Ties use ascending date order. The share of
positive returns is unavailable when there are no positive dates; the fractional
removal is also unavailable then. These are posthoc arithmetic diagnostics, not
counterfactual ledger replays. No positions or costs are refitted after removal.

## Paired inference

The six ordered endpoints and their material increments remain those in the
[registered protocol](../research/extension_protocol.json). The first four are
mean net increments; the fifth is the annual certainty-equivalent difference:

\[
\operatorname{CER}(r)=252\left(\bar r-\frac{3}{2}s_r^2\right),
\]

where variance uses n − 1. Returns are pooled on the identical all-five mask
**before** evaluating CER. Averaging five instrument CERs would be a different
statistic. The sixth endpoint averages control-minus-candidate QLIKE on identical
forecast/target dates, independently of whether the economic account has stopped.

Every draw resamples the full development calendar jointly across instruments
and endpoints. Non-circular blocks have uniform starts in 0 through T − L;
concatenated blocks are truncated at T. Missing positions remain missing, and each
statistic is recomputed on the draw's valid pairs. There are 10,000 draws, seed
290026. The explicit MT19937-64 rejection mapping is shared with existing native
studies; the independent Python sampler reproduces it. A digest of block starts
and lengths is retained for every block length.

Marginal 95% percentile intervals use blocks 5 and 10 for intraday endpoints and
20 and 40 for risk endpoints. The simultaneous family uses a single-step,
one-sided centered max-T with block 40 primary and block 20 sensitivity. The
studentizer is fixed at the original sample's Bartlett HAC estimate with L − 1
lags. This is not a stepdown procedure. For a mean, the calendar influence is
T/n times its centered paired contribution on valid dates and zero elsewhere.
For CER, the variance influence includes the n/(n − 1) sample-variance correction.
For either endpoint, with calendar influence u:

\[
\widehat{\mathrm{SE}}^2=\frac{1}{T^2}
\left[\sum_tu_t^2+2\sum_{h=1}^{L-1}\left(1-\frac hL\right)
\sum_{t=h+1}^T u_tu_{t-h}\right].
\]

The 95th percentile of the joint centered standardized maxima determines each
simultaneous lower bound. A zero/nonfinite studentizer or more than 1% undefined
family draws makes the family unavailable. Missing endpoints are never dropped
to shrink the registered family. At least 200 valid dates and 80% calendar
coverage are required for adequate evidence. Numerical availability, coverage,
and whether a bound clears its material increment are reported separately.
A narrow interval on a short observed prefix does not establish full-period
adequacy.

These are development diagnostics. Controlled size/coverage validation is still
a later gate; the numerical fixtures here do not claim nominal finite-sample
coverage. Risk-matched economic comparisons also remain unexecuted, so the report
makes no advancement decision.

Method references: [Newey and West (1986)](https://www.nber.org/papers/t0055) for
positive semidefinite HAC estimation; [Romano and Wolf (2005)](https://doi.org/10.1111/j.1468-0262.2005.00615.x)
for resampling-based multiple-testing context. The exact single-step procedure
above is the registered implementation, rather than the paper's full stepdown
algorithm.

## Reproduction and output

See [the runbook](running.md#primary-stability-and-uncertainty-m7) for guarded
PowerShell commands. `--check` constructs its own M6 inputs and runs numerical
fixtures, including missing masks and zero studentizers. It requires no credentials.
The historical mode consumes existing accepted M6 directories without writing to
them. It performs no historical strategy replay and needs no raw-price archive.

The static HTML report contains coverage bars, paired interval plots, attribution
heatmaps and winner-removal tables. `summary.json` retains every group and all
four block lengths. Derived observation, market-condition, endpoint and plan
exports support independent recomputation. `verify_stability.py` checks saved M6
source identities, extraction, causal labels, pooling, concentration, influence
studentizers, all joint bootstrap draws and plotted marks. `--review-only` checks
only derived evidence and explicitly does not reopen M6 sources. `review.zip`
contains derived evidence, not raw price pages or order records.

## Verification checkpoint

The local GNU 13.3.0 Release build passed for all 27 applications. Constructed
and saved-export runs agreed exactly on 77 series and 48 development dates.
The independent oracle reproduced all four 10,000-draw analyses and the separate
360-date numerical fixtures, including constant and unavailable endpoints.
Six malformed-source cases were rejected without modifying their inputs. The
existing five cost/delay artifacts and seven continuous-account artifacts remained
byte-identical; their independent checks, the panel replay and the 259-check
offline demonstration also passed. Static report pages were rendered and reviewed.
Windows build acceptance and the full historical M7 run remain pending; these
synthetic checks are not historical strategy findings or inference calibration.

## Remaining M7 work

1. Execute the fourteen registered one-factor secondary cases at the primary
   cost and delay, retaining every instrument/window and unavailable case.
2. Replay the predefined risk-matched sizing, costs and ledgers using 252 prior
   returns, without leverage or rescaling realized outcomes after the fact.
3. Integrate those results with the primary diagnostics before M8's controlled
   inference validation and 2024 evaluation. The 2025 final retrospective stage
   remains M9; final publication remains M10.
