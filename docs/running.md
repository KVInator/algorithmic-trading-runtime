# Build and run procedures

Application paths are anchored to the source root, independently of the IDE
working directory. Each diagnostic or study writes a new directory under
`scratch/runs/`. Existing raw evidence and run directories are retained.

## Windows and Linux builds

On Windows, open the repository folder in Visual Studio 2026. Select
`windows-x64` and `windows-debug`, then **Build > Build All** and
**Test > Run Test Preset > windows-debug**. Select an executable and use
**Ctrl+F5**. The routine preset runs twenty-two contract groups.

`windows-release` builds optimized targets under `scratch/build/Release/` and
has a matching twenty-two-group test preset. Financial/artifact validation stays
enabled. `windows-debug-full` additionally runs the optional 497-session
synthetic disk integration test.

Linux requires CMake 3.25 or later, a C++20 compiler, Make and OpenSSL development
headers/libraries. Initial configuration fetches nlohmann/json 3.12.0 using its
pinned archive digest.

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
scratch/build-linux/pipeline_diagnostic
```

A focused check can select existing groups without changing the full preset:

```sh
ctest --preset linux-debug -R '(intraday_reversion|offline_pipeline)_contracts' --output-on-failure
```

Use relevant contracts during focused edits and the full routine preset at a
verified milestone. The routine suite includes substantial synthetic accounting
and file/reload workloads; its elapsed time is not a core-event latency benchmark.
Windows HTTP and Credential Manager adapters are not built on Linux.

Visual Studio **Git > Pull** updates the checked-out tracking branch, currently
`research/continuation-study`. **Git > Fetch** retrieves unavailable remote
branches. Local datasets remain in place.

## Offline diagnostics

| Target | Expected evidence |
| --- | --- |
| `continuation_diagnostic` | `trace.html` and `decisions.csv`; constructed completed-prefix price/volume decisions |
| `execution_diagnostic` | Six scenarios, 69 transitions, identical repeated replay and retained input/trace files |
| `recovery_diagnostic` | 33 checks, seven checkpoints and valid/torn/recovered/corrupt journals |
| `pipeline_diagnostic` | 58 checks, three cases, paired interrupted/uninterrupted paths and journals |
| `reversion_validation` | Thirteen cases, 1,000 trials each, whole-session coverage/noise evidence and retained first-trial observations |
| `reversion_preflight --synthetic-only` | 22 checks, four estimator controls, exact synthetic prefix and hashed report |

`reversion_validation` runs a separate Monte Carlo workload; routine contracts
use small constructed cases and do not repeat the full diagnostic. Release
is suitable for the full run. Parameter ranges are descriptive; the diagnostic
contains deliberate failures of latent-parameter coverage.

These targets need no credentials or private market datasets. Quarantined/denied
rows in prescribed fault schedules are expected. The journal test group
separately launches four abruptly exiting child processes. These checks do not
establish physical power-loss behavior or historical strategy performance.

## Historical and acquisition targets

| Target | Prerequisites and action |
| --- | --- |
| `alpaca_development` | Windows credentials and SIP access; bounded 2022-2023 collection with checkpoint reuse and explicit unresolved sessions |
| `development_finalize` | Retained acquisition, policy, gap probes and audit; verifies or publishes the revised immutable study manifest |
| `development_replay` | Frozen manifest; baseline SPY-IM-01 ten-case replay and uncertainty |
| `continuation_development` | Frozen snapshot and scheduled-release reference; twenty paired SPY-TOD-01 cost/delay cases plus descriptive diagnostics |
| `risk_preflight` | Frozen development data; all-calendar coverage and constructed accounting. Windows adds three read-only corporate-action access probes. |
| `risk_acquire` | Windows credentials; isolated calibration/warm-up/shortened-session acquisition and action reconciliation |
| `risk_development` | Completed risk acquisition and frozen development data; registered SPY-RISK-01 forty-case study |
| `reversion_development` | Registered SPY-MR-01 protocol/settings and frozen snapshot; twenty prior sessions, 476 scored dates and forty fixed account cases |
| `reversion_preflight` | Frozen snapshot; constructed checks and read-only prefix/reference availability |

Completed acquisition need not repeat for replay. Finalization verifies an
existing frozen study rather than overwriting it. Risk acquisition uses a separate
dataset and reuses completed evidence. Its corporate-action process dates extend
through March 2024 solely for earlier entitlements; later-period prices are not
requested.

`reversion_preflight` verifies inputs for registered revision 1 without scoring.
It audits timestamps and positive opening volume without historical residuals, fits, forecasts, trades or
returns. Default full-data verification can take several minutes;
`--synthetic-only` omits it.

Acquisition exit code 2 means unresolved sessions remain after the audit. HTTP,
calendar, schema, integrity and filesystem errors stop acquisition. Unresolved
acquisition does not authorize scoring. Offline progress messages identify work
boundaries, including verification/replay/bootstrap stages.

## Credentials and optional live diagnostics

Windows collection reads the generic Credential Manager entry
`algorithmic-trading-runtime/alpaca-paper`. Its user-name field contains the
paper API key and its password field contains the secret. A missing entry
triggers hidden console prompts. Credentials are not written to project files.
WinHTTP uses certificate checks, bounded responses/timeouts, disabled redirects
and the existing bounded HTTP-429 policy.

`TRADING_BUILD_DIAGNOSTICS=ON` enables optional `alpaca_gap_probe`. It retains
probe responses separately without modifying checkpoints or policy. Retired
setup/pilot launchers are not required; their retained evidence stays in `scratch/`.

## Frozen sample and exports

The original plan contained 497 eligible sessions after three early closes and
the 2023-01-03 diagnostic fixture were excluded. Repeated/targeted responses for
2023-06-05 lack four minute bars. Revision 2 excludes that whole session before
evaluation, producing 496 complete sessions: 250 in 2022 and 246 in 2023.

The original March manifest retains SHA-256
`a8d9489284efe604ab8350f945e0e5393ae769c6bc3a0350450a2328759416e5`.
The original 497-session acquisition manifest remains unpublished because its
contract was not met. The separate `study_manifest.json` identifies the revised
sample and is verified on every historical study load.

| Run | Export files |
| --- | --- |
| SPY-IM-01 | `daily_returns.csv` and `summary.json` |
| SPY-TOD-01 | `report.html`, `daily_returns.csv`, `features.csv`, `trades.csv`, `removed_exposure.csv`, `opportunity_diagnostics.csv`, `entry_timing.csv`, `scheduled_releases.csv` and `summary.json` |
| SPY-RISK-01 | `report.html`, `measurements.csv`, `features.csv`, `daily_accounts.csv`, `account_ledger.csv` and `summary.json` |
| SPY-MR-01 synthetic validation | `report.html`, `trials_<case-index>.json`, `reference_sessions.csv`, `source_inventory.txt` and `summary.json` |
| SPY-MR-01 preflight | `report.html`, `controls.json`, `coverage.csv`, `synthetic_prefix.csv`, `source_inventory.txt` and `summary.json` |

Continuation's twenty initial sessions are training history, not zero-return
scored days. Paired bootstraps retain common dates and explicitly undefined
groups. Post-result exposure/calendar/covariate/entry diagnostics do not revise
gates or authorize later evaluation.

Risk exports preserve dividends, receivables, payment dates, costs, fallback and
terminal liquidation. Its independent standard-library reference runs as:

```sh
python research/verify_daily_risk_report.py <report-directory>
```

The [research index](../research/README.md) links exact conventions and findings.
Historical delayed price proxies do not establish broker fills or market impact.

The independent synthetic reversion report check uses only the standard library:

```sh
python research/verify_reversion_validation.py <report-directory>
```

`reversion_development` verifies the unchanged protocol/settings and full input
evidence before fitting. It writes forty normalized account paths in four policy
CSV files, a cash reference, measurements, decisions, conditional parameter ranges,
paired outcome ranges and all gates. No later-period data are opened.

Its independent verification uses Python and NumPy only for the separate oracle:

```sh
python research/verify_reversion_study.py <report-directory>
```

The verifier independently implements MT19937-64/session sampling, Decimal OLS,
account arithmetic and NumPy bulk resampling. NumPy is not a C++ build/runtime
dependency. The [development evidence](reversion_development.md) records results
and native verification status.
