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

To retain the console after a manual run, use **Debug > Start Without Debugging
(Ctrl+F5)**. In **Tools > Options > Debugging > General**, clear **Automatically
close the console when debugging stops**. This is a local Visual Studio setting,
not a build setting; changing it requires no rebuild. Microsoft's
[console application guide](https://learn.microsoft.com/en-us/visualstudio/ide/getting-started-with-cpp-in-visual-studio)
describes console retention and the run shortcuts. Directly opening an executable
can still close its window at exit. Launching it from an existing PowerShell
window retains the output in that window, for example:

```powershell
& .\scratch\native\Release\panel_finalize.exe
```

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
| `panel_replay` | M3: original-SPY parity, prior-only five-instrument decisions and primary reference outcomes through 2023; requires the registered M2 archive |
| `cost_execution_check` | M6 constructed quote pagination, invalid-input and resumable-archive checks; no credentials |
| `cost_quote_acquire` | M6 fixed 2017-2023 SIP quote sample; Windows historical-data credentials and accepted M2 masks |
| `cost_delay_development` | M6 frozen-decision cost/delay evaluation; `--check` uses constructed observations, default uses the accepted pre-2024 panel |
| `cost_execution_study` | M6 registered 680-case synthetic broker/client execution study; no credentials or market dataset |
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
JSON precede acquisition/scoring. The completed Windows M2 registration and
its artifact identities are recorded in the
[roadmap](extension_plan.md#m2-windows-registration-completed). The commands
below reproduce input preparation and registration; they do not score a
later-period model or strategy.

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

Prepare the resolved corporate-action reference from free sources:

```sh
py -3 -B research/prepare_panel_actions.py
```

It reuses existing issuer receipts, authenticates the GLD issuer income statement,
and obtains five bounded 2016-2025 secondary event responses. It writes
`issuer/reference_resolved.json`; the original `issuer/reference.json` and raw
price archive remain intact. Each response has a retrieval timestamp and SHA-256.
Issuer cash amounts and declared dates determine one event per symbol/ex-date.
Provider conflicts are retained and duplicate announcements never double income.
The split check requires complete 120-month coverage and zero returned split
events from the secondary source, in agreement with the provider. This is a
cross-provider coverage assumption, not a claim of complete issuer split records.
No subscriptions, broker connections or orders are involved.

After source preparation, select `$(OutDir)panel_finalize.exe` and run with
**Ctrl+F5**. It uses saved responses without network requests. It writes `scratch/runs/panel_readiness_<suffix>/summary.json` and
`report.html`, even when an issuer reference is absent or incomplete. These
contain all coverage/daily-mark gates, contiguous pre-2024 missing intervals,
source coverage declarations and pre-2024 cash discrepancies. A successful run
also records the study-manifest and vintage-bridge identities in the review.
They contain no bar prices or detailed reserved-year gaps. Missing-interval
causes remain undetermined; no bars or payments are invented.
The provider's [bar-construction rules](https://docs.alpaca.markets/us/docs/market-data-faq)
allow an absent bar when there are no trades or no trades eligible to update
prices. Coverage flags alone do not distinguish those cases from missing data.

Verify the readiness coverage/gap calculations and create its review ZIP:

```sh
py -3 -B research/verify_historical_panel.py scratch/datasets/extension_panel_2016_2025 --readiness scratch/runs/panel_readiness_0
```

Use the actual printed readiness suffix. **Share the readiness directory's
`review.zip`; raw responses and issuer source files stay local.** This ZIP has
two files, `summary.json` and `report.html`. The independent oracle reproduces
annual coverage, endpoint gates and exact missing intervals; it authenticates
source receipts, secondary event coverage, canonical cash-event uniqueness and
registration identities. It also checks that daily risk history never crosses
an unavailable closing mark.

`Status: input_limited` and exit 2 denote expected unresolved prerequisites.
They are not a completed study registration. Exit 1 indicates invalid evidence
or a software failure. If all input gates pass, the finalizer then authenticates
the original SPY snapshot and writes the vintage bridge, masks and study
manifest. Readiness assessment alone cannot publish those records. The
acquisition manifest always retains `research_inputs_ready=false`.

The [registered input amendment](../research/extension_input_policy.md) separates
scheduled execution observability from feature completeness. Readiness reports
retain the parent complete-grid failure and show the amended assessment separately.
Feature availability does not imply an active trade or an admissible fit.

The completed provider recovery returned zero bars for SPY/IWM/GLD on
2019-08-12. **Do not rerun acquisition to search for these marks.** Their three
existing immutable recovery receipts are sufficient. The separately pinned
[source resolution](../research/extension_source_resolution.json) permits only
these specific unavailable closes after authenticated recovery attempts. Missing
opens or any other missing close still block registration. Daily outcomes remain
NA and the consecutive 253-close history resets; there is no interpolation,
forward-fill, price substitution or return across a gap. The original parent
assessment and all missing flags are retained.

The original `spy_distribution_reference.json` is also build-pinned. A Windows
checkout may convert its LF newlines to CRLF without changing its JSON values;
that still changes the SHA-256 stored by the original risk acquisition. The
build restores only a newline conversion whose result matches the exact original
digest. A changed financial field is rejected. No acquisition manifest or cached
response is rewritten. The finalizer authenticates these legacy inputs before
the full panel scan, and inventory failures identify the differing field paths.

The full eligibility mask is approximately 13 MB. Finalization reads and hashes
it with an explicit 16-MiB bound; the ordinary 4-MiB artifact bound is unchanged.
If an earlier run wrote the masks and bridge before stopping at hashing, keep
both files. The next run checks their contents and reuses them without rewriting.

After pulling and rebuilding, run the preparation command above, then
`panel_finalize.exe`, then verify its printed readiness directory with
`--readiness`. Expected success is `Status: registered_inputs_ready`.
Seven operational development years do not imply seven active strategy years.
The 2024 validation and 2025 final-evaluation prices and strategy outcomes remain
sealed until their registered milestones.

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


## Causal panel replay (M3)

The M2 archive and its original SPY development/risk snapshots are required.
No acquisition or source-preparation rerun is needed.

1. Pull the current source and build **Release | x64**.
2. Run `panel_check.exe` for constructed acquisition, engine, missing-data and
   causal replay checks. It prints its evidence directory.
3. Run `panel_replay.exe` with the existing console setup. For a retained `cmd /k`
   console, change the executable in **Command Arguments**; keep Command as
   `$(SystemRoot)\System32\cmd.exe` and Working Directory as `$(ProjectDir)`.
4. Run the independent verification using the actual printed suffix:

```sh
py -3 -B research/verify_panel_replay.py scratch/runs/panel_replay_0
```

The native success status is `causal_panel_replay_completed`. It verifies
original-reference parity before opening the expanded development streams.
Progress reports every 25 dates. The report has an instrument selector for
saved target traces; the five bounded JSONL files retain all records locally.
The verifier creates `review.zip` containing the summary and HTML report.

For the constructed run, both independent checks accept its printed directory:

```sh
py -3 -B research/verify_historical_panel.py scratch/runs/panel_check_0
py -3 -B research/verify_panel_replay.py scratch/runs/panel_check_0
```

Use the actual native `panel_check` suffix. Detailed methods and subsequent
work are in [causal panel replay](panel_replay.md).

For the reversion equilibrium comparison correction, existing replay records
can be verified directly after pulling the updated Python script. Rebuilding
the native applications or repeating the historical replay is unnecessary.
Constructed numerical regression checks are available separately:

```sh
py -3 -B research/verify_panel_replay.py --self-check
```

## Shared-capital diagnostic (M4)

Pull the current source and build **Release | x64**. Run
`shared_capital_diagnostic.exe` with the existing retained-console setup. Change
only the executable in Command Arguments to
`/d /s /k ""$(OutDir)shared_capital_diagnostic.exe""`; keep Command as
`$(SystemRoot)\System32\cmd.exe` and Working Directory as `$(ProjectDir)`.

No private archive, credential, acquisition or historical replay is needed.
Verify the actual printed directory:

```sh
py -3 -B research/verify_shared_capital.py scratch/runs/shared_capital_0 --self-check
```

Expected local evidence is 182 native checks, 15 cases and 163 audited durable
transitions. The verifier creates a small synthetic `review.zip`. The
[shared-capital contract](shared_capital.md) describes its ownership, budget,
expiry and recovery conventions and records the completed Windows acceptance.

## Independent broker process (M5)

Build **Release | x64**, which builds both the diagnostic and its worker. Run
`process_broker_diagnostic.exe` using the existing retained-console setup:
Command remains `$(SystemRoot)\System32\cmd.exe`, Working Directory remains
`$(ProjectDir)`, and Command Arguments becomes
`/d /s /k ""$(OutDir)process_broker_diagnostic.exe""`.

The diagnostic launches `synthetic_broker_peer.exe` automatically. Do not launch
that worker manually. The two malformed-frame fixtures intentionally stop their
children; the parent checks those failures and continues to its final totals.
No private data, credential, subscription or historical replay is required.

```sh
py -3 -B research/verify_process_broker.py scratch/runs/process_broker_0 --self-check
```

Use the actual printed suffix. The verifier writes a compact synthetic
`review.zip`. The [process contract](process_broker.md) describes the twelve
scenario families, fee/cash conventions, bounded recovery and completed Windows
acceptance.
Expected evidence is 313 native checks, twelve scenario families and twenty-one
child processes. The independent verifier checks 139 broker commits, 98 client
inputs and 294 logical calls. The saved report has 28 broker/client ledger views.

## Cost and execution inputs (M6)

Pull `main` and build **Release | x64**. From an existing PowerShell window at
the repository root, run the constructed check and its independent verifier:

```powershell
& .\scratch\native\Release\cost_execution_check.exe
py -3 -B research/verify_cost_quotes.py scratch/runs/cost_execution_check_0
```

Use the printed suffix. Expected evidence is seventeen constructed acquisition
cases and 2,210 verified windows, with zero network requests. The native total
is 355 checks when the accepted M2 masks are present, or 354 without them.
The 680 synthetic execution cases are registered, not executed by this check.

Then run the historical sampler using the existing Windows historical-data
credentials and registered M2 archive:

```powershell
& .\scratch\native\Release\cost_quote_acquire.exe
py -3 -B research/verify_cost_quotes.py scratch/runs/cost_quotes_0
```

The sampler prints its archive and report directories before requesting data.
Use its actual report suffix for the verifier. It runs through **2023 only**,
over exactly 910 fixed windows; it does not score a strategy or open reserved
2024-2025 strategy inputs. Pagination means 910 windows can require more than
910 requests. Progress is printed every 25 windows, with new HTTP requests paced
at 750 ms plus HTTP and bounded retry time. It is not the full panel acquisition.

Rerun the same executable after an interrupted transport to resume committed
pages. Keep the default `scratch/datasets/cost_quote_sample_2017_2023` archive
intact. A denied route is retained as unestimated coverage and is not repeatedly
requested. Exit 0 means the audit completed, including an explicit denied,
empty or incomplete disposition; inspect the status counts. Exit 1 means an
invalid input or interrupted run, with completed response envelopes retained.
Do not delete old evidence to force a new result.

The independent verifier creates `review.zip` in each report directory. Share
the constructed check's bundle, the provider sample's bundle and the verifier's
console result. Raw quote envelopes, credentials and the full dataset stay local.
The [M6 note](cost_execution.md) describes the remaining comparisons and the
limits of the compact review bundle.

For the existing Visual Studio retained-console setup, keep Command as
`$(SystemRoot)\System32\cmd.exe` and Working Directory as `$(ProjectDir)`.
Set Command Arguments first to
`/d /s /k ""$(OutDir)cost_execution_check.exe""`, then to
`/d /s /k ""$(OutDir)cost_quote_acquire.exe""` for the provider acquisition.
Neither command requires changing the solution or restoring CMake/CTest.

To regenerate the plots from an existing acquisition summary, use the rendering
mode after pulling and building the updated application:

```powershell
& .\scratch\native\Release\cost_quote_acquire.exe --render scratch/runs/cost_quotes_0/summary.json
py -3 -B research/verify_cost_quotes.py scratch/runs/cost_quotes_view_0 --report-only
```

Open the printed `report.html` in a browser. This mode creates a new
`cost_quotes_view_<suffix>` directory and preserves the original summary bytes.
It performs zero requests and does not load credentials or the raw archive.
Use the actual printed suffix. Static coverage/status plots and the initial
spread detail remain visible when scripts are disabled. Enabled scripts allow
switching between the pre-rendered instrument/year views; empty groups show
an explicit unavailable message. `--report-only` verifies saved arithmetic and
displayed marks; omit that flag for the full local raw-response verification.

To inspect the retained source of condition failures and latest-timestamp
conflicts, run the cached diagnostic and its full independent verifier:

```powershell
& .\scratch\native\Release\cost_quote_acquire.exe --inspect scratch/runs/cost_quotes_0/summary.json
py -3 -B research/verify_cost_quotes.py scratch/runs/cost_quote_diagnostics_0
```

Use the actual printed suffix. This mode opens the existing quote archive and
M2 selection masks, performs zero requests, loads no credentials and preserves
the original sample. It creates `cost_quote_diagnostics_<suffix>` with a static
HTML report and diagnostic manifest. The verifier writes `review.zip` after
checking the original raw responses and every diagnostic observation. Share
that compact ZIP and the console result; quote prices and raw bodies remain
local. The existing `--render` mode is for presentation; `--inspect` additionally
reads the retained raw pages. Neither mode acquires new data or changes the
registered eligibility rule.

## Historical cost and delay evaluation (M6)

Build **Release | x64** in the existing solution, or build only this application:

```powershell
py -3 -B build.py --configuration Release --target cost_delay_development
```

Run the constructed numerical/missing-input checks first:

```powershell
& .\scratch\native\Release\cost_delay_development.exe --check
py -3 -B research/verify_cost_delay.py scratch/runs/cost_delay_check_0 --self-check
```

Then evaluate the accepted historical panel. No new acquisition or credentials
are used; the original panel and issuer/action evidence must remain in place.
The reader authenticates the M2 registration and opens strategy prices only
through 2023. The existing M3 replay need not be rerun separately.

```powershell
& .\scratch\native\Release\cost_delay_development.exe
py -3 -B research/verify_cost_delay.py scratch/runs/cost_delay_0 --self-check
```

Use the actual directories printed by each run. Expect 400 instrument/policy
series, 240 paired comparisons, primary-reference parity and zero requests.
The historical independent verification authenticates source observations,
reconstructs causal histories/decisions, verifies every charged reference path,
then checks all summary denominators and static curves/tables. It is a deliberate
checkpoint workload rather than an ordinary build check. Share each run's
`review.zip` and its console result. Provider bundles contain summary/HTML only;
raw provider pages and the five local path files stay local. The constructed
bundle also includes its generated path files.

If an earlier verifier stopped at `Cost curve lines differ from verified
coordinates`, retain that run. Pull the corrected verifier and rerun only the
Python command against the existing `cost_delay_<suffix>` directory. No C++
rebuild, historical replay or report regeneration is required. The fix accounts
for the SVG's six-decimal serialization; source, cash and return checks remain
unchanged.

For the existing retained-console Visual Studio setup, the application arguments
are `/d /s /k ""$(OutDir)cost_delay_development.exe" --check"` for constructed
checks and `/d /s /k ""$(OutDir)cost_delay_development.exe""` for historical
evaluation. Keep the existing Command and Working Directory settings.

`--report-only` on the Python verifier checks a compact saved report without
claiming source or aggregate-path authentication. No cost or delay is selected
as a replacement primary strategy. Synthetic matching, continuous daily-risk
accounts and shared-capital economic evaluation remain separate work.

## Synthetic execution study (M6)

Build **Release | x64** with the existing solution, or select the single target:

```powershell
py -3 -B build.py --configuration Release --target cost_execution_study
```

Run the full fixed synthetic matrix and its independent verifier:

```powershell
& .\scratch\native\Release\cost_execution_study.exe
py -3 -B research/verify_cost_execution.py scratch/runs/cost_execution_0 --self-check
```

Use the actual printed suffix. Expect 680 account cases, 510 matched comparisons,
680 reconciled terminal accounts and zero requests. The progress counter prints
every forty cases. The report has ten instrument/side heatmaps, filled/unfilled
outcomes, explicit fees and separate source/receipt timing. All plotted cells
are embedded; scripts only select the visible panel.

The verifier writes a self-contained `review.zip` containing `summary.json`,
`report.html` and `execution_cases.jsonl`. Share that ZIP and the console result.
No quote acquisition, credentials, historical archive, network, child broker
process or reserved-year strategy data is used. Separate-process recovery is
the existing M5 diagnostic, not a claim of this measurement run.

For the retained-console Visual Studio setup, keep the existing Command and
Working Directory and use arguments
`/d /s /k ""$(OutDir)cost_execution_study.exe""`.
