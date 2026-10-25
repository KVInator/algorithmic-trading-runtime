# Opening-anchor reversion protocol

SPY-MR-01, revision 1. Registered 23 October 2026 after the prescribed synthetic
sampling/noise experiment and before any historical residual, model fit,
forecast, decision or strategy score for this candidate. Machine-readable
settings are in [intraday_reversion_protocol.json](intraday_reversion_protocol.json).
The [dated brief](intraday_reversion_brief.md) is retained unchanged; this record
governs scoring. Changes to observations, policies, costs, uncertainty or screens
require a declared revision before affected scoring.

## Question, scope and input identity

Does a prior-session fitted opening-anchor regression improve noon forecasts or
long/cash outcomes over a sign-only contrarian rule and a no-reversion drift
control? This is one bounded development comparison, not a search for another
candidate after earlier failed SPY studies.

Use only the immutable 496 complete 2022-2023 SPY SIP sessions, study-manifest
SHA-256 `70fdd01093b9026cc4e673def25718e547c0052b728163bac9bdf30b9c25126e`.
Preserve the existing exclusions: 2022-11-25, 2023-01-03, 2023-06-05, 2023-07-03
and 2023-11-24. The first twenty eligible sessions are warm-up; score the
remaining 476 common dated records. Warm-up is not inserted as cash returns.
No cross-session return or dividend entitlement arises from the flat intraday
account. Unsupported intraday split/adjustment anomalies stop scoring rather
than trigger a repair. Local 2024-2025 prices and performance remain unopened.

The sample was already inspected during previous designs. Literature and these
synthetic outcomes were available before registration. Registration fixes this
comparison prospectively with respect to this candidate's historical score; it
does not remove prior selection exposure or create an untouched test period.
Final saved SIP bars lack original receipt/revision timestamps. Causal timestamp
replay cannot establish their historical real-time availability.

## Measurements, fitting and chronology

Exchange times are America/New_York, resolved through the verified calendar.
Every selected session requires its full ordered 390-minute grid. Authentication,
integrity, schema, missing/duplicate/out-of-order bars, invalid prices and
nonfinite arithmetic stop the run; none becomes a fabricated cash observation.

Fix the opening reference A to the arithmetic volume-weighted mean of closes
of bars starting at elapsed minutes 0 through 29. It is not transaction VWAP.
Use natural-log residuals `X[k] = log(C[29+5*k]) - log(A)`, k=0,...,24.
The last close completes at noon. Exactly the completed 150-minute prefix is
available at the noon decision; later observations cannot enter it.

At each decision, regress Y on an intercept and X using 24 within-session
transitions from each of the latest twenty strictly earlier eligible sessions.
There are 480 pairs, with no session-boundary pair. Current-session dynamics
never enter the fit. Append a session to history only after its replay completes;
retain an unavailable-anchor slot rather than substituting an older session.
A zero total opening volume makes that session's residual unavailable. The
candidate and drift require all twenty history slots to be available. A current
unavailable anchor blocks the candidate and sign-only rule; the drift and
always-long policies retain their own stated inputs. Every scored date remains.

Use `PooledReversionEstimator` centered unrestricted OLS and direct SSE/(N-2).
Keep raw intercept, slope and variance even for unsupported OU interpretations.
Numerical rank tolerance is `64*double_epsilon*max_abs_X^2*N` for centered XX;
unit-root equality tolerance is `64*double_epsilon`. Positive variance and
`0 < phi < 1` outside that equality tolerance are required for OU admissibility.
Insufficient pairs, singular design, nonpositive slope, unit-root boundary,
nonstationary slope and zero innovation variance remain distinct failures.
No shrinkage, clipping, alternate estimator, half-life cap or best-window search
is permitted. An unsupported candidate fit produces cash with a dated reason.

For admissible fits, report `theta = a/(1-phi)`, `kappa = -log(phi)/5` and
`half_life = log(2)/kappa`, in minutes. The expected delayed log move is
`g(x;delta,H) = (theta-x)*exp(-kappa*delta)*(1-exp(-kappa*H))`.
The fractional-step OU embedding transfers a five-minute close regression to
minute-open outcomes; this is a tested model assumption, not an identified
latent diffusion. Positive sub-unit slope is not a stationarity test.

## Frozen decisions and comparison accounts

The decision is noon and exit decision is 12:30. Primary delayed entry/exit
references are the 12:01/12:31 minute opens. The fixed two-minute stress uses
12:02/12:32. These are price proxies rather than observed fills. Use fractional
unlevered equity, one full-equity long/cash window per day, no short, borrowing,
overnight holding, stop search or intraminute-path fill assumption.

| Policy | Frozen noon decision |
| --- | --- |
| Candidate | Admissible prior fit, available current residual below theta, and g(x;1,30) strictly greater than log(1.0001)-log(0.9999) |
| Sign-only | Current opening residual strictly below zero |
| Drift | Six times the mean five-minute increment over the same 480 earlier pairs strictly greater than the candidate's fixed cost hurdle |
| Always long | Long each scored date |
| Cash | Zero return each scored date |

The four market policies retain delays 1 and 2 minutes and costs 0, 0.5, 1, 2
and 5 bp per side: forty account cases. Signal choices always use the primary
one-minute/1-bp rule, including the fixed g query. Recosting and delay stresses
do not reselect entries. A cash day has zero gross/net return. An active window
uses the shared `evaluate_long_prices`: gross `P_exit/P_entry-1`, net
`P_exit*(1-c)/(P_entry*(1+c))-1`, c=bp/10000. Hypothetical all-in costs are
not combined with a second assumed spread charge. Independent normalized
accounts start at equity one and compound their dated returns. No risk matching
with always-long or cash is implied by different exposures.

## Parameter uncertainty and observed failures

[The prescribed synthetic study](reversion_validation.md) fixes thirteen cases,
1,000 point trials per case and 200 coverage trials per case. It finds severe
undercoverage and false OU admissibility under specified noise/dependence.
The [evidence record](reversion_validation_result.md) retains the exact findings.
No historical noise calibration or noise-robust latent estimator is introduced.

For each history with available residuals, perform 499 whole-session refits at
blocks 1 and 5 using `ReversionUncertainty`. Seed is 20261023 plus the scored
date encoded as YYYYMMDD, identical at both blocks. Use the same sampled session
indices for all reported parameters. Never join session boundaries. All seven
status counts and the total are recorded. Missing coefficients in any replicate
make the slope range unavailable. OU-derived ranges require at least twenty
admissible refits and condition on them; report that count and excluded fraction.
Use linear-interpolated 2.5/97.5 percentiles. No range is a stationarity test,
validity guarantee or entry filter. Low admissible fractions, long half-lives and
cross-block disagreement remain visible; they do not retroactively delete days.
The g range uses the current residual and fixed one-minute/thirty-minute query;
if the current anchor is unavailable, that query is unavailable. The parameter
fit and other parameter ranges can still be reported from available history.

## Forecast comparisons and outcome uncertainty

On every scored date with an admissible candidate fit, available current
residual and available drift history, compare g(x;1,30) and the drift forecast
with the realized log move from the primary entry/exit opens. Use the same dates
for squared and absolute error. Export availability and cash reasons across all
476 dates. Report drift loss minus candidate loss, so positive means improvement.
These are descriptive conditional-mask comparisons. Forecast superiority would
require positive means and strictly positive lower bounds for both loss
contrasts at both outcome block lengths; undefined comparisons block that claim.
It is separate from the economic advancement screen below.

Report gross/net arithmetic means, active dates, holding minutes, round trips,
turnover (two one-equity sides per active date), annual means/compounding,
full-calendar compounding and daily-close drawdown. Sharpe uses sample daily
standard deviation and sqrt(252), with zero variance explicitly undefined.
Report concentration, break-even cost and paired gross/net differences from
sign-only, drift and always-long; differences from cash equal candidate returns.
Drawdown does not measure intraday worst-case loss.

Resample all 476 ordered scored-session records with common indices across all
policies, costs, delays, forecasts and failure masks. Use 10,000 non-circular
moving-block draws at blocks 5 and 10, both seeded 20261023. Preserve genuine
cash dates and masked forecast records. Use the shared unsigned rejection/modulo
sampler and linear percentile convention. Export primary mean-net, paired net
and gross increments and both forecast-loss contrast ranges. Any undefined
statistic replicate makes its entire range unavailable; retain its failure count.
Do not drop failed draws. These percentile ranges do not correct for previous
candidate selection or guarantee nominal coverage under all dependence.

## Advancement and failure rules

Require all of the following for economic advancement:

1. At least 100 primary active dates; strictly positive candidate primary gross
   and net daily means; strictly positive primary net means in both years.
2. Strictly positive candidate net means at one-minute/2-bp and two-minute/1-bp
   stresses. The other matrix cases are descriptive and cannot replace primary.
3. The five largest positive primary daily net returns contribute no more than
   50% of the positive-return sum; removing those five contributions leaves a
   strictly positive arithmetic net sum. Undefined concentration fails.
4. Strictly positive paired primary net increments over sign-only and drift,
   with nonnegative paired gross increments over each.
5. Strictly positive lower percentile bounds for the primary candidate net mean
   and both paired net increments at both outcome block lengths. Unavailable
   ranges fail. Forecast superiority is assessed separately as defined above.

Nonpositive gross mean is `no_gross_edge`; positive gross but nonpositive primary
net is `cost_dominated`. Otherwise any failed screen gives
`failed_registered_screens`; all pass gives `passes_development_screens`.
Retain every gate value regardless of an earlier failure. Passing would support
only this selection-exposed development comparison, not latent-OU identification,
confirmed alpha or automatic later-period evaluation. A failed study is complete
once its registered comparisons and reproducibility checks are delivered. No
alternative threshold/window/strategy is selected to reverse a negative finding.

Before historical scoring, implement and verify a prior-only history container,
all four policies, cash/failure paths, cost/delay parity, paired uncertainty and
protocol/settings identity checks. A successful synthetic/native run does not
substitute for these implementation prerequisites. Later-period evaluation,
paper transport and execution measurements remain separately governed work.
