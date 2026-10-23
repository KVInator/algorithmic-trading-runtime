# Completion milestone ledger

Updated 23 October 2026. The [completion plan](completion_plan.md) governs scope
and acceptance. This ledger distinguishes implemented foundations, completed
substeps and open end-to-end milestones; contract counts are verification
evidence rather than a completion percentage.

| Milestone | Status | Evidence and remaining acceptance |
| --- | --- | --- |
| 1. Native integration checkpoint | Open: pipeline diagnostic artifacts pending | The current twenty-group Windows test log and reversion preflight are verified. The combined pipeline diagnostic still needs its native report/journals; passing its contract group does not supply those artifacts. |
| 2. Strategy research | In progress: reversion specification/preflight | Two trend-oriented designs and the separate daily-risk study are retained. The new reversion brief, numerical kernel and structural coverage audit are implemented. Final registration, uncertainty/stability experiments, prior-only history, decisions, controls and scored study remain. |
| 3. Shared execution path | Partially implemented | The original momentum adapter has synthetic fill/restart evidence. Continuation/reversion adapters, historical reference replay and account-convention comparisons remain. |
| 4. Paper broker | Not implemented | Broker-generated order events, transport semantics, TIF/expiry, operational capture and observed reconciliation remain. |
| 5. Execution measurement | Not implemented | Quote/event capture, timestamp alignment, execution-cost/fill/delay tables and the declared paper observation window remain. |
| 6. C++ performance and CI | Partially implemented verification foundation | Existing contract and sanitizer checks are available. Formal transition/invariant explanation, Release benchmarks and portable CI remain. |
| 7. Final technical publication | Not complete | Individual reports and research notes exist. The unified analytical HTML/PDF report, offline showcase and final repository presentation remain. |

## Current checkpoint: reversion specification and feasibility

SPY-MR-01 is proposed and unscored. The [brief](../research/intraday_reversion_brief.md)
defines a fixed opening reference, a noon decision, prior-session estimation
and common-window controls. It records the difference between conditional OU
interpretation and empirical evidence of stationary residuals.

The authoritative C++ measurement accepts exactly 150 completed minutes,
produces 25 five-minute closing residuals and never updates its opening reference
after 10:00. Pooled OLS constructs only within-session transitions and keeps
unrestricted coefficients when a stationary interpretation is unavailable.
The historical audit checks data coverage and positive opening volume only.

GCC Debug builds with warnings as errors and all twenty routine contract groups
pass. The new contract group also passes AddressSanitizer/UndefinedBehaviorSanitizer
with leak detection disabled. The default preflight passes 22 constructed checks,
verifies all 496 frozen sessions and identifies 476 dates with the proposed inputs
after warm-up. It records zero historical model fits and zero strategy scores.

An independent 50-digit Decimal calculation reproduces all four unrestricted
synthetic fits and their residual variances. Separate checks reproduce the opening
reference and residual samples, validate the five artifact hashes against the
149-file source inventory and check all 496 coverage cells and report links.
These are numerical and structural checks, not finite-sample estimator or
historical profitability evidence. Native verification of this preflight is
recorded below; the separate pipeline diagnostic remains open.

## Windows reversion verification

For implementation revision `8e19546`, the MSVC 19.51.36260.0 Debug build was
reported successful on 23 October 2026. The supplied console log records all
twenty routine groups passing in 307.50 seconds. The separate native preflight
passes all 22 checks, verifies 496 sessions and records 476 input-ready dates,
zero historical model fits and zero strategy scores. Its frozen study-manifest
identity matches the Linux run.

The uploaded summary SHA-256 is
`3b69053b03f48a44ebbec765fb356d4b6783a858af35dbbd0f80509c42c1bf04`.
All five artifact hashes match. Coverage and the exact synthetic prefix are
byte-identical to Linux. All four controls retain identical statuses and input
sequences; fitted numerical fields agree within relative tolerance 1e-12 and
absolute tolerance 1e-15. The largest absolute parameter difference is
approximately 1.07e-14. The 149-entry source inventory has 141 exact byte matches
and eight exact CRLF variants, with no unexplained source differences.

The supplied timing log attributes 175.19 seconds to `daily_risk_study_contracts`
and 101.46 seconds to `risk_input_contracts`, together 89.97% of the suite.
These groups exercise full-calendar synthetic study/replay and persisted
acquisition/reload scenarios. The new reversion contract takes 0.19 seconds.
The preflight's frozen-input verification completes at 188.2 seconds; the
log does not isolate disk, decoder, hashing or other system costs. Build timings
were not supplied, so the cause of the reported slow build is not established.
No performance improvement is inferred from successful correctness checks.

## Next dependency

Complete finite-sample and observation-noise controls for estimator/interval
behavior, define session-level uncertainty and failure conventions, then
register the final protocol and machine-readable settings before historical
model fitting. Subsequent work implements the prior-only history, decision,
control accounts and scored comparison. A negative empirical result does not
reopen frozen rules or trigger an additional strategy search.

Native pipeline verification can proceed separately through Build All,
the current windows-debug routine preset and pipeline_diagnostic. The new
reversion_preflight performs only the documented unscored audit.
