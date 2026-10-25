# Synthetic reversion validation findings

SPY-MR-01 sampling/noise diagnostic, 23 October 2026. The
[prescribed design](reversion_validation.md) was fixed before the first run.
Thirteen cases each contain 1,000 point trials; the first 200 trials additionally
use 199 whole-session refits at each of blocks one and five: 1,034,800 refits.
No historical dataset is loaded, model fitted or strategy scored.

## Point estimates and latent-slope coverage

Bias and RMSE refer to the declared latent slope, not the observed population
regression coefficient. Coverage entries are covered trials out of 200 available
slope ranges. The expected nominal percentile coverage is 95%; it is not
established by these results.

| Case | Numerically OU-admissible / 1,000 | Slope bias | Slope RMSE | Coverage, block 1 | Coverage, block 5 |
| --- | ---: | ---: | ---: | ---: | ---: |
| fast_stationary | 1,000 | -0.005134 | 0.038583 | 178 | 151 |
| slow_stationary | 1,000 | -0.005203 | 0.021793 | 174 | 154 |
| near_unit_stationary | 992 | -0.002310 | 0.010227 | 169 | 148 |
| unit_root | 618 | -0.005043 | 0.015398 | 176 | 148 |
| explosive | 116 | -0.004179 | 0.013317 | 165 | 152 |
| alternating | 0 | 0.002235 | 0.039760 | 186 | 165 |
| short_history | 1,000 | -0.023278 | 0.051631 | 145 | 0 |
| long_history | 1,000 | -0.001847 | 0.011854 | 180 | 174 |
| opening_transient | 1,000 | -0.006151 | 0.023857 | 176 | 151 |
| observation_noise | 1,000 | -0.155130 | 0.160563 | 0 | 1 |
| noisy_unit_root | 1,000 | -0.090014 | 0.096186 | 5 | 3 |
| anchor_noise | 1,000 | 0.080524 | 0.080897 | 0 | 1 |
| dependent_sessions | 1,000 | -0.024533 | 0.044252 | 114 | 109 |

Unit-root and explosive trials classified as admissible are false positives for
a latent mean-reversion interpretation. All noisy unit-root trials appear
admissible. The common-anchor perturbation shifts the population observed slope
upward; independent observation error shifts it downward. The ordinary OLS fit
cannot identify latent reversion from those observations without further
assumptions. More sessions reduce clean-case sampling error but do not remove
measurement misspecification.

Even clean twenty-session cases under-cover: slow-stationary slope coverage is
87% at block one and 77% at block five, with binomial Monte Carlo SE about 2.38
and 2.98 percentage points. With five source sessions and block five, every
resample repeats the original ordered sample, giving a degenerate range.
The dependence stress further reduces coverage. No resampling convention is
selected from this comparison to obtain a preferred historical conclusion.

The generated report also retains equilibrium, half-life and expected-move
coverage, their available denominators and Monte Carlo SE. OU truths are
undefined for unit-root, explosive and alternating cases, even when individual
estimated fits appear admissible. Derived ranges condition on admissible
refits, with all excluded status counts retained; they cannot be interpreted as
unconditional confidence intervals. Parameter ranges for historical evaluation
are therefore descriptive and cannot establish stationarity, identify a latent
OU process or filter trading entries.

## Reproduction and interpretation

Build and run `reversion_validation` without credentials or private data. It
writes case settings/seeds, every point fit, coverage-trial range records,
first-trial observations for all thirteen cases, source/build identities and
artifact hashes. Case trial files are separate to remain within the existing
bounded artifact reader. A failed output write stops the run and does not publish
a summary as complete.

`python research/verify_reversion_validation.py <report-directory>` independently
recomputes the thirteen first-trial OLS fits and variances using 60-digit Decimal
arithmetic. It also checks every point-fit aggregate, coverage denominator,
refit-status total and artifact identity. This verifies numerical/report
consistency; it does not establish an adequate latent model or a valid bootstrap.
The exact local verification run and native status are recorded in the
[milestone ledger](../docs/verification.md).

These failures limit the claims of the registered historical comparison. They
do not authorize a noise-robust replacement model, a new strategy search or later
performance access. The candidate can still be evaluated as a specified
observed-residual forecasting/decision rule against its controls, with negative
findings retained as a completed experiment.
