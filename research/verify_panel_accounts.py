"""Independent source, Decimal ledger and integer shared-account verification."""

from __future__ import annotations
import argparse
from collections import defaultdict
from copy import deepcopy
from decimal import Decimal, localcontext
from datetime import date as Date, timedelta
from html.parser import HTMLParser
import hashlib
import json
import math
from pathlib import Path
import zipfile
from verify_historical_panel import require
from evaluation_scope import scored, scope_claim
from verify_panel_replay import ReplayVerifier, clock, seconds
from verify_shared_capital import AccountAudit, SYMBOLS, FAMILIES, OWNERS
from verify_quote_reference import authenticated_sample

SCALE = 10**9
COSTS = (0, 0.5, 1, 2, 5)
NAMES = ("intraday_risk", "daily_only", "static_risk_budget", "passive_equity")
VARIANTS = (
    "candidate",
    "momentum_control",
    "continuation_control",
    "reversion_sign_control",
    "reversion_drift_control",
)


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda: f.read(1024**2), b""):
            h.update(b)
    return h.hexdigest()


def equal(a, b, label="account", money=False):
    if isinstance(b, dict):
        require(a.keys() == b.keys(), label + " fields differ")
        for k in b:
            equal(
                a[k],
                b[k],
                label + "/" + k,
                k
                in {
                    "cash",
                    "shares",
                    "receivable",
                    "liquid",
                    "equity",
                    "price",
                    "notional",
                    "cost",
                },
            )
    elif isinstance(b, list):
        require(len(a) == len(b), label + " length differs")
        for x, y in zip(a, b):
            equal(x, y, label, money)
    elif isinstance(b, (float, Decimal)):
        require(
            a is not None and math.isfinite(float(a)), label + " unavailable/nonfinite"
        )
        tolerance = (1e-7 if money else 1e-10) + 2e-12 * abs(float(b))
        require(abs(float(a) - float(b)) <= tolerance, label + " arithmetic differs")
    else:
        require(type(a) == type(b) and a == b, label + " value differs")


class RiskAudit:
    def __init__(self):
        self.cash = Decimal(100000)
        self.shares = Decimal(0)
        self.claims = {}
        self.last = 0
        self.stopped = False
        self.reason = ""
        self.previous = 100000.0
        self.price = 100.0
        self.started = False
        self.start_date = None
        self.action_date = None

    def sequence(self, row, market, actions, target, terminal):
        primary, bars, opening, minutes, _ = market
        date = primary["date"]
        events = []
        if self.start_date is None and target is None:
            return events, "awaiting_history", ""
        if self.start_date is None:
            self.start_date = date
        cursor = (
            Date.fromisoformat(self.action_date) + timedelta(days=1)
            if self.action_date
            else Date.fromisoformat(date)
        )
        continuity = Decimal(str(self.price))
        while cursor <= Date.fromisoformat(date):
            day = cursor.isoformat()
            at = seconds(day + "T00:00:00Z")
            for a in actions:
                if (
                    a["symbol"] == row["symbol"]
                    and a["ex_date"] == day
                    and day >= self.start_date
                ):
                    continuity -= Decimal(str(a["usd_per_share"]))
                    events.append(
                        ("dividend_ex", at, continuity, a["symbol"] + "_" + day)
                    )
            events.append(("dividend_payment", at, continuity, ""))
            cursor += timedelta(days=1)
        self.action_date = date
        if 0 not in bars:
            return events, "stopped", "opening_mark_unavailable"
        events.append(("mark", opening, bars[0][0], ""))
        if row["account"] != "passive_equity" or self.start_date == date:
            if target is None:
                return events, "stopped", "required_prior_risk_unavailable"
            if row["delay"] not in bars:
                return events, "stopped", "execution_open_unavailable"
            events.append(
                ("rebalance", opening + row["delay"] * 60, bars[row["delay"]][0], "")
            )
        if terminal:
            if minutes - 4 not in bars:
                return events, "stopped", "terminal_liquidation_open_unavailable"
            events.append(
                ("rebalance", opening + (minutes - 4) * 60, bars[minutes - 4][0], "")
            )
        if minutes - 1 not in bars:
            return events, "stopped", "closing_mark_unavailable"
        events.append(("mark", opening + minutes * 60, bars[minutes - 1][1], ""))
        return events, "observed", ""

    def value(self, p):
        liquid = self.cash + self.shares * Decimal(str(p))
        receivable = sum((v[1] for v in self.claims.values()), Decimal(0))
        return dict(
            cash=self.cash,
            shares=self.shares,
            receivable=receivable,
            liquid=liquid,
            equity=liquid + receivable,
        )

    def verify(self, row, market, actions, terminal, explicit_target=None):
        primary, bars, opening, minutes, previous_close = market
        prediction = primary["risk"]
        key = {
            "intraday_risk": "intraday_weight",
            "daily_only": "daily_weight",
            "static_risk_budget": "static_weight",
            "passive_equity": "passive_weight",
        }[row["account"]]
        target = (
            prediction[key]
            if all(
                prediction[k] is not None
                for k in ("intraday_weight", "daily_weight", "static_weight")
            )
            else None
        )
        if explicit_target is not None:
            target = explicit_target[0]
        equal(row["target_weight"], target, "prior-only target")
        if self.stopped:
            require(
                row["status"] == "stopped"
                and row["net_return"] is None
                and not row["events"],
                "Stopped account restarted",
            )
            require(
                row["stop_reason"] == self.reason and not row["terminal_liquidation"],
                "Stopped risk state changed",
            )
            equal(row["last_price"], self.price, "retained stopped price", True)
            equal(row["value"], self.value(self.price))
            return
        self.price = (
            previous_close
            if not self.last and previous_close is not None
            else self.price
        )
        sequence, status, reason = self.sequence(row, market, actions, target, terminal)
        require(
            row["status"] == status and row["stop_reason"] == reason,
            "Risk account availability/stop rule differs",
        )
        require(
            len(sequence) == len(row["events"]),
            "Required risk ledger event omitted or repeated",
        )
        for event, (kind, at, price, identity) in zip(row["events"], sequence):
            require(
                seconds(event["at"]) == at and event["id"] == identity,
                "Risk ledger source clock/action differs",
            )
            require(
                (
                    event["kind"] in {"buy", "sell", "hold"}
                    if kind == "rebalance"
                    else event["kind"] == kind
                ),
                "Risk ledger event sequence differs",
            )
            equal(event["price"], price, "event source price", True)
        require(
            row["fallback"]
            == (
                row["account"] == "intraday_risk"
                and prediction["available"]
                and prediction["h_intraday"] is None
            ),
            "Risk fallback flag differs",
        )
        require(
            row["terminal_liquidation"] == (terminal and status == "observed"),
            "Terminal liquidation flag differs",
        )
        trades = 0
        for event in row["events"]:
            at = seconds(event["at"])
            require(at >= self.last, "Risk ledger clock moved backwards")
            self.last = at
            p = Decimal(str(event["price"]))
            kind = event["kind"]
            notional = fee = Decimal(0)
            if kind == "dividend_ex":
                action = next(
                    (
                        a
                        for a in actions
                        if a["symbol"] == row["symbol"]
                        and a["symbol"] + "_" + a["ex_date"] == event["id"]
                    ),
                    None,
                )
                require(
                    action is not None
                    and at == seconds(action["ex_date"] + "T00:00:00Z")
                    and event["id"] not in self.claims,
                    "Unknown/duplicate dividend entitlement",
                )
                self.claims[event["id"]] = (
                    action["payable_date"],
                    self.shares * Decimal(str(action["usd_per_share"])),
                )
            elif kind == "dividend_payment":
                for identity, (due, amount) in list(self.claims.items()):
                    if due <= event["at"][:10]:
                        self.cash += amount
                        del self.claims[identity]
            else:
                minute = (at - opening) // 60
                if kind == "mark" and at == opening + minutes * 60:
                    require(minutes - 1 in bars, "Invented closing mark")
                    equal(float(p), bars[minutes - 1][1], "closing price")
                else:
                    require(minute in bars, "Invented execution/opening price")
                    equal(float(p), bars[minute][0], "observed raw open")
                if kind in {"buy", "sell", "hold"}:
                    trades += 1
                    liquid = self.cash + self.shares * p
                    weight = (
                        Decimal(0)
                        if terminal and at == opening + (minutes - 4) * 60
                        else Decimal(str(target))
                    )
                    require(
                        at
                        in {opening + row["delay"] * 60, opening + (minutes - 4) * 60}
                        and (at != opening + (minutes - 4) * 60 or terminal),
                        "Unregistered rebalance clock",
                    )
                    rate = Decimal(str(row["cost_bps"])) / 10000
                    gap = weight * liquid - self.shares * p
                    signed = gap / (1 + (rate if gap >= 0 else -rate) * weight)
                    notional = abs(signed)
                    fee = notional * rate
                    self.cash -= signed + fee
                    self.shares += signed / p
                    if abs(self.cash) < Decimal("1e-30"):
                        self.cash = Decimal(0)
                    if abs(self.shares) < Decimal("1e-30"):
                        self.shares = Decimal(0)
                else:
                    require(kind == "mark", "Unknown risk ledger event")
            equal(event["notional"], notional, "traded notional", True)
            equal(event["cost"], fee, "paid fee", True)
            equal(event["value"], self.value(p))
            self.price = float(p)
        equal(row["last_price"], self.price, "last observed risk price", True)
        equal(row["value"], self.value(row["last_price"]))
        self.price = row["last_price"]
        if row["status"] == "stopped":
            require(row["net_return"] is None, "Stopped-day return fabricated")
            self.stopped = True
            reason = row["stop_reason"]
            self.reason = reason
            require(
                (reason == "closing_mark_unavailable" and minutes - 1 not in bars)
                or (reason == "opening_mark_unavailable" and 0 not in bars)
                or (reason == "execution_open_unavailable" and row["delay"] not in bars)
                or (
                    reason == "terminal_liquidation_open_unavailable"
                    and terminal
                    and minutes - 4 not in bars
                )
                or (reason == "required_prior_risk_unavailable" and target is None),
                "Unjustified continuous-account stop",
            )
        else:
            if row["status"] == "observed":
                require(
                    any(
                        e["kind"] == "mark"
                        and seconds(e["at"]) == opening + minutes * 60
                        for e in row["events"]
                    ),
                    "Closing mark omitted",
                )
                expected = (
                    row["account"] != "passive_equity" or not self.started
                ) + int(terminal)
                require(trades == expected, "Required rebalance omitted or repeated")
                self.started = True
            else:
                require(
                    target is None and not row["events"],
                    "History-unavailable cash state differs",
                )
            nav = float(self.value(self.price)["equity"])
            equal(row["net_return"], nav / self.previous - 1, "account return")
            self.previous = nav


def selection(variant, family):
    if family == "momentum":
        return "momentum_always_long" if variant == "momentum_control" else "momentum"
    if family == "volume_conditioned_continuation":
        return (
            "continuation_price"
            if variant == "continuation_control"
            else "continuation_volume"
        )
    return {
        "reversion_sign_control": "reversion_sign_only",
        "reversion_drift_control": "reversion_drift",
    }.get(variant, "reversion_candidate")


class SharedAudit:
    def __init__(self):
        self.cash = 10**14
        self.flows = {k: 0 for k in OWNERS}
        self.generation = 0
        self.at = 0
        self.stopped = False
        self.reason = ""

    def verify(self, row, markets):
        if self.stopped:
            require(
                row["status"] == "stopped"
                and row["net_return"] is None
                and not row["transitions"]
                and row["final"] is None,
                "Shared account restarted",
            )
            require(
                row["stop_reason"] == self.reason
                and row["traded_mid_notional_units"]
                == row["crossing_cost_units"]
                == row["internal_transfers"]
                == 0,
                "Stopped shared ledger changed",
            )
            return
        header = dict(
            schema_version=4,
            model_contract="shared_capital_v1",
            generation=self.generation,
            configuration=dict(
                symbols=SYMBOLS,
                families=FAMILIES,
                usd_scale=SCALE,
                initial_cash_units=10**14,
                owner_budget_denominator=15,
                family_budget_denominator=3,
                instrument_budget_denominator=5,
                fee_per_share_ceiling_units=0,
                maximum_quote_age_seconds=2,
                dispatch_window_seconds=30,
            ),
            flat_checkpoint=dict(
                cash_units=self.cash,
                at_seconds=self.at,
                owner_cash_flows=[
                    dict(family=k[0], symbol=k[1], cash_flow_units=v)
                    for k, v in self.flows.items()
                ],
            ),
        )
        audit = AccountAudit(header)
        events = iter(row["transitions"])
        notional = paid = transfers = 0
        exposure = 0.0

        def step(i):
            nonlocal transfers, exposure
            record = next(events)
            effects = record["effects"]
            audit.apply_compact(
                i, effects, record["claims"], record["checkpoint_sha256"]
            )
            transfers += len(effects["transfers"])
            exposure = max(
                exposure,
                sum(
                    v["shares"] * audit.mark(k[1])
                    for k, v in audit.owners.items()
                    if v["shares"]
                )
                / audit.equity(),
            )
            return effects

        opening = next(iter(markets.values()))[2]
        before = self.cash
        step(dict(kind="reconnect", at_seconds=opening))
        step(
            dict(
                kind="reconcile",
                at_seconds=opening,
                cash_units=self.cash,
                positions=dict.fromkeys(SYMBOLS, 0),
                open_orders=[],
            )
        )
        schedule = defaultdict(list)
        unavailable = set()
        for symbol, (primary, _, _, _, _) in markets.items():
            for f in FAMILIES:
                policy = next(
                    (
                        p
                        for p in primary["policies"]
                        if p["policy"] == selection(row["variant"], f)
                    ),
                    None,
                )
                if policy is None:
                    continue
                for d in policy["decisions"]:
                    at = seconds(d["at"]) + row["delay"] * 60
                    if d["target_long"] is None:
                        unavailable.add(at)
                        schedule[at]
                        break
                    schedule[at].append(
                        dict(family=f, symbol=symbol, long_target=d["target_long"])
                    )

        def dispatch(first, at):
            nonlocal paid, notional
            tasks = [("effects", first)]
            count = 0

            def report(
                identity,
                order,
                kind,
                execution="",
                quantity=0,
                price=0,
                cumulative=0,
                cancel="",
            ):
                return dict(
                    kind="report",
                    at_seconds=at,
                    report=dict(
                        event_id=identity,
                        order_id=order,
                        report_kind=kind,
                        execution_id=execution,
                        fill_shares=quantity,
                        fill_price_units=price,
                        fee_units=0,
                        cumulative_filled_shares=cumulative,
                        cancel_id=cancel,
                    ),
                )

            while tasks:
                count += 1
                require(count < 10000, "Shared replay loop")
                kind, v = tasks.pop()
                if kind == "effects":
                    tasks.extend(("ack", o) for o in reversed(v["orders"]))
                    tasks.extend(("cancel", c) for c in reversed(v["cancels"]))
                elif kind == "ack":
                    tasks.append(("fill", v))
                    tasks.append(
                        ("effects", step(report("A" + v["id"], v["id"], "accepted")))
                    )
                elif kind == "fill":
                    if v["id"] not in audit.active():
                        continue
                    mid = audit.mark(v["symbol"])
                    notional += v["shares"] * mid
                    paid += v["shares"] * abs(v["limit_units"] - mid)
                    tasks.append(
                        (
                            "effects",
                            step(
                                report(
                                    "F" + v["id"],
                                    v["id"],
                                    "fill",
                                    "X" + v["id"],
                                    v["shares"],
                                    v["limit_units"],
                                )
                            ),
                        )
                    )
                else:
                    o = audit.active().get(v["order_id"])
                    tasks.append(
                        (
                            "effects",
                            step(
                                report(
                                    "C" + v["cancel_id"],
                                    v["order_id"],
                                    "canceled" if o else "cancel_rejected",
                                    cumulative=o["filled_shares"] if o else 0,
                                    cancel=v["cancel_id"],
                                )
                            ),
                        )
                    )

        for index, (at, signals) in enumerate(sorted(schedule.items()), 1):
            quotes = []
            for symbol, (primary, bars, start, _, _) in markets.items():
                minute = (at - start) // 60
                if minute not in bars:
                    break
                mid = math.floor(bars[minute][0] * SCALE + 0.5)
                charge = (mid * round(row["cost_bps"] * 2) + 19999) // 20000
                quotes.append(
                    dict(
                        symbol=symbol,
                        source_at_seconds=at,
                        bid_units=mid - charge,
                        ask_units=mid + charge,
                    )
                )
            if len(quotes) != 5 or at in unavailable:
                require(
                    row["status"] == "stopped"
                    and row["stop_reason"]
                    == (
                        "shared_execution_quote_unavailable"
                        if len(quotes) != 5
                        else "shared_primary_target_unavailable"
                    ),
                    "Shared missing-reference stop differs",
                )
                self.stopped = True
                self.reason = row["stop_reason"]
                break
            dispatch(
                step(
                    dict(
                        kind="targets",
                        id="D" + str(index),
                        at_seconds=at,
                        quotes=quotes,
                        signals=signals,
                    )
                ),
                at,
            )
        if not self.stopped:
            require(
                not audit.active()
                and all(
                    v["shares"] == v["desired_shares"] == 0
                    for v in audit.owners.values()
                ),
                "Shared session not flat",
            )
            close = opening + next(iter(markets.values()))[3] * 60
            step(dict(kind="seal", at_seconds=close))
            self.at = close
            self.cash = audit.cash
            self.flows = {k: v["cash_flow_units"] for k, v in audit.owners.items()}
            self.generation += 1
            equal(row["net_return"], (self.cash - before) / before, "shared return")
        require(next(events, None) is None, "Extra shared transition")
        require(row["final"] == audit.snapshot(), "Final shared ledger differs")
        require(
            row["traded_mid_notional_units"] == notional
            and row["crossing_cost_units"] == paid
            and row["internal_transfers"] == transfers,
            "Shared economic totals differ",
        )
        equal(row["maximum_gross_fraction"], exposure, "shared exposure")


def cer(values):
    if len(values) < 2:
        return None
    mean = math.fsum(values) / len(values)
    variance = math.fsum((v - mean) ** 2 for v in values) / (len(values) - 1)
    return 252 * (mean - 1.5 * variance)


class SeriesAudit:
    def __init__(self):
        self.path = []
        self.returns = []
        self.nav = self.peak = 100000.0
        self.drawdown = 0.0
        self.exposure = 0.0
        self.cost = self.notional = 0.0
        self.started = self.stopped = None
        self.reason = ""

    def append(self, date, row, risk):
        if self.started is None and row["status"] != "awaiting_history":
            self.started = date
        if self.stopped is None and row["status"] == "stopped":
            self.stopped = date
            self.reason = row["stop_reason"]
        if risk:
            self.cost += math.fsum(e["cost"] for e in row["events"])
            self.notional += math.fsum(e["notional"] for e in row["events"])
        else:
            self.cost += row["crossing_cost_units"] / SCALE
            self.notional += row["traded_mid_notional_units"] / SCALE
        if row["net_return"] is None:
            return
        nav = row["value"]["equity"] if risk else row["final"]["cash_units"] / SCALE
        self.returns.append(nav / self.nav - 1)
        self.nav = nav
        self.path.append([date, nav])
        self.peak = max(self.peak, nav)
        self.drawdown = max(self.drawdown, 1 - nav / self.peak)
        self.exposure += (
            row["value"]["shares"] * row["last_price"] / row["value"]["liquid"]
            if risk
            else row["maximum_gross_fraction"]
        )

    def statistics(self):
        n = len(self.returns)
        return dict(
            observed_dates=n,
            started=self.started,
            stopped=self.stopped,
            stop_reason=self.reason,
            last_observed_equity=self.nav,
            mean_return=math.fsum(self.returns) / n if n else None,
            cer=cer(self.returns),
            observed_prefix_drawdown=self.drawdown,
            traded_notional=self.notional,
            cost_paid=self.cost,
            mean_exposure=self.exposure / n if n else None,
            equity_path=self.path,
        )


def forecasts(markets, days, index, actions, scope=None):
    result = []
    current = days[index]["date"]
    for symbol in ("SPY", "QQQ", "IWM", "TLT", "GLD"):
        if index and scored(days[index - 1]["date"], scope):
            prior = days[index - 1]["date"]
            p, first, _, length, _ = markets[prior][symbol]
            now, bars, _, _, _ = markets[current][symbol]
            for delay in (1, 2):
                rv = endpoint = qlike = None
                if delay in first and delay in bars and 0 in bars:
                    dividend = math.fsum(
                        a["usd_per_share"]
                        for a in actions
                        if a["symbol"] == symbol and a["ex_date"] == current
                    )
                    entry = first[delay][0]
                    exit = bars[delay][0] + dividend
                    endpoint = math.log(exit / entry) ** 2
                    if all(m in first for m in range(4, length, 5)):
                        levels = (
                            [entry]
                            + [first[m][1] for m in range(4, length, 5)]
                            + [bars[0][0] + dividend, exit]
                        )
                        rv = math.fsum(
                            math.log(b / a) ** 2 for a, b in zip(levels, levels[1:])
                        )
                        hd, hi = p["risk"]["h_daily"], p["risk"]["h_intraday"]
                        if hi is not None:
                            qlike = math.log(hd) - math.log(hi) + rv * (1 / hd - 1 / hi)
                result.append(
                    dict(
                        symbol=symbol,
                        date=prior,
                        next_date=current,
                        delay=delay,
                        future_rv5=rv,
                        endpoint2=endpoint,
                        control_minus_candidate_qlike=qlike,
                    )
                )
        if index + 1 == len(days) and scored(current, scope):
            for delay in (1, 2):
                result.append(
                    dict(
                        symbol=symbol,
                        date=current,
                        next_date=None,
                        delay=delay,
                        future_rv5=None,
                        endpoint2=None,
                        control_minus_candidate_qlike=None,
                    )
                )
    return result


def forecast_statistics(rows):
    groups = defaultdict(list)
    for row in rows:
        groups[row["symbol"], row["delay"]].append(row)
    result = []
    for (symbol, delay), values in sorted(groups.items()):
        losses = [
            v["control_minus_candidate_qlike"]
            for v in values
            if v["control_minus_candidate_qlike"] is not None
        ]
        result.append(
            dict(
                symbol=symbol,
                delay=delay,
                forecast_dates=len(values),
                future_rv5_dates=sum(v["future_rv5"] is not None for v in values),
                endpoint_dates=sum(v["endpoint2"] is not None for v in values),
                qlike_dates=len(losses),
                mean_control_minus_candidate_qlike=(
                    math.fsum(losses) / len(losses) if losses else None
                ),
            )
        )
    return result


def comparisons(rows, risk):
    result = []
    for a in rows:
        if (risk and a["account"] != "intraday_risk") or (
            not risk and a["variant"] != "candidate"
        ):
            continue
        for b in rows:
            if a["delay"] != b["delay"] or a["cost_bps"] != b["cost_bps"]:
                continue
            if risk and (a["symbol"] != b["symbol"] or b["account"] == "intraday_risk"):
                continue
            if not risk and b["variant"] == "candidate":
                continue
            x, y = a["statistics"]["equity_path"], b["statistics"]["equity_path"]
            n = min(len(x), len(y))
            ar = []
            br = []
            ap = bp = 100000.0
            for av, bv in zip(x[:n], y[:n]):
                require(av[0] == bv[0], "Paired account dates differ")
                ar.append(av[1] / ap - 1)
                br.append(bv[1] / bp - 1)
                ap, bp = av[1], bv[1]
            key = lambda v: v["symbol"] + "/" + v["account"] if risk else v["variant"]
            result.append(
                dict(
                    candidate=key(a),
                    control=key(b),
                    delay=a["delay"],
                    cost_bps=a["cost_bps"],
                    common_observed_dates=n,
                    mean_daily_increment=(
                        math.fsum(v - w for v, w in zip(ar, br)) / n if n else None
                    ),
                    annual_cer_increment=cer(ar) - cer(br) if n > 1 else None,
                )
            )
    return result


class ReportMarks(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.charts = {}
        self.curves = {}
        self.rows = {}
        self.active = None
        self.cell = None
        self.chart = None
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "svg":
            self.chart = a["data-chart"]
            require(self.chart not in self.charts, "Duplicate account chart")
            self.charts[self.chart] = a
        if tag == "polyline":
            key = self.chart, a["data-series"]
            require(key not in self.curves, "Duplicate plotted series")
            self.curves[key] = a["points"]
        if tag == "tr":
            for key in ("data-case", "data-pair", "data-forecast"):
                if key in a:
                    self.active = (key, a[key])
                    require(self.active not in self.rows, "Duplicate account table row")
                    self.rows[self.active] = []
        if tag == "td" and self.active is not None:
            self.cell = ""

    def handle_data(self, value):
        if self.cell is not None:
            self.cell += value

    def handle_endtag(self, tag):
        if tag == "td" and self.cell is not None:
            self.rows[self.active].append(self.cell)
            self.cell = None
        if tag == "tr":
            self.active = None
        if tag == "svg":
            self.chart = None


def verify_report(s, text):
    marks = ReportMarks(text)
    require(
        set(marks.charts) == set(SYMBOLS) | {"shared"}, "Account chart coverage differs"
    )
    dates = s["development_dates"]
    positions = {d: i for i, d in enumerate(dates)}
    curves = 0
    tables = 0
    key = lambda r, risk: r["symbol"] + "/" + r["account"] if risk else r["variant"]

    def cells(actual, expected):
        require(len(actual) == len(expected), "Account table width differs")
        for a, b in zip(actual, expected):
            if isinstance(b, (int, float)):
                require(
                    math.isfinite(float(a))
                    and abs(float(a) - b) <= 0.500001e-6 + 1e-10 * abs(b),
                    "Displayed account value differs",
                )
            else:
                require(a == b, "Displayed account label/NA differs")

    for chart in list(("SPY", "QQQ", "IWM", "TLT", "GLD")) + ["shared"]:
        risk = chart != "shared"
        rows = [
            r
            for r in s["risk_series" if risk else "shared_series"]
            if r["delay"] == 1
            and r["cost_bps"] == 1
            and (not risk or r["symbol"] == chart)
        ]
        nav = [100000.0] + [p[1] for r in rows for p in r["statistics"]["equity_path"]]
        lo = min(nav)
        hi = max(nav)
        pad = max(1.0, (hi - lo) * 0.08)
        lo -= pad
        hi += pad
        require(
            abs(float(marks.charts[chart]["data-low"]) - lo) < 1e-7
            and abs(float(marks.charts[chart]["data-high"]) - hi) < 1e-7,
            "Account plot scale differs",
        )
        for r in rows:
            actual = [
                tuple(map(float, p.split(",")))
                for p in marks.curves[chart, key(r, risk)].split()
            ]
            wanted = [
                (
                    80 + 785 * positions[d] / max(1, len(dates) - 1),
                    270 - 230 * (v - lo) / (hi - lo),
                )
                for d, v in r["statistics"]["equity_path"]
            ]
            require(
                len(actual) == len(wanted)
                and all(
                    abs(a - x) <= 0.5e-6 + 1e-8 and abs(b - y) <= 0.5e-6 + 1e-8
                    for (a, b), (x, y) in zip(actual, wanted)
                ),
                "Account curve vertices differ",
            )
            curves += 1
    require(len(marks.curves) == curves == 25, "Extra or missing account curves")
    for risk in (True, False):
        for r in s["risk_series" if risk else "shared_series"]:
            st = r["statistics"]
            identity = (
                key(r, risk) + "/" + str(r["delay"]) + "/" + json.dumps(r["cost_bps"])
            )
            actual = marks.rows["data-case", identity]
            cells(
                actual,
                [
                    key(r, risk),
                    r["delay"],
                    r["cost_bps"],
                    st["observed_dates"],
                    st["last_observed_equity"],
                    100 * st["observed_prefix_drawdown"],
                    st["traded_notional"] / 100000,
                    st["cost_paid"],
                    "NA" if st["mean_exposure"] is None else 100 * st["mean_exposure"],
                    st["stopped"] or "none",
                ],
            )
            tables += 1
        for p in s["risk_comparisons" if risk else "shared_comparisons"]:
            if p["delay"] != 1 or p["cost_bps"] != 1:
                continue
            cells(
                marks.rows["data-pair", p["candidate"] + "|" + p["control"]],
                [
                    p["candidate"] + " / " + p["control"],
                    p["common_observed_dates"],
                    (
                        "NA"
                        if p["mean_daily_increment"] is None
                        else 10000 * p["mean_daily_increment"]
                    ),
                    (
                        "NA"
                        if p["annual_cer_increment"] is None
                        else 100 * p["annual_cer_increment"]
                    ),
                ],
            )
            tables += 1
    for f in s["forecast_statistics"]:
        cells(
            marks.rows["data-forecast", f["symbol"] + "/" + str(f["delay"])],
            [
                f["symbol"] + " / " + str(f["delay"]),
                f["forecast_dates"],
                f["future_rv5_dates"],
                f["endpoint_dates"],
                f["qlike_dates"],
                (
                    "NA"
                    if f["mean_control_minus_candidate_qlike"] is None
                    else f["mean_control_minus_candidate_qlike"]
                ),
            ],
        )
        tables += 1
    require(len(marks.rows) == tables == 279, "Account table coverage differs")


def expected_plan(actions, scope=None):
    return dict(
        policy_sha256="827dee82278dbabf0f5d8fd6d0980afe07cd01c876847507df38080a587dc2ca",
        protocol_sha256="269d6e677a3e62a21ede6e88659edb2f7a998bfe0137e62b13458e411f0c55ee",
        symbols=["SPY", "QQQ", "IWM", "TLT", "GLD"],
        shared_usd_scale=SCALE,
        shared_fee_per_share_units=0,
        shared_budget_denominators=dict(owner=15, family=3, instrument=5),
        risk_accounts=list(NAMES),
        costs_bps=list(map(float, COSTS)),
        delays_minutes=[1, 2],
        shared_variants=list(VARIANTS),
        initial_usd=100000,
        risk_start="first_scored_date_with_all_three_prior_weights",
        risk_terminal=(
            "last_evaluation_session_close_minus_four_minutes_actual_open"
            if scope
            else "last_development_session_close_minus_four_minutes_actual_open"
        ),
        shared_fill_model="synchronous_full_fill_at_cost_shifted_raw_opens_no_observed_liquidity",
        shared_cost_rounding="raw_price_nearest_nanodollar_charge_ceiling_nanodollar_per_share",
        shared_sizing_clock="arrival_at_decision_plus_delay",
        shared_daily_handover="sealed_flat_checkpoint_no_capital_reset",
        missing_required_reference="stop_at_observation_without_pre_gap_liquidation_or_restart",
        cash_interest=0,
        accounting_actions=actions,
    )


def semantic_checks(samples, s, report, observations):
    passed = []
    absent = []

    def rejects(label, run):
        try:
            run()
        except ValueError:
            passed.append(label)
        else:
            raise ValueError("Semantic corruption accepted: " + label)

    def risk_mutation(label, source, change):
        if source not in samples:
            absent.append(label)
            return
        audit, row, market, actions, terminal = deepcopy(samples[source])
        change(row)
        rejects(label, lambda: audit.verify(row, market, actions, terminal))

    risk_mutation(
        "risk cash",
        "risk_trade",
        lambda r: r["value"].__setitem__("cash", r["value"]["cash"] + 1),
    )
    risk_mutation(
        "risk fee",
        "risk_trade",
        lambda r: next(
            e for e in r["events"] if e["kind"] in {"buy", "sell"}
        ).__setitem__(
            "cost",
            0.01 + next(e for e in r["events"] if e["kind"] in {"buy", "sell"})["cost"],
        ),
    )
    risk_mutation(
        "omitted rebalance",
        "risk_trade",
        lambda r: r["events"].pop(
            next(i for i, e in enumerate(r["events"]) if e["kind"] in {"buy", "sell"})
        ),
    )
    risk_mutation(
        "omitted dividend",
        "risk_dividend",
        lambda r: r["events"].pop(
            next(i for i, e in enumerate(r["events"]) if e["kind"] == "dividend_ex")
        ),
    )
    risk_mutation(
        "risk restart",
        "risk_stopped",
        lambda r: r.update(status="observed", net_return=0.0),
    )
    for label in ("shared quantity", "shared cash"):
        if "shared_order" not in samples:
            absent.append(label)
            continue
        audit, row, markets = deepcopy(samples["shared_order"])
        if label == "shared quantity":
            next(t for t in row["transitions"] if t["effects"]["orders"])["effects"][
                "orders"
            ][0]["shares"] += 1
        else:
            row["final"]["cash_units"] += 1
        rejects(label, lambda: audit.verify(row, markets))
    if "shared_stopped" in samples:
        audit, row, markets = deepcopy(samples["shared_stopped"])
        row.update(status="observed", net_return=0.0)
        rejects("shared restart", lambda: audit.verify(row, markets))
    else:
        absent.append("shared restart")
    if "forecast" in samples:
        actual, wanted = deepcopy(samples["forecast"])
        next(v for v in actual if v["future_rv5"] is not None)["future_rv5"] += 0.01
        rejects("future risk measurement", lambda: equal(actual, wanted, "forecast"))
        actual, wanted = deepcopy(samples["forecast"])
        actual[0]["next_date"] = "2024-01-02"
        rejects("future risk clock", lambda: equal(actual, wanted, "forecast"))
    else:
        absent.extend(["future risk measurement", "future risk clock"])
    r = s["risk_series"][0]
    key = (r["symbol"], r["account"], r["delay"], r["cost_bps"])
    stats = deepcopy(r["statistics"])
    stats["cost_paid"] += 1
    rejects(
        "summary cost",
        lambda: equal(stats, observations["risk", key].statistics(), "summary"),
    )
    marks = ReportMarks(report)
    points = next(p for p in marks.curves.values() if p.strip())
    first, *other = points.split()
    x, y = map(float, first.split(","))
    changed = f"{x+.01},{y} " + " ".join(other) + " "
    altered = report.replace('points="' + points + '"', 'points="' + changed + '"', 1)
    require(altered != report, "Plot corruption fixture not applied")
    rejects("plotted vertex", lambda: verify_report(s, altered))
    if s["provenance"].get("fixture") == "analytic_panel_300":
        require(
            len(passed) == 12 and not absent,
            "Constructed corruption boundary coverage incomplete",
        )
    print(
        f"Semantic corruption checks passed: {len(passed)}; artifact hashes bypassed for these checks."
    )
    if absent:
        print("Corruption boundaries unavailable in this dataset:", ", ".join(absent))


def verify(run, self_check=False, scope=None):
    oracle = ReplayVerifier(run, scope=scope)
    # The source oracle retains Decimal tokens for acquisition identities.
    # Derived statistics use binary64; cash/share reconstruction stays Decimal.
    s = json.loads((run / "summary.json").read_text(encoding="utf-8"))
    plan = authenticated_sample((run / "plan.json").read_bytes(), s["plan_sha256"])
    require(plan == s["plan"], "Saved account plan differs")
    require(
        s["kind"] == "panel_account_study"
        and s["network_requests"] == 0
        and scope_claim(s, scope),
        "Account scope differs",
    )
    require(
        {a["path"] for a in s["artifacts"]}
        == {symbol + ".jsonl" for symbol in SYMBOLS}
        | {"account_days.jsonl", "plan.json"}
        and len(s["artifacts"]) == 7,
        "Account artifact inventory differs",
    )
    for a in s["artifacts"]:
        require(
            digest(run / a["path"]) == a["sha256"]
            and (run / a["path"]).stat().st_size == a["bytes"],
            "Account artifact changed",
        )
    markets = defaultdict(dict)
    calendar = {d["date"]: d for d in oracle.source.days}
    for symbol in ("SPY", "QQQ", "IWM", "TLT", "GLD"):
        previous = [None]
        captured = [0]

        def capture(primary, bars, opening, symbol=symbol):
            minutes = 390 if calendar[primary["date"]]["close"] == "16:00" else 210
            if (symbol, primary["date"]) in oracle.source.recovered:
                bars = dict(bars)
                bars[minutes - 1] = (
                    0,
                    oracle.source.recovered[symbol, primary["date"]],
                    0,
                )
            needed = {0, 1, 2, minutes - 4, minutes - 1} | set(range(4, minutes, 5))
            for p in primary["policies"]:
                for d in p["decisions"]:
                    needed.update(
                        (int((seconds(d["at"]) - opening) // 60) + lag)
                        for lag in (1, 2)
                    )
            markets[primary["date"]][symbol] = (
                primary,
                {i: v for i, v in bars.items() if i in needed},
                opening,
                minutes,
                previous[0],
            )
            previous[0] = bars.get(minutes - 1, (0, None))[1]
            captured[0] += 1
            if captured[0] % 250 == 0:
                print(
                    f"Authenticated causal account inputs | {symbol} | {captured[0]}/{len(oracle.source.days)}",
                    flush=True,
                )

        oracle.instrument(symbol, run / (symbol + ".jsonl"), observer=capture)
    actions = s["plan"]["accounting_actions"]
    if oracle.source.synthetic:
        expected = [
            dict(
                id="constructed_QQQ_cash",
                symbol="QQQ",
                ex_date=oracle.source.days[268]["date"],
                payable_date=oracle.source.days[271]["date"],
                usd_per_share=1.25,
            )
        ]
        if scope:
            expected.append(
                dict(
                    id="constructed_QQQ_evaluation_cash",
                    symbol="QQQ",
                    ex_date=oracle.source.days[600]["date"],
                    payable_date=oracle.source.days[603]["date"],
                    usd_per_share=0.75,
                )
            )
    else:
        expected = [
            a
            for a in json.loads(
                (
                    oracle.source.oracle.directory / "sealed/accounting_actions.json"
                ).read_text(encoding="utf-8")
            )["cash_distributions"]
            if a["ex_date"] <= (scope.last if scope else "2023-12-31")
        ]
    require(actions == expected, "Accounting action provenance differs")
    equal(plan, expected_plan(actions, scope), "executable account plan")
    for d, values in oracle.common.items():
        require(
            len(values) == 5
            and all(reported == all(v[0] for v in values) for _, reported in values),
            "Common operational mask differs",
        )
    risk = {}
    shared = {}
    counts = [0, 0]
    observations = defaultdict(SeriesAudit)
    forecast_rows = []
    samples = {}
    day_count = 0
    risk_keys = {
        (symbol, name, delay, cost)
        for symbol in ("SPY", "QQQ", "IWM", "TLT", "GLD")
        for name in NAMES
        for delay in (1, 2)
        for cost in COSTS
    }
    shared_keys = {
        (variant, delay, cost)
        for variant in VARIANTS
        for delay in (1, 2)
        for cost in COSTS
    }
    with localcontext() as context:
        context.prec = 50
        with (run / "account_days.jsonl").open(encoding="utf-8") as f:
            for index, line in enumerate(f):
                require(len(line) <= 16 * 1024**2, "Account day exceeds its bound")
                require(
                    index < len(oracle.source.days), "Account calendar has extra dates"
                )
                day_count += 1
                row = json.loads(line)
                date = oracle.source.days[index]["date"]
                require(row["date"] == date, "Account calendar differs")
                expected_forecasts = forecasts(
                    markets, oracle.source.days, index, actions, scope
                )
                equal(row["forecasts"], expected_forecasts, "holding-risk outcomes")
                forecast_rows.extend(expected_forecasts)
                if (
                    self_check
                    and "forecast" not in samples
                    and any(v["future_rv5"] is not None for v in expected_forecasts)
                ):
                    samples["forecast"] = (
                        deepcopy(row["forecasts"]),
                        deepcopy(expected_forecasts),
                    )
                if not scored(date, scope):
                    require(
                        not row["risk"] and not row["shared"], "Initialization traded"
                    )
                    continue
                require(
                    len(row["risk"]) == 200 and len(row["shared"]) == 50,
                    "Account matrix incomplete",
                )
                require(
                    {
                        (r["symbol"], r["account"], r["delay"], r["cost_bps"])
                        for r in row["risk"]
                    }
                    == risk_keys
                    and {
                        (r["variant"], r["delay"], r["cost_bps"]) for r in row["shared"]
                    }
                    == shared_keys,
                    "Registered daily case identities differ",
                )
                for r in row["risk"]:
                    key = (r["symbol"], r["account"], r["delay"], r["cost_bps"])
                    audit = risk.setdefault(key, RiskAudit())
                    if self_check:
                        labels = []
                        if any(e["kind"] in {"buy", "sell"} for e in r["events"]):
                            labels.append("risk_trade")
                        if any(e["kind"] == "dividend_ex" for e in r["events"]):
                            labels.append("risk_dividend")
                        if audit.stopped:
                            labels.append("risk_stopped")
                        for label in labels:
                            if label not in samples:
                                samples[label] = (
                                    deepcopy(audit),
                                    deepcopy(r),
                                    markets[date][r["symbol"]],
                                    actions,
                                    index + 1 == len(oracle.source.days),
                                )
                    audit.verify(
                        r,
                        markets[date][r["symbol"]],
                        actions,
                        index + 1 == len(oracle.source.days),
                    )
                    counts[0] += 1
                    observations["risk", key].append(date, r, True)
                for r in row["shared"]:
                    key = (r["variant"], r["delay"], r["cost_bps"])
                    audit = shared.setdefault(key, SharedAudit())
                    if self_check:
                        labels = []
                        if any(t["effects"]["orders"] for t in r["transitions"]):
                            labels.append("shared_order")
                        if audit.stopped:
                            labels.append("shared_stopped")
                        for label in labels:
                            if label not in samples:
                                samples[label] = (
                                    deepcopy(audit),
                                    deepcopy(r),
                                    markets[date],
                                )
                    audit.verify(r, markets[date])
                    counts[1] += 1
                    observations["shared", key].append(date, r, False)
                if (index + 1) % 250 == 0:
                    print(
                        f"Verified account ledgers | {index+1}/{len(oracle.source.days)} dates",
                        flush=True,
                    )
    require(
        day_count == len(oracle.source.days) == s["calendar_dates"],
        "Account calendar was truncated",
    )
    require(
        s["development_dates"]
        == [d["date"] for d in oracle.source.days if scored(d["date"], scope)],
        "Development calendar differs",
    )
    require(len(risk) == 200 and len(shared) == 50, "Registered case identities differ")
    for kind in ("risk", "shared"):
        require(
            len(s[kind + "_series"]) == (200 if kind == "risk" else 50),
            "Summary case coverage differs",
        )
        for r in s[kind + "_series"]:
            key = (
                (r["symbol"], r["account"], r["delay"], r["cost_bps"])
                if kind == "risk"
                else (r["variant"], r["delay"], r["cost_bps"])
            )
            equal(
                r["statistics"],
                observations[kind, key].statistics(),
                "summary statistics",
            )
    equal(
        s["forecast_statistics"],
        forecast_statistics(forecast_rows),
        "forecast statistics",
    )
    equal(
        s["risk_comparisons"],
        comparisons(s["risk_series"], True),
        "paired risk contrasts",
    )
    equal(
        s["shared_comparisons"],
        comparisons(s["shared_series"], False),
        "paired shared contrasts",
    )
    require(digest(run / "report.html") == s["report_sha256"], "Account report changed")
    report = (run / "report.html").read_text(encoding="utf-8")
    verify_report(s, report)
    if self_check:
        semantic_checks(samples, s, report, observations)
    with zipfile.ZipFile(run / "review.zip", "w", zipfile.ZIP_DEFLATED) as z:
        for name in ("summary.json", "report.html", "plan.json"):
            z.write(run / name, name)
        if oracle.source.synthetic:
            for a in s["artifacts"]:
                if a["path"] != "plan.json":
                    z.write(run / a["path"], a["path"])
    print(
        f"Verified causal sources, {counts[0]} daily risk records and {counts[1]} shared account days."
    )
    print("Review bundle:", run / "review.zip")


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("run", type=Path)
    p.add_argument("--self-check", action="store_true")
    a = p.parse_args()
    try:
        verify(a.run.resolve(), a.self_check)
    except (ValueError, KeyError, OSError, StopIteration, TypeError) as e:
        raise SystemExit("Account verification failed: " + str(e))
