# Registered opening-anchor reversion implementation

SPY-MR-01 revision 1 uses the unchanged [protocol](../research/intraday_reversion_protocol.md)
and [settings](../research/intraday_reversion_protocol.json). Earlier synthetic
sampling/noise evidence remains a limitation on parameter interpretation.

## Chronology and accounts

`ReversionHistory` accepts only ordered, calendar-verified full sessions at their
completion time. Its twenty slots retain unavailable anchors. `ReversionStrategy`
accepts exactly the completed noon prefix and queries history before the current
session is appended. No same-day dynamics or cross-session regression pair enters
the estimate. Missing-current and missing-history anchors affect only policies
that require those inputs. Unsupported OU interpretations retain raw fit statuses
and produce candidate cash, rather than deleting a scored date.

`ReversionStudy` freezes one noon action for each policy. Every cost/delay case
uses that action with its own delayed entry/exit references and the shared
fractional long-price return calculation. The research core also owns fractional
quantities, normalized equity and drawdown paths; reports serialize those results.
Four policies across ten fixed
cost/delay combinations give forty account paths, plus cash. Turnover uses the
registered two equity sides per active date, rather than an execution notional
or broker fee measurement. Current residual and all prior-fit coefficients are
exported with their completed information boundaries.

The production boundary verifies both registered document digests, the frozen
manifest bytes, the original March identity, all exclusions, the complete expected
calendar sequence and full fixed resampling budgets. The read-only dataset loader
also authenticates retained raw responses, CSVs and checkpoints. Constructed
fixtures can use smaller budgets; they cannot run through the authenticated
historical boundary or claim historical status in their reports.

Parameter ranges resample complete session sequences through the existing
estimator. Outcome resampling applies common indices to all forty account cases,
paired control differences and the full dated forecast masks. Undefined forecast
replicates invalidate the corresponding whole range. Economic and conditional
forecast screens remain separate. Every gate is reported even when an earlier
one fails. No decision rule, threshold, window or cost changes after scoring.

## Verification and reproduction

Build All and the routine preset include `reversion_study_contracts`. Constructed
cases check prior-only chronology, rejection of early/duplicate history commits,
missing-anchor rollout, future-input noninterference, controls, cost/delay parity,
zero paired differences, cash/undefined Sharpe, masked bootstrap failures,
strict gate boundaries and protocol tampering.

Run `reversion_development` without command-line options using the retained
2022-2023 frozen input evidence. It writes a new directory under `scratch/runs/`.
No source dataset or previous report is overwritten. No 2024-2025 price or
performance access is authorized. Historical minute-open proxies do not establish
broker fills, bid/ask liquidity or real-time SIP data availability.

`python research/verify_reversion_study.py <report-directory>` checks the saved
artifacts through an independent implementation. It uses 60-digit Decimal OLS
for rolling point fits, independent fractional account arithmetic and an explicit
MT19937-64 implementation with rejection/modulo block sampling. NumPy handles
bulk parameter refits and paired outcome calculations. The oracle is separate
from the authoritative C++ engine; it is not another strategy implementation.

## Development finding, 24 October 2026

Revision 1 completes on Linux with the registered 496 eligible sessions, twenty
warm-up sessions and 476 scored dates. All forty market account cases and the
cash reference are retained. Parameter ranges contain 952 dated block records,
covering 475,048 refits. Prior fits are numerically OU-admissible on 262 dates;
214 have a slope above one. Every date remains in economic statistics; the
forecast comparison uses the 262-date common mask.

Primary one-minute delay and 1 bp per side:

| Policy | Active dates | Gross bp per scored date | Net bp per scored date | Compounded net | Daily-close drawdown |
| --- | ---: | ---: | ---: | ---: | ---: |
| Candidate | 83 | 0.2646 | -0.0841 | -0.4171% | 2.8355% |
| Sign-only | 231 | 0.3618 | -0.6088 | -2.9188% | 5.8808% |
| Drift | 120 | 0.1145 | -0.3897 | -1.8582% | 2.9489% |
| Always long | 476 | -0.7841 | -2.7837 | -12.5139% | 14.2807% |
| Cash | 0 | 0 | 0 | 0% | 0% |

Candidate gross compounding is +1.2498%, but the primary net result is negative;
its positive-mean break-even cost is approximately 0.7588 bp per side. This does
not authorize selecting a cheaper cost scenario as primary. The candidate also
fails the required 100 active dates. The one-minute/2-bp and two-minute/1-bp
net stresses are negative. Primary net compounding is -1.4441% in 2022 and
+1.0421% in 2023. Removing the five largest positive daily net contributions
leaves a negative net sum, although those winners' share of positive returns
passes the 50% concentration screen.

The candidate's paired net increments are +0.5246 bp/date over sign-only and
+0.3055 bp/date over drift. Its gross increment over sign-only is -0.0972
bp/date; the positive net comparison does not establish better gross prediction.
Both block-length ranges include zero for the candidate net mean and both paired
net increments. The required strictly positive lower bounds fail. The assessment
is `cost_dominated`, with no advancement under the registered design.

On the common forecast mask, drift loss minus candidate loss is -8.0941e-8 for
squared loss and -2.6945e-5 for absolute loss. Negative point contrasts indicate
larger candidate losses. Both block-length ranges include zero, so these results
do not establish forecast superiority or statistically conclusive inferiority.
All six registered forecast gates fail.

## Acceptance evidence and remaining work

GCC 13.3 Debug Build All and all twenty-two routine groups pass with warnings as
errors. The new study group also passes ASan/UBSan with leak detection disabled.
The independent oracle, using NumPy 2.3.5, reproduces all rolling Decimal fits,
decisions, account/annual calculations, both outcome bootstraps, all parameter
ranges, economic/forecast gates and artifact identities. Protocol/settings and
the frozen study manifest retain their registered identities.

Repeated runs retain eight byte-identical numerical artifacts and identical
case metrics, ranges and gates. Intervening source changes complete the
independent verifier, place account-path calculations in the research core and
format the new C++ sources; trading rules and numerical exports remain unchanged.
Final evidence is retained under
`scratch/runs/SPY_MR_01_development_2022-2023_4`. Its ten artifact hashes verify;
all HTML links resolve. Browser visual rendering remains unavailable in the
current environment. The [ledger](progress.md) records exact summary/source
identities and checkpoint status.

The historical research implementation and Linux comparison are complete. A
new Windows build, twenty-two-group suite and native `reversion_development`
output remain pending before closing milestone 2. The earlier native sampling
and execution checkpoints remain valid for their recorded implementations.
Execution adapters, account parity, actual paper transport/capture, execution
measurements, Release benchmarks/CI and the unified report remain separate open
work. Failed economic screens complete this comparison; they do not trigger an
additional strategy search or permit later-period performance access.
