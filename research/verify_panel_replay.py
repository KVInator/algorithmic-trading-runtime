"""Independently verify pre-2024 replay features, histories, decisions and arithmetic."""
from __future__ import annotations

import argparse
from collections import defaultdict, deque
from datetime import date, datetime, timezone
from decimal import Decimal, localcontext
import hashlib
import html
import re
import json
import math
import random
from pathlib import Path
import statistics
import zipfile

from verify_historical_panel import PanelVerifier
from verify_historical_capability import utc_clock


def require(value, message):
    if not value:
        raise ValueError(message)


def same(actual, expected, label):
    if expected is None:
        require(actual is None, label)
    else:
        require(actual is not None and math.isfinite(actual) and abs(actual - expected) <= 1e-11 * max(1, abs(expected)),
                f"{label}; actual={actual!r}, expected={expected!r}")


def clock(seconds):
    return datetime.fromtimestamp(seconds, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def seconds(text):
    return int(datetime.fromisoformat(text.replace("Z", "+00:00")).timestamp())


def measurement(bars, previous, dividend, minutes):
    if previous is None or minutes - 1 not in bars:
        return None
    move = math.log((bars[minutes - 1][1] + dividend) / previous)
    result = dict(log_return=move, daily2=move * move)
    for step in (1, 5, 15):
        slots = [0, *range(step - 1, minutes, step)]
        if any(i not in bars for i in slots):
            result[f"rv{step}"] = None
            continue
        values = [previous, bars[0][0] + dividend, *(bars[i][1] + dividend for i in range(step - 1, minutes, step))]
        result[f"rv{step}"] = math.fsum(math.log(b / a) ** 2 for a, b in zip(values, values[1:]))
    return result


def residual(bars):
    slots = set(range(30)) | set(range(29, 150, 5))
    if not slots <= bars.keys():
        return None
    volume = math.fsum(bars[i][2] for i in range(30))
    if volume <= 0:
        return None
    anchor = math.fsum(bars[i][1] * bars[i][2] for i in range(30)) / volume
    return [math.log(bars[i][1]) - math.log(anchor) for i in range(29, 150, 5)]


def fit(sequences):
    pairs = [(a, b) for seq in sequences for a, b in zip(seq, seq[1:])]
    n = len(pairs)
    if n < 3:
        return dict(status="insufficient_pairs", pairs=n)
    mx = math.fsum(a for a, _ in pairs) / n
    my = math.fsum(b for _, b in pairs) / n
    xx = math.fsum((a - mx) ** 2 for a, _ in pairs)
    xy = math.fsum((a - mx) * (b - my) for a, b in pairs)
    if xx <= 64 * 2.220446049250313e-16 * max(abs(a) for a, _ in pairs) ** 2 * n:
        return dict(status="singular_design", pairs=n)
    slope = xy / xx
    intercept = my - slope * mx
    variance = math.fsum((b - intercept - slope * a) ** 2 for a, b in pairs) / (n - 2)
    status = ("nonpositive_slope" if slope <= 0 else "unit_root_boundary" if abs(slope - 1) <= 64 * 2.220446049250313e-16
              else "nonstationary_slope" if slope > 1 else "zero_innovation_variance" if variance == 0 else "admissible")
    result = dict(status=status, pairs=n, slope=slope, intercept=intercept, innovation_variance=variance)
    if status == "admissible":
        result.update(equilibrium=intercept / (1 - slope), speed_per_minute=-math.log(slope) / 5)
    return result


def verify_reversion_coefficients(actual, expected, context):
    """Check independent OLS coefficients, then their rounded derived identities.

    The equilibrium a/(1-b) is ill-conditioned near b=1. Applying the primitive
    coefficient tolerance directly to that ratio gives false disagreements.
    Its backward residual is checked against rounding of the saved a, b and
    equilibrium instead. This also covers unrounded extended-precision native
    intermediates. It does not widen the independent OLS coefficient tolerance.
    """
    for key in ("intercept", "slope", "innovation_variance"):
        same(actual[key], expected.get(key), context + ": Reversion coefficient differs: " + key)
    if expected["status"] != "admissible":
        for key in ("equilibrium", "speed_per_minute"):
            same(actual[key], None, context + ": Inadmissible fit has a derived value: " + key)
        return False
    a, b = actual["intercept"], actual["slope"]
    mu, speed = actual["equilibrium"], actual["speed_per_minute"]
    require(0 < b < 1 and actual["innovation_variance"] > 0
            and mu is not None and math.isfinite(mu) and speed is not None and speed > 0,
            context + ": Invalid admissible reversion parameters")
    gap = 1 - b
    product = mu * gap
    # Half-ulp input intervals allow the native unrounded coefficients. The
    # remaining ulps cover subtraction, multiplication and output rounding.
    budget = (.5 * math.ulp(a) + abs(mu) * (.5 * math.ulp(b) + math.ulp(gap))
              + 4 * (math.ulp(product) + abs(gap) * math.ulp(mu)))
    error = abs(math.fsum((product, -a)))
    require(math.isfinite(product) and math.isfinite(budget) and error <= budget,
            f"{context}: Reversion equilibrium identity differs; residual={error!r}, rounding_bound={budget!r}")
    same(speed, -math.log(b) / 5, context + ": Reversion speed identity differs")
    reference = expected["equilibrium"]
    return abs(mu - reference) > 1e-11 * max(1, abs(reference))


def verify_reversion_numerics():
    """Exercise the binary64/extended-precision cases without market inputs."""
    checks = 0
    near_unit_disagreements = 0
    for gap in (1e-3, 1e-5, 1e-7, 1e-9, 1e-11):
        rng = random.Random(912)
        sequences = []
        for j in range(20):
            sequence = [.001 * (j - 10)]
            for _ in range(24):
                sequence.append(1e-7 + (1 - gap) * sequence[-1] + rng.uniform(-1e-16, 1e-16))
            sequences.append(sequence)
        expected = fit(sequences)
        # Reproduce binary64 online OLS arithmetic, independently of the
        # two-pass, compensated-sum oracle. No native strategy is called.
        mx = my = xx = xy = 0.
        n = 0
        for sequence in sequences:
            for x, y in zip(sequence, sequence[1:]):
                n += 1
                dx, dy = x - mx, y - my
                mx += dx / n
                my += dy / n
                xx += dx * (x - mx)
                xy += dx * (y - my)
        b = xy / xx
        a = my - b * mx
        actual = dict(expected, intercept=a, slope=b, equilibrium=a / (1 - b), speed_per_minute=-math.log(b) / 5)
        near_unit_disagreements += verify_reversion_coefficients(actual, expected, f"constructed gap {gap}")
        checks += 1
        for field in ("intercept", "slope", "innovation_variance", "equilibrium", "speed_per_minute"):
            altered = dict(actual)
            altered[field] += max(1, abs(altered[field])) * .01
            try:
                verify_reversion_coefficients(altered, expected, "constructed corruption")
            except ValueError:
                checks += 1
            else:
                raise ValueError("Corrupted reversion parameter was accepted: " + field)
    require(near_unit_disagreements > 0, "Near-unit regression did not reproduce the old comparison failure")
    # A native extended-precision fit can round its saved slope before the
    # independently rounded equilibrium. Both must fit their rounding cells.
    with localcontext() as decimal_context:
        decimal_context.prec = 60
        a = Decimal("0.000000100000000000000005")
        b = Decimal("0.99999990000000005")
        record = dict(status="admissible", intercept=float(a), slope=float(b), innovation_variance=1e-10,
                      equilibrium=float(a / (1 - b)), speed_per_minute=-math.log(float(b)) / 5)
    verify_reversion_coefficients(record, record, "extended-precision rounding")
    checks += 1
    print(f"Reversion numerical checks passed: {checks}; {near_unit_disagreements} near-unit disagreements reproduced and validated.")


class InputSource:
    def __init__(self, summary):
        self.summary = summary
        self.synthetic = summary["provenance"].get("fixture") == "analytic_panel_300"
        if self.synthetic:
            require(summary["provenance"]["evidence_origin"] == "synthetic", "Analytic fixture cannot claim provider evidence")
        self.oracle = PanelVerifier(Path(summary["input_dataset"])) if not self.synthetic else None
        self.cash = {}
        self.checkpoints = {}
        self.recovered = {}
        if self.synthetic:
            self.days = [d for year in (2016, 2017) for d in PanelVerifier(Path.cwd()).calendar(f"{year}-01-01", f"{year}-12-31")][:300]
        else:
            p = self.oracle
            provenance = summary["provenance"]
            if provenance["evidence_origin"] == "provider":
                require(provenance["study_manifest_sha256"] == "edeee4275c9a01df3859f6c5090073c2b496df31e625e3bf6eed79bee7a8715c",
                        "Provider study differs from the accepted M2 registration")
            require(p.digest(p.directory / "study_manifest.json") == provenance["study_manifest_sha256"], "Study identity differs")
            require(p.digest(p.directory / "acquisition_manifest.json") == provenance["acquisition_manifest_sha256"], "Acquisition identity differs")
            require(p.digest(p.directory / "eligibility_masks.json") == provenance["eligibility_masks_sha256"], "Registered masks differ")
            require(p.digest(p.directory / "sealed/accounting_actions.json") == provenance["canonical_actions_sha256"], "Canonical actions differ")
            manifest = p.load(p.directory / "acquisition_manifest.json")
            self.days = []
            for reference in manifest["months"]:
                if reference["file"].split("/")[-1][:4] >= "2024":
                    continue
                month = p.checked(reference)
                for ref in month["checkpoints"]:
                    name = ref["file"].split("/")
                    symbol, day = name[-2], name[-1][:-5]
                    self.checkpoints[symbol, day] = ref
                # Clocks use the independent fixed-date calendar oracle.
            dates = sorted({d for _, d in self.checkpoints})
            for day in dates:
                expected = p.calendar(day, day)
                require(len(expected) == 1, "Unexpected exchange date")
                self.days.append(expected[0])
            if provenance["evidence_origin"] == "provider":
                require(dates == [d["date"] for d in p.calendar("2016-01-01", "2023-12-31")], "Pre-2024 calendar incomplete")
            for row in p.load(p.directory / "sealed/accounting_actions.json")["cash_distributions"]:
                if row["ex_date"] < "2024":
                    key = row["symbol"], row["ex_date"]
                    require(key not in self.cash, "Duplicated cash event")
                    self.cash[key] = float(row["usd_per_share"])
            recovery_file = p.directory / "resolutions/closing_marks/manifest.json"
            if provenance.get("closing_recovery_manifest_sha256") is not None:
                require(p.digest(recovery_file) == provenance["closing_recovery_manifest_sha256"], "Recovery identity differs")
                recovery = p.load(recovery_file)
                recovery_reader = PanelVerifier(recovery_file.parent)
                for row in recovery["records"]:
                    require(row["date"] < "2024", "Reserved recovery access")
                    for ref in row["pages"]:
                        envelope = recovery_reader.checked(ref)
                        body = bytes.fromhex(envelope["body_hex"])
                        require(hashlib.sha256(body).hexdigest() == envelope["body_sha256"], "Recovery body changed")
                        values = json.loads(body)["bars"].get(row["symbol"], [])
                        if values:
                            self.recovered[row["symbol"], row["date"]] = float(values[0]["c"])

    def load(self, symbol, index):
        day = self.days[index]
        opening = int(utc_clock(date.fromisoformat(day["date"]), 9, 30) // 10**9)
        minutes = 390 if day["close"] == "16:00" else 210
        if self.synthetic:
            number = PanelVerifier.symbols.index(symbol)
            base = 100 + 10 * number + .02 * index
            previous = base
            bars = {}
            for i in range(minutes):
                close = base * math.exp(.000002 * i + .0015 * math.sin(.2 * i + .17 * index))
                missing = ((symbol == "TLT" and index == 240 and i == 100) or (symbol == "GLD" and index == 250 and i == 149)
                           or (symbol == "SPY" and index == 260 and i == 389) or (symbol == "IWM" and index == 265 and i == 151))
                if not missing:
                    bars[i] = (previous, close, 1000 + number * 50 + (index % 5) * 10)
                previous = close
            dividend = 1.25 if symbol == "QQQ" and index == 268 else 0
        else:
            captured = []
            checkpoint = self.oracle.checked(self.checkpoints[symbol, day["date"]])
            self.oracle.session(checkpoint, day, symbol, self.summary["provenance"]["protocol_sha256"], captured)
            bars = {int((seconds(b["t"]) - opening) // 60): (float(b["o"]), float(b["c"]), float(b["v"])) for b in captured}
            dividend = self.cash.get((symbol, day["date"]), 0)
        return day, opening, minutes, bars, dividend


class ReplayVerifier:
    def __init__(self, directory):
        self.directory = directory
        self.summary = PanelVerifier.load(directory / "summary.json")
        self.source = InputSource(self.summary)
        self.common = defaultdict(list)
        self.samples = {}
        self.conditioned_fits = 0

    @staticmethod
    def path(actual, targets, bars, opening, available):
        entry = None
        resolved = True
        trades = []
        require(len(actual["decisions"]) == len(targets), "Decision count differs")
        for (minute, desired, features), decision in zip(targets, actual["decisions"]):
            require(decision["at"] == clock(opening + minute * 60), "Decision clock differs")
            require(decision["target_long"] == (desired if resolved else None), "Causal target differs")
            if resolved and features is not None:
                for key, value in features.items():
                    if isinstance(value, (float, int)):
                        same(decision["features"][key], value, "Continuation feature differs: " + key)
                    else:
                        require(decision["features"][key] == value, "Continuation history boundary differs")
            else:
                require(decision["features"] is None, "Unavailable feature was fabricated")
            if resolved and desired != (entry is not None):
                if minute + 1 not in bars:
                    resolved = False
                elif desired:
                    entry = minute, bars[minute + 1][0]
                else:
                    start, buy = entry
                    sell = bars[minute + 1][0]
                    gross = sell / buy - 1
                    net = sell * .9999 / (buy * 1.0001) - 1
                    trades.append((start, minute, buy, sell, gross, net))
                    entry = None
        require(actual["execution_resolved"] == resolved and len(actual["trades"]) == len(trades), "Execution state or trade count differs")
        for t, (start, end, buy, sell, gross, net) in zip(actual["trades"], trades):
            require(t["entry_at"] == clock(opening + (start + 1) * 60) and t["exit_at"] == clock(opening + (end + 1) * 60), "Execution clock differs")
            for key, value in (("entry_price", buy), ("exit_price", sell), ("gross", gross), ("net", net)):
                same(t[key], value, "Trade arithmetic differs")
        valid = available and resolved
        require(actual["outcome_available"] == valid, "Outcome availability differs")
        same(actual["gross"], math.prod(1 + t[4] for t in trades) - 1 if valid else None, "Gross path differs")
        same(actual["net"], math.prod(1 + t[5] for t in trades) - 1 if valid else None, "Net path differs")

    def instrument(self, symbol, path):
        continuation, reversion, risk = deque(maxlen=20), deque(maxlen=20), deque(maxlen=252)
        previous = None
        cutoff = ""
        static = None
        counts = dict(calendar_records=0, development_records=0, common_operational_records=0, risk_forecasts=0)
        with path.open(encoding="utf-8") as stream:
            for index, line in enumerate(stream):
                require(len(line) <= 256 * 1024, "Replay record exceeds its bound")
                row = json.loads(line)
                day, opening, minutes, bars, dividend = self.source.load(symbol, index)
                d = day["date"]
                require(d < "2024" and row["date"] == d and row["symbol"] == symbol and row["session_open_utc"] == clock(opening), "Replay identity/access boundary differs")
                scored = d > "2016-12-31"
                regular = minutes == 390
                diagnostic = symbol == "SPY" and d == "2023-01-03"
                executions = {0,31,32,61,62,91,92,121,122,151,152,166,167,181,182,211,212,241,242,271,272,301,302,331,332,361,362,371,372,381,382,386,387}
                operational = regular and not diagnostic and executions <= bars.keys()
                require(row["availability"]["operational"] == operational and row["scored_period"] == scored, "Operational/scored mask differs")
                availability = row["availability"]
                require(availability["momentum_features"] == ({0, 359} <= bars.keys())
                        and availability["reversion_features"] == (set(range(30)) | set(range(29, 150, 5)) <= bars.keys())
                        and availability["continuation_prefixes"] == [set(range(m)) <= bars.keys() for m in range(30, 361, 30)]
                        and availability["missing_execution_slots"] == sorted(executions - bars.keys()),
                        "Recorded feature/execution availability differs")
                self.common[d].append((operational, row["common_operational"]))
                if scored and row["common_operational"] and symbol not in self.samples:
                    self.samples[symbol] = row
                current = residual(bars)
                expected_paths = {}
                if regular:
                    signal = (bars[359][1] - bars[0][0]) / bars[0][0] if not diagnostic and {0,359} <= bars.keys() else None
                    same(row["momentum_signal"], signal, "Momentum signal differs")
                    for control in (False, True):
                        expected_paths["momentum_always_long" if control else "momentum"] = [(360, signal is not None and (control or signal > 0), None), (385, False, None)]
                    for volume in (False, True):
                        targets = []
                        held = False
                        unknown = False
                        for minute in range(30, 361, 30):
                            f = None
                            if len(continuation) == 20 and not diagnostic and set(range(minute)) <= bars.keys():
                                scale = math.fsum(abs((h[1][minute - 1][1] - h[1][0][0]) / h[1][0][0]) for h in continuation) / 20
                                median = statistics.median(math.fsum(h[1][i][2] for i in range(minute)) for h in continuation)
                                if median > 0:
                                    v = math.fsum(bars[i][2] for i in range(minute))
                                    move = (bars[minute - 1][1] - bars[0][0]) / bars[0][0]
                                    f = dict(move=move, scale=scale, volume=v, median_volume=median, relative_volume=v/median,
                                             history_first=clock(continuation[0][0]), history_last=clock(continuation[-1][0]))
                            target = f is not None and f["move"] > f["scale"] and (held or not volume or f["relative_volume"] > 1)
                            targets.append((minute, target, f))
                            if not unknown and target != held and minute + 1 not in bars:
                                unknown = True
                            if not unknown:
                                held = target
                        targets.append((385, False, None))
                        expected_paths["continuation_volume" if volume else "continuation_price"] = targets
                    actions = [False] * 4
                    if len(reversion) == 20 and not diagnostic:
                        complete = all(h[1] is not None for h in reversion)
                        fitted = fit([h[1] for h in reversion]) if complete else fit([])
                        actual = row["reversion"]
                        require(actual is not None and actual["complete"] == complete and actual["fit_status"] == fitted["status"] and actual["pairs"] == fitted["pairs"], "Reversion fit/status differs")
                        require(actual["history_first"] == clock(reversion[0][0]) and actual["history_last"] == clock(reversion[-1][0]), "Reversion history slots differ")
                        self.conditioned_fits += verify_reversion_coefficients(actual, fitted, symbol + " " + d)
                        same(actual["current"], current[-1] if current else None, "Current residual differs")
                        expected_move = None
                        if complete and current is not None:
                            hurdle = math.log1p(.0001) - math.log1p(-.0001)
                            drift = 6 * math.fsum(b-a for _, seq in reversion for a,b in zip(seq,seq[1:])) / 480
                            actions[1:] = [current[-1] < 0, drift > hurdle, True]
                            if fitted["status"] == "admissible":
                                b, a = fitted["slope"], fitted["intercept"]
                                # Thirty minutes is six five-minute steps. This
                                # equivalent polynomial avoids cancellation of
                                # the ill-conditioned equilibrium and speed.
                                expected_move = (a - (1 - b) * current[-1]) * b ** .2 * math.fsum(b ** j for j in range(6))
                                actions[0] = current[-1] < fitted["equilibrium"] and expected_move > hurdle
                        same(actual["expected_move"], expected_move, "Reversion forecast differs")
                    else:
                        require(row["reversion"] is None, "Reversion used uninitialized history")
                    for name, target in zip(("candidate","sign_only","drift","always_long"), actions):
                        expected_paths["reversion_"+name] = [(150,target,None),(180,False,None)]
                require(len(row["policies"]) == len(expected_paths)
                        and {p["policy"] for p in row["policies"]} == set(expected_paths), "Policy matrix differs")
                for p in row["policies"]:
                    self.path(p, expected_paths[p["policy"]], bars, opening, scored and row["common_operational"])
                prediction = row["risk"]
                require(prediction["prior_returns"] == len(risk) and prediction["cutoff_utc"] == cutoff, "Risk history/cutoff differs")
                estimate = None
                if len(risk) == 252:
                    prior = list(risk)[-20:]
                    hd = math.fsum(h[1]["daily2"] for h in prior) / 20
                    hi = math.fsum(h[1]["rv5"] for h in prior) / 20 if all(h[1]["rv5"] is not None for h in prior) else None
                    if hd > 0 and (hi is None or hi > 0):
                        estimate = hd, hi
                        if static is None:
                            static = min(1, .1 / math.sqrt(252 * math.fsum(h[1]["daily2"] for h in risk) / 252))
                        require(prediction["history_first"] == prior[0][0] and prediction["history_last"] == prior[-1][0], "Risk estimator window differs")
                        require(prediction["missing_rv5_dates"] == [d for d,m in prior if m["rv5"] is None], "Risk fallback dates differ")
                require(prediction["available"] == (estimate is not None), "Risk forecast availability differs")
                same(prediction["passive_weight"], 1 if static is not None else None, "Passive risk target differs")
                same(prediction["static_weight"], static, "Static risk initialization differs")
                for key, expected in (("h_daily", estimate[0] if estimate else None), ("h_intraday", estimate[1] if estimate else None),
                                      ("daily_weight", min(1,.1/math.sqrt(252*estimate[0])) if estimate else None),
                                      ("intraday_weight", min(1,.1/math.sqrt(252*(estimate[1] if estimate[1] is not None else estimate[0]))) if estimate else None)):
                    same(prediction[key], expected, "Lagged risk forecast differs: "+key)
                risk_bars = dict(bars)
                recovered = self.source.recovered.get((symbol,d))
                if recovered is not None:
                    risk_bars[minutes-1] = (recovered,recovered,0)
                measured = measurement(risk_bars, previous, dividend, minutes)
                require((row["measurement"] is None) == (measured is None), "Daily measurement availability differs")
                if measured:
                    for key, value in measured.items():
                        same(row["measurement"][key], value, "Daily measurement differs: " + key)
                    risk.append((d, measured))
                else:
                    risk.clear()
                previous = risk_bars[minutes - 1][1] if minutes - 1 in risk_bars else None
                cutoff = clock(opening + minutes * 60 + 300)
                if regular:
                    reversion.append((opening, current if not diagnostic else None))
                    if len(bars) == 390 and not diagnostic:
                        continuation.append((opening, bars))
                counts["calendar_records"] += 1
                counts["development_records"] += scored
                counts["common_operational_records"] += scored and row["common_operational"]
                counts["risk_forecasts"] += scored and estimate is not None
        require(counts["calendar_records"] == len(self.source.days), "Replay date sequence was truncated")
        return counts

    def run(self):
        s = self.summary
        require(s["schema_version"] == 1 and s["kind"] == "causal_panel_replay"
                and s["reference_parity"]["passed"] and s["reserved_strategy_inputs_opened"] is False,
                "Replay provenance/access claims differ")
        require(s["primary_cost_bps_per_side"] == 1 and s["primary_delay_minutes"] == 1,
                "Primary reference treatment differs")
        require(len(s["artifacts"]) == 5 and len(s["instruments"]) == 5
                and {Path(r["path"]).stem for r in s["artifacts"]} == set(PanelVerifier.symbols)
                and {r["symbol"] for r in s["instruments"]} == set(PanelVerifier.symbols), "Instrument matrix differs")
        require(s["evidence_origin"] == s["provenance"]["evidence_origin"], "Evidence origin differs")
        require(s["network_requests"] == 0 and s["broker_events"] == 0 and s["strategy_scored"] is True
                and s["shared_capital_replayed"] is False and s["daily_risk_accounts_replayed"] is False
                and s["simultaneous_inference_completed"] is False, "Replay scope claims differ")
        if s["evidence_origin"] == "provider":
            for key, expected in (("momentum_dates", 496), ("continuation_dates", 476),
                                  ("reversion_dates", 476), ("risk_estimate_windows", 733)):
                require(s["reference_parity"][key] == expected, "Original-reference parity coverage differs")
        for ref in s["artifacts"]:
            path = self.directory / ref["path"]
            require(path.parent == self.directory and path.stat().st_size == ref["bytes"]
                    and path.stat().st_size <= 128 * 1024 * 1024 and PanelVerifier.digest(path) == ref["sha256"],
                    "Replay artifact changed or exceeded its bound")
            symbol = path.stem
            try:
                expected = self.instrument(symbol, path)
            except (ValueError, KeyError, TypeError, IndexError) as error:
                raise ValueError(f"{symbol} replay: {error}") from None
            actual = next(r for r in s["instruments"] if r["symbol"] == symbol)
            require(all(actual[k] == v for k, v in expected.items()), "Instrument summary counts differ")
        for values in self.common.values():
            require(len(values) == 5 and all(c == all(x[0] for x in values) for _, c in values),
                    "Common mask used another date or omitted an instrument")
        require(PanelVerifier.digest(self.directory / "report.html") == s["report_sha256"], "Replay report changed")
        report = (self.directory / "report.html").read_text(encoding="utf-8")
        metadata = re.search(r"<pre>(.*?)</pre>", report, re.DOTALL)
        sample_data = re.search(r"const samples=(\[.*?\]);const select=", report, re.DOTALL)
        require(metadata is not None and sample_data is not None, "Report metadata or target traces are missing")
        embedded = json.loads(html.unescape(metadata.group(1)), parse_float=Decimal)
        require(embedded == {k: v for k, v in s.items() if k != "report_sha256"}, "Embedded report evidence differs")
        samples = json.loads(sample_data.group(1))
        require(len(samples) == len(self.samples) and {sample["symbol"]: sample for sample in samples} == self.samples,
                "Displayed target traces differ from saved records")
        bundle = self.directory / "review.zip"
        if not bundle.exists():
            with zipfile.ZipFile(bundle, "x", compression=zipfile.ZIP_DEFLATED) as z:
                for name in ("summary.json", "report.html"):
                    z.write(self.directory / name, name)
        else:
            with zipfile.ZipFile(bundle) as z:
                require(set(z.namelist()) == {"summary.json", "report.html"}, "Existing review bundle differs")
                for name in z.namelist():
                    require(z.read(name) == (self.directory / name).read_bytes(), "Existing review bundle is stale")
        print(f"Verified causal panel replay: {len(self.common)} dates, five instruments; source, target, history and arithmetic checks passed.")
        print(f"Equilibrium differences explained by conditioning: {self.conditioned_fits}; primitive tolerances and exact target checks retained.")
        print(f"Review bundle: {bundle}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, nargs="?")
    parser.add_argument("--self-check", action="store_true", help="Run constructed reversion rounding/conditioning checks")
    args = parser.parse_args()
    if args.self_check:
        verify_reversion_numerics()
    if args.directory is None:
        if not args.self_check:
            parser.error("a replay directory or --self-check is required")
        return
    directory = args.directory.resolve()
    if (directory / "checks.json").is_file():
        checks = PanelVerifier.load(directory / "checks.json")
        for name in checks["replay_reports"]:
            ReplayVerifier(directory / name).run()
    else:
        ReplayVerifier(directory).run()


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, TypeError, IndexError, OSError) as error:
        raise SystemExit(f"Panel replay verification failed: {error}") from None
