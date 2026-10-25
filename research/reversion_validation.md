# Reversion sampling and observation-noise experiment

SPY-MR-01 synthetic specification, fixed 23 October 2026 before the first
Monte Carlo run. No historical model fit or strategy score is authorized here.
The original candidate brief remains a dated proposal.

## Prescribed experiment

The authoritative `ReversionValidationStudy` runs 1,000 independent trials in
each of thirteen cases. The first 200 trials per case additionally use 199
whole-session refits at each block length, 1 and 5. Each session has 25
observations, 24 within-session transitions and no cross-session transition.
The latent innovation SD is 0.001 and equilibrium is 0.001. The expected-move
query fixes current residual -0.004, entry delay one minute and holding time
thirty minutes. These states are synthetic residuals, not calibrated SPY prices.

| Case, in seed order | Latent slope | Sessions | Observation SD / innovation SD | Anchor SD / innovation SD | Across-session shock correlation | Initial state |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| fast_stationary | 0.5 | 20 | 0 | 0 | 0 | Stationary |
| slow_stationary | 0.9 | 20 | 0 | 0 | 0 | Stationary |
| near_unit_stationary | 0.98 | 20 | 0 | 0 | 0 | Stationary |
| unit_root | 1 | 20 | 0 | 0 | 0 | Zero |
| explosive | 1.02 | 20 | 0 | 0 | 0 | Zero |
| alternating | -0.5 | 20 | 0 | 0 | 0 | Stationary |
| short_history | 0.9 | 5 | 0 | 0 | 0 | Stationary |
| long_history | 0.9 | 60 | 0 | 0 | 0 | Stationary |
| opening_transient | 0.9 | 20 | 0 | 0 | 0 | Zero |
| observation_noise | 0.9 | 20 | 1 | 0 | 0 | Stationary |
| noisy_unit_root | 1 | 20 | 1 | 0 | 0 | Zero |
| anchor_noise | 0.9 | 20 | 0 | 5 | 0 | Stationary |
| dependent_sessions | 0.9 | 20 | 0 | 0 | 0.75 | Stationary |

Latent transitions are `X[k+1] = theta*(1-phi) + phi*X[k] + sigma*Z[k+1]`.
Stationary initial states are `theta + sigma*Z[0]/sqrt(1-phi^2)`.
For the dependence case, each of the 25 Gaussian shocks follows an independent
stationary AR(1) across sessions. That dependence resets between trials.
Observed values are `Y[d,k] = X[d,k] + e[d,k] - a[d]`, with independent Gaussian
observation errors e and a session-common Gaussian anchor error a. This anchor
stress does not reproduce the opening anchor's price-dependent construction.

For stationary initialization, the population pooled observed slope is
`(phi*V + Var(a))/(V + Var(e) + Var(a))`, where
`V = sigma^2/(1-phi^2)`. It is unavailable for transient/nonstationary cases.
Finite-sample bias and RMSE are measured against the latent slope; the population
observed slope distinguishes measurement misspecification from sampling bias.
Unit-root and explosive controls report false numerical OU admissibility.

## Randomness, ranges and failure accounting

Base seed 20261023. Case seed is base plus zero-based case index times 1000003.
Bootstrap seed is case seed plus 500000003 plus zero-based trial index, with the
same seed at both block lengths. The generator is `mt19937_64`; each normal uses
two open-unit high-52-bit midpoint uniforms and the cosine Box-Muller transform,
without cached values. A zero noise multiplier still consumes its variate.
Portable integer sampling uses the existing unsigned rejection/modulo sampler.
Transcendental functions may differ by floating-point roundoff across compilers;
bitwise cross-platform identity is not claimed.

Whole ordered sessions are sampled with replacement through non-circular blocks;
concatenated blocks are truncated to the original session count. The same
unrestricted pooled estimator fits every replicate. All seven fit-status counts
are retained. A slope range requires coefficients from every replicate; any
missing coefficient makes it unavailable. Equilibrium, half-life and expected
move ranges require at least twenty admissible refits, and are explicitly
conditional on that admissibility. Missing or invalid refits are never clipped,
repaired or silently discarded from the reported probability mass.

Ranges use the shared linear-interpolated 2.5/97.5 percentiles. Coverage records
both available and covered trials, the full number of coverage trials and
binomial Monte Carlo SE conditional on availability. OU truths exist only for
positive sub-unit latent slopes. Coverage under misspecification deliberately
tests the latent target, rather than redefining truth to the observed fit.
The short-history/block-five combination is a deliberate degenerate-resample
control. Synthetic coverage is evidence to inspect, not a pass/fail optimization
objective or a guarantee of nominal historical coverage.

The model's positive sub-unit coefficient is an admissibility condition, not a
stationarity test. Derived conditional ranges remain descriptive. They do not
select historical entry thresholds. Input corruption or nonfinite arithmetic
stops a run; an economically unsupported but numerically valid model produces
an explicit fit status. Historical cash/failure rules require the separate final
protocol. No noise-robust latent estimator is claimed by this OLS implementation.

## Methodological source

Holy and Tomanova, [Estimation of Ornstein-Uhlenbeck Process Using
Ultra-High-Frequency Data](https://arxiv.org/html/1811.09312v4), version 4,
19 October 2025, theoretical model/noise discussion: additive observation noise
changes the sampled OU observation structure. This diagnostic implements its
own declared stress cases and does not reproduce that paper's estimator or
empirical trading results.
