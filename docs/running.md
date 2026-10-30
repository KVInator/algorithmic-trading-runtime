# Build and reproduction

The native financial implementation is C++20. Python builds the applications,
checks exported evidence independently and renders the publication. CMake and
CTest are not used. Source-relative datasets, native objects and generated runs
remain under ignored `scratch/`.

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

The driver selects x64 MSVC. Source/toolchain fingerprints invalidate incompatible
objects; no dataset or earlier run needs deletion.

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

Run independent verification against the directory printed by the application.
A successful check writes a receipt or compact review bundle. Historical
verification requires the local raw archive; a saved HTML view or review ZIP is
not a substitute for raw-source authentication. No broker service is required.
Most verifiers use the Python standard library. The original reversion-study
oracle additionally requires NumPy and `tzdata` on systems without IANA time zones.

```powershell
py -3 -B research/verify_native_build.py
py -3 -B research/verify_project_demo.py <demo-directory>
py -3 -B research/verify_shared_capital.py <shared-capital-directory> --self-check
py -3 -B research/verify_process_broker.py <process-broker-directory> --self-check
py -3 -B research/verify_publication.py
```

## Supported entry points

Build a single application with `--target <name>`, or omit it to build all
supported applications. Host-specific acquisition applications require Windows.
Applications with `--check` produce declared analytic fixtures; they do not
reproduce provider-market findings.

| Application | Responsibility | Independent verifier |
| --- | --- | --- |
| `project_demo` | Original offline strategy, broker and recovery demonstration | `verify_project_demo.py` |
| `shared_capital_diagnostic` | Global cash, ownership, netting and recovery | `verify_shared_capital.py` |
| `process_broker_diagnostic` | Separate synthetic broker process and wire faults | `verify_process_broker.py` |
| `synthetic_broker_peer` | Child worker launched by the process diagnostic | Verified through its parent |
| `execution_measurement` | Original 144-case synthetic execution comparison | `verify_execution_measurement.py` |
| `runtime_benchmark` | Bounded Release processing/storage/recovery workload | `verify_runtime_benchmark.py` |
| `historical_capability_check`, `historical_capability_probe` | Constructed/free historical-access evidence | `verify_historical_capability.py` |
| `panel_check`, `panel_acquire`, `panel_finalize` | Archive acquisition, masks, actions and registration | `verify_historical_panel.py` |
| `panel_replay` | Causal five-instrument decisions and forecasts | `verify_panel_replay.py` |
| `cost_execution_check`, `cost_quote_acquire` | Quote archive, immutable resume and cached diagnosis | `verify_cost_quotes.py` |
| `quote_reference_development` | Historical quote crossing-cost references | `verify_quote_reference.py` |
| `cost_delay_development` | Frozen-decision cost/delay grid | `verify_cost_delay.py` |
| `cost_execution_study` | 680 synthetic execution cases | `verify_cost_execution.py` |
| `panel_account_development` | Funded risk and shared-capital accounts | `verify_panel_accounts.py` |
| `stability_development` | Primary attribution, concentration and uncertainty | `verify_stability.py` |
| `stability_extension_development` | Fourteen sensitivities and funded risk matching | `verify_stability_extension.py` |
| `inference_calibration` | Controlled full-family size experiment | `verify_inference_calibration.py` |
| `panel_evaluation` | Authorized 2024 evaluation, freeze and final 2025 evaluation | `verify_panel_evaluation.py` |
| `development_replay`, `continuation_development`, `reversion_development`, `risk_development` | Original registered SPY studies | Governing checks in `research/README.md` |
| `strategy_reference_replay`, `reversion_validation` | Reference adapter and synthetic identification diagnostics | `verify_strategy_execution.py`, `verify_reversion_validation.py` |
| `development_finalize`, `alpaca_development`, `risk_acquire` | Original SPY registration and retained prerequisite acquisition | Original policy and input checks |

## Extension data capability

Historical market-data credentials use Windows Credential Manager. No broker
account, subscription purchase, latest-quote probe or real order is required.
The archive is `scratch/datasets/extension_panel_2016_2025`; original SPY datasets
retain their own paths and identities. See the [input contract](extension_plan.md)
for eligibility, prior exposure and source-resolution assumptions.

## Extension panel acquisition and registration

Existing accepted archives do not need reacquisition. A fresh reproduction uses
the native capability/acquisition applications, retained raw HTTP envelopes,
issuer evidence and source-resolution policy. Run the readiness verifier before
registration; missing evidence stops the finalizer rather than silently changing
inputs. The action preparer retains original cached files.

```powershell
& .\scratch\native\Release\panel_acquire.exe
py -3 -B research/prepare_panel_actions.py
& .\scratch\native\Release\panel_finalize.exe
py -3 -B research/verify_historical_panel.py scratch/datasets/extension_panel_2016_2025
```

The acquisition registration fixes 2016-2025. It does not authorize later-year
strategy scoring. Do not overwrite a registered archive with a new provider
vintage. Review exports and coverage metadata can be shared; raw pages and
credentials remain local.

## Causal panel replay (M3)

`panel_replay` authenticates registered inputs and emits prior-only decisions.
Its independent oracle checks source observations, history cutoffs, targets and
numerical conditioning. A reversion equilibrium near a unit slope can be
ill-conditioned even when primitive coefficients agree; the oracle retains its
primitive tolerances and exact target checks.

## Shared-capital diagnostic (M4)

Run `shared_capital_diagnostic`, then its verifier. The [contract](shared_capital.md)
separates global cash from owner allocation and defines netting and restart.

## Independent broker process (M5)

Run `process_broker_diagnostic`, which launches `synthetic_broker_peer`
automatically. Two malformed-frame children intentionally stop; their parent
checks those failures. All activity is synthetic and local. The [process note](process_broker.md)
defines the durable independent peer and twelve scenario families.

## Cost and execution inputs (M6)

`cost_execution_check` uses constructed pagination and quote-failure fixtures.
`cost_quote_acquire` resumes its fixed pre-2024 sample without altering committed
pages. A denied/empty disposition remains unestimated. Its `--render <summary>`
mode produces plots from saved summaries; `--inspect <summary>` reauthenticates
cached conditions and timestamp conflicts without acquisition. Both modes retain
the registered filter. The [cost note](cost_execution.md) is the method authority.

## Cached quote-reference comparison (M6)

Run `quote_reference_development <quote-summary>` for the retained sample, or
`--check` for constructed data. `verify_quote_reference.py` checks raw evidence,
comparison identity, crossing costs, delay pairs and plotted marks.

## Historical cost and delay evaluation (M6)

Run `cost_delay_development --check`, verify its printed directory with
`verify_cost_delay.py --self-check`, then omit `--check` for registered historical
inputs through 2023. A completed native run is retained if only verification
fails; rerun the corrected verifier against that directory.

## Synthetic execution study (M6)

Run `cost_execution_study`, then `verify_cost_execution.py <directory> --self-check`.
The fixed study produces 680 account cases and 510 matched comparisons. It uses
neither historical acquisition nor an external broker.

## Continuous risk and shared-capital evaluation (M6)

Run `panel_account_development --check` and verify with
`verify_panel_accounts.py <directory> --self-check`. Omit `--check` for historical
inputs. The 200 risk and fifty shared cases retain stopped outcomes; later
forecast-history recovery cannot restart capital paths.

## Primary stability and uncertainty (M7)

`stability_development <cost-delay-directory> <account-directory>` authenticates
saved records. Its independent verifier checks full-date attribution and all
four paired block lengths. `--check` produces analytic source records.

## Secondary stability and risk matching (M7)

Run `stability_extension_development --check` and its verifier before a historical
reproduction. Omitting `--check` retains all fourteen registered variations,
45 unit accounts and fifty funded matched accounts. No variant is selected from
its result.

## Controlled inference calibration (M8)

`inference_calibration --check` is a short engineering fixture. The full command,
without arguments, runs 20,000 joint-family trials and is independently verified
with `verify_inference_calibration.py <directory> --self-check`. The short fixture
cannot authorize provider validation. Reuse the accepted full calibration when
the registered statistical core is unchanged.

## Integrated validation and freeze (M8)

These are reproduction steps, not actions required after accepted evaluation.
Use actual printed directories. Verify before freezing the exact executable.

```powershell
& .\scratch\native\Release\panel_evaluation.exe --validation <verified-calibration-directory>
py -3 -B research/verify_panel_evaluation.py <validation-directory> --self-check
& .\scratch\native\Release\panel_evaluation.exe --freeze <validation-directory>
py -3 -B research/verify_panel_evaluation.py <freeze-directory> --freeze
```

## Frozen final evaluation (M9)

The accepted run is complete. Reproduction must use the retained copied runner
and its verified replacement freeze, not a newly built executable. The corrected
2024 output and original accepted freeze remain part of the maintenance lineage.

```powershell
py -3 -B research/verify_panel_evaluation.py <freeze-directory> --freeze
& <freeze-directory>\panel_evaluation.exe --final <freeze-directory>
py -3 -B research/verify_panel_evaluation.py <final-directory> --self-check
```

### Cross-year payment-date maintenance

The original M8 runner used a bounded price-calendar parser for payment metadata.
The accepted replacement separates civil payment dates, preserves receivables
and rejects 2026 market sessions. Its 2024 output passed exact parity of all
26 financial artifact identities and four scientific summaries before freezing.
The [acceptance record](validation.md#windows-provider-2025-final-acceptance)
retains both freeze identities. No replacement is needed after acceptance.

### Resume a completed final run after verifier failure

Run only `verify_panel_evaluation.py <completed-directory> --self-check` after a
verifier correction. Do not repeat completed native replays, calibration or
freeze construction. The final receipt binds the actual copied runner and
verifier; the full source/ledger checks precede receipt creation.

### Retained evidence and scratch storage

Keep registered datasets, calibration, original/replacement validation, copied
runners and accepted final output. Their paths and hashes establish reproduction.
Generated object files are rebuildable; old CMake output under `scratch/build`
is not used. Compact review bundles omit detailed raw records and cannot replace
local evidence. No blanket deletion of JSON/JSONL/CSV files is valid.

## Publication

The committed PDF and standalone HTML contain the full study. The HTML embeds
saved event views and images and opens without a server or script. Install the
pinned display dependencies in an existing environment:

```powershell
py -3 -m pip install -r research/report_requirements.txt
py -3 -B research/build_report.py --demo <verified-demo-directory>
py -3 -B research/verify_publication.py
```

One content model in `docs/report_source.json` feeds both formats. The evidence
manifest binds all curated tables; the publication manifest binds renderer,
content and outputs. Text uses UTF-8/LF hashes; original records and binary PDFs
retain raw-byte identities. Publication formatting does not rerun research.
