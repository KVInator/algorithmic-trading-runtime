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
output hashes. Distributed publication text uses UTF-8 with LF-normalized
checksums so Git newline conversion does not alter its logical identity; PDFs
use raw-byte checksums. Registered protocols are restored to their exact
registered LF bytes by the build only when their content is otherwise identical.
Acquisition pages, run journals and original result identities retain raw-byte
hashes. `research/verify_publication.py` checks these identities, local
links, embedded-view controls and static asset boundaries without private data.
