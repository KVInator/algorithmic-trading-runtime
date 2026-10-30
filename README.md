# Algorithmic Trading Research and Execution

C++20 research and execution framework for prior-only intraday strategies,
volatility-based allocation, shared-capital accounting and synthetic broker
recovery across **SPY, QQQ, IWM, TLT and GLD**.

## Overview

This project studies whether simple intraday signals retain an economically
meaningful advantage once their information sets, transaction costs, execution
delays and market exposure are made explicit. It also implements the account
and order machinery needed to translate those decisions into a recoverable
execution process.

The research combines late-session momentum, volume-conditioned continuation
and opening-anchor mean reversion, with a separate daily-risk allocation study.
The registered archive spans **2016-2025**. Development is followed by a
chronological 2024 validation and a frozen 2025 final retrospective evaluation.

The C++ implementation owns signal estimation, financial accounts, order
transitions, matching and journal recovery. Historical market-data acquisition,
independent broker simulation and recorded-result publication have separate
responsibilities. Python is limited in the active tree to build orchestration,
issuer-data preparation and publication utilities.

The completed evaluation retains both positive and negative findings. In 2025,
volume-conditioned continuation and reversion have small positive pooled index
means, but both depend on their largest winning date. The primary shared account
finishes below its starting capital, and the registered uncertainty and
risk-matching results do not support strategy advancement.

The [technical report](docs/report.pdf) contains the full mathematics,
experimental conventions and results. Its [standalone HTML version](docs/report.html)
embeds saved synthetic event views and opens without a server or JavaScript.
The original [SPY release](https://github.com/KVInator/algorithmic-trading-runtime/releases/tag/v1.0.0)
retains its separate 2022-2023 sample and findings.

## Contents

- [Visual Summary](#visual-summary)
- [Research Questions](#research-questions)
- [Dataset and Study Design](#dataset-and-study-design)
- [Pipeline Design](#pipeline-design)
- [Strategy Models](#strategy-models)
- [Risk and Exposure Matching](#risk-and-exposure-matching)
- [Shared Capital and Broker Lifecycle](#shared-capital-and-broker-lifecycle)
- [Key Results](#key-results)
- [Statistical Interpretation](#statistical-interpretation)
- [Runtime Measurements](#runtime-measurements)
- [Implementation and Reproduction](#implementation-and-reproduction)
- [Repository Structure](#repository-structure)
- [References](#references)

## Visual Summary

### Development, Validation and Final Evaluation

![Five-instrument research results and final coverage](docs/figures/panel_results.svg)

Equal-weight research-index means at 1 bp per side and one-minute delay, with
final primary paired-date coverage. Each period retains its actual observation
mask. These indices are separate from continuously funded portfolio wealth.

### Final Incremental Effects and Material Thresholds

![Final point estimates and numerical lower bounds](docs/figures/panel_bounds.svg)

Dots show estimates and left ticks show block-40 numerical lower bounds. Gold
lines mark the registered material increments. The panels have different units;
calibration size failures limit their confidence interpretation.

### Shared-Capital Outcomes and Market Exposure

![Shared-account returns and exposure](docs/figures/panel_accounts.svg)

The candidate and four control accounts each start with USD 100,000 and complete
250 evaluation dates in 2025. Their different market exposures remain visible.

### Saved Synthetic Order and Account States

![Saved broker lifecycle and account transitions](docs/figures/broker_lifecycle.svg)

The original broker demonstration records order, cash and reservation states.
The expanded study additionally tests an independently journaled native child
broker. Both use constructed events rather than observed external-broker fills.

## Research Questions

The project separates four questions that require different evidence:

1. **Directional information:** do the intraday rules improve outcomes relative
   to controls using comparable decision and holding conventions?
2. **Risk allocation:** does intraday-aware risk estimation improve forecasting
   loss or allocation value over a daily-only reference?
3. **Economic robustness:** do differences survive costs, delayed references,
   concentration diagnostics and predefined exposure matching?
4. **Execution correctness:** can concurrent strategy ownership, partial fills,
   cancellations and interrupted delivery be reconciled without changing balances
   or applying an execution twice?

A valid recovery trace does not establish a profitable signal. A positive
research-index mean does not establish a successful funded portfolio.

## Dataset and Study Design

| Component | Specification |
| --- | --- |
| Instruments | SPY, QQQ, IWM, TLT and GLD |
| Market observations | Historical minute bars, exchange calendars and corporate-action evidence |
| Acquisition | Retained Alpaca historical responses with pagination, checkpoints and immutable identities |
| Archive horizon | 2016-2025; 2,514 exchange sessions |
| Initialization | 2016 and subsequent available history before each decision |
| Development | 2017-2023; 1,760 calendar dates |
| Validation | 2024; 252 evaluation dates |
| Final evaluation | 2025; 250 evaluation dates |
| Historical cost scenarios | 0, 0.5, 1, 2 and 5 bp per side |
| Execution-reference delays | One and two minutes |

The universe is selected and includes correlated equity exposures. Earlier GLD
research and inspected SPY outcomes mean the archive is not globally unseen.
The chronological reservation controls selection using this extension's own
later-period outcomes; it does not erase prior market or literature exposure.

### Observation Availability and Corporate Actions

The original complete-minute intersection fails its development-year coverage
gate. A separately registered input amendment distinguishes required operational
execution slots from feature availability. The parent failure remains recorded.

Three missing SPY/IWM/GLD closes on 12 August 2019 remain unavailable after
bounded recovery. Daily histories reset across the gap. Required missing prices
stop the affected capital path; later recovery of model history does not restart
that account or fill its missing tail.

Canonical cash events retain issuer evidence, ex-dates and payment dates.
Duplicate announcements are not summed. Split-absence and GLD cash-absence
cross-checks retain their stated completeness assumptions. The
[study design](docs/extension_plan.md) records source amendments, masks,
prior exposure and the original-SPY provider-vintage comparison.

## Pipeline Design

### 1. Authenticate and Register Inputs

Raw responses, calendars, action references and eligibility masks are bound to a
registered study manifest. Missing prerequisites stop registration. An updated
provider vintage cannot silently overwrite an accepted snapshot.

### 2. Construct Causal Decisions

Signals use completed observations. Rolling estimates use strictly earlier
sessions, while execution references occur after the decision timestamp.
Unavailable features and unsupported model fits remain dated outcomes.

### 3. Replay Policies and Controls

The same primary decisions feed the fixed cost/delay grid. Control policies and
fourteen one-factor history, observation-grid and holding-period variations
remain in the comparison. No alternative is selected from final results.

### 4. Maintain Funded Accounts

Fractional research accounts retain cash, shares and corporate-action claims.
Whole-share execution accounts additionally maintain reservations, explicit
strategy ownership, target netting and aggregate admission limits.

### 5. Evaluate Stability and Uncertainty

Year, instrument, prior-only market-condition and holding-period views accompany
concentration, turnover, drawdown, exposure and costs. Joint calendar resampling
retains paired comparisons and unavailable observations. Controlled synthetic
experiments assess the inference procedure separately.

### 6. Freeze and Evaluate the Final Period

Verified 2024 results authorize a copied executable and specification freeze
before 2025 access. Final capital starts fresh while model histories remain
warmed from earlier observations. The accepted replacement runner reproduces
all 26 financial artifact identities and four 2024 scientific summaries before
its final evaluation. The [acceptance record](docs/validation.md) retains both
freeze identities and the payment-date implementation correction.

## Strategy Models

### Late-Session Momentum

The late-session rule observes the completed move from the opening reference
and enters long only after a positive signal. Its comparison includes a
same-window always-long control, so avoiding market exposure can be distinguished
from earning an incremental return over that exposure.

### Volume-Conditioned Continuation

Continuation compares the current opening-relative move with the mean absolute
same-clock move over twenty earlier sessions:

```math
s_{d,m}=\frac{1}{20}\sum_{j=d-20}^{d-1}|u_{j,m}|.
```

The volume-conditioned variant requires cumulative volume above its prior
same-clock median for fresh entries. Volume does not determine exits. The
price-only continuation policy retains the directional reference without this
additional entry condition.

### Opening-Anchor Mean Reversion

Five-minute log residuals around a fixed opening anchor follow the estimated
relation

```math
X_{k+1}=a+\phi X_k+\epsilon_{k+1}.
```

For an admissible positive sub-unit slope, the corresponding OU parameters and
expected delayed holding-period move are

```math
\theta=\frac{a}{1-\phi},\qquad
\kappa=-\frac{\log\phi}{5},\qquad
 g(x;\delta,H)=(\theta-x)e^{-\kappa\delta}(1-e^{-\kappa H}).
```

The candidate compares this expected move with the fixed cost hurdle. Sign-only,
drift and always-long controls separate the fitted reversion effect from simpler
entry rules and market exposure. Numerical OU admissibility does not establish
stationarity or identify a latent diffusion.

The constructed finite-sample and observation-noise study examines that
identification problem directly:

![Finite-sample and observation-noise diagnostics](docs/figures/reversion_validation.svg)

### Transaction Costs and Execution References

For a round trip with entry and exit references $P_e,P_x$ and one-way charge
$c=k/10^4$, the net return is

```math
R(c)=\frac{P_x(1-c)}{P_e(1+c)}-1.
```

The historical comparison retains **400 instrument/policy/cost/delay series**
and **240 paired contrasts**. These charges are explicit hypothetical scenarios,
not measured all-in spreads or broker fees.

A separate fixed historical SIP quote sample contains 910 windows, of which
232 meet the registered condition and timestamp rules. Its crossing-cost
references do not identify market impact, queue position or synthetic fill
probabilities. Sparse or ambiguous observations remain unestimated.

## Risk and Exposure Matching

The separate daily-risk study compares strictly lagged daily-only and
intraday-aware estimates. Forecasting loss and subsequent account economics are
evaluated separately. For forecast variance $h$ and realized proxy $y$, the
QLIKE convention is

```math
L(h,y)=\frac{y}{h}+\log h.
```

Allocation comparisons include annualized certainty-equivalent return, with
risk-aversion coefficient three:

```math
\mathrm{CER}=252\left(\bar r-\frac{3}{2}s_r^2\right).
```

The predefined exposure comparison uses 252 prior session slots. Candidate and
control scale toward the smaller of their estimated annual volatilities and a
fixed 10% target:

```math
\sigma_{t}^{\ast} = \min\left(0.10,\; \widehat{\sigma}_{c,t-1},\; \widehat{\sigma}_{b,t-1}\right), \qquad w_{j,t} = \frac{\sigma_{t}^{\ast}}{\widehat{\sigma}_{j,t-1}}
```

The implementation retains missing slots and unsupported sizing. Forty-five
unit-reference accounts feed fifty funded matched accounts. This comparison
asks whether an apparent advantage survives a predefined exposure convention;
it does not infer risk-matched alpha from separately masked return means.

## Shared Capital and Broker Lifecycle

One whole-share USD account supports **five inventories and fifteen strategy
owners**. Owner claims, target netting and aggregate limits operate on actual
balances. Daily-risk allocation remains a separate experiment rather than a
fourth directional owner.

The independently journaled synthetic broker runs in a native child process.
Its in-process and separate-process routes share the matching core. The lifecycle
covers:

- DAY, IOC and GTC limit orders with finite quote liquidity;
- acknowledgments, partial fills, fees and cancellation races;
- managed cancel/new replacement and stable execution identities;
- disconnected delivery, interrupted commits and activity-history recovery;
- reconciliation of independent broker and client accounts before routing resumes.

The process study records **313 checks in twelve scenario families across
21 child processes**. A separate execution study retains **680 account cases
and 510 matched contrasts**, with every terminal broker/client account reconciled.
These case counts describe synthetic experimental coverage, not market observations.

`OrderRuntime`, `SharedCapitalBook` and their journal owners maintain exact
integer monetary values, whole shares and reservations. Journal schemas declare
their units, including cents and USD 1e-9. Candidate state is synchronized before
dispatch eligibility is published. Recovery replays the same financial rules;
contradictory snapshots cannot overwrite the ledger.

Anonymous pipes, constructed quote budgets and simulated fills do not establish
external-broker connectivity or real-market execution quality. Historical Alpaca
data acquisition remains available without a paid feed or live broker service.
The [architecture](docs/architecture.md) and [broker contract](docs/process_broker.md)
define ownership, persistence and recovery boundaries.

## Key Results

### Final Research-Index Outcomes

At **1 bp per side and one-minute delay**, each pooled intraday index has
247 observed common dates out of 250 final evaluation sessions:

| Research index | Net mean, bp per observed date | Mean after zeroing its largest winner, bp/date |
| --- | ---: | ---: |
| Momentum | -0.7684 | -1.0026 |
| Volume-conditioned continuation | +0.3157 | -0.7021 |
| Reversion candidate | +0.1042 | -0.0961 |

Winner removal retains the original denominator and does not refit a strategy
or simulate another funded account. Both positive point means become negative
under that diagnostic, demonstrating concentration in the recorded index paths.

### Funded Shared-Capital Outcome

All **200 risk-account cases and fifty shared-capital cases** complete 2025.
For the primary shared candidate:

| Metric | Recorded result |
| --- | ---: |
| Starting capital | USD 100,000 |
| Terminal equity | USD 99,763.25 |
| Funded return | -0.2367% |
| Charged transaction costs | USD 1,759.59 |
| Mean market exposure | 23.9159% |
| Observed-path drawdown | 1.0196% |
| Observed dates | 250 |

All four shared control accounts also complete the year. Their distinct
exposures remain part of interpretation. The positive continuation and reversion
index means therefore do not imply a profitable combined funded account.

Earlier development ledgers and the primary 2024 shared account have stopped
tails. The latter records only 79 observed dates. Complete 2025 paths do not
repair those earlier gaps or retroactively pass the original SPY screens.

## Statistical Interpretation

Paired full-calendar moving-block resampling retains missing slots and uses
joint draws across the six registered endpoints. With HAC standard error
$\widehat s_j$, the numerical lower bound is

```math
L_j=\widehat\theta_j-c_{0.95}\widehat s_j.
```

Blocks **5, 10, 20 and 40**, the complete endpoint family and material increments
remain fixed. Final primary coverage meets the registered count and fraction
requirements for all six endpoints, including **238/250 QLIKE pairs**.

Two qualifications govern the conclusion:

- **Material improvement remains unestablished.** No primary numerical lower
  bound exceeds its material increment at any registered block length. A positive
  lower bound for QLIKE is still below its specified material threshold.
- **Inference remains limited.** The 20,000-family controlled calibration detects
  size excess in 19/20 case/block cells, preventing a validated nominal 95%
  simultaneous-confidence interpretation. The matched family is unavailable
  because reversion versus drift has zero paired observations; that endpoint is
  retained rather than dropped.

The study supports a completed research and engineering evaluation, with no
strategy advancement under the registered design. The
[source-bound endpoint table](docs/evidence/m9_final_endpoints.csv) and
[final acceptance record](docs/validation.md#windows-provider-2025-final-acceptance)
retain estimates, denominators, unavailable values and source identities.

## Runtime Measurements

The archived Release benchmark contains **120 rows across eight modes**, with
five repetitions at 8, 32 and 128 round trips. At 128 round trips, the Windows
median trial p50 measurements are:

| Processing mode | Median trial p50 |
| --- | ---: |
| Direct core calls | 24.4 us |
| Synchronized typed journals | 2.4384 ms |
| Synchronized paper journals | 3.5602 ms |

![Archived Release processing costs](docs/figures/runtime_costs.svg)

These timings measure the original bounded single-symbol workload, not the
expanded five-instrument peer or end-to-end market latency. Raw samples,
compiler/host identities and variability remain recorded. Cross-host
Windows/Linux differences cannot isolate an operating-system effect.
The [benchmark specification](docs/runtime_benchmark.md) defines timer scope,
warm-cache recovery, instrumentation and resource limits.

## Implementation and Reproduction

Requirements are **Python 3.10+ and C++20**. Windows uses Visual Studio 2022/2026
with Desktop development with C++ and a Windows SDK. Linux uses GCC/Clang, `ar`
and OpenSSL development headers. The first build acquires pinned nlohmann/json
3.12.0; `--json-include` accepts a preinstalled copy.

```powershell
py -3 -B build.py --configuration Release --target project_demo
& .\scratch\native\Release\project_demo.exe
py -3 -B research/verify_publication.py
```

In Visual Studio, open `Runtime.sln`, select **Release | x64** and build.
**Ctrl+F5** runs the default demo. On Linux, use `python` instead of `py -3`,
forward-slash executable paths and no `.exe` suffix. Debug supports stepping
through code; Release provides the performance reference.

The offline demonstration needs no market credentials or private dataset.
Full historical reproduction additionally requires the registered raw archive,
issuer/action references, calibration and original/replacement frozen-run
evidence. Public summaries cannot reconstruct omitted observations.

The active Python files serve build, issuer-data and publication tasks. The
complete independent numerical verification suite is preserved at the
[verified publication revision](https://github.com/KVInator/algorithmic-trading-runtime/tree/409508fe3e90550052dc691147ffe4834b13089a/research).
The [runbook](docs/running.md#scientific-verification) provides its pinned checkout
and execution commands. The original report's scientific verification commands
apply to that complete revision.

## Repository Structure

| Location | Responsibility |
| --- | --- |
| `src/research` | Prior-only models, registered comparisons, risk estimates and inference |
| `src/accounting`, `src/execution` | Fractional/whole-share ledgers, ownership, matching and recovery |
| `src/market`, `src/alpaca`, `src/storage`, `src/platform` | Validated observations, archive identities and platform resources |
| `src/reporting`, `apps` | Result-bound reports and thin application composition |
| `research` | Frozen protocols, issuer-data preparation and one publication builder |
| `docs` | Methods, source-bound tables, figures and the standalone report |
| `scratch` | Ignored datasets, native builds and full run evidence |

There is one authoritative C++ implementation of the financial engines.
Domain owners enforce state and resource invariants, while pure formulas remain
small functions. The independent Python oracles are verification references.
The current source tree uses one build driver, without CMake/CTest or a hosted
workflow.

Material limits include prior research exposure, selected and correlated
instruments, historical final-bar information, hypothetical costs, no cash yield,
uncalibrated synthetic liquidity and bounded journals. Frozen protocols and
observed failures remain part of the completed result.

## References

- Gao, Han, Li and Zhou (2018), [Market intraday momentum](https://doi.org/10.1016/j.jfineco.2018.05.009).
- Moreira and Muir (2017), [Volatility-managed portfolios](https://www.nber.org/papers/w22208).
- Kunsch (1989), [The jackknife and the bootstrap for general stationary observations](https://doi.org/10.1214/aos/1176347265).
- [Registered research inputs](research/README.md) and [full technical report](docs/report.pdf).

## Author

**Kartik Vij**
