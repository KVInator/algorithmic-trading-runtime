# Stability, concentration and risk matching

M7 retains the accepted primary diagnostics of authenticated M6 development
exports and adds fourteen registered one-factor sensitivity cases and funded
risk-matched ledgers. The extension implementation requires its own Windows
historical acceptance. It does not select replacement parameters, advance a
model or open 2024–2025 strategy inputs.

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
On a shortened exchange session, M6 records no intraday policy paths. The loader
checks the exchange calendar and requires both the primary policy list and the
cost/delay path list to be empty, with operational masks false. All eight intraday
observations remain unavailable on that retained date. A regular session still
requires all eight primary paths; an absent path is not an accepted exclusion.
Daily-risk measurements and continuous-account records are read independently.
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
coverage. This primary export does not execute risk-matched economic comparisons;
its report makes no advancement decision.

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
360-date numerical fixtures, including constant and unavailable endpoints. Shortened-session conversion and
regular-session cash/missing-policy cases have dedicated native and independent
calendar regressions.
Six malformed-source cases were rejected without modifying their inputs. The
existing five cost/delay artifacts and seven continuous-account artifacts remained
byte-identical; their independent checks, the panel replay and the 259-check
offline demonstration also passed. Static report pages were rendered and reviewed.
The Windows development run and independent derived-bundle review are accepted
below. The constructed checks remain separate from historical findings and
controlled inference calibration.

## Windows development acceptance

The 29 October 2026 MSVC 19.51.36260 x64 Release run `stability_1` completed
77 primary series on 1,760 development dates, with 1,106 descriptive groups and
six registered endpoints. The Windows verifier authenticated both saved M6
sources and reproduced all four 10,000-draw block analyses. The source summaries
match the accepted M6 cost/delay and continuous-account identities. The uploaded
derived bundle independently passed `--review-only --self-check`, including
causal labels, pooling, concentration, inference and static plot checks; all eight
rendered report pages were inspected. That local review did not reopen the
Windows M6 files or reauthenticate the provider archive.

Accepted `summary.json` SHA-256:
`b1664fb97d8f541bfb8395eb6b58c00d40e1438d178ae367ec231e1d9fe75874`.

The [endpoint table](evidence/m7_stability_endpoints.csv) retains all 24
endpoint/block rows in their original numerical units. The [primary descriptive
table](evidence/m7_stability_primary.csv) retains the twelve equal-weight research
indices and five shared accounts, including concentration diagnostics. Every row
references the accepted summary identity. Reproduce the source exports with the
[runbook](running.md#primary-stability-and-uncertainty-m7), then use the verifier
before comparing these tables.

The primary simultaneous block is 40; block 20 is the registered sensitivity.
Both numerical families are available, with no undefined resamples, but the
complete family's calendar coverage is inadequate. The following are provisional
development bounds, pending controlled calibration and risk matching:

| Endpoint | Valid dates / 1,760 | Observed increment | Block-40 simultaneous lower bound | Material increment |
| --- | --- | --- | --- | --- |
| Momentum minus same-window always-long | 1,682 | +1.368720 bp/day | +0.569945 bp/day | +0.5 bp/day |
| Volume continuation minus price-only | 1,682 | +0.241233 bp/day | -0.287867 bp/day | +0.5 bp/day |
| Reversion minus sign-only | 1,682 | +0.616468 bp/day | +0.225313 bp/day | +0.5 bp/day |
| Reversion minus drift | 1,682 | +0.003075 bp/day | -0.301498 bp/day | +0.5 bp/day |
| Intraday-risk minus daily-only annual CER | 655 | +0.364677 percentage points/year | -0.211072 percentage points/year | +0.5 percentage points/year |
| Control-minus-candidate QLIKE | 479 | +0.023751 loss units | +0.000465 loss units | +0.01 loss units |

Only momentum's numerical lower bound clears its material increment for both
simultaneous blocks. This is a relative comparison: the momentum research index's
own mean is **-1.062061 bp/day**, versus **-2.430781 bp/day** for its always-long
control. Its mean is negative in every development year. Mean session-time
exposure also differs, so the pending risk-matched replay remains necessary.
These results do not establish profitable performance or authorize advancement.

The volume-continuation research index has a positive mean of +0.277001 bp/day,
but zeroing its five largest winning-date contributions reduces that mean to
-0.309719 bp/day. This is arithmetic concentration attribution, not a funded
counterfactual replay. The shared candidate has only 65 observed dates; it does
not supply a seven-year portfolio outcome. The risk CER and QLIKE endpoints have
37.22% and 27.22% calendar coverage, respectively, below the registered 80%
requirement. Stopped tails remain unavailable.

No 2024 or 2025 strategy inputs were opened. Fourteen secondary cases,
risk-matched ledgers and later controlled inference calibration remain open.
The primary diagnostic checkpoint is accepted; M7 is not complete.

## Registered sensitivity cases

`stability_extension_development` loads each original pre-2024 panel session once
and advances fourteen independent parameter states. It composes the existing
strategy and accounting engines; the accepted primary defaults remain unchanged.
Each case uses one-minute reference delay and one basis point per side, with no
crossing of secondary parameters and no best-case selection.

| Family | One-factor alternatives | Primary setting retained elsewhere |
| --- | --- | --- |
| Momentum | Hold 10 or 20 minutes | 25 minutes |
| Continuation | 10 or 40 complete prior regular sessions | 20 sessions |
| Reversion | History 10/40 regular slots, observation grid 1/15 minutes, hold 15/60 minutes | 20 slots, 5-minute grid, 30-minute hold |
| Daily risk | History 10/40 daily measurements, realized-variance predictor grid 1/15 minutes | 20 measurements, 5-minute predictor |

Reversion keeps its first-30-minute volume-weighted closing-price anchor and
minute-150 decision. Residual closes occur at minute indices 29, 29+g, ..., 149.
The sampled close is the closing value of the completed source interval; no
interior OHLCV bar or missing endpoint is manufactured. The physical AR(1) speed
is `-log(b)/g` per minute. The delayed expected change uses the variant's physical
holding period. Its drift control has that same horizon and grid; controls do
not retain a different holding window. A change in the daily-risk predictor grid
does not replace the future RV5 target or the primary QLIKE diagnostic.

All five instruments and every development calendar date remain represented,
including initialization, unavailable histories, shortened sessions and stopped
accounts. Intraday secondary outcome statistics retain the original all-five
operational mask. Continuous risk accounts retain their own observed prefixes.
There are 200 instrument/policy cases and 40 equal-weight views: 240 secondary
series. Equal-weight views are research indices on all-five observed dates, not
an additional globally funded portfolio.

## Funded prior-risk matching

The four primary intraday candidate/control pairs and the intraday-risk versus
daily-only pair each have a genuine unit ledger for estimating prior volatility.
Shared controls are reused: seven intraday and two risk policies across five
instruments give 45 unit accounts. Each pair then has two independently funded
accounts per instrument, giving 50 matched accounts and ten pooled views.

For each pair and session t, retain the last 252 exchange-calendar slots of net
unit-account returns. Let their annual sample volatilities be sigma_A,t and
sigma_B,t. All 252 pair slots must be observed. The registered common target and
multipliers are

$$
 v_t = \min(0.10,\sigma_{A,t},\sigma_{B,t}), \qquad
 m_{j,t}=v_t/\sigma_{j,t}, \quad j\in\{A,B\}, \quad 0<m_{j,t}\le1.
$$

The sample variance uses denominator 251 and annualization factor 252. Only
strictly earlier returns enter sizing. A zero or missing volatility makes the
multiplier unavailable. Missing slots are retained; older observations do not
replace them. Unit returns include the primary cost. They are not zero-filled
forecast placeholders: momentum starts after its first observed feature,
continuation after twenty complete prior regular sessions, reversion after twenty
prior regular slots, and risk accounts after their first deployable forecast.
Subsequent genuine flat model decisions are observed cash. A shortened session
is known cash only for a started, proved-flat intraday account; its original
primary intraday comparison outcome remains unavailable. Diagnostic exclusions
retain a missing training slot.

Intraday entry targets use m_j,t, with scheduled exits to zero. Risk targets use
m_j,t times the original daily-risk weight. Both replay paid costs and the
self-financing cash/share ledger. Multiplying an already realized return by the
multiplier would miss fees, equity changes, action entitlements and stopped
positions, and is not used. Intraday exposure is allocated fraction times holding
minutes divided by 390; it is a time-allocation proxy, not a marked exposure
integral. Risk exposure is closing liquid stock fraction. Costs and turnover are
reported relative to initial account capital.

If an execution endpoint is missing, completed fills and charges remain in the
ledger and the account stops with its retained position. There is no invented
liquidation, cash return, reset or restart. A required overlay missing after an
intraday account starts likewise stops that account on a regular date. Before
initialization the account waits. Risk accounts reuse the existing event,
dividend-entitlement, payment, mark and terminal-liquidation rules.

The combined derived matrix has 345 series: 240 secondary, 45 unit, 50 matched
and ten matched pooled views. All have year, prior-only condition, concentration,
exposure, turnover, drawdown and coverage diagnostics; intraday groups also retain
holding-time attribution.

## Matched uncertainty and acceptance boundary

The matched family uses the six registered endpoint definitions and material
increments, the same full-calendar joint block draws and the same coverage
requirements. Intraday increments require both matched returns and the original
common operational mask. Risk CER requires both observed funded returns. The
sixth endpoint carries the unchanged primary variance forecasts and future RV5
losses: changing a capital allocation does not change a variance forecast.
This is a secondary diagnostic family and does not replace the accepted primary
family or select different hypotheses. Its provisional bounds do not authorize
advancement. Controlled inference calibration remains M8.

The native constructed run uses 600 analytic exchange sessions, long enough to
exercise both the original risk initialization and a later 252-return sizing
window. It includes missing closes, residuals and executions, shortened sessions,
and a dividend. This is synthetic evidence, not an expanded historical result.
`verify_stability_extension.py` independently reconstructs raw-source features and
every variant decision, checks funded cash arithmetic with Decimal, rebuilds
prior sizing, attribution and all four inference blocks, and checks static SVG
coordinates. Its corruption checks bypass artifact identities.

Full verification reauthenticates only the pre-2024 registered archive. A
provider review bundle includes derived observations and diagnostics, not raw
market source or the detailed instrument replay files. `--review-only` verifies
that derived mathematics and display; it does not authenticate the source
ledgers or provider archive. Commands are in the [runbook](running.md#secondary-stability-and-risk-matching-m7).

M7 remains in progress until the Windows constructed and historical runs and
independent verification have been reviewed with the accepted primary outputs.
M8 then performs controlled inference validation and 2024 evaluation; M9 performs
the 2025 retrospective evaluation; M10 consolidates the publication. Reserved
strategy observations remain closed at this checkpoint.


## Constructed extension verification

GNU 13.3.0 Release builds all 28 applications. The 600-session constructed
extension has 348 development dates, 345 derived series and 3,148 descriptive
groups. Fourteen native numerical/failure checks and fourteen independent
numerical/semantic checks pass, including direct ledger corruptions without
artifact-hash protection. Source observations are reconstructed analytically;
this is not provider-archive authentication or historical economic evidence.
The seven-page static report has been rendered and visually reviewed.

The existing constructed M6 cost/delay artifacts (five files) and continuous
account artifacts (seven files) retain their bytes. The accepted primary M7
groups and joint inference also agree exactly. The existing project demo,
reversion sampling validation, panel replay and their independent checks pass.
MSVC execution and historical extension acceptance remain to be completed on
Windows before closing M7.
