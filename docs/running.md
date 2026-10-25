# Build and reproduction

Paths are anchored to the source root. Runnable studies and demos create a new
`scratch/runs/<name>_<suffix>/` directory and preserve earlier outputs. Initial
configuration downloads nlohmann/json 3.12.0 with a pinned SHA-256. Builds are C++20
with project warnings as errors.

## Windows

Open the repository folder in Visual Studio 2026 with C++/CMake tools and a Windows
SDK. Select `windows-x64` and `windows-release`, then **Build > Build All**.
Choose **Test > Run Test Preset > windows-release** for all 29 routine groups.
Select **project_demo.exe**, then **Ctrl+F5**, and open its printed HTML report.

Everyday changes use `windows-debug` and **windows-debug-quick**, or the matching
Release quick preset. Quick presets run 25 groups, excluding four `extended`
study/disk groups. Changes to dataset/batch/risk code also require the affected
extended group; use the full preset at checkpoints. `windows-debug-full`
additionally includes the optional full-period synthetic disk integration test.
A first Release build compiles its own configuration under `scratch/build/Release`.

**Git > Pull** updates the existing tracking branch. Reconfiguration refreshes
supported targets. Private datasets and previous run directories remain local.

## Linux

Requirements: CMake 3.25+, a C++20 compiler, Make and OpenSSL development libraries.

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure
scratch/build-linux-release/project_demo
python -B research/verify_project_demo.py scratch/runs/project_demo_0
```

`linux-debug` and `linux-release` each have full and `-quick` test presets.
Use the actual printed output suffix. The full 144-case execution grid and
120-row benchmark are separate runnable workloads, not repeated by every edit:

```sh
scratch/build-linux-release/execution_measurement
python -B research/verify_execution_measurement.py <printed-directory>
scratch/build-linux-release/runtime_benchmark
python -B research/verify_runtime_benchmark.py <printed-directory>
```

## Supported entry points

| Target | Purpose / additional prerequisites |
| --- | --- |
| `project_demo` | Offline strategy decisions, account parity, broker lifecycle and recovery; no credentials/data |
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
offline completion scope. No local 2024-2025 strategy evaluation is authorized.

Historical exports have independent checks in `research/verify_*.py`. Risk,
reversion and synthetic sampling oracles may require NumPy; their original method
and numerical conventions are recorded beside the governing protocols.
Historical final bars cannot reproduce original live receipt/revision timestamps.
The report's curated result snapshot reproduces figures/tables, not unbundled raw
prices or historical daily paths.

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
output hashes. `research/verify_publication.py` checks these identities, local
links, embedded-view controls and static asset boundaries without private data.

## CI

`.github/workflows/offline-ci.yml` runs on code/research/workflow pushes to the
working/default branches or manual dispatch. Standard Windows/Linux Release
jobs build the full project, run routine contracts and independently verify the
offline demo and small benchmark export. Ubuntu ASan/UBSan covers execution and
recovery with leak detection. General documentation-only edits are skipped;
research protocols remain covered. Actions are commit-pinned with read-only
permissions. No credentials, paid runner or timing gate is used. Hosted availability
and the account's included quota remain external constraints.
