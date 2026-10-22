# Daily-risk data and accounting preflight

This checkpoint implements the feasibility work proposed in the
[SPY-RISK-01 brief](intraday_risk_allocation_brief.md). It does not register the
candidate, estimate risk forecasts, generate target weights or score a strategy.
The earlier intraday studies and their frozen inputs retain their existing roles.

## Coverage contract

`risk_preflight` verifies the existing development study before inspecting all
501 exchange sessions in the 24 retained 2022–2023 calendar snapshots. It reuses
the policy-identified incomplete June session and, when retained, the original
January diagnostic or shortened-session evidence. Every reused session is loaded
through the raw-page, CSV and metadata verifier. No evidence is rewritten.

The coverage table distinguishes three requirements:

| Requirement | References |
| --- | --- |
| Full minute grid | Every actual regular-session minute, using the calendar's shortened close where applicable |
| Proposed account references | Session open, 09:31 and 09:32 minute opens, final regular-session minute close |
| Proposed RV5 endpoints | Session open and each five-minute interval close, including the final close |

An interval ending at 09:55 uses the close of the minute bar starting at 09:54.
The June gap therefore blocks a risk endpoint even though its morning and closing
account references remain present. Complete endpoints do not establish a complete
minute grid. Prior closing references and corporate actions are further inputs;
an overnight account cannot delete economic exposure on a date excluded from a
different intraday experiment. The preflight does not fill gaps or select a
missing-risk policy.

Full 2021 daily calibration and the leading twenty exchange sessions of intraday
history remain outside this verified input contract. Their absence from that
contract is not a claim that no unrelated files exist on the machine.

## Account contract

`LongCashAccount` owns the cash, fractional shares, unpaid USD dividend claims,
action identities and chronological event ledger. It uses raw prices, zero cash
interest, immediate reuse of sale proceeds and no borrowing. Taxes, whole-share
constraints, settlement delays, market impact and broker execution are outside
this analytical account.

Let liquid wealth be `L = cash + shares * price`, stock value be `S`, target
liquid stock weight be `w` and per-side proportional cost be `c`. A buy trades
`B = (wL - S) / (1 + cw)`; a sale trades
`A = (S - wL) / (1 - cw)`. The fee is `c` times the actual nonnegative traded
notional. The resulting stock value is `w` times post-cost liquid wealth. Thus
an unchanged target can require a trade after price drift; a flat account incurs
no fictitious fee. Small negative cash/stock residuals within 32 machine epsilons
of liquid wealth are rounded to zero solely for floating-point conservation.

Dividend entitlement is recognized at the start of the ex-date from the shares
already held. A sale afterward retains that claim. A sale beforehand does not
earn it. Claims enter equity immediately, remain unavailable for purchases and
transfer to cash on payment without a second income credit. Due payments must
be settled before rebalancing. A terminal unpaid claim remains separately valued
at face value. Duplicate action IDs and backward events are rejected.

A split scales shares and inverse quote units while leaving cash and existing
USD claims unchanged. Supported actions occur at the start of their effective
UTC date, before same-time trades. Same-date splits precede dividend entitlement;
the supplied dividend amount must use the resulting share units. This is an
explicit synthetic ordering contract. Actual same-date actions and any unsupported
distribution type require reconciliation before constructing a historical ledger.

Eight analytical identities appear in the report. The contract suite additionally
checks partial rebalances against an independent bisection of the budget equation,
overnight marking, the existing fully invested round-trip limit, cash constraints,
reverse splits, chronology, invalid inputs, scale and overflow. Passing those
checks establishes the implemented arithmetic within its stated scope.

## Corporate-action access

On Windows the same launcher reads the existing Alpaca paper credential entry
and makes three read-only requests to the
[corporate-actions endpoint](https://docs.alpaca.markets/us/reference/corporateactions-1),
one process-date interval for each year from 2021 through 2023. It retains the
status, path, receipt time, raw body and SHA-256. Each request has a 1,000-record
limit, includes all action types and uses `data_quality=complete`. The existing
transport retains its TLS, response-size, timeout, redirect and HTTP-429 controls.

This bounded capability probe retains one page per year. A continuation token is
reported explicitly; pages are not treated as a complete action history. An HTTP
200 response establishes access, not ex-date coverage, correct distribution totals
or point-in-time announcement availability. Authentication/subscription rejection,
malformed JSON and transport failure remain explicit evidence. The report is
still produced when the endpoint is unavailable. Linux runs the same offline
audit and records that live endpoint access was not checked.

The provider documents inclusive process-date filters, incomplete-record handling
and possible processing delays. Those limits require a separate action-ledger
reconciliation. The [paper-trading documentation](https://docs.alpaca.markets/us/docs/paper-trading)
also states that dividends are not simulated in paper accounts. A broker paper
balance therefore cannot serve as the dividend-accounting oracle.

## Output and remaining work

Each invocation creates a new `scratch/runs/SPY_RISK_01_preflight_*` directory:

| File | Contents |
| --- | --- |
| `report.html` | Calendar coverage chart, unresolved sessions, access results, synthetic identities and ledger |
| `coverage.csv` | All exchange sessions, endpoint flags, missing endpoints and canonical bar hashes |
| `synthetic_ledger.csv` | Constructed chronological account events, cash, shares, claims, equity and fees |
| `accounting_checks.csv` | Analytical expected values and absolute tolerances |
| `corporate_actions_*_response.txt` | Raw response bodies when a Windows probe completes |
| `summary.json` | Publication marker, input/calendar/artifact hashes, access evidence, build identity and blockers |

`blocked_before_registration` is an expected research status, not an application
failure. A successful process exit means the audit and synthetic checks completed.
Acquisition of the additional calendar/price history, distribution reconciliation,
the missing-risk policy and protocol registration remain prerequisites for any
candidate scoring. Local 2024–2025 candidate performance remains unopened.
