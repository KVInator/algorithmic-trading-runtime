# Cost and execution extension, edition 1

This M6 contract precedes the bounded quote acquisition and extended cost,
execution and account evaluation. It supplements MULTI-ASSET-01 without
changing its primary signals, comparison masks, input identities or reserved
2024/2025 strategy boundaries. No purchase or latest-data entitlement is needed.

## Evidence layers

1. Frozen bar-reference decisions: costs 0, 0.5, 1, 2 and 5 basis points per
   side, crossed with one- and two-minute execution delays. Signals, entry
   hurdles and histories remain those of the primary registered design. A cost
   stress changes execution/accounting, not the model fit or selected signals.
2. Historical SIP quotes: the fixed sample below supplies descriptive spread,
   age and displayed-size evidence. It is not an observed fill or an estimate of
   complete strategy implementation shortfall.
3. Synthetic matching: the fixed matrix below uses the existing five-symbol
   FIFO broker and explicit quote/liquidity/delivery assumptions. Its fee is the
   M5 engineering fee, not calibrated from historical quotes. Source-clock,
   outbound, receipt and cancellation delays remain distinct.

These layers are reported separately. Historical quote crossing costs replace
an all-in reference charge in their own comparison; they are never added to it.
Neither sampled quotes nor synthetic fills establish real broker execution.

## Fixed historical quote sample

The accepted M2 study and its mask identity are authenticated. For each year
2017-2023, select the earliest regular exchange date on which all five symbols
have operational inputs. Selection uses masks only, before any strategy outcome
or quote inspection. All five instruments remain: SPY, QQQ, IWM, TLT and GLD.
This is seven sampled dates and thirty-five instrument-days, not a random or
representative sample of every market condition.

Decision offsets are 30, 60, ..., 360 and 385 minutes from session open. Cross
these thirteen clocks with the registered one-/two-minute delays. For each
reference instant, request the inclusive interval [instant - 1 second, instant].
There are 910 fixed windows. Requests use SIP, USD, ascending order, `asof=-`,
100 records per page and at most sixteen pages per window. Reaching a page cap
with a continuation token leaves that window incomplete and unestimated.
An empty page is not a termination signal while a continuation token remains.

All HTTP responses are retained before interpretation, using the existing
lossless byte-envelope archive. Resume authenticates request paths, body hashes,
attempt ordering and plan identity. Up to nine retained attempts per page are
permitted across invocations, with at most three in one invocation, the existing
750-ms pacing and bounded Retry-After/server retry policy. Completed requests
are not repeated. Authentication/access denial is retained and suppresses
further requests in that invocation; it does not cause a feed switch, subscription
request or replacement date. An unavailable route remains unestimated.

Records must belong to the requested symbol/window and be ordered across pages.
Select the latest source timestamp across all returned records, then assess its
eligibility. Do not skip an invalid latest quote to select an older favorable
one. Exact duplicate latest records are harmless; conflicting latest records
are ambiguous and unestimated. Quote eligibility requires positive bid/ask and
sizes, ask strictly above bid, valid exchange fields and one or two condition
flags, all `R`. Locked, crossed, missing-side, nonregular and ambiguous latest
quotes remain separately counted. Unknown/schema-invalid fields do not become
plausible prices. No later quote is used to fill a missing reference.

This entire sample predates 2024. Quote sizes are interpreted as round lots and
multiplied by 100 shares under the declared five-ETF convention. No post-November
2025 size convention is applied. Quote timestamps retain nanoseconds. Maximum
source age is one second by construction. Retrieval time is acquisition metadata,
not an observed client receipt time or historical network latency.

Report quoted spread and half-spread in basis points, bid/ask displayed shares,
quote age and eligible/invalid/empty/incomplete/denied counts by symbol/year and
clock. These are descriptive sampled NBBO attributes, without a claim that all
displayed size could actually have been executed or replenished.

## Fixed synthetic execution matrix

Use five instruments, both buy/sell directions and seventeen one-factor
workloads per instrument/side. The base is a steady USD 100 midpoint, ten
requested shares, ten displayed shares, a two-cent spread, zero outbound delay,
zero receipt delay, one-second cancellation processing and a fresh source quote.
The additional sixteen workloads change exactly one factor:

- price path: adverse or reverting;
- requested quantity: 1 or 100 shares;
- displayed quantity: 0 or 1 share;
- spread: 0 or 10 cents;
- outbound delay: 1 or 3 seconds;
- receipt delay: 1 or 3 seconds;
- cancellation processing: 0 or 3 seconds;
- source quote age: 1 or 3 seconds.

Cross the resulting 170 workloads with aggressive/passive DAY limits and
five-/eleven-second cancellation deadlines: 680 account cases. Signals,
workloads, common horizons and denominators remain matched between policies.
Zero displayed liquidity and stale quotes are intentional failure stresses,
not exclusions. Retain unfilled quantity and terminal opportunity cost. Report
fill rate, implementation shortfall including unfilled shares, fees, spread
cost and separate source/receipt delays. No best-case policy selection replaces
the declared comparison matrix.

## Daily-risk accounts and missing observations

Extend the existing LongCashAccount and lagged risk estimator to the registered
five-instrument development calendar. Retain intraday-risk, daily-only,
static-risk-budget and passive accounts, prior-only sizing, actual raw execution
endpoints, dividend receivables/payment and the existing cost/delay matrix.
Initial conditions and terminal liquidation must be explicit. Do not scale a
realized return series in place of replaying its sizing and costs.

An unavailable required price endpoint stops the affected continuous account.
No price interpolation, retrospective pre-gap liquidation, return bridge,
capital reset or silent restart repairs the registered missing close. Preserve
its last valid ledger and mark later account outcomes unavailable. Forecast
measurement history may reset and recover under the M3 rules; that does not
restore a stopped continuously held account. Coverage and unavailable economic
comparisons remain visible. A failed coverage or benefit assessment does not
prevent completion of the engineering/research milestone.

## Method references

- [Alpaca historical quotes](https://docs.alpaca.markets/us/reference/stockquotes-1):
  inclusive endpoints, SIP source and continuation-token pagination.
- [Alpaca quote schema](https://docs.alpaca.markets/us/reference/stocklatestquotesingle-1):
  quote conditions, exchange fields and round-lot units before 2025-11-03.
- [Alpaca quote description](https://alpaca.markets/learn/understanding-alpacas-market-data-api-with-pandas-and-plotly):
  NBBO attributes. Provider descriptions do not establish realized fills.

The plan, raw-response archive and derived measurements retain separate hashes.
Synthetic checks do not load historical prices or score strategies. Statistical
stability, risk matching and simultaneous uncertainty remain M7-M8 work.
