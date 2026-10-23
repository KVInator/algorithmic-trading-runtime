# Opening-anchor intraday reversion

Candidate SPY-MR-01. Proposed design dated 23 October 2026. Status:
unregistered and unscored. This brief specifies a bounded experiment within
[completion milestone 2](../docs/completion_plan.md). It is not a revision of
SPY-IM-01, SPY-TOD-01 or SPY-RISK-01.

## Question and economic interpretation

Does a model of reversion toward a fixed opening reference add forecast or
economic value over simple contrarian and no-reversion controls?

The hypothesis is that some deviations after opening price discovery reflect
temporary demand rather than a permanent change in value. This mechanism is
unverified. A single ETF's opening reference is not a cointegrating relation or
a traded market-neutral spread. Long/cash positions retain directional exposure.

The reference stops updating at 10:00 New York. A moving reference can produce
apparent residual contraction mechanically; the fixed reference removes that
particular explanation. It does not make the residual stationary. Common
opening transients, time-of-day effects, selection and microstructure noise can
still produce misleading estimates.

## Observations and chronology

Use the same immutable 496 complete SPY sessions in the 2022-2023 development
snapshot, including the existing exclusions. Twenty earlier eligible sessions
provide initial training history; 476 subsequent dates are the proposed scored
calendar. This inspected sample remains development data. Local 2024-2025 prices
and candidate performance remain unopened.

For each session, define the fixed reference from its first thirty minute bars:

\[
A_d=\frac{\sum_{m=0}^{29} V_{d,m}C_{d,m}}
           {\sum_{m=0}^{29} V_{d,m}}.
\]

This is an arithmetic volume-weighted mean of minute closes, not transaction
VWAP. Zero opening volume makes the reference unavailable. There is no
forward-filled or alternate-anchor fallback.

At elapsed minutes t = 30, 35, ..., 150 after 09:30, define

\[
X_{d,t}=\log(C_{d,t-1})-\log(A_d).
\]

There are 25 observations and 24 five-minute transitions per session. The last
observation is the 11:59-12:00 bar's close. The decision is at 12:00; the entire
150-minute prefix must be complete. The signal cannot access a 12:00 bar or a
later revised observation before its declared availability.

Final historical SIP bars do not retain the original delivery/revision times.
Timestamp-causal replay on those saved bars does not establish that identical
values were available at the historical decision. The preflight is restricted
to the independently checked 2022-2023 exchange calendar.

Fit only the first 150 minutes of twenty strictly earlier eligible sessions,
giving 480 within-session pairs. No transition connects one session to the next.
The current opening anchor is observed on the current day, but the dynamics are
estimated only from earlier days. Keep all current dates in the account record;
an unavailable model produces cash rather than deleting its day's result.

The planned exit decision is 12:30. Primary price references are the 12:01 and
12:31 minute opens; the fixed two-minute stress uses 12:02 and 12:32. These are
historical price proxies, not verified broker fills. The reference remains fixed
through exit. No overnight position, stop search or intraminute path assumption
is added.

## Statistical model and decision

Estimate the unrestricted regression with an intercept:

\[
X_{d,t+5}=a+\phi X_{d,t}+\epsilon_{d,t+5}.
\]

The implementation uses centered pooled OLS, with innovation variance estimated
as SSE divided by N-2. It retains unrestricted coefficients even if they cannot
be interpreted as a stationary continuous-time model. Insufficient observations,
singular designs, nonpositive slopes, a numerically unresolved unit-root boundary,
slopes above one and zero innovation variance have explicit statuses.

For an admissible fit, the proposed OU interpretation is

\[
\kappa=-\log(\phi)/5,\qquad
\theta=a/(1-\phi),\qquad
t_{1/2}=\log(2)/\kappa.
\]

Time is in minutes and residuals are natural-log price units. Positive
sub-unit slope is an admissibility condition; it is not a unit-root test or proof
that SPY's transformed prices follow an OU diffusion. Near-unit-root estimation
and short histories can make the equilibrium and half-life unreliable.

With current residual x, entry delay delta and holding length H, the model's
expected log appreciation between delayed entry and exit is

\[
g(x;\delta,H)=(\theta-x)e^{-\kappa\delta}(1-e^{-\kappa H}).
\]

The fractional-step interpolation assumes the continuous-time embedding. The
five-minute closing-price fit and minute-open holding outcome have different
measurement clocks. That transfer is a model assumption to evaluate.

The proposed primary policy enters long only if the fitted model is admissible,
x is below the estimated equilibrium, and g(x;1,30) exceeds
log(1+c)-log(1-c), with primary per-side cost c = 0.0001. Otherwise it remains in
cash. This is a conditional-mean log-growth rule, not a prediction interval or
an optimal-stopping solution. Residual uncertainty and parameter stability are
reported separately. A simple Z-score threshold does not determine the decision.

Report whole-session resampling uncertainty for phi, equilibrium, half-life and
the expected move. Retain counts of singular or inadmissible resamples rather
than silently dropping them or clipping their parameters. Any interval calculated
only on admissible refits must be labelled conditional. The formal registration
must fix resampling counts, dependence treatment, seeds and undefined-replicate
handling before historical fits. No bootstrap-based entry filter is selected
after inspecting historical results.

## Comparisons and interpretation

Use the same date sequence, decision/exit clocks and costs for:

1. The fitted reversion candidate.
2. A sign-only contrarian control: long when the current opening residual is
   negative, without estimated dynamics.
3. A no-reversion drift control: estimate a five-minute increment mean on the
   same historical pairs and enter only when its thirty-minute expected log move
   clears the same primary cost hurdle.
4. Always long over the same delayed holding window.
5. Cash.

The four market policies each retain the fixed five cost rates
0, 0.5, 1, 2 and 5 bp per side and two execution delays. Signal selection remains
fixed at the primary cost/delay; recosting does not retune thresholds.
Use the existing return, concentration and paired resampling implementations.
Keep gross information, exposure, cost savings and realized net outcomes distinct.

Evaluate forward log-move forecasts against the no-reversion control on a common
availability mask, with squared-error and absolute-error diagnostics. Report
coverage and model failures across the full date sequence. Conditional forecast
comparisons cannot establish superiority on dates where the model is unavailable.

Proposed advancement screens require at least 100 active dates, positive primary
gross and net means, positive primary net means in both years, positive fixed
delay/cost stresses, and winner-concentration checks consistent with the existing
intraday studies. Require positive paired net increments over the sign-only and
drift controls, with nonnegative gross increments. The primary net mean and both
paired net increments require positive lower 95% bounds under both five- and
ten-session moving blocks. Undefined comparisons block the corresponding claim.
Registration must state the exact screens and seeds before scoring.

The study remains complete if its registered screens fail. No profitable
finding, claim of stationarity or later-period evaluation follows from input
availability or synthetic estimator agreement.

## Current feasibility boundary

The preflight implements the causal measurement and unrestricted estimator,
checks constructed mean-reverting, unit-root, explosive and alternating controls,
and audits timestamps/positive opening volume in the verified frozen dataset.
It does not transform historical prices into residuals, estimate historical
parameters, calculate forecasts, generate historical trades or score returns.
Future reference clocks are checked only as ex-post data coverage, never as
inputs to the causal decision.

Remaining prerequisites before scoring are the final protocol/settings
registration, resampling and instability conventions, a tested prior-only
history container, decision/replay implementation and all control accounts.
Finite-sample simulation and observation-noise stress must test estimator bias
and interval behavior; the exact constructed fits are algebraic controls only.

## Methodological references and exposure

- Avellaneda and Lee, author manuscript dated 15 June 2009,
  [Statistical Arbitrage in the U.S. Equities Market](https://math.nyu.edu/inmemoriam/avellaneda/AvellanedaLeeStatArb20090616.pdf),
  section 3 and Appendix A. The inspected residual/AR(1)-to-OU construction
  motivates explicit model assumptions. Its factor-residual, multi-asset
  strategy and performance results are not reproduced by this single-ETF design.
- Holy and Tomanova,
  [Estimation of Ornstein-Uhlenbeck Process Using Ultra-High-Frequency Data](https://arxiv.org/html/1811.09312v4),
  version 4 dated 19 October 2025, estimator/noise discussion. Observation noise
  can distort apparent reversion. This preflight does not implement that paper's
  noise-robust estimator or import its empirical results.

The literature was consulted after the earlier SPY development findings.
The new design is selection-exposed; neither a new identifier nor later
publication dates create an untouched historical test.
