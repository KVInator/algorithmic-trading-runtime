# Repository instructions

Apply the available `build-pristine-code` skill to changes. C++20 under `src/`
is authoritative for models, accounts and state transitions; applications compose
those owners and reports bind their results. The final scope and acceptance map
are in `docs/verification.md`; the analytical publication is `docs/report.pdf`
and `docs/report.html`. Do not reopen completed strategy selection during cleanup.

## Research boundaries

Preserve the exact registered bytes of:

- `research/intraday_momentum_protocol.md` and `development_data_policy.json`;
- `research/intraday_continuation_protocol.md`;
- `research/daily_risk_protocol.md` and `.json`;
- `research/intraday_reversion_protocol.md` and `.json`.

Preserve retained acquisition, exclusion/gap evidence and study-manifest identities.
Historical intraday studies use the verified 496-session 2022-2023 snapshot;
continuation/reversion have twenty warm-up sessions and 476 scored records.
Risk retains all 501 exchange dates and its fixed daily-only fallback. Dated
failures and undefined ranges remain visible. Registered sampling budgets and
criteria cannot be shortened or changed in a production historical run.
Parameter ranges are descriptive, not a stationarity filter. Local 2024-2025
strategy prices and performance remain unopened. Synthetic scenarios cannot be
labelled historical strategy returns or observed broker executions.

## Architecture and invariants

Use cohesive classes, explicit units and ownership, thin entry points and one
calculation per domain behavior. Result records are values; report objects borrow
const results and must not outlive them. Text encoding belongs to `ReportText`.
Publication code may format/display recorded evidence but cannot become another
signal, financial-account or statistical-scoring implementation.

Conserve cash/share reservations, deduplicate executions, retain pending-cancel
resources and require exact reconciliation after contradiction/restart. Synchronize
journal inputs before publication/dispatch eligibility; preserve exclusive writers.
Reject complete corruption; copy incomplete verified prefixes into separate files.
Never truncate original evidence or blindly resend uncertain durable orders.
Legacy schemas 1/2 remain readable; schema 3 declares USD 1e-9. Preserve legacy
JSON cent meanings. Matching uses independent peer state and finite quote budgets.
Historical Alpaca HTTP/credentials remain supported; native broker services and
paid/current-data probes are outside scope.

## Verification and artifacts

`docs/running.md` defines supported targets and prerequisites. Full Windows/Linux
Debug/Release test presets run 29 routine groups, excluding optional integration.
Matching `-quick` presets run 25 groups, excluding four extended study/disk groups.
Run quick/affected contracts during edits and relevant extended groups when their
code changes. Run the full routine suite at substantive checkpoints. Keep warnings
as errors. Execution/recovery changes also require available sanitizer checks.

Run `project_demo` and its independent verifier for the complete constructed
workflow. Run the full measurement/benchmark grids only when those calculations
change or fresh measured evidence is needed. Timings are host observations, not
deterministic test constants. Publication changes require hash/link checks and
rendered PDF/figure inspection. Preserve source and evidence provenance.

Keep builds, private inputs, temporary analysis and full generated runs under
ignored `scratch/`. Retain expensive existing datasets/results. Curate only small,
permitted publication evidence outside it. Do not add obsolete launchers, duplicate
reports, temporary exports or generic progress checklists to the final tree.

## Authorized publication

Standing authorization covers focused commits, normal non-force pushes to
`KVInator/algorithmic-trading-runtime` on `research/continuation-study`, and the
corresponding pull-request updates. Complete verified eligible work without
renewed approval for those operations. This does not authorize merges, releases,
visibility changes, live orders, history rewrites or publication of credentials/
private datasets. Platform approval controls remain applicable.
