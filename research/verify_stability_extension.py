"""Independent causal sensitivity, funded-ledger and prior-risk matching verification.

Full verification authenticates the pre-2024 source archive. Review-only mode
checks derived mathematics and display, without authenticating the source ledgers.
"""

from __future__ import annotations

import argparse
from collections import defaultdict, deque
from copy import deepcopy
from decimal import Decimal, localcontext
from datetime import date as Date, timedelta
from html.parser import HTMLParser
import json
import math
from pathlib import Path
import statistics
import zipfile

from verify_cost_delay import charge
from verify_historical_panel import PanelVerifier
from verify_panel_accounts import RiskAudit, equal
from verify_panel_replay import InputSource, ReplayVerifier, seconds
from verify_stability import (
    ENDPOINTS,
    FIELDS,
    POLICY,
    PROTOCOL,
    SYMBOLS,
    JointInference,
    PriorConditions,
    decode,
    empty_observation,
    groups,
    require,
    same,
    sha,
)

PAIRS = (
    ("momentum_increment", "momentum", "momentum_always_long"),
    ("continuation_increment", "continuation_volume", "continuation_price"),
    ("reversion_sign_increment", "reversion_candidate", "reversion_sign_only"),
    ("reversion_drift_increment", "reversion_candidate", "reversion_drift"),
    ("risk_cer_increment", "intraday_risk", "daily_only"),
)
UNITS = tuple(dict.fromkeys(p for _, a, b in PAIRS for p in (a, b)))
DEFAULTS = dict(
    family="all",
    momentum_hold_minutes=25,
    continuation_sessions=20,
    reversion_sessions=20,
    reversion_grid_minutes=5,
    reversion_hold_minutes=30,
    risk_sessions=20,
    risk_grid_minutes=5,
)


def variants():
    root = Path(__file__).resolve().parents[1]
    raw = (root / "research/extension_protocol.json").read_bytes()
    require(sha(raw) == PROTOCOL, "Registered protocol changed")
    protocol = decode(raw)
    cases = []
    fields = {
        ("momentum", "hold_minutes"): "momentum_hold_minutes",
        ("continuation", "history_sessions"): "continuation_sessions",
        ("reversion", "history_sessions"): "reversion_sessions",
        ("reversion", "observation_minutes"): "reversion_grid_minutes",
        ("reversion", "hold_minutes"): "reversion_hold_minutes",
        ("daily_risk", "history_sessions"): "risk_sessions",
        ("daily_risk_measurement", "observation_minutes"): "risk_grid_minutes",
    }
    for row in protocol["stability"]["variants"]:
        parameters = dict(
            DEFAULTS,
            family="risk" if row["family"].startswith("daily_risk") else row["family"],
        )
        parameters[fields[row["family"], row["parameter"]]] = row["value"]
        cases.append(
            dict(id=row["id"], registered_family=row["family"], parameters=parameters)
        )
    require(len(cases) == 14, "Incomplete registered neighborhood")
    return protocol, cases


def policies(parameters):
    return {
        "momentum": ("momentum", "momentum_always_long"),
        "continuation": ("continuation_price", "continuation_volume"),
        "reversion": (
            "reversion_candidate",
            "reversion_sign_only",
            "reversion_drift",
            "reversion_always_long",
        ),
        "risk": ("intraday_risk", "daily_only"),
    }[parameters["family"]]


def sizing(history):
    result = dict(
        first=history[0][0] if history else "",
        last=history[-1][0] if history else "",
        slots=len(history),
        valid=sum(p is not None for _, p in history),
        candidate_volatility=None,
        control_volatility=None,
        target=None,
        candidate_scale=None,
        control_scale=None,
    )
    if result["slots"] == result["valid"] == 252:
        a, b = (
            statistics.stdev(p[i] for _, p in history) * math.sqrt(252) for i in (0, 1)
        )
        result.update(candidate_volatility=a, control_volatility=b)
        if a > 0 and b > 0:
            target = min(0.1, a, b)
            result.update(
                target=target, candidate_scale=target / a, control_scale=target / b
            )
    return result


def observed(row):
    return row["net_return"] if row["status"] == "observed" else None


def ledger_observation(row, risk):
    out = empty_observation()
    if row["status"] == "observed":
        out["net"] = row["net_return"]
        if risk:
            out["exposure"] = (
                row["value"]["shares"] * row["last_price"] / row["value"]["liquid"]
            )
        else:
            for field in ("exposure", "trades"):
                out[field] = float(row[field])
            out["holding_minutes"] = float(row["minutes_held"])
    out["cost"] = math.fsum(e["cost"] for e in row["events"]) / 100000
    out["turnover"] = math.fsum(abs(e["notional"]) for e in row["events"]) / 100000
    return out


class IntradayAudit:
    """Decimal self-financing account with independently required event clocks."""

    def __init__(self):
        self.cash = Decimal(100000)
        self.shares = Decimal(0)
        self.previous = Decimal(100000)
        self.price = Decimal(100)
        self.started = self.held = False
        self.reason = ""

    def verify(
        self, row, primary, bars, opening, policy, ready, scale, session_minutes
    ):
        same(row["scale"], scale, "Intraday sizing")
        if scale is not None:
            require(0 <= scale <= 1, "Levered overlay")
        status, reason = "awaiting_history", self.reason
        sequence = []
        minutes = trades = 0
        if reason:
            status = "stopped"
        elif primary["symbol"] == "SPY" and primary["date"] == "2023-01-03":
            require(not self.held, "Excluded date conceals unresolved holdings")
            status = "excluded"
        elif session_minutes != 390:
            require(not self.held, "Short session conceals unresolved holdings")
            status = "observed" if self.started else "awaiting_history"
        elif (not ready or scale is None) and self.started:
            status, reason = "stopped", "required_prior_overlay_unavailable"
        elif ready and scale is not None:
            require(policy is not None, "Missing regular policy")
            self.started = True
            status = "observed"
            entry = None
            for decision in policy["decisions"]:
                desired = decision["target_long"]
                if desired is None:
                    status, reason = "stopped", "unresolved_frozen_target"
                    break
                if desired == self.held:
                    continue
                minute = (seconds(decision["at"]) - opening) // 60
                if minute + 1 not in bars:
                    status, reason = "stopped", "required_execution_open_unavailable"
                    break
                sequence.append(
                    (
                        opening + (minute + 1) * 60,
                        bars[minute + 1][0],
                        scale if desired else 0,
                    )
                )
                if desired:
                    entry = minute
                else:
                    require(entry is not None, "Exit without entry")
                    minutes += minute - entry
                    trades += 1
                    entry = None
                self.held = desired
            require(status == "stopped" or not self.held, "Missing intraday exit")
        require(
            row["status"] == status and row["stop_reason"] == reason,
            "Intraday stopping/history state differs",
        )
        require(
            len(row["events"]) == len(sequence),
            "Required intraday event missing/repeated",
        )
        fees = turnover = Decimal(0)
        for event, (at, price, weight) in zip(row["events"], sequence):
            p, w, rate = Decimal(str(price)), Decimal(str(weight)), Decimal(".0001")
            liquid = self.cash + self.shares * p
            gap = w * liquid - self.shares * p
            signed = gap / (1 + (rate if gap >= 0 else -rate) * w)
            fee = abs(signed) * rate
            self.cash -= signed + fee
            self.shares += signed / p
            # Native double state can retain a positive rounding residue at zero.
            if abs(self.shares) < Decimal("1e-30"):
                self.shares = Decimal(0)
            if abs(self.cash) < Decimal("1e-30"):
                self.cash = Decimal(0)
            require(
                seconds(event["at"]) == at
                and event["kind"]
                == ("buy" if signed > 0 else "sell" if signed < 0 else "hold"),
                "Intraday event clock/side differs",
            )
            equal(
                event,
                dict(
                    at=event["at"],
                    kind=event["kind"],
                    price=p,
                    notional=abs(signed),
                    cost=fee,
                    cash=self.cash,
                    shares=self.shares,
                ),
                "Intraday event",
            )
            fees += fee
            turnover += abs(signed)
            self.price = p
        nav = self.cash + self.shares * self.price
        for key, value in dict(
            cash=self.cash, shares=self.shares, equity=nav, cost=fees, turnover=turnover
        ).items():
            equal(row[key], value, key, True)
        same(
            row["net_return"],
            float(nav / self.previous - 1) if status == "observed" else None,
            "Funded intraday return",
        )
        same(
            row["minutes_held"],
            float(minutes) if status == "observed" else None,
            "Holding duration",
        )
        same(
            row["trades"],
            float(trades) if status == "observed" else None,
            "Round trips",
        )
        same(
            row["exposure"],
            (scale or 0) * minutes / 390 if status == "observed" else None,
            "Allocated-time exposure",
        )
        if status == "observed":
            self.previous = nav
        self.reason = reason


class CachedSource:
    def __init__(self, source, symbol):
        self.scope = source.scope
        self.days, self.recovered = source.days, source.recovered
        self.values = []
        for i in range(len(self.days)):
            self.values.append(source.load(symbol, i))
            if (i + 1) % 250 == 0:
                print(
                    f"Verified source sessions | {symbol} | {i+1}/{len(self.days)}",
                    flush=True,
                )
        self.synthetic = source.synthetic

    def load(self, symbol, index):
        return self.values[index]


class Marks(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.charts, self.points, self.coverage = {}, {}, []
        self.chart = None
        self.cells, self.active, self.cell = {}, None, None
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "svg":
            self.chart = a["data-chart"]
            self.charts[self.chart] = a
        elif tag == "circle":
            require(a["data-series"] not in self.points, "Repeated chart marker")
            self.points[a["data-series"]] = (self.chart, float(a["cx"]), float(a["cy"]))
        elif tag == "tr" and "data-coverage" in a:
            self.coverage.append(a["data-coverage"])
            self.active = a["data-coverage"]
            self.cells[self.active] = []
        elif tag == "td" and self.active is not None:
            self.cell = ""

    def handle_data(self, text):
        if self.cell is not None:
            self.cell += text

    def handle_endtag(self, tag):
        if tag == "svg":
            self.chart = None
        if tag == "td" and self.cell is not None:
            self.cells[self.active].append(self.cell)
            self.cell = None
        if tag == "tr":
            self.active = None


from evaluation_scope import scored, authorized, scope_claim


class ExtensionVerifier:
    def __init__(self, directory, review_only=False, scope=None):
        self.scope = scope
        self.directory = Path(directory).resolve()
        self.summary = decode((self.directory / "summary.json").read_bytes())
        self.protocol, self.cases = variants()
        self.review_only = review_only
        names = {f"{s}.jsonl" for s in SYMBOLS} | {
            "plan.json",
            "market.json",
            "observations.jsonl",
            "inference_inputs.json",
            "kernel_checks.json",
        }
        refs = self.summary["artifacts"]
        require(
            len(refs) == 10 and {a["path"] for a in refs} == names,
            "Extension inventory differs",
        )
        for a in refs:
            path = self.directory / a["path"]
            require(
                path.parent == self.directory and a["bytes"] <= 1024**3,
                "Invalid artifact path/bound",
            )
            if review_only and a["path"] in {f"{s}.jsonl" for s in SYMBOLS}:
                continue
            require(
                path.stat().st_size == a["bytes"]
                and PanelVerifier.digest(path) == a["sha256"],
                "Extension artifact changed: " + a["path"],
            )
        self.plan = self.load("plan.json")
        self.series = [
            decode(x)
            for x in (self.directory / "observations.jsonl").read_bytes().splitlines()
        ]
        self.market = self.load("market.json")
        self.inputs = self.load("inference_inputs.json")
        self.report = (self.directory / "report.html").read_text(encoding="utf-8")
        self.dates = self.summary["dates"]
        require(
            sha(self.report.encode()) == self.summary["report_sha256"], "Report changed"
        )

    def load(self, name):
        return decode((self.directory / name).read_bytes())

    def verify_plan(self):
        s, p = self.summary, self.plan
        require(
            s["kind"] == "stability_extension_study"
            and s["schema_version"] == 1
            and s["protocol_sha256"] == PROTOCOL
            and s["policy_sha256"] == POLICY,
            "Extension identity differs",
        )
        require(
            s["network_requests"] == 0
            and scope_claim(s, self.scope)
            and not s["advancement_assessed"]
            and not s["inference_calibration_validated"],
            "Extension scope differs",
        )
        require(
            s["secondary_variants_executed"]
            and s["risk_matching_executed"]
            and s["secondary_variants"] == 14
            and s["secondary_series"] == 240
            and s["matched_accounts"] == 50
            and s["unit_accounts"] == 45
            and s["series_count"] == 345,
            "Executed case matrix differs",
        )
        if self.scope:
            self.scope.check(p)
        same(p["variants"], self.cases, "Registered variants")
        same(p["risk_matching"], self.protocol["risk_matching"], "Registered overlay")
        require(
            p["protocol_sha256"] == PROTOCOL
            and p["stage"]
            == (
                "M7_secondary_and_risk_matched"
                if self.scope is None
                else (
                    "M8_secondary_validation"
                    if self.scope.year == 2024
                    else "M9_secondary_final"
                )
            )
            and p["primary_cost_bps"] == p["delay_minutes"] == 1
            and p["secondary_parameter_crossing"] is False
            and p["primary_rules_replaced"] is False,
            "Extension treatment differs",
        )
        require(
            p["risk_window"]
            == "252_prior_calendar_slots_of_genuine_unit_ledger_returns_missing_slots_retained"
            and p["unit_costs"]
            == "primary_one_basis_point_per_side_net_unit_ledger_returns",
            "Overlay training convention differs",
        )
        require(
            self.dates == [d for d in self.market["dates"] if scored(d, self.scope)]
            and self.dates == sorted(set(self.dates))
            and all(authorized(d, self.scope) for d in self.market["dates"]),
            "Development calendar differs",
        )

    def verify_sources(self):
        source = InputSource(self.summary, self.scope)
        expected = {}
        logs, qlike, common = {}, defaultdict(dict), defaultdict(list)
        actions = (
            [
                dict(
                    symbol="QQQ",
                    ex_date=source.days[268]["date"],
                    payable_date=source.days[271]["date"],
                    usd_per_share=1.25,
                )
            ]
            if source.synthetic
            else source.oracle.load(
                source.oracle.directory / "sealed/accounting_actions.json"
            )["cash_distributions"]
        )
        if source.synthetic and self.scope:
            actions.append(
                dict(
                    id="constructed_QQQ_evaluation_cash",
                    symbol="QQQ",
                    ex_date=source.days[600]["date"],
                    payable_date=source.days[603]["date"],
                    usd_per_share=0.75,
                )
            )
        actions = [
            a
            for a in actions
            if a["ex_date"] <= (self.scope.last if self.scope else "2023-12-31")
        ]
        for symbol in SYMBOLS:
            records = []
            with (self.directory / f"{symbol}.jsonl").open(encoding="utf-8") as stream:
                for line in stream:
                    require(
                        len(line) <= 2 * 1024**2 and line.endswith("\n"),
                        "Source record oversized/truncated",
                    )
                    records.append(decode(line))
            cache = CachedSource(source, symbol)
            print(f"Checking primary causal decisions | {symbol}", flush=True)
            primary = ReplayVerifier(self.directory, source=cache)
            primary.instrument(symbol, record_member="primary", records=records)
            for case in self.cases:
                print(
                    f"Checking registered variant | {symbol} | {case["id"]}", flush=True
                )
                ReplayVerifier(self.directory, case["parameters"], cache).instrument(
                    symbol,
                    record_member=lambda e, key=case["id"]: e["variants"][key],
                    records=records,
                )
            conditions = self.market["conditions"][symbol]
            readiness = dict(complete=0, regular=0, momentum=False)
            audits = {}
            windows = {id: deque(maxlen=252) for id, _, _ in PAIRS}
            previous = None
            logs[symbol] = []
            pending = None

            def retain(id, layer, policy, row, index):
                if not scored(source.days[index]["date"], self.scope):
                    return
                row["condition"] = conditions[index]["label"]
                expected.setdefault(
                    id, dict(id=id, layer=layer, symbol=symbol, policy=policy, days=[])
                )["days"].append(row)

            def risk_audit(key, row, day, market, terminal, target=None):
                if not day["scored_period"]:
                    require(
                        row["status"] == "initialization"
                        and row["net_return"] is None
                        and not row["events"],
                        "Risk initialization invented a return",
                    )
                    return
                audit = audits.setdefault(key, RiskAudit())
                audit.verify(row, (day, *market), actions, terminal, target)

            for i, envelope in enumerate(records):
                day = envelope["primary"]
                date = day["date"]
                _, opening, minutes, bars, dividend = cache.load(symbol, i)
                require(
                    set(envelope["variants"]) == {c["id"] for c in self.cases}
                    and set(envelope["unit"]) == set(UNITS)
                    and set(envelope["matched"]) == {id for id, _, _ in PAIRS},
                    "Per-date matrix differs",
                )
                fixed = {p["policy"]: p for p in day["policies"]}
                risk_bars = dict(bars)
                if (symbol, date) in source.recovered:
                    price = source.recovered[symbol, date]
                    risk_bars[minutes - 1] = (price, price, 0)
                market = (risk_bars, opening, minutes, previous)
                terminal = i + 1 == len(records)
                common[date].append(
                    (day["availability"]["operational"], day["common_operational"])
                )
                logs[symbol].append(
                    day["measurement"]["log_return"] if day["measurement"] else None
                )
                # Recompute next-session RV5 from authenticated raw endpoints.
                if pending and pending[0]["scored_period"]:
                    prior, first, old_opening, old_length = pending
                    forecast = dict(
                        date=prior["date"],
                        next_date=date,
                        future_rv5=None,
                        control_minus_candidate_qlike=None,
                    )
                    if (
                        1 in first
                        and 1 in risk_bars
                        and 0 in risk_bars
                        and all(j in first for j in range(4, old_length, 5))
                    ):
                        values = [
                            first[1][0],
                            *[first[j][1] for j in range(4, old_length, 5)],
                            risk_bars[0][0] + dividend,
                            risk_bars[1][0] + dividend,
                        ]
                        rv = math.fsum(
                            math.log(b / a) ** 2 for a, b in zip(values, values[1:])
                        )
                        hd, hi = prior["risk"]["h_daily"], prior["risk"]["h_intraday"]
                        forecast["future_rv5"] = rv
                        if hi is not None:
                            forecast["control_minus_candidate_qlike"] = (
                                math.log(hd) - math.log(hi) + rv * (1 / hd - 1 / hi)
                            )
                    same(envelope["forecast"], forecast, "Unchanged primary QLIKE")
                    qlike[symbol][prior["date"]] = forecast[
                        "control_minus_candidate_qlike"
                    ]
                else:
                    require(
                        envelope["forecast"] is None,
                        "Forecast evaluated before development",
                    )
                if terminal:
                    qlike[symbol][date] = None
                pending = (day, risk_bars, opening, minutes)
                readiness["momentum"] |= day["momentum_signal"] is not None
                for name, row in envelope["unit"].items():
                    risk = name in {"intraday_risk", "daily_only"}
                    if risk:
                        unit_day = (
                            dict(day, scored_period=self.scope.unit(date))
                            if self.scope
                            else day
                        )
                        risk_audit("unit/" + name, row, unit_day, market, terminal)
                    else:
                        ready = (self.scope is None or self.scope.unit(date)) and (
                            readiness["momentum"]
                            if name.startswith("momentum")
                            else (
                                readiness["complete"] >= 20
                                if name.startswith("continuation")
                                else readiness["regular"] >= 20
                            )
                        )
                        audits.setdefault("unit/" + name, IntradayAudit()).verify(
                            row,
                            day,
                            bars,
                            opening,
                            fixed.get(name),
                            ready,
                            1.0,
                            minutes,
                        )
                    retain(
                        f"unit/{symbol}/{name}",
                        "risk" if risk else "intraday",
                        name,
                        ledger_observation(row, risk),
                        i,
                    )
                for id, a, b in PAIRS:
                    actual = envelope["matched"][id]
                    scale = sizing(windows[id])
                    same(actual["sizing"], scale, "Prior 252 calendar slots")
                    require(
                        not windows[id] or windows[id][-1][0] < date,
                        "Current-day sizing leakage",
                    )
                    risk = id == "risk_cer_increment"
                    for side, name in (("candidate", a), ("control", b)):
                        row = actual[side]
                        multiplier = scale[side + "_scale"]
                        if risk:
                            weight = day["risk"][
                                (
                                    "intraday_weight"
                                    if side == "candidate"
                                    else "daily_weight"
                                )
                            ]
                            target = (
                                multiplier * weight
                                if multiplier is not None and weight is not None
                                else None
                            )
                            risk_audit(
                                "matched/" + id + side,
                                row,
                                day,
                                market,
                                terminal,
                                (target,),
                            )
                        else:
                            audit = audits.setdefault(
                                "matched/" + id + side, IntradayAudit()
                            )
                            if row["events"] and not hasattr(self, "ledger_fixture"):
                                self.ledger_fixture = deepcopy(
                                    (
                                        audit,
                                        row,
                                        day,
                                        bars,
                                        opening,
                                        fixed.get(name),
                                        True,
                                        multiplier,
                                        minutes,
                                    )
                                )
                            audit.verify(
                                row,
                                day,
                                bars,
                                opening,
                                fixed.get(name),
                                self.scope is None or scored(date, self.scope),
                                multiplier,
                                minutes,
                            )
                        retain(
                            f"matched/{id}/{symbol}/{side}",
                            "risk" if risk else "intraday",
                            name,
                            ledger_observation(row, risk),
                            i,
                        )
                    x, y = (observed(envelope["unit"][name]) for name in (a, b))
                    windows[id].append(
                        (date, [x, y] if x is not None and y is not None else None)
                    )
                require(
                    set(envelope["secondary_accounts"])
                    == {
                        c["id"]
                        for c in self.cases
                        if c["parameters"]["family"] == "risk"
                    },
                    "Secondary risk matrix differs",
                )
                for case in self.cases:
                    id, parameters = case["id"], case["parameters"]
                    variant = envelope["variants"][id]
                    paths = {p["policy"]: p for p in variant["policies"]}
                    for name in policies(parameters):
                        risk = parameters["family"] == "risk"
                        if risk:
                            row = envelope["secondary_accounts"][id][name]
                            risk_audit(id + name, row, variant, market, terminal)
                            out = ledger_observation(row, True)
                        else:
                            out = empty_observation()
                            if day["scored_period"] and day["common_operational"]:
                                path = paths[name]
                                require(
                                    path["execution_resolved"],
                                    "Secondary common path unresolved",
                                )
                                value = charge(path["trades"], 1)
                                held = sum(
                                    (seconds(t["exit_at"]) - seconds(t["entry_at"]))
                                    / 60
                                    for t in path["trades"]
                                )
                                out.update(
                                    net=value["net"],
                                    gross=value["gross"],
                                    cost=value["cost_per_initial_equity"],
                                    turnover=value[
                                        "traded_notional_per_initial_equity"
                                    ],
                                    holding_minutes=float(held),
                                    exposure=held / 390,
                                    trades=float(len(path["trades"])),
                                )
                        retain(
                            f"secondary/{id}/{symbol}/{name}",
                            "risk" if risk else "intraday",
                            name,
                            out,
                            i,
                        )
                if minutes == 390:
                    readiness["regular"] += 1
                    readiness["complete"] += len(bars) == 390 and not (
                        symbol == "SPY" and date == "2023-01-03"
                    )
                previous = (
                    risk_bars[minutes - 1][1] if minutes - 1 in risk_bars else None
                )
                if (i + 1) % 250 == 0:
                    print(
                        f"Authenticated sensitivity and funded accounts | {symbol} | {i+1}/{len(records)}",
                        flush=True,
                    )
            print(
                f"Verified fourteen causal variants and funded ledgers | {symbol}",
                flush=True,
            )
        for values in common.values():
            require(
                len(values) == 5
                and all(v[1] == all(x[0] for x in values) for v in values),
                "Original common mask differs",
            )
        same(self.market["log_returns"], logs, "Market source returns")
        self.pool(expected)
        same(
            self.series,
            [expected[k] for k in sorted(expected)],
            "Ledger-derived observations",
        )
        for i, date in enumerate(self.dates):
            values = [qlike[symbol][date] for symbol in SYMBOLS]
            value = math.fsum(values) / 5 if None not in values else None
            same(
                self.inputs["endpoints"][5]["days"][i],
                [value, 0.0] if value is not None else None,
                "Primary forecast diagnostic",
            )
        self.common = {d: all(x[0] for x in v) for d, v in common.items()}
        same(
            self.market["common_operational"],
            [self.common[d] for d in self.market["dates"]],
            "Authenticated primary mask",
        )

    def pool(self, rows):
        selections = [
            (
                f"secondary/{c['id']}",
                name,
                "risk" if c["parameters"]["family"] == "risk" else "intraday",
            )
            for c in self.cases
            for name in policies(c["parameters"])
        ]
        selections += [
            (
                "matched/" + id,
                side,
                "risk" if id == "risk_cer_increment" else "intraday",
            )
            for id, _, _ in PAIRS
            for side in ("candidate", "control")
        ]
        for prefix, name, layer in selections:
            id = f"{prefix}/equal_weight/{name}"
            days = []
            for i, date in enumerate(self.dates):
                components = [
                    rows[f"{prefix}/{symbol}/{name}"]["days"][i] for symbol in SYMBOLS
                ]
                out = {
                    k: (
                        math.fsum(c[k] for c in components) / 5
                        if all(c[k] is not None for c in components)
                        else None
                    )
                    for k in FIELDS
                }
                out["condition"] = self.market["conditions"]["equal_weight"][
                    self.market["dates"].index(date)
                ]["label"]
                days.append(out)
            rows[id] = dict(
                id=id, layer=layer, symbol="equal_weight", policy=name, days=days
            )

    def verify_derived(self):
        require(
            len(self.series) == len({r["id"] for r in self.series}) == 345,
            "Derived series matrix differs",
        )
        require(
            all(len(r["days"]) == len(self.dates) for r in self.series),
            "Derived calendar truncated",
        )
        require(
            len(self.market["common_operational"]) == len(self.market["dates"])
            and all(type(x) is bool for x in self.market["common_operational"]),
            "Derived mask calendar differs",
        )
        self.common = dict(zip(self.market["dates"], self.market["common_operational"]))
        rows = {r["id"]: r for r in self.series}
        original = deepcopy(rows)
        self.pool(rows)
        same(rows, original, "Pooled account indices")
        for symbol in (*SYMBOLS, "equal_weight"):
            condition = PriorConditions()
            values = (
                self.market["log_returns"][symbol]
                if symbol != "equal_weight"
                else [
                    math.fsum(x) / 5 if None not in x else None
                    for x in zip(*(self.market["log_returns"][s] for s in SYMBOLS))
                ]
            )
            expected = [
                condition.advance(d, r) for d, r in zip(self.market["dates"], values)
            ]
            same(self.market["conditions"][symbol], expected, "Prior-only condition")
            for row in self.series:
                if row["symbol"] == symbol:
                    same(
                        [x["condition"] for x in row["days"]],
                        [x["label"] for x in expected if scored(x["date"], self.scope)],
                        "Attribution condition",
                    )
        same(
            self.summary["groups"], groups(self.series, self.dates), "Stability groups"
        )
        require(self.inputs["dates"] == self.dates, "Inference calendar differs")
        expected_ids = [r["id"] for r in self.protocol["primary_endpoints"]]
        require(
            [e["id"] for e in self.inputs["endpoints"]] == expected_ids,
            "Matched endpoint order differs",
        )
        for i, e in enumerate(self.inputs["endpoints"]):
            same(
                e["material"],
                self.protocol["primary_endpoints"][i]["material_increment"],
                "Material increment",
            )
            require(e["cer"] == (i == 4), "Endpoint units differ")
            if i < 5:
                prefix = "matched/" + PAIRS[i][0] + "/equal_weight/"
                for j, date in enumerate(self.dates):
                    a, b = (
                        rows[prefix + side]["days"][j]["net"]
                        for side in ("candidate", "control")
                    )
                    valid = (
                        a is not None
                        and b is not None
                        and (i == 4 or self.common[date])
                    )
                    same(
                        e["days"][j],
                        [a, b] if valid else None,
                        "Matched economic endpoint",
                    )
        for index, block in enumerate((5, 10, 20, 40)):
            print(
                f"Independent secondary matched uncertainty | block {block}", flush=True
            )
            same(
                self.summary["matched_inference"][index],
                JointInference(self.inputs["endpoints"]).run(block),
                "Matched uncertainty",
            )
        self.verify_plots()

    def verify_plots(self):
        marks = Marks(self.report)
        require(
            set(marks.charts)
            == {"secondary0", "secondary1", "secondary2", "secondary3", "matched"}
            and marks.coverage == list(ENDPOINTS),
            "Static figure inventory differs",
        )
        for i, e in enumerate(self.summary["matched_inference"][3]["endpoints"]):
            cells = marks.cells[e["id"]]
            scale = 10000 if i < 4 else 100 if i == 4 else 1
            require(len(cells) == 6, "Endpoint table width differs")
            for cell, key in zip(
                cells[1:4], ("estimate", "family_lower", "material_increment")
            ):
                if e[key] is None:
                    require(cell == "NA", "Displayed unavailable bound differs")
                else:
                    require(
                        abs(float(cell) - e[key] * scale) <= 0.500001e-4,
                        "Displayed endpoint arithmetic differs",
                    )
            require(
                cells[4] == f"{e['observed_dates']} / {e['calendar_dates']}"
                and cells[5] == ("yes" if e["adequate_coverage"] else "input limited"),
                "Displayed coverage differs",
            )
        expected_points = {}
        for chart, prefix, origin, width, spacing in (
            ("secondary", "secondary/", 850, 260, 28),
            ("matched", "matched/", 800, 220, 32),
        ):
            rows = [
                g
                for g in self.summary["groups"]
                if g["dimension"] == "period"
                and g["series"].startswith(prefix)
                and "/equal_weight/" in g["series"]
            ]
            limit = (
                max(
                    [
                        0.5,
                        *[
                            abs(r["statistics"]["mean"] * 10000)
                            for r in rows
                            if r["statistics"] is not None
                        ],
                    ]
                )
                * 1.15
            )
            for key in (
                ["secondary" + str(i) for i in range(4)]
                if chart == "secondary"
                else [chart]
            ):
                same(float(marks.charts[key]["data-limit"]), limit, "Plot scale")
            for i, r in enumerate(rows):
                if r["statistics"] is not None:
                    expected_points[r["series"]] = (
                        "secondary" + str(i // 10) if chart == "secondary" else chart,
                        origin + width * r["statistics"]["mean"] * 10000 / limit,
                        40 + spacing * (i % 10 if chart == "secondary" else i),
                    )
        require(
            marks.points.keys() == expected_points.keys(), "Static marker set differs"
        )
        for id, (chart, x, y) in expected_points.items():
            actual = marks.points[id]
            require(
                actual[0] == chart
                and abs(actual[1] - x) < 1e-7
                and abs(actual[2] - y) < 1e-7,
                "Static marker coordinate differs",
            )

    def self_check(self):
        checks = 0

        def rejects(action):
            nonlocal checks
            try:
                action()
            except (ValueError, KeyError):
                checks += 1
            else:
                raise ValueError("Semantic corruption accepted")

        # These checks call semantic kernels directly and bypass artifact hashes.
        history = deque(
            (
                (
                    str(Date(2016, 1, 1) + timedelta(days=i)),
                    [0.001 * math.sin(0.13 * i), 0.002 * math.cos(0.17 * i)],
                )
                for i in range(252)
            ),
            maxlen=252,
        )
        value = sizing(history)
        native = self.load("kernel_checks.json")
        same(native["sizing"], value, "Independent native sizing fixture")
        require(
            len(native["native_checks"]) == 14
            and len(set(native["native_checks"])) == 14,
            "Native failure-check matrix differs",
        )
        require(
            value["target"] is not None
            and value["candidate_scale"] <= 1
            and value["control_scale"] <= 1,
            "Valid overlay unavailable",
        )
        history.append((str(Date(2016, 1, 1) + timedelta(days=252)), None))
        same(native["missing_slot"], sizing(history), "Retained missing-slot fixture")
        require(
            sizing(history)["target"] is None and sizing(history)["valid"] == 251,
            "Missing slot was imputed",
        )
        zero = deque(((str(i), [0.003, 0.002]) for i in range(252)), maxlen=252)
        require(
            sizing(zero)["target"] is None, "Constant returns produced nonzero risk"
        )
        checks += 3
        if hasattr(self, "ledger_fixture"):
            for field in ("cost", "cash", "notional", "price"):
                audit, row, *args = deepcopy(self.ledger_fixture)
                row["events"][0][field] += 1
                rejects(
                    lambda audit=audit, row=row, args=args: audit.verify(row, *args)
                )
            for field, value in (
                ("net_return", 0.01),
                ("scale", 1.1),
                ("status", "stopped"),
            ):
                audit, row, *args = deepcopy(self.ledger_fixture)
                row[field] = value
                rejects(
                    lambda audit=audit, row=row, args=args: audit.verify(row, *args)
                )
        altered = deepcopy(self.plan["variants"])
        altered[0]["parameters"]["momentum_hold_minutes"] = 7
        rejects(lambda: same(altered, self.cases, "Changed registered parameter"))
        rejects(lambda: same(self.plan["variants"][:-1], self.cases, "Omitted variant"))
        rows = {r["id"]: r for r in self.series}
        pooled = next(
            r
            for r in self.series
            if r["symbol"] == "equal_weight"
            and any(x["net"] is not None for x in r["days"])
        )
        changed = deepcopy(rows)
        next(x for x in changed[pooled["id"]]["days"] if x["net"] is not None)[
            "net"
        ] += 0.01
        recomputed = deepcopy(changed)
        self.pool(recomputed)
        rejects(lambda: same(changed, recomputed, "Altered pooled return"))
        old = self.report
        self.report = self.report.replace('cx="', 'cx="9', 1)
        rejects(self.verify_plots)
        self.report = old
        print(
            f"Numerical and semantic checks passed: {checks}; corruption checks bypass artifact hashes.",
            flush=True,
        )

    def bundle(self):
        names = [
            "summary.json",
            "report.html",
            "plan.json",
            "observations.jsonl",
            "market.json",
            "inference_inputs.json",
            "kernel_checks.json",
        ]
        if self.summary["provenance"]["evidence_origin"] == "synthetic":
            names += [f"{s}.jsonl" for s in SYMBOLS]
        path = self.directory / "review.zip"
        if not path.exists():
            with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED) as z:
                for name in names:
                    z.write(self.directory / name, name)
        else:
            with zipfile.ZipFile(path) as z:
                require(
                    set(z.namelist()) == set(names)
                    and all(
                        z.read(n) == (self.directory / n).read_bytes() for n in names
                    ),
                    "Stale review bundle",
                )
        print(f"Review bundle: {path}")

    def run(self, self_check=False):
        self.verify_plan()
        if not self.review_only:
            with localcontext() as context:
                context.prec = 50
                self.verify_sources()
        self.verify_derived()
        if self_check:
            self.self_check()
        self.bundle()
        stage = (
            "M7 extension"
            if self.scope is None
            else f"{self.scope.year} secondary evaluation"
        )
        role = "development" if self.scope is None else "evaluation"
        print(
            f"Verified {stage}: fourteen variants, 50 matched accounts, 45 unit accounts and 345 series across {len(self.dates)} {role} dates."
        )
        print(
            "Source scope: derived mathematics/display only; raw archive and source ledgers were not reauthenticated."
            if self.review_only
            else (
                "Source scope: analytic observations independently reconstructed; causal variants, Decimal funded ledgers and prior-only sizing verified; no provider archive used."
                if self.summary["provenance"]["evidence_origin"] == "synthetic"
                else (
                    "Source scope: pre-2024 raw archive, causal variants, Decimal funded ledgers and prior-only sizing authenticated."
                    if self.scope is None
                    else f"Source scope: authorized archive through {self.scope.year}, causal variants, Decimal funded ledgers and prior-only sizing authenticated."
                )
            )
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--self-check", action="store_true")
    parser.add_argument("--review-only", action="store_true")
    args = parser.parse_args()
    ExtensionVerifier(args.directory, args.review_only).run(args.self_check)


if __name__ == "__main__":
    try:
        main()
    except (
        ValueError,
        KeyError,
        TypeError,
        IndexError,
        OSError,
        ArithmeticError,
    ) as error:
        raise SystemExit(f"Stability extension verification failed: {error}") from None
