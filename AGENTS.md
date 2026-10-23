# Repository instructions

Apply the `build-pristine-code` skill throughout this workflow, including
research, implementation, reviews, refactoring and technical documentation.
Verify that the skill is available and read its applicable instructions before
substantive work. The authoritative implementation is C++20
under `src/`, built by the root CMake configuration.
Application and reporting layers call that implementation; do not duplicate
signal or financial calculations in presentation code.

The remaining project scope and milestone dependencies are defined in
`docs/completion_plan.md`. Preserve dated candidate briefs as research history;
they do not supersede this completion plan. Its broker work is paper-only and
does not authorize later-period strategy access or changes to frozen results.
Milestone updates must identify the completed acceptance evidence, open plan
items and the next dependency. Test counts do not define a completion percentage.

## Technical language

Commit messages describe the concrete change in concise, conventional language.
Reports, comments and Markdown describe methods, evidence and limitations in an
impersonal technical voice. Omit chat instructions, promotional quality claims
and unsupported claims of profitability or production readiness. Preserve the
published commit history; improve wording in subsequent work.

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
- `research/daily_risk_protocol.md` and its JSON register SPY-RISK-01 revision 1.
  Verify the three registered input identities before any historical score.
  Preserve all dates, log daily-only fallback for incomplete RV5 histories, and
  use the full date sequence for paired uncertainty. Revisions must precede
  scoring of changed rules.
- The frozen 2022-2023 development sample and unopened local 2024-2025 candidate
  results have different roles. Record external-literature exposure as specified
  in the protocol. Do not access later performance casually during a code fix.

## Build and verification

Windows uses the existing Visual Studio `windows-x64` configure preset and
`windows-debug` build/test presets. The routine test preset excludes the labelled
full-period integration test. `continuation_diagnostic` runs offline and writes
HTML/CSV under `scratch/runs/`. `continuation_development` verifies the frozen
snapshot and generates the paired historical study with HTML/SVG, CSV and JSON.
The routine presets run twenty contract groups; full-period disk integration stays
optional.

`risk_preflight` audits the full 2022-2023 exchange calendar and synthetic
long/cash accounting. Its Windows corporate-action requests are access probes,
not a reconciled ledger. Preserve the distinction between observed coverage,
constructed account traces and strategy results. Its preflight does not itself
authorize scoring or later-period access; the registered protocol governs the study.

`risk_acquire` collects the isolated 2020 closing boundary, 2021 calibration,
twenty full warm-up sessions and three shortened-session supplements. Corporate-
action process dates extend through March 2024 solely for earlier entitlements.
Its issuer reference and acquisition inventory do not authorize strategy scoring.

`risk_development` is the offline SPY-RISK-01 experiment. It verifies registered
identities before measurement and retains every development account date. Its
routine contracts use explicitly synthetic full-calendar fixtures; successful
contracts do not establish historical scores. Run the historical entry point on
the machine retaining the completed isolated acquisition dataset.

`execution_diagnostic` exercises the single-symbol whole-share limit-order core
with synthetic broker messages. Its integer-cent cash/inventory model is separate
from historical fractional target-weight accounting. Preserve reservations until
terminal quantities reconcile, deduplicate both events and executions, and gate
new orders on contradictory reports or reconnect until an exact snapshot matches.
Keep this diagnostic offline; it requires neither datasets nor credentials.
The per-file source inventory is embedded with the configure-time aggregate.

`recovery_diagnostic` exercises the journaled wrapper with real synchronized
files, partial-fill restart, separate verified-prefix recovery and complete-frame
corruption rejection. Journal before dispatch eligibility, retain exclusive writer
ownership, replay through the existing order core and persist a disconnect barrier
on writer creation/recovery. Never truncate a damaged source or automatically resend uncertain
intents. The journal contract group also launches four abrupt-exit child processes.

`pipeline_diagnostic` connects the unchanged momentum signal to durable share
targets and a deterministic top-of-book simulator. It uses only constructed
bars and quotes. Pipeline journals use schema 2; legacy schema-1 journals remain
readable and cannot accept target inputs. Reconstruct a fresh client against the
same independent broker state, replay reports, and reconcile before routing.
Never resend an already durable order automatically, even if the broker snapshot
does not contain it. Keep these engineering fixtures separate from study scores.

`reversion_preflight` follows the proposed SPY-MR-01 brief. It fits constructed
controls and audits the frozen development input coverage without historical
residuals, model fits, forecasts or returns. Keep it unregistered/unscored until
the final protocol and instability/resampling conventions are fixed. The exact
synthetic fits do not establish statistical stationarity or SPY reversion.

On Linux, use `cmake --preset linux-debug`, `cmake --build --preset linux-debug`
and `ctest --preset linux-debug`. Keep warnings as errors. For a targeted change,
run its contracts and relevant existing regression checks.

## Source and runtime artifacts

Keep generated builds, datasets, reports, local checks and Git transfer bundles
under the ignored `scratch/` directory. `.vs/` and `CMakeUserPresets.json` remain
local. Preserve expensive data and results during cleanup. Stage explicit source
paths; never include credentials, data archives or generated build trees in a
source commit. Respect the established remote and task-branch workflow.

## Authorized publication workflow

Standing project authorization covers focused source/documentation commits,
normal non-force pushes to `KVInator/algorithmic-trading-runtime`, and updates to
the corresponding pull request. Continue on `research/continuation-study` for
the current milestone. Complete verification and publish eligible work without
requesting renewed approval for those operations. This authorization does not
extend to merges, releases, live orders, repository visibility changes, force
pushes or publication of credentials and local datasets. Platform approval
controls remain applicable.
