# Algorithmic Trading Runtime

C++20 research and execution software connecting causal SPY strategy decisions to
exact order accounting, a deterministic broker peer and durable restart recovery.
The project combines **three intraday strategy families**, a separate daily-risk
study, **144 synthetic execution cases** and **120 Release benchmark trials per
host**. Python provides independent numerical checks and publication rendering.

**Read the [technical report (PDF)](docs/report.pdf) or download the
[self-contained HTML report](docs/report.html)** for the full mathematics,
results, assumptions and interactive saved-event replay.

## Research findings

The historical sample is inspected **2022-2023 development data**. No registered
candidate advances to later-period evaluation. Positive point returns and failed
advancement screens are reported together.

| Experiment | Sample | Main finding |
| --- | --- | --- |
| SPY-IM-01: late-session momentum | 496 dates; 268 active | Gross compounded return **-2.8207%**; primary net **-7.8924%**. No demonstrated gross edge. |
| SPY-TOD-01: time-of-day continuation | 476 scored dates after 20 warm-up sessions | Volume variant net **+11.9652%**, falling to **-0.4292%** when its five largest winning dates become cash. Both variants fail registered screens; the volume increment remains unconfirmed. |
| SPY-MR-01: opening-anchor reversion | 476 dates; 83 active | Candidate gross **+1.2498%**, primary net **-0.4171%**; cost-dominated and below the 100-active-date coverage convention. |
| SPY-RISK-01: daily risk allocation | 501 exchange dates; 40 account cases | Intraday-risk CER increment over daily-only **-1.5430 percentage points/year**. Positive QLIKE point improvement has intervals spanning zero. |

Primary intraday comparisons use one-minute delayed minute-open references and
**1 bp per side**. These are hypothetical all-in costs, not measured spreads or
broker fees. The designs have different information sets and holding windows;
the table is not a risk-matched performance ranking.

![Recorded historical development findings](docs/figures/research_findings.svg)

## Mathematical design

**Momentum and continuation.** Late momentum observes
$m_d=C_{d,15:29}/O_{d,09:30}-1$ at 15:30 and enters only when $m_d>0$.
Time-of-day continuation compares the current move $u_{d,m}$ with the mean absolute
same-clock move over twenty strictly earlier sessions:

$$
s_{d,m}=\frac{1}{20}\sum_{j=d-20}^{d-1}|u_{j,m}|.
$$

The volume variant also requires cumulative shares above the prior same-clock
median for fresh entries. Volume does not determine exits. Signals use completed
bars; delayed references occur after decisions. For one-way cost $c=k/10^4$:

$$
R(c)=\frac{P_x(1-c)}{P_e(1+c)}-1.
$$

**Opening-anchor reversion.** Five-minute log residuals around a fixed
volume-weighted opening-close anchor follow the estimated relation
$X_{k+1}=a+\phi X_k+\epsilon_{k+1}$. Pooled centered OLS uses 480 within-session
pairs from twenty prior sessions. For an admissible positive sub-unit slope:

$$
\theta=\frac{a}{1-\phi},\qquad
\kappa=-\frac{\log\phi}{5},\qquad
 g(x;\delta,H)=(\theta-x)e^{-\kappa\delta}(1-e^{-\kappa H}).
$$

The noon candidate compares this expected delayed move with the fixed primary
log-cost hurdle. Unsupported fits produce dated cash decisions. Numerical
OU admissibility does not establish stationarity or identify a latent diffusion.
The synthetic noise study demonstrates false admissibility and interval
undercoverage; historical parameter ranges remain descriptive.

![Finite-sample and observation-noise evidence](docs/figures/reversion_validation.svg)

**Daily risk.** Prior-only daily-squared-return and RV5 estimates feed
$w_d=\min(1,0.10/\sqrt{252h_d})$. Fractional accounts retain cash, shares,
dividend receivables/payments and split identities. Forecast loss and allocation
value are assessed separately using paired whole-calendar resampling. Intraday
studies use five-/ten-session outcome blocks; risk uses twenty/forty. Fixed
screens and prior research exposure remain explicit in the
[registered protocols](research/README.md).

## Decisions, orders and recovery

```mermaid
flowchart TD
    B["Validated completed observations"] --> S["Authoritative strategy engines"]
    S --> R["Fractional research accounts"]
    S --> T["Whole-share target adapters"]
    T --> E["Order state and reservations"]
    E --> J["Synchronized input journal"]
    J --> P["Deterministic broker peer"]
    P --> F["Incremental fills and snapshots"]
    F --> E
    J --> G["Verified replay and closed routing gate"]
    G --> E
```

The core reserves cash or shares before dispatch eligibility. Partial fills apply
exact integer money once per stable execution identity. Pending cancellation
retains resources. Contradictory evidence closes entry; broker snapshots must
match cash, positions and complete open-order terms before routing reopens.

The schema-3 bridge uses **USD 1e-9 units** and durable broker identities. A
candidate input/checkpoint is appended and synchronized before state publication.
Recovery replays the same core and persists a new disconnect barrier. An
incomplete tail can be recovered only into a separate verified-prefix file;
complete corruption is rejected. Uncertain durable orders are not blindly resent.
Local once-only accounting does not guarantee exactly-once external dispatch.

![Partial-fill and cancellation accounting](docs/figures/broker_lifecycle.svg)

`project_demo` exposes a keyboard-operable event playhead, targets, fills,
reservations, client/broker state, account-parity cases and interrupted paths.
Its **259 checks across 24 cases** run without credentials or private data.
Quotes, messages and event times are synthetic. Historical Alpaca acquisition is
preserved separately; no paid feed or native broker service is required.

## Execution measurements

Four fixed limit/deadline policies run on 36 matched quote/delivery workloads,
yielding **144 accounts, 108 paired policy comparisons and 120 assumption
comparisons**. Signed arrival-midpoint shortfall is decomposed into spread and
timing; unfilled shares receive a separate common-terminal opportunity mark.
The arithmetic retains exact half-cents.

| Policy | Filled/requested shares | Completed cases / 36 | Total marked shortfall, bp of requested notional |
| --- | ---: | ---: | ---: |
| Crossing limit; 5-second cancel | 72.22% | 20 | 3.5889 |
| Crossing limit; 11-second cancel | 76.67% | 24 | 3.5889 |
| Same-side limit; 5-second cancel | 14.44% | 0 | 3.7667 |
| Same-side limit; 11-second cancel | 20.00% | 0 | 3.6000 |

![Fill coverage and execution-cost components](docs/figures/execution_policies.svg)

These are constructed workload aggregates, not empirical fill probabilities.
Source and receipt clocks are distinct. Incomplete completion times stay null;
cancellation is informative censoring. Synthetic prices do not estimate market
impact, exchange queue priority or actual broker latency.

## Measured implementation costs

Matched Release workloads compare direct processing, additional candidate
copying, volatile/synchronized schema-2 and schema-3 journals, and warm restart.
At 128 round trips on the verified Windows workstation:

| Operation | Median of five trial p50 values |
| --- | ---: |
| Direct core input | 24.4 us |
| Input with additional candidate copy | 83.8 us |
| Synchronized typed-journal input | 2.4384 ms |
| Synchronized paper-journal input | 3.5602 ms |
| Typed warm recovery | 72.7635 ms |
| Paper warm recovery | 982.5807 ms |

![State-size latency and timed-call throughput](docs/figures/runtime_costs.svg)

MSVC 19.51.36260.0 Release uses `/O2 /Ob2 /DNDEBUG`, C++20 and warnings as errors.
The OS reports Intel Family 6 Model 183, 24 logical processors. Process-wide peak
working set is 20.730 MiB; this does not isolate per-case allocations. Throughput
uses summed timed calls, excluding preparation, validation and reporting. Recovery
reads are warm-cache. Timings depend on the host, storage and instrumentation.

Journals are bounded to **10,000 inputs, 32 MiB per file and 64 KiB per frame**.
Copies and retained histories have measured costs; no capacity extrapolation,
low-latency target, rotation or compaction is claimed.

## Build and reproduce

Windows requires Visual Studio 2022 or 2026 with **Desktop development with C++**,
a Windows SDK and Python 3.10+ with the `py` launcher. Open **Runtime.sln**, select
**Release | x64**, then **Build > Build Solution**. Press **Ctrl+F5** to run
`project_demo` and open its printed `report.html`. Select **Debug | x64** when
stepping through code. Executables are written to `scratch/native/<configuration>/`.

Linux requires Python 3.10+, GCC/Clang with C++20, `ar` and OpenSSL development headers:

```sh
python -B build.py --configuration Release
scratch/native/Release/project_demo
python -B research/verify_project_demo.py scratch/runs/project_demo_0
```

Use the actual printed suffix. The first build acquires checksum-pinned
nlohmann/json 3.12.0; subsequent builds reuse the local headers and current
objects. `build.py --target project_demo --run` builds and runs one application.
The native solution delegates to this same compiler driver, so there is one
build definition for the shared core and applications.

The [runbook](docs/running.md) covers execution measurements, benchmarks,
historical acquisition/reproduction and rebuilding the publication. Independent
Python checks reconstruct Decimal ledgers, fit/statistical references, quote
budgets, source/receipt timing, censoring and artifact identities. Full historical
reproduction requires the retained private input evidence; the distributed
[summary snapshot](docs/evidence/manifest.json) does not manufacture daily paths.

## Structure and limits

| Location | Responsibility |
| --- | --- |
| `src/market`, `src/alpaca`, `src/storage`, `src/platform` | Validated observations, message contracts, integrity and platform resources |
| `src/research`, `src/accounting` | Authoritative causal models, statistics and fractional accounts |
| `src/execution` | Whole-share runtime, journals, adapters and deterministic broker/replay scenarios |
| `src/reporting`, `apps` | Result-bound exports and thin composition roots |
| `research/verify_*.py` | Independent numerical, accounting and artifact references |
| `docs` | Technical report, curated figures/evidence and operational contracts |
| `Runtime.sln`, `Runtime.vcxproj`, `build.py` | Native Visual Studio entry point and shared compiler driver |

The [architecture](docs/architecture.md) explains ownership and units.
Generated builds, private data and full run directories stay under ignored
`scratch/`.

The evidence is limited by one ETF, two inspected development years,
finalized historical bars, assumed costs, long/cash scope, observation-model
identification and uncalibrated synthetic liquidity. Local **2024-2025 strategy
prices and performance remain unopened**. The project is a completed bounded
research and execution study, with no demonstrated live-trading edge.
