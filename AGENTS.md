# Repository instructions

Before substantive code changes, verify that the `build-pristine-code` skill is
available, then read and apply it. The authoritative implementation is C++20
under `src/`, built by the root CMake configuration.
Application and reporting layers call that implementation; do not duplicate
signal or financial calculations in presentation code.

## Research boundaries

- `research/intraday_momentum_protocol.md` and
  `research/development_data_policy.json` identify the frozen SPY-IM-01 study.
  Preserve their bytes and all retained acquisition evidence.
- `research/intraday_continuation_protocol.md` specifies SPY-TOD-01. Changes to
  its signal, data eligibility, costs or gates require a declared revision before
  scoring. Historical development uses the verified 496-session snapshot,
  twenty warm-up sessions and 476 scored paired records.
- Synthetic diagnostic traces are not historical strategy results. No new
  historical score or profitable-strategy claim follows from passing contracts.
- The frozen 2022-2023 development sample and unopened local 2024-2025 candidate
  results have different roles. Record external-literature exposure as specified
  in the protocol. Do not access later performance casually during a code fix.

## Build and verification

Windows uses the existing Visual Studio `windows-x64` configure preset and
`windows-debug` build/test presets. The routine test preset excludes the labelled
full-period integration test. `continuation_diagnostic` runs offline and writes
HTML/CSV under `scratch/runs/`. `continuation_development` verifies the frozen
snapshot and generates the paired historical study with HTML/SVG, CSV and JSON.
The routine presets run ten contract groups; full-period disk integration stays
optional.

On Linux, use `cmake --preset linux-debug`, `cmake --build --preset linux-debug`
and `ctest --preset linux-debug`. Keep warnings as errors. For a targeted change,
run its contracts and relevant existing regression checks.

## Source and runtime artifacts

Keep generated builds, datasets, reports, local checks and Git transfer bundles
under the ignored `scratch/` directory. `.vs/` and `CMakeUserPresets.json` remain
local. Preserve expensive data and results during cleanup. Stage explicit source
paths; never include credentials, data archives or generated build trees in a
source commit. Respect the established remote and task-branch workflow.
