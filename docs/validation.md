# Integrated validation and executable freeze

M8 follows the accepted M0-M7 work. The build pins
[validation_protocol.json](../research/validation_protocol.json) to SHA-256
`587ff9244737e15d61ab91c4dff4a19bfd9cbbe546788e742cc684388e251b54`.
It retains the original extension protocol, strategies, fourteen secondary cases,
material increments, costs and delays. Provider 2024 evaluation and its final
executable freeze require independent verification; 2025 remains reserved for M9.

## Controlled inference calibration

The original six-endpoint `FullCalendarInference` kernel is reused. Its fixed
original-sample calendar-indexed influence-function HAC studentizer uses Bartlett
weights through lag `block-1`. Joint non-circular moving-block resampling retains
calendar gaps. Simultaneous one-sided lower bounds are

$$L_j=\widehat\theta_j-c_{0.95}\widehat s_j.$$

The complete family is retained when an endpoint is unavailable. Coverage still
requires 200 observations and 80% of calendar slots. Blocks 20 and 40, 10,000
resamples and seed 290026 are unchanged. Caching stores immutable draw indices
only; each outer trial estimates its own moments, covariance and bootstrap law.

Nine cases combine daily noise of 5, 10 or 20 bp with stationary Gaussian AR(1)
coefficient 0, 0.3 or 0.7. A tenth uses 10 bp, coefficient 0.3 and fixed missing
slots 4, 14, ..., 244 in a 252-session calendar. It retains 227 observations.
There are 1,000 independent-seed outer trials per case and two block lengths,
for 20,000 complete-family experiments. Short `--check` mode uses four outer
trials per case and cannot authorize provider validation.

Eight latent processes start at independent stationary standard normals:

$$X_{k,t}=\phi X_{k,t-1}+\sqrt{1-\phi^2}\epsilon_{k,t}.$$

MT19937-64 uses the existing high-52-bit midpoint, cosine Box-Muller mapping,
without cached normal variates. Let $Z_{j,t}=(X_{0,t}+X_{j+1,t})/\sqrt2$ and
$B_t=0.5\sigma X_{7,t}$. The first four candidates are
$A_{j,t}=B_t+\delta_j+\sigma Z_{j,t}$. For annual CER with risk aversion 3,

$$\operatorname{CER}(A)-\operatorname{CER}(B)=252\{E[A-B]-1.5[\operatorname{Var}(A)-\operatorname{Var}(B)]\}.$$

Its candidate mean shift is $\delta_j/252+1.5\sigma^2$, so the known population
increment equals the material boundary. Population and finite-sample variances
remain distinct under serial dependence. The sixth controlled loss-difference
score is $\delta_j+20\sigma Z_{j,t}$ against zero; it is not an asserted model
for individual market QLIKE losses.

The diagnostic counts any simultaneous lower bound above its true material
boundary. It reports 95% Wilson Monte Carlo intervals and a one-sided binomial
size-excess test under 0.05, with cutoff `0.05/20` across case/block cells. These
are intervals for simulation frequencies, not market returns. Undefined families
are retained separately. No market-wide coverage certification follows from a
pass under these declared Gaussian cases.

Prespecified alternatives use the Bonferroni 80%-power planning shift based on
the exact stationary AR(1) variance of the full-calendar mean. CER shifts scale
by 252 and QLIKE shifts by 20. Native checks compare location equivariance with
actual shifted-family replays. The masked case keeps the same planning shift.
Detection rates do not establish power for trading strategies, especially if
size control fails.

A size-excess finding is retained. It does not trigger a different critical
value, block, endpoint or data filter. Descriptive 2024 validation can proceed,
with supported inference claims withheld and the warning carried into the
executable freeze. Heavy tails, heteroskedasticity and outcome-dependent missingness
remain outside this controlled diagnostic's claim scope.

## Verification and remaining M8 work

`verify_inference_calibration.py` independently reconstructs every outer sample,
estimate, HAC error, bound and aggregate. It fully reruns the first and last
outer trial per case/block: 40 complete bootstrap reference replays. The other
native critical values are authenticated outputs; their full bootstrap laws are
not independently rerun. Static coordinates and detection tables are checked,
and semantic corruption tests bypass artifact hashes. The exported standalone
SVG packages verified native coordinates rather than a second calculation.

The next M8 component composes existing reference and account engines under an
explicit 2024 capability. Features warm from 2016; fresh evaluation capital is
separate from stopped development accounts. Unit references start exactly 252
exchange sessions before the evaluation boundary and never restart after a
stop. All fourteen variants remain present; no best variant is selected. The
last forecast is unavailable if it would need a 2025 target price.

A verified provider 2024 run must freeze the exact executable, source inventory,
input registration, policy, calibration and validation identities before an
explicit M9 final run. Constructed verification does not constitute private
Windows historical acceptance. M9 final evaluation and M10 publication follow.
