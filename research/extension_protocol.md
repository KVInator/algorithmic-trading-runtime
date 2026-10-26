# Multi-instrument dataset and study contract

Study: **MULTI-ASSET-01**, registration edition 1, 26 October 2026.
The [registered JSON](extension_protocol.json) is the numerical authority;
`build.py` pins its SHA-256 and the native loader rejects different bytes.
This contract precedes expanded strategy scoring. It does not change the v1
protocols, failed advancement screens or original acquisition snapshots.

## Data, chronology and eligibility

The fixed universe is SPY, QQQ, IWM, TLT and GLD. Acquire 2016-2025 raw,
USD-denominated, unmapped one-minute SIP bars in ascending order. Preserve
source timestamps, exact requests, retrieval UTC, response bytes, HTTP attempts,
pagination and content hashes. The independent NYSE reference includes shortened
sessions, the 2018-12-05 and 2025-01-09 closures, and Juneteenth from 2022.
The fixed-date independent verifier checks the production calendar separately.

2016 initializes histories. 2017-2021 are retrospective transfer years;
2022-2023 remain selection-exposed references. 2024 validation opens at M8;
2025 final retrospective evaluation opens at M9 after the executable freeze.
GLD has known earlier research exposure and other exposure remains qualified
as recorded in the [exposure inventory](../docs/extension_plan.md#research-exposure-and-evaluation-roles).
No globally untouched panel is claimed.

During M2, acquisition/authentication may decode every year automatically but
emit only dates, availability, counts, masks and hashes. Reserved response
bodies stay under `sealed/`. The public native price reader rejects 2024-2025
before any filesystem lookup. It requires a caller-supplied acquisition hash
and authenticates the month, checkpoint and raw-page hierarchy. Future stage
access requires a separate implemented gate; passing a later date is insufficient.

| Input | Registered treatment |
| --- | --- |
| Primary intraday session | Full 09:30-16:00 session and complete expected minute grid |
| Missing minute | Retain missing UTC slot; no OHLCV imputation, replacement date or inferred zero volume |
| Shortened session | Retain raw inputs and daily marks; exclude primary intraday comparisons |
| Existing diagnostic | SPY 2023-01-03 excluded; other instruments retain their ordinary eligibility |
| Common comparison | Intersection of five-symbol eligibility on the original exchange-date index |
| Continuation initialization | Twenty earlier eligible full sessions; never the current unfinished session |
| Reversion initialization | Twenty earlier regular calendar slots; missing/excluded slots stay occupied |
| Daily-risk initialization | 252 earlier returns, requiring 253 consecutive actual closes; no invented pre-2016 boundary |
| Required account mark | An absent required endpoint blocks the affected continuous account; it does not become zero P&L |
| RV fallback | Preserve the registered v1 daily-return fallback and flag its use; do not fabricate intraday RV |

The registered first scored *input* dates are computed per symbol/family from
these masks after acquisition. They are not evidence of an admissible fit or
strategy activation. Model/forecast availability and the 252-unit-return overlay
warm-up are evaluated by the causal replay at M3/M7. Histories continue across
year boundaries; neither account capital nor positions reset annually.
After twenty reversion calendar slots exist, incomplete history remains an
explicit fit-unavailable cash decision, rather than deleting that date from
the candidate/control comparison. History completeness is a separate mask.

At least five 2017-2023 development years must each retain 200 common regular
dates and 80% of their regular calendar dates for the intended broad development
scope. An input-limited result remains a valid project outcome; changing the
universe or window requires an explicit contract revision before scoring.

## Acquisition and immutable identities

`HistoricalPanelDataset` reuses `SessionDownloader`, the raw-page decoder and
calendar/request value objects. It does not create another strategy engine.
Each returned HTTP attempt is archived before decoding; successful pages are
reused on resumption. Each invocation makes at most three attempts per request, with
the existing 750-ms pacing and bounded 429/transient-server retries. Terminal
HTTP failures stop collection, preserving their diagnosis and completed pages.
Only retained transient 429/500/502/503/504 failures may be retried by a later
invocation, up to nine retained attempts in total. Persistent authentication or
entitlement failures are never retried merely by reopening the application.
An interrupted page is retried; earlier completed pages are not downloaded again.
No concurrent writers may operate on one dataset directory.

The default destination is
`scratch/datasets/extension_panel_2016_2025/`. The dataset contract pins the
universe, period, origin and protocol. Per-session checkpoints authenticate
pages and availability; monthly records authenticate checkpoints. The acquisition
manifest is published last. Gapped sessions may belong to an authenticated
acquisition; they remain ineligible under their recorded masks.

The fixed plan has **2,514 exchange dates**, **12,570 symbol-days** and roughly
50,000 bar pages at 100 rows/page, plus calendars/actions. Pacing alone is about
10.5 hours; network and disk work add time. This is an unattended checkpoint
workload, not a routine test. Closing/interruption preserves completed responses.
No price response is overwritten to obtain a cleaner vintage.

`panel_finalize` requires a complete archive, action reconciliation and the
original authenticated SPY study and daily-risk acquisition. The vintage bridge
retains all 496 original scored dates and the 256 original calibration/supplement
windows, and compares common OHLCV records and missing timestamps without
calculating strategy returns. The new vendor vintage never replaces the old
snapshot. The final study manifest pins acquisition, actions, issuer reference,
eligibility masks, bridge, protocol and build-source identities.

## Corporate actions and primary sources

Raw provider actions are paginated by process date for every year and through
2026-03-31 for the final boundary tail. This does not establish complete ex-date
coverage or historical announcement availability.

Authenticated issuer evidence is the authority for accounting distributions.
Issuer-only records and dates absent from provider responses are retained with
explicit provenance and counts. Nonempty provider/issuer date or amount
disagreements block reconciliation. Same-ex-date cash components must be
explicitly consolidated; contradictory records are not silently averaged.
The amount tolerance is USD 0.000001/share, not a performance tolerance.

For an entitled quantity $q$ and distribution $D$, recognize a receivable $qD$
on the ex-date and transfer that receivable to cash on the payment date. Wealth
includes cash, marked inventory and receivables. Payment is a transfer, not a
second income event. Splits require authenticated ratios and effective dates;
past price/share-volume features must be expressed on the current share basis
using only already-effective actions. Unsupported actions block the affected
account until their treatment is implemented and registered.

The preparer uses free primary sources and reports only retained record counts:

- SPY: [State Street historical distributions](https://www.ssga.com/us/en/intermediary/resources/documents/etf-dividend-distributions),
  using the existing issuer-workbook reader, including cash/capital-gain components.
- QQQ: [Invesco product page](https://www.invesco.com/us/en/financial-products/etfs/invesco-qqq-trust-series-1.html)
  and its publicly advertised distribution API for CUSIP 46090E103.
- IWM: [iShares product page](https://www.ishares.com/us/products/239710/ishares-russell-2000-etf),
  extracting only its embedded distribution table.
- TLT: [iShares product page](https://www.ishares.com/us/products/239454/ishares-20-year-treasury-bond-etf),
  using the same table adapter.

Source pages/workbooks and provenance are retained locally under
`issuer/sealed_sources/`. Price/performance sections of product pages are not
used as study inputs or emitted in review evidence. Availability of a dividend
table does not establish split history, and an empty provider response does not
prove GLD has no actions. The preparer deliberately leaves those coverage
declarations incomplete. They cannot be changed to complete without an
identified independent source and source location supporting the whole horizon.

The local `issuer/reference.json` schema contains:

| Field | Required content |
| --- | --- |
| `schema_version`, `evidence_origin` | 1 and provider/synthetic; constructed evidence cannot authenticate provider inputs |
| `first_ex_date`, `last_ex_date` | 2016-01-01 and 2025-12-31 |
| `sources` | Relative file, primary-source URL, retrieval UTC and SHA-256 |
| `coverage` | One row per symbol, cash/split completeness, source files and coverage locator; empty histories need evidence too |
| `cash_distributions` | Symbol, ex/record/payable dates, total USD/share, source file and row/document locator |
| `splits` | Symbol, ex-date, old/new share ratios, source file and locator |

Schema flags express a reviewed coverage declaration; hashing a document alone
cannot prove its factual completeness. Missing coverage keeps registration
blocked. The generated reference and reserved financial fields stay local.

## Comparisons, pooling and materiality

The primary comparison family contains six endpoints, all with positive values
defined as candidate improvement. Primary reference execution retains a one-minute
delay and 1 bp per side. Costs 0/0.5/1/2/5 bp and delays one/two minutes are frozen
signal stresses. Historical quotes and synthetic matching remain separate later
evidence levels; they are not charged on top of an assumed all-in cost.

| Endpoint | Contrast | Minimum relevant increment |
| --- | --- | --- |
| Momentum | Signal-conditioned minus same-window always-long | 0.5 bp/calendar session |
| Continuation | Volume-conditioned minus price-only | 0.5 bp/calendar session |
| Reversion/sign | Candidate minus sign-only | 0.5 bp/calendar session |
| Reversion/drift | Candidate minus drift | 0.5 bp/calendar session |
| Daily-risk CER | Intraday-risk minus daily-only annual CER | 0.5 percentage points/year |
| Daily-risk QLIKE | Daily-only loss minus intraday-risk loss | 0.01 loss units |

The return threshold is one quarter of a 2-bp round-trip reference charge,
averaged over the full calendar, and corresponds to 1.26 annual percentage points
under mean-return annualization. The CER threshold is USD 500/year on the
USD 100,000 planning scale. These are declared research materiality settings,
not estimated execution costs or universal investment standards. QLIKE materiality
is in loss units and cannot be interpreted as a percentage return improvement.

Pool with fixed 20% instrument weights on common comparison dates. Form the
pooled return first, including cross-instrument covariance, then compute CER:
$252(\bar r-1.5s_r^2)$. Do not average instrument CERs/CAGRs or claim fivefold
independent sample size. QLIKE compares losses on the identical forecast/target
mask and is reported separately from allocation benefit. Retain cash, unconditional
controls, static risk budget and passive holding as declared contextual views.
An inactive valid session differs from unavailable input and stays in denominators.

## Uncertainty and adequacy

Use 10,000 non-circular moving-block draws on full date vectors, carrying all
instrument/endpoint masks together. Sample starts uniformly from 0 through
$T-L$, concatenate blocks and truncate the last block to $T$. Preserve gaps;
never compress valid observations into false adjacency. The seed is 290026;
the run identity records block length and a stable endpoint/case order.

Marginal intraday intervals use blocks 5 and 10; daily-risk intervals use 20
and 40. The simultaneous six-endpoint family uses a one-sided centered max-T
block procedure with block 40 primary and block 20 sensitivity, family alpha 0.05.
Recompute nonlinear CER in each draw. Use the original-sample Bartlett HAC
studentizer with $L-1$ lags, fixed across draws. For missing-mask endpoints,
use calendar-indexed centered influence contributions, zero on missing slots,
with $T/n$ weighting; masks and effective counts travel with each draw.

Center bootstrap increments at the observed estimate, take their standardized
maximum over all six endpoints, and use its 95th percentile for simultaneous
lower bounds. A supported material benefit requires its bound above the declared
material increment, adequate coverage and the registered exposure/cost assessment.
Zero/nonfinite studentizers or more than 1% undefined draws make the family
assessment unavailable. Report all such counts. Percentile bounds are not
relabeled as hypothesis-test p-values.

The final procedure still requires controlled coverage/size verification before
M8/M9; registration is not evidence that its implementation has passed. A window
needs 200 valid dates and 80% calendar coverage for the intended primary claim.
Active dates, trades and confidence-bound width remain visible; the old
100-active-date convention is not a universal power rule. Classify insufficient
precision as inconclusive and unavailable required inputs as input-limited.

`extension_precision.py` supplies a controlled one-year planning calculation.
For stationary daily noise $\sigma$ and AR(1) coefficient $\phi$,

$$
\operatorname{Var}(\bar X)=\frac{\sigma^2}{n^2}
\left[n+2\sum_{k=1}^{n-1}(n-k)\phi^k\right].
$$

Nine cases use 252 sessions, daily standard deviations 5/10/20 bp and
$\phi=0/0.3/0.7$. Each has 2,000 Gaussian trials checking mean variance and
power against analytical references. A conservative Bonferroni planning critical
value for six tests gives the additional mean increment above the null boundary
required for 80% power. These constructed values do not estimate market power
or validate max-T coverage. The [retained planning table](../docs/evidence/m2_precision.csv)
shows why one final year may remain imprecise even with a long historical archive.

## Remaining registered extensions

The JSON fixes the risk overlay, finite one-factor history/grid/holding-period
neighborhood, concentration removals and shared-capital budgets. Risk matching
uses 252 earlier unit-account returns and targets the lower prior volatility,
capped at 10% annually, without leverage; unavailable/zero estimates remain
unavailable. Replay sizing, costs and ledgers rather than rescaling realized P&L.
Secondary variants do not replace failed primary specifications.

The catalog contains fourteen additional cases, each at the primary cost/delay:
two momentum holding durations, two continuation histories, six reversion
history/grid/holding-duration cases, two daily-risk histories and two risk
measurement grids. Secondary parameters are not crossed. Momentum and
same-clock continuation aggregations that preserve their physical information
sets are parity controls, not independent strategy variants. Market-condition
terciles use the registered interpolation rule on earlier volatility history;
their labels remain descriptive.

Broker policy registration at M5 and the bounded quote/execution run matrix at
M6 precede their evaluations. Final statistical validation and executable/data
freeze occur at M8. This contract authorizes neither historical scoring at M2
nor early use of 2024/2025 outcomes. Project completion does not require profit
or a supported comparative benefit.

Calendar conventions follow [NYSE calendars](https://www.nyse.com/markets/hours-calendars),
the [2025 holiday/early-close announcement](https://ir.theice.com/press/news-details/2024/NYSE-Group-Announces-2025-2026-and-2027-Holiday-and-Early-Closings-Calendar/default.aspx)
and the [Carter closure notice](https://ir.theice.com/press/news-details/2024/The-New-York-Stock-Exchange-Will-Close-Markets-on-January-9-to-Honor-the-Passing-of-Former-President-Jimmy-Carter-on-National-Day-of-Mourning/default.aspx).
Endpoint conventions retain the primary references in
[data capability](../docs/running.md#extension-data-capability).
