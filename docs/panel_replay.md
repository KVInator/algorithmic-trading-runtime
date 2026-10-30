# Causal panel replay

M3 implements the primary one-minute-delay, one-basis-point-per-side reference
replay for SPY, QQQ, IWM, TLT and GLD. It consumes the registered M2 acquisition,
source resolution and eligibility masks. Strategy inputs are restricted to
2016-2023; 2016 initializes history. The access boundary remains closed for
2024 validation and 2025 final evaluation.

## Engines and histories

The authoritative momentum, continuation, opening-anchor/AR(1) and daily-risk
calculations are reused. An explicit panel mode changes the supported universe
and registered missing-input behavior. Legacy defaults preserve their existing
SPY contracts. The original reference check uses identical original inputs and
histories, so expanded-history effects are not mislabeled as numerical changes.

| Component | Decision information |
| --- | --- |
| Momentum | Observed opening price and completed minute-359 close; entry at minute 360, liquidation decision at 385 |
| Continuation | Every observed minute of the completed decision prefix, with twenty earlier complete eligible sessions; decisions every thirty minutes |
| Reversion | Observed first-30-minute volume-weighted closes and exact five-minute closes through minute 149; twenty prior regular calendar slots, including unavailable slots |
| Daily risk | Twenty earlier daily second moments or RV5 measurements, after 252 consecutive actual prior returns are available; RV5 gaps trigger the registered daily fallback |

The opening anchor is an arithmetic volume-weighted average of minute closes,
not transaction VWAP. The unrestricted pooled AR(1) fit retains the existing
numerical/status rules. Model inadmissibility remains a model outcome; it is
not repaired by clipping a slope or replacing a history date.

The daily-risk targets retain the existing sizing rule

\[
w(h)=\min\left(1,\frac{0.10}{\sqrt{252h}}\right).
\]

The static target is initialized from the first 252 available prior daily
second moments and then frozen. A missing close resets the consecutive daily
return history. A forecast can exist before an unavailable future closing mark;
forecast availability and subsequent outcome availability are separate fields.
The four original risk-account allocation targets are recorded, but their
extended ledgers are not replayed at M3.

## Missingness and outcome selection

Each instrument owns its histories. Current sessions enter history only after
all current decisions. Missing timestamps retain their original clock slots;
prices and volumes are not imputed, and exchange dates cannot be compressed.

An unavailable current feature sends every member of its paired comparison to
cash. A missing continuation prefix liquidates an existing position at that
scheduled decision's delayed reference if it is observed. If an actual entry
or exit reference is missing, the policy path becomes unresolved; subsequent
targets and its outcome remain unavailable.

Common operational coverage is an ex-post outcome mask. It is computed only
after all five instruments' decisions and never supplied as a signal. Candidate
and control returns are NA outside that common observable calendar. Cash
returns on observable dates remain zero. This is coverage-conditioned research,
not proof of continuous executable performance or a shared-capital account.

Primary long reference returns use the existing proportional-cost calculation:

\[
R_{net}=\frac{P_{exit}(1-c)}{P_{entry}(1+c)}-1,\qquad c=10^{-4}.
\]

These are bar-price reference outcomes. They do not establish historical
spreads, broker fills, queue position or achievable execution quality.

## Outputs and verification

`panel_replay.exe` authenticates the accepted M2 identities and performs the
original-reference check before wider replay. It writes one bounded JSONL stream
per instrument, a compact manifest and an HTML report with saved target traces.
The report labels synthetic and provider evidence explicitly. The independent
verifier reconstructs source-page features, prior histories, pooled fit,
allocation weights, targets and arithmetic without calling the native engines.

Independent OLS intercept, slope and innovation variance comparisons retain the
`1e-11 * max(1, |reference|)` tolerance. The equilibrium `a/(1-b)` amplifies
coefficient differences near a unit slope, so it is checked through its backward
identity `(1-b) * equilibrium = a`, with a bound for rounding of the saved
coefficients and derived value. Reversion speed is checked against `-log(b)/5`.
The independent primary expected-move calculation uses the equivalent expression

\[
\left(a-(1-b)x\right)b^{1/5}\sum_{j=0}^{5}b^j,
\]

which avoids dividing by `1-b`. Fit-status and target comparisons remain exact.
`verify_panel_replay.py --self-check` exercises binary64 online fits, extended
precision rounding and deliberately corrupted parameters without market data.

The registered pre-2024 calendar contains 2,012 dates, so a complete run writes
10,060 instrument-date records. The registered common operational mask contains
1,682 development dates in 2017-2023. These are input-coverage counts; model
availability, activity and returns are determined by the replay.

[Run commands](running.md#causal-panel-replay-m3) describe native checks,
historical execution and creation of the small review ZIP. The full JSONL
streams and raw inputs remain local. Original v1 results and the M2 registration
are retained.

The subsequent [M4 shared ledger](shared_capital.md), [M5 independent broker](process_broker.md)
and [M6 cost/account comparisons](cost_execution.md) are accepted under their
separate contracts and coverage limits. The [accepted M7 analysis](stability.md)
reports stability variants, risk matching and uncertainty. This primary replay
does not perform a simultaneous
advancement assessment or reserved-period evaluation.
