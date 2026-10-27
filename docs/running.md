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
| `panel_check` | Constructed resumable panel, calendar/mask, action-reconciliation, vintage-bridge and access-control checks |
| `panel_acquire` | Resumable 2016-2025 five-instrument archive; Windows historical-data credentials; long-running |
| `panel_finalize` | Local issuer reconciliation and original-snapshot bridge; publishes the new study manifest only after prerequisites pass |
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

The [M1 provider assessment](extension_plan.md#m1-provider-evidence-and-disposition)
records the reviewed 26 October 2026 run. All 570 requests succeeded; four missing
minute bars and incomplete early dividend fields require M2 treatment. The
reported `provisional_common_price_start` is the earliest suffix of sampled
years with no observed bar gap. Its value of 2025 is not an entitlement boundary
or a recommendation to discard earlier history. Full coverage, action/issuer
agreement, process-date tails and warm-up remain M2 requirements. Empty actions
do not prove that no distribution or split occurred. A first quote/trade page
establishes neither full-window coverage nor empirical execution quality.

Endpoint conventions follow the official
[historical bars](https://docs.alpaca.markets/us/reference/stockbars),
[quotes](https://docs.alpaca.markets/us/reference/stockquotes-1),
[trades](https://docs.alpaca.markets/us/reference/stocktradesingle-1) and
[corporate actions](https://docs.alpaca.markets/us/reference/corporateactions-1)
references, reviewed 26 October 2026. Documentation does not establish this
account's entitlement.

## Extension panel acquisition and registration

The governing [study contract](../research/extension_protocol.md) and its pinned
JSON precede acquisition/scoring. M2 remains in progress until the full local
archive, actions and original-SPY bridge pass. No later-period model or return
is needed for these checks.

1. Build **Release | x64** in `Runtime.sln` with **Build Solution**.
2. Set **Runtime > Properties > Debugging > Command** to
   `$(OutDir)panel_check.exe`, keep working directory `$(ProjectDir)`, and use
   **Ctrl+F5**. This is the fast constructed check; no credentials or network.
3. Set Command to `$(OutDir)panel_acquire.exe`, then **Ctrl+F5**. The default
   output is `scratch/datasets/extension_panel_2016_2025/`. The first full run
   takes hours: roughly 50,000 bar pages, with about 10.5 hours of pacing alone.
   Rerunning resumes the same vintage and authenticates earlier pages without
   downloading them again. Leave one writer running; do not run two copies.
4. Wait for the final `Status: prices_acquired_action_reconciliation_required`
   and `Visual report` lines. December 2025 price progress alone is not completion:
   corporate-action collection follows, including a January-March 2026 process-date
   tail for late-processed events. Completion writes `acquisition_manifest.json`
   in the dataset root. If that file is absent, resume `panel_acquire.exe` and retain
   all existing responses. If acquisition stops, retain its final error lines.
   After completion, run the independent verifier from the repository root:

```sh
py -3 -B research/verify_historical_panel.py scratch/datasets/extension_panel_2016_2025
```

The script audits request/page identities, calendars, grids, masks and actions
without printing prices. It creates `review/review.zip` with only a summary and
coverage CSV. **Share that ZIP; keep raw/sealed inputs local.** The same verifier
accepts the printed `scratch/runs/panel_check_<suffix>` directory for constructed
evidence. Use actual suffixes.

Free issuer cash-reference preparation is a separate local command:

```sh
py -3 -B research/prepare_panel_actions.py
```

It caches primary-source files with retrieval/hash provenance and prepares
`issuer/reference.json` inside the new dataset. It does not infer split
completeness or GLD's history from a dividend table. The initial reference is
therefore intentionally incomplete, with its unresolved declarations visible.
Do not mark coverage complete without supporting whole-horizon source evidence.

Once those sources have been reconciled, select `$(OutDir)panel_finalize.exe`.
It authenticates the panel, issuer evidence and original registered 496-session
SPY dataset before writing `study_manifest.json`, masks and the source-vintage
bridge. Exit 2 means an expected unresolved prerequisite; exit 1 indicates a
verification/software failure. Neither is permission to score. The acquisition
manifest alone always retains `research_inputs_ready=false`.

The controlled precision design can be reproduced independently:

```sh
py -3 -B research/extension_precision.py
```

It uses synthetic Gaussian AR(1) trials only. Its planning table is not a market
result or final validation of the registered block-bootstrap procedure.

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
