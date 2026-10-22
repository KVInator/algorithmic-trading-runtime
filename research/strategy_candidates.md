# Strategy candidate comparison

Research checkpoint: 22 October 2026. Scope: compare two proposed experiments
before implementing or scoring either. The designs are literature-informed and
selected after inspection of 2022-2023 development results. Neither is an
independent confirmation of the previous strategies.

## Decision

Prioritize the data and accounting preflight for **daily exposure informed by
intraday risk**. Retain opening-range continuation as a bounded alternative.
Do not build both strategy families concurrently.

The daily proposal asks whether minute data improves an investment decision
made once per session. It adds a useful account and risk component even if its
forecast advantage fails. That capability has a clear purpose: separating
signal, target exposure, execution, cash and investor wealth across days.
However, its economic objective is risk-management value, not directional alpha.
A higher equity curve caused by ordinary SPY exposure would not answer the
research question.

The intraday proposal is less expensive to implement and has a precise
falsification comparison. It is also another continuation trial on the same
inspected ETF and years. The current evidence does not justify prioritizing it
merely because its engineering path is shorter or an ORB paper reports attractive
returns on a different instrument and execution model.

The recommendation is an implementation-priority judgment, not a probability
ranking of profitability. The daily preflight is decisive: if required inputs
cannot be verified, resolve the feasibility question before committing to a
performance study or choosing a different data policy.

## The two briefs

| Dimension | [Opening-range continuation](opening_range_continuation_brief.md) | [Daily risk allocation](intraday_risk_allocation_brief.md) |
| --- | --- | --- |
| Question | Does first-hour acceptance above the initial range predict the remaining session beyond same-clock momentum? | Does intraday risk measurement improve a daily risk controller beyond daily-return information? |
| Trading | One long/cash decision at 10:30; close before the session ends | One morning rebalance; overnight positions allowed |
| Minute data | Opening range, completed confirmation, causal controls and delayed references | Realized risk, aligned outcome measurements, overnight accounting and execution references |
| Economic hypothesis | Persistent information processing or demand after opening price discovery | Persistent risk allows useful exposure adjustment without a proportional expected-return increase |
| Primary comparison | Normalized momentum at the same decision and exit, with the same trade limit | Daily-only risk controller and a static ex-ante risk-budget comparator |
| Key weakness | Another selection-exposed SPY continuation hypothesis; risk-state differences remain | Lower risk can be purchased simply by holding cash; forecast improvement may not improve net utility |
| Data readiness | Existing frozen snapshot covers the proposed inputs | Additional calendar, calibration and corporate-action evidence required |
| Main C++ addition | Completed opening-range feature and one-decision integration | Long/cash account, action ledger, risk measurements and target-weight policy |
| First stopping test | No incremental gross/net evidence over the momentum control | No verified full-calendar account inputs or no incremental value over daily-only risk |

The detailed briefs specify proposed clocks, costs, controls and advancement
conditions. They are reviewable designs, not registrations. Registration must
freeze unresolved choices before a candidate score is calculated.

A read-only input check confirmed all required opening-range reference clocks
on the 476 paired scored dates. The 24 saved calendar responses verify 501
exchange dates for a full account; the retained incomplete June session lacks
the required 09:55 five-minute endpoint. These are coverage findings only, not
new signal or performance results.

## What the completed studies establish

SPY-IM-01's baseline finding remains `no_gross_edge`. SPY-TOD-01's price-only
and volume-conditioned variants remain `development_screen_failed`; incremental
volume remains `positive_point_estimates_unproven`. Native Windows builds,
eleven routine contract groups and the full study completed. Uploaded Windows
and GCC results agree exactly in the seven CSV files, financial/diagnostic
statistics and four SVG payloads. Verification of numerical implementation is
separate from evidence of a profitable strategy.

The [continuation diagnostics](continuation_diagnostics.md) support three
decisions for the next designs:

- Keep gross information, saved costs and risk exposure distinct. Approximately
  49% of the observed net volume increment is lower cost drag.
- Account for positive-tail dependence rather than changing a failed screen.
  Common-date winner removal and incremental-volume attribution answer different
  questions.
- Do not interpret volume as an isolated mechanism. Larger normalized moves and
  higher realized risk accompany high volume at every inspected decision clock.

No new return calculation, event-filter backtest, threshold optimization or
later-period access is part of this checkpoint.

## Evidence review

The following records identify the primary source and the material inspected.
Published performance figures are not imported as expected results or targets.
Full-text availability does not mean every table or implementation was audited.

| Reference | Material inspected | Implication for the briefs |
| --- | --- | --- |
| Zarattini and Aziz, *Can Day Trading Really Be Profitable?*, author-hosted version dated 22 September 2025, [PDF](https://concretumgroup.com/wp-content/uploads/2026/02/Can-Day-Trading-Really-Be-Profitable.pdf), section 2 and Table 1 | Full-text strategy and execution definitions | Entry follows the first five-minute candle's direction at the next candle open; stops, targets, sizing and leverage materially define the experiment. This is different from a completed close beyond a prior range. |
| Zarattini, Barbon and Aziz, *A Profitable Day Trading Strategy for the U.S. Equity Market*, [author PDF](https://concretumgroup.com/wp-content/uploads/2026/02/A-Profitable-Day-Trading-Strategy-For-The-U.S.-Equity-Market.pdf), sections 2 and 4 | Full-text entry, universe, stop and relative-volume selection definitions | Its cross-sectional stock selection and range-triggered orders are not the single-ETF opening-candle experiment. A SPY adaptation cannot inherit their results. |
| Fleming, Kirby and Ostdiek, *The Economic Value of Volatility Timing Using Realized Volatility*, [author manuscript](https://www.ruf.rice.edu/~jfleming/wp/ii.pdf), introduction and allocation framework; published JFE 67 (2003), 473-509 | Full-text motivation and daily allocation framework | Intraday measurements can inform slower portfolio decisions. The multi-asset futures allocation is a methodological precedent, not a tested implementation of the proposed ETF account. |
| Moreira and Muir, *Volatility-Managed Portfolios*, [author PDF](https://amoreira2.github.io/alan-moreira.github.io/VolPortfolios_published.pdf), strategy definition and leverage discussion | Full-text scaling and constraint definitions | The main construction scales monthly factor returns by prior realized variance. The proposed daily, capped inverse-volatility controller is materially different. |
| Cederburg, O'Doherty, Wang and Yan, *On the Performance of Volatility-Managed Portfolios*, [author PDF](https://www.lehigh.edu/~xuy219/research/COWY.pdf), introduction and section 4.3 | Full-text real-time portfolio-comparison design and findings | Attractive full-sample spanning results need not produce implementable gains. Freeze scales and weights from prior information and compare economic value against simple accounts. |
| Patton, *Volatility Forecast Comparison Using Imperfect Volatility Proxies*, [author PDF](https://public.econ.duke.edu/~ap172/Patton_vol_proxies_JoE_2011.pdf), sections 3-4 | Full-text proxy assumptions and loss-function results | Compare variance forecasts with appropriate losses and a common outcome proxy. Robustness to proxy noise relies on assumptions; QLIKE does not validate arbitrary missing-data repairs or biased proxies. |
| Alpaca [historical stock bars](https://docs.alpaca.markets/us/reference/stockbars) and [corporate actions](https://docs.alpaca.markets/us/reference/corporateactions-1) | Current adjustment options and action-data availability specifications | Use a consistent raw-price/action ledger or an explicitly reconciled adjusted-return representation. The API warns that corporate-action availability can lag announcements. |
| Alpaca [paper trading](https://docs.alpaca.markets/us/docs/paper-trading) and [real-time bars](https://docs.alpaca.markets/us/docs/real-time-stock-pricing-data) | Current simulator limitations and updated-bar semantics | Paper accounts do not simulate dividends, and bars can be revised after late trades. Neither paper P&L nor final historical bars substitute for a reconciled total-return account or proof of live information availability. |

Two details constrain interpretation. The inspected ETF ORB version's methods
and Table 1 end on 17 February 2023, while its abstract describes the period
more broadly as 2016-2023. Its profit figures also depend on a leveraged,
stop/target execution model with an explicit no-slippage assumption. Those
details must not be summarized as evidence that a simple unlevered SPY breakout
is already supported.

Cederburg et al.'s adverse results concern specific volatility-managed
comparisons and real-time combinations. They do not prove that every capped
daily SPY controller fails. Conversely, the earlier favorable papers do not
prove that more precise risk inputs improve this account after costs. That
increment is the empirical question.

## Preserve the trial history

| Study | Recorded status | Consequence |
| --- | --- | --- |
| SPY-IM-01 | Baseline development rejected | Preserve its finding and frozen evidence |
| SPY-TOD-01 | Both directional screens failed; volume increment unproven | Preserve all variants, stresses and gates |
| SPY-ORC-01 | Proposed, uncomputed | Register before scoring; remains selection-exposed |
| SPY-RISK-01 | Proposed, feasibility unresolved | Verify data and accounting before registration |

2022-2023 remains inspected development data for both proposals. A new name or
objective does not make it independent again. External literature exposure
includes later publications and examples. Local 2024-2025 candidate performance
remains unopened. Any subsequent evaluation needs a separately frozen design,
declared role for each period and its exposure limitations recorded in advance.
Neither proposal authorizes immediate later-period testing.

## Concrete next milestone

Build a data/accounting preflight for SPY-RISK-01, with no strategy performance
calculation. It should produce a coverage and accounting report through the
existing Visual Studio workflow:

1. Reconcile the full exchange calendar, available references, the four-minute
   gap, shortened sessions and required 2021 history. Reference reusable evidence
   by hash; preserve the frozen study and raw responses.
2. Check corporate-action endpoint access and field coverage for amounts,
   ex-dates, payment dates and splits. Record provenance and availability limits.
3. Define one account and action-event contract. Verify wealth continuity,
   purchase limits, cost accounting, overnight attribution and terminal handling
   with analytical synthetic cases.
4. Report every unresolved input or convention explicitly. A data gap may block a
   risk feature while its session still remains mandatory for a held account.
5. Freeze the final risk protocol and data-failure policy only after that review,
   before implementing the strategy's scored historical comparison.

No new dependency, live order route, parameter optimizer or dashboard framework
is required for this milestone. The existing static HTML/SVG and CSV/JSON
reporting pattern is sufficient. If the data cannot support a defensible account,
the preflight should conclude that clearly rather than manufacture a return.
