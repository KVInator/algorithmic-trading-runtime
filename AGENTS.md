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

## Architecture and verification

Substantive domain, diagnostic and reporting workflows use cohesive classes and
explicit composition. Results are value records; report objects borrow const
results and must not outlive them. Keep entry points thin and numerical engines
authoritative. Shared report text encoding belongs to `ReportText`.
The responsibility and ownership map is in `docs/architecture.md`.

Build/test presets and target prerequisites are documented in `docs/running.md`.
Windows uses `windows-x64` and `windows-debug`; Linux uses `linux-debug` for
configure/build/test; `linux-release` supplies the optimized benchmark build.
Full routine presets run twenty-nine groups and exclude
optional full-period disk integration. The matching `*-quick` test presets run
twenty-five groups, excluding four `extended` study/disk regressions. Use quick and
relevant affected contracts during edits; changes to excluded study/storage code
also require its extended group. Run the complete routine preset at a milestone.
Keep warnings as errors.

Execution changes must conserve reservations, deduplicate events/executions and
require exact reconciliation after contradictions or restart. Journal before
dispatch eligibility, preserve exclusive writer ownership, reject complete-frame
corruption and recover incomplete tails into a separate verified-prefix file.
Never truncate damaged evidence or automatically resend uncertain durable orders.
Legacy schema-1 journals remain readable; target inputs require schema 2.
The paper bridge uses schema 3 with USD 1e-9 units and an explicit paper fee
policy. Preserve legacy JSON cent keys while C++ core fields use neutral units.
Its accounting/persistence contract is in `docs/paper_execution.md`.
The simulated broker, logical health/dispatch gates and separate deadline/stop
owner are specified in `docs/paper_broker.md`. No native paper-broker wire,
current quote/calendar probe or paid feed is required. Keep historical Alpaca
acquisition and credentials intact. Synthetic messages must never be labelled
observed broker evidence.
Milestone-5 measurement scheduling and signed cost conventions are defined in
`docs/execution_measurement.md`. Keep source/receipt virtual times distinct from
wall-clock benchmarks. The full measurement grid runs in its executable; routine
contracts use a small constructed subset.
Milestone-6 timing boundaries, storage modes, resource limits and CI scope are in
`docs/runtime_benchmark.md`. Preserve measured clock samples; do not compare host
timings as deterministic checks or confuse them with virtual delivery schedules.
The runtime, journal and pipeline contracts in `docs/` define these invariants.

`research/intraday_reversion_protocol.md` and its JSON register SPY-MR-01
revision 1 after synthetic sampling/noise validation. Preserve their bytes.
The registered historical runner authenticates protocol/settings and the frozen
496-session snapshot before prior-only fitting/scoring. Keep all 476 scored dates,
499 parameter refits at blocks 1/5 and 10,000 paired outcome resamples at blocks 5/10.
`reversion_preflight` only audits inputs; the synthetic diagnostic does not authorize later-period access.
Conditional parameter ranges are descriptive and cannot filter entries or
establish stationarity. Preserve all registered failure dates and raw fit statuses.

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
