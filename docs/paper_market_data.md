# Current paper quote and calendar access

`PaperMarketProbe` checks current quote/calendar access without submitting or
canceling orders, enrolling sessions or accessing historical strategy prices.
The [five-session plan](../research/paper_capture_plan.md) remains unchanged.

## Requests and transport

`PaperMarketRequests` permits two GETs:

- `https://paper-api.alpaca.markets/v2/calendar?start=YYYY-MM-DD&end=YYYY-MM-DD&date_type=TRADING`
- `https://data.alpaca.markets/v2/stocks/SPY/quotes/latest?feed=sip&currency=USD`

The credential-bearing Windows reader independently validates the complete URL
before invoking the existing WinHTTP market-data client. Other origins, symbols,
feeds, paths and extra query parameters are refused. Redirects remain disabled,
normal TLS validation stays enabled and credentials use the existing Windows
Credential Manager/hidden-console path. Response bodies and Retry-After values
are checked for credential echoes before archival.

Read phases are bounded. The shared 64-KiB body limit remains, with a new
twenty-second aggregate body-read deadline. Historical readers retain their
URLs and response/value contracts. Each GET attempt and raw response is captured
with local UTC/monotonic clocks, status and available Retry-After before decoding.
The probe makes one attempt per resource, with no retries or feed fallback.
Access errors remain failed evidence. SIP entitlement is determined by the
native response, not inferred from historical-bar or order-stream access.
The explicit request supplies feed provenance; the quote body does not
independently authenticate feed identity.

## Calendar roles

The shared conversion has an explicit `CalendarRole`. Its historical default
retains the 2020-12-31 through 2025 boundary. The paper role permits 2026-2029,
using the same validated daytime clocks and post-2007 US DST rule. This is a
bounded New York daytime conversion, not a general timezone database.
Paper sessions cannot construct historical bar requests.

The probe requests at most 31 days from its local UTC receipt date. Paper mode
permits empty holiday/weekend ranges; records must be unique, ascending, in range
and on weekdays. Provider early closes are retained and excluded from regular
eligibility. Holidays/exceptional closures come from the provider.
The first five future regular sessions at response time are provisional
candidates, not an immutable enrollment or a declaration of readiness.

## Exact prices and current usability

`PaperQuoteDecoder` retains original numeric price digits as `BrokerDecimal`,
nanosecond source times, exchange codes, conditions, tape and displayed sizes
in provider round lots. The shared JSON parser checks syntax/depth/duplicate keys;
its number-text view preserves numeric lexemes before price calculations.
Plain decimal syntax and integer sizes are required. String-valued prices,
scientific notation and malformed/unsupported values fail explicitly.

`PaperQuotePolicy` uses receipt clocks, a fresh evaluation clock and the calendar.
It denies future source times, source age over two seconds, nonpositive sizes,
prices below USD 1, crossed/locked quotes, closed/missing/shortened sessions and
quotes outside the current regular session. Exactly two seconds is allowed;
a one-nanosecond breach is denied. Receipt clock regression, receipt age over
two seconds and UTC/monotonic elapsed-time disagreement over 250 ms also deny
use. That disagreement bound is a conservative local clock guard.

Usable passive buys and marketable sells round the bid down to cents; marketable
buys round the ask up. Both raw prices and projected integer cents are retained.
Unsupported integer-cent ranges deny use. Denied quotes return no projected
limits. Re-evaluation is required immediately before future dispatch.
Quote usability does not establish account/control eligibility, the entry
window, available liquidity or a fill guarantee.

Source age assumes synchronized local/provider UTC clocks. Monotonic consistency
cannot detect a constant host clock offset. This is one REST observation, not a
continuous feed, feed heartbeat, order-book reconstruction or latency benchmark.
Financial/control journals and frozen historical protocols/results are unchanged.

## Verification and native run

`paper_market_diagnostic` runs four constructed cases: regular hours, off hours,
SIP HTTP 403 and HTTP 429, with zero network requests. Its report includes
calendar tables, exact quotes, limit projections, clocks and denial details.
Contracts also check precision collisions under binary doubles, age boundaries,
clock failures, DST changes, early closes, malformed data and URL restrictions.

`research/verify_paper_market.py` independently checks captures/artifacts,
Decimal prices/rounding, nanosecond clocks and IANA `America/New_York` conversion:

```sh
PYTHONDONTWRITEBYTECODE=1 python research/verify_paper_market.py scratch/runs/paper_market_0
```

On Windows, pull, Build All, run `windows-debug-quick`, then select
`paper_market_diagnostic.exe` and `paper_market_probe.exe` with Ctrl+F5.
The probe exits zero when both resources return supported data. Off-hours/stale
quotes can correctly be unusable while access succeeds; `quote_usable` and
denials are separate from access status. Missing SIP access exits nonzero and
retains the response. Preserve both complete folders for native verification.

Native access acceptance, the reviewed control/launcher composition,
reactivation/closing-inventory procedures and five-session observed lifecycles
remain open. This prerequisite launches no orders.

## Primary references

- [Latest quote API](https://docs.alpaca.markets/us/reference/stocklatestquotesingle-1):
  best bid/ask, explicit feed and currency.
- [US calendar](https://docs.alpaca.markets/us/reference/legacycalendar):
  dates/open/close times, including early closes.
- [Stock quote schema](https://docs.alpaca.markets/us/docs/real-time-stock-pricing-data):
  nanosecond times, exchange/condition/tape fields and round-lot sizes.
