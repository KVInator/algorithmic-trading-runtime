# Build and reproduction

Paths are anchored to the source root. Runnable studies and demos create a new
`scratch/runs/<name>_<suffix>/` directory and preserve earlier outputs. Initial
builds acquire nlohmann/json 3.12.0 with a pinned archive SHA-256. Builds use C++20
with project warnings as errors.

## Windows

Install Visual Studio 2022 or 2026 with **Desktop development with C++**, a Windows
SDK and Python 3.10+ with its `py` launcher. Pull `main`, then open **Runtime.sln**.
Select **Release | x64** and **Build > Build Solution**. **Ctrl+F5** runs the default
`project_demo` application. Open its printed HTML report.

The solution calls `build.py`; it does not maintain another source list or build
algorithm. **Debug | x64** builds with symbols and disabled optimization for
stepping through code. Both configurations write to `scratch/native/`, with
separate object and executable directories. A first build compiles the shared
core once and links the applications. Later builds reuse matching source/header
fingerprints. Source-inventory changes can invalidate all objects.

Other applications can be launched directly from `scratch/native/Release/`.
To run one with the Visual Studio debugger, open **Runtime > Properties >
Configuration Properties > Debugging > Command** and select its `.exe`.
The working directory is the repository root. **Clean Solution** removes only
that configuration's native outputs; private datasets and run directories remain.

For a terminal build, the driver discovers the installed x64 MSVC toolchain:

```sh
py -3 build.py --configuration Release --target project_demo --run
```

## Linux

Requirements: Python 3.10+, GCC/Clang with C++20, `ar` and OpenSSL development
headers/libraries. Set `CXX` to a compiler executable when choosing Clang.

```sh
python -B build.py --configuration Release
scratch/native/Release/project_demo
python -B research/verify_project_demo.py scratch/runs/project_demo_0
```

Use the actual printed suffix. The full 144-case execution grid and 120-row
benchmark are separate runnable workloads:

```sh
scratch/native/Release/execution_measurement
python -B research/verify_execution_measurement.py <printed-directory>
scratch/native/Release/runtime_benchmark
python -B research/verify_runtime_benchmark.py <printed-directory>
```

`--target <name>` selects one application; `--jobs <count>` controls compilation
parallelism. `--rebuild` clears only the selected configuration before compiling;
`--clean` clears it without compiling. An offline installation can supply an
existing nlohmann/json 3.12.0 include directory with `--json-include <path>`.
The default downloaded dependency remains under `scratch/dependencies/`.

## Scientific verification

The final repository retains independent `research/verify_*.py` entry points
for the published studies, saved ledgers, execution measurements, benchmarks
and publication. They inspect exported results through separate arithmetic and
replay references. Run the relevant verifier after changing its financial or
persistence contract. There is no committed unit-test tree or hosted workflow;
these checks are explicit local commands.

## Supported entry points

| Target | Purpose / additional prerequisites |
| --- | --- |
| `project_demo` | Offline strategy decisions, account parity, broker lifecycle and recovery; no credentials/data |
| `historical_capability_check` | Focused native wider-request/calendar and capability-evidence checks; synthetic, no credentials |
| `historical_capability_probe` | Read-only five-instrument 2016-2025 capability samples; Windows historical data credentials |
| `execution_measurement` | 144 matched synthetic execution cases and saved quote/fill views |
| `runtime_benchmark` | 120 Release processing/storage/recovery rows; Debug is diagnostic only |
| `reversion_validation` | Thirteen seeded finite-sample/noise controls; full interval experiment |
| `development_finalize` | Read-only authentication/finalization of the retained 2022-2023 acquisition tree |
| `development_replay` | Registered SPY-IM-01 development comparison on the frozen snapshot |
| `continuation_development` | Registered SPY-TOD-01 comparison and declared descriptive diagnostics |
| `reversion_development` | Registered prior-only SPY-MR-01 comparison and controls |
| `risk_development` | Registered SPY-RISK-01 comparison, calibration and corporate-action evidence |
| `strategy_reference_replay` | First eligible scored-date adapter replay with explicit constructed bar-price quotes |
| `risk_acquire` | Bounded daily-risk prerequisite acquisition; Windows credentials/HTTP |
| `alpaca_development` | Historical 2022-2023 minute/calendar acquisition; Windows credentials/HTTP |

Historical Alpaca credentials use Windows Credential Manager. The retained data
policy, raw response pages, monthly manifests/checkpoints, gap evidence and study
manifest define historical reproduction. `DevelopmentStudyDataset` validates the
frozen 496-session tree under `scratch/datasets/SPY_1Min_SIP_development_2022-2023`; daily risk additionally verifies
its registered calibration, warm-up, shortened-session and issuer-action inputs.
Use the paths declared by those loaders and governing JSON records; a new provider
snapshot cannot overwrite their identities. Missing prerequisites stop the runner.
No broker account, real-time quote probe or paid subscription is required by the
offline completion scope. The v1 entry points remain restricted to their
registered inputs; no v1 2024-2025 strategy evaluation is authorized. The
[extension plan](extension_plan.md) records a separate registration and access
sequence for later periods. M0 opens no later-period strategy inputs or results.

Historical exports have independent checks in `research/verify_*.py`; their
methods and numerical conventions are recorded beside the governing protocols.
The verification entry points use the Python standard library except
`verify_reversion_study.py`, which additionally requires NumPy and an available
IANA time-zone database. Install NumPy and `tzdata` before running that verifier,
particularly on Windows:

```sh
python -m pip install numpy tzdata
```

These extras are not required for the offline demo, execution-measurement,
benchmark or publication-integrity verifiers. Publication generation has its
separate dependencies in `research/report_requirements.txt`.

Historical final bars cannot reproduce original live receipt/revision timestamps.
The report's curated result snapshot reproduces figures/tables, not unbundled raw
prices or historical daily paths.

## Extension data capability

Build **Release | x64** in `Runtime.sln`. The default startup application remains
`project_demo`. Select `scratch/native/Release/historical_capability_check.exe`
as the project's **Debugging > Command**, then Ctrl+F5, for the constructed
checks. Select `historical_capability_probe.exe` there for the provider run.
Keep the repository root as the working directory.

The provider runner uses the existing Windows Credential Manager entry. If it
is absent, hidden console input accepts the same Alpaca API key and secret from
the Alpaca dashboard's API Keys control. Credentials are never included in the
report. No order, position, account-balance, latest-quote or stream endpoint is
called. The existing calendar GET uses `paper-api.alpaca.markets`; historical
prices and actions use `data.alpaca.markets`.

The fixed plan covers SPY, QQQ, IWM, TLT and GLD for 2016-2025. January's first
and December's last full session come from the returned calendar. Their complete
minute grids are sampled without replacements for inconvenient gaps. One noon
minute per symbol/year supplies a first-page quote/trade capability sample.
Corporate actions are paginated over each process-date year. A nominal complete
run uses about 570 HTTP attempts before pagination variation/retries, with a
750-ms pause per attempt. This one-time capability run takes several minutes
and is separate from routine editing checks.

Every response is retained before interpretation, including failures and retry
responses. HTTP 401 stops further calls; 400/403/404 block that endpoint/symbol
for the run. HTTP 429 and 500/502/503/504 receive at most two retries. Persistent
failure is recorded without further entitlement experiments. Exit 0 means the
sampled bar route is ready for review, not that the dataset or actions are
complete. Exit 2 means price access needs review. Both produce a report. Exit 1
is an interrupted software/storage failure; captured evidence remains in the
printed directory.

Provider runs write to `scratch/runs/historical_capability_<suffix>/`:

- `review/`: shareable report, capability CSV and summary with counts and hashes;
- `responses/`, `sealed/`: local raw evidence, with 2024-2025 under `sealed/`;
- `index/`: an append-only attempt index with request paths and status.

Verify a completed provider run locally and create its price-free review ZIP:

```sh
py -3 -B research/verify_historical_capability.py scratch/runs/historical_capability_0
```

Use the actual printed suffix. The standard-library script independently checks
raw hashes, pagination, grids, action fields, metadata, CSV values and the sealed
boundary, without printing observed prices or financial summaries. On success
it creates `review.zip` containing exactly `summary.json`, `capability.csv` and
`report.html`. **Share only this ZIP or `review/`; keep the full run directory
and raw responses local.** The same script accepts the printed
`historical_capability_check_<suffix>` directory for offline constructed checks.

M1 remains open until the provider matrix is reviewed. The common price start
is provisional and sample-based. Full coverage, action/issuer agreement,
process-date tails and warm-up are M2 requirements. Empty actions do not prove
that no distribution or split occurred. A first quote/trade page establishes
neither full-window coverage nor empirical execution quality.

Endpoint conventions follow the official
[historical bars](https://docs.alpaca.markets/us/reference/stockbars),
[quotes](https://docs.alpaca.markets/us/reference/stockquotes-1),
[trades](https://docs.alpaca.markets/us/reference/stocktradesingle-1) and
[corporate actions](https://docs.alpaca.markets/us/reference/corporateactions-1)
references, reviewed 26 October 2026. Documentation does not establish this
account's entitlement.

## Publication

The PDF and standalone HTML are committed under `docs/`. The HTML embeds the
C++ saved-event views and images and opens without a server, script or network.
Install the pinned publication dependencies in an existing Python 3.10+ environment:

```sh
python -m pip install -r research/report_requirements.txt
python -B research/build_report.py --demo <verified-project-demo-directory>
python -B research/verify_publication.py
```

The builder reads `docs/report_source.json` and hash-checked `docs/evidence/`.
It formats recorded data and descriptive summaries; it does not fit models,
score strategies or change financial conventions. Review regenerated figures and
PDF pages before publishing. The publication manifest records source/evidence and
output hashes. Distributed publication text uses UTF-8 with LF-normalized
checksums so Git newline conversion does not alter its logical identity; PDFs
use raw-byte checksums. Registered protocols are restored to their exact
registered LF bytes by the build only when their content is otherwise identical.
Acquisition pages, run journals and original result identities retain raw-byte
hashes. `research/verify_publication.py` checks these identities, local
links, embedded-view controls and static asset boundaries without private data.
