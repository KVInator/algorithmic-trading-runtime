"""Rebuild the registered synthetic execution matrix and both independent ledgers."""

from __future__ import annotations

import argparse
from collections import defaultdict
from copy import deepcopy
from decimal import Decimal, localcontext
from html.parser import HTMLParser
import hashlib
import json
import math
from pathlib import Path
import re
import zipfile

from verify_process_broker import (
    BrokerAudit,
    SYMBOLS,
    FEE,
    INITIAL,
    POLICY as BROKER_POLICY,
)
from verify_shared_capital import require

POLICY = "827dee82278dbabf0f5d8fd6d0980afe07cd01c876847507df38080a587dc2ca"
SCALE = 10**9
BASE = 1672756200
POLICIES = [
    dict(id=name, aggressive_limit=crossing, cancel_after_seconds=deadline)
    for name, crossing, deadline in [
        ("cross_5s", True, 5),
        ("cross_11s", True, 11),
        ("passive_5s", False, 5),
        ("passive_11s", False, 11),
    ]
]
MOVES = dict(
    steady=[[10, 0], [12, 0], [15, 0], [18, 0], [21, 0], [24, 0], [30, 0]],
    adverse=[[10, 0], [12, 2], [15, 4], [18, 6], [21, 8], [24, 10], [30, 12]],
    reverting=[[10, 0], [12, 0], [15, -2], [18, -4], [21, 0], [24, 2], [30, 0]],
)


def same(actual, expected, context):
    if isinstance(expected, dict):
        require(
            isinstance(actual, dict) and set(actual) == set(expected),
            context + ": fields differ",
        )
        for key, value in expected.items():
            same(actual[key], value, context + "/" + key)
    elif isinstance(expected, list):
        require(
            isinstance(actual, list) and len(actual) == len(expected),
            context + ": length differs",
        )
        for i, (a, b) in enumerate(zip(actual, expected)):
            same(a, b, context + "/" + str(i))
    elif isinstance(expected, (float, Decimal)):
        require(
            isinstance(actual, (int, float))
            and math.isfinite(actual)
            and abs(Decimal(str(actual)) - Decimal(str(expected))) <= Decimal("1e-10"),
            context + ": ratio differs",
        )
    else:
        require(
            type(actual) is type(expected) and actual == expected,
            context + ": value differs",
        )


def workloads():
    result = []
    for symbol in ["SPY", "QQQ", "IWM", "TLT", "GLD"]:
        for side in ["buy", "sell"]:
            base = dict(
                id="base",
                symbol=symbol,
                side=side,
                path="steady",
                requested_shares=10,
                displayed_shares=10,
                spread_cents=2,
                outbound_seconds=0,
                receipt_seconds=0,
                cancel_processing_seconds=1,
                source_age_seconds=0,
            )
            variations = [
                base,
                dict(base, id="adverse", path="adverse"),
                dict(base, id="reverting", path="reverting"),
            ]
            for field, prefix, values in [
                ("requested_shares", "quantity", (1, 100)),
                ("displayed_shares", "displayed", (0, 1)),
                ("spread_cents", "spread", (0, 10)),
                ("outbound_seconds", "outbound", (1, 3)),
                ("receipt_seconds", "receipt", (1, 3)),
                ("cancel_processing_seconds", "cancel", (0, 3)),
                ("source_age_seconds", "age", (1, 3)),
            ]:
                variations.extend(
                    dict(base, **{"id": prefix + "_" + str(value), field: value})
                    for value in values
                )
            result.extend(
                dict(w, id=symbol + "_" + side + "_" + w["id"]) for w in variations
            )
    return result


def expected_plan():
    return dict(
        policy_sha256=POLICY,
        broker_policy_sha256=BROKER_POLICY,
        usd_scale=SCALE,
        fee_per_filled_share_units=FEE,
        initial_cash_units=INITIAL,
        session_date="2023-01-03",
        decision_seconds=10,
        dispatch_seconds=10,
        horizon_seconds=30,
        seed_seconds=1,
        seed_price_units=100 * SCALE,
        maximum_quote_age_seconds=2,
        path_moves_cents=MOVES,
        workloads=workloads(),
        policies=POLICIES,
        baseline_policy="cross_5s",
        event_order="advance_quotes_submit_cancel_completion_receipts_client_deadline",
        mode="in_process_existing_broker_state_and_independent_order_runtime",
        fees_from_historical_quotes=False,
        symbol_paths_identical=True,
        one_factor_only=True,
    )


class ClientLedger:
    def __init__(self):
        self.cash = INITIAL
        self.positions = dict.fromkeys(SYMBOLS, 0)
        self.orders = {}

    def submit(self, order):
        require(order["id"] not in self.orders, "Duplicate independent client order")
        self.orders[order["id"]] = dict(
            order=order,
            filled_shares=0,
            status="pending_new",
            acknowledged=False,
            pending_cancel_id="",
        )

    def cancel(self):
        o = self.orders["measured"]
        o["status"] = "pending_cancel"
        o["pending_cancel_id"] = "deadline"

    def receive(self, report):
        o = self.orders[report["order_id"]]
        r = o["order"]
        kind = report["report_kind"]
        if kind == "accepted":
            o["acknowledged"] = True
            if o["status"] == "pending_new":
                o["status"] = "working"
        elif kind == "fill":
            q = report["fill_shares"]
            price = report["fill_price_units"]
            fee = report["fee_units"]
            require(
                q > 0 and o["filled_shares"] + q <= r["shares"] and fee == q * FEE,
                "Independent fill quantity/fee mismatch",
            )
            buy = r["side"] == "buy"
            sign = 1 if buy else -1
            require(
                price <= r["limit_units"] if buy else price >= r["limit_units"],
                "Independent fill violates limit",
            )
            self.cash -= sign * q * price + fee
            self.positions[r["symbol"]] += sign * q
            o["filled_shares"] += q
            o["acknowledged"] = True
            if o["filled_shares"] == r["shares"]:
                o["status"] = "filled"
                o["pending_cancel_id"] = ""
            elif o["status"] != "pending_cancel":
                o["status"] = "partially_filled"
        elif kind == "canceled":
            require(
                report["cumulative_filled_shares"] == o["filled_shares"],
                "Independent cancel cumulative mismatch",
            )
            o["status"] = "canceled"
            o["pending_cancel_id"] = ""
        elif kind == "cancel_rejected":
            if o["status"] not in {"filled", "canceled", "rejected"}:
                o["status"] = "partially_filled" if o["filled_shares"] else "working"
                o["pending_cancel_id"] = ""
                o["acknowledged"] = True
        else:
            raise ValueError("Unexpected report in registered execution matrix")

    def snapshot(self):
        active = [
            o
            for o in self.orders.values()
            if o["status"] not in {"filled", "canceled", "rejected"}
        ]
        sells = dict.fromkeys(SYMBOLS, 0)
        buys = dict(sells)
        reserved = 0
        for o in active:
            r = o["order"]
            left = r["shares"] - o["filled_shares"]
            if r["side"] == "buy":
                reserved += left * (r["limit_units"] + r["fee_per_share_ceiling_units"])
                buys[r["symbol"]] += left
            else:
                sells[r["symbol"]] += left
        require(
            self.cash >= reserved >= 0
            and all(0 <= sells[s] <= self.positions[s] for s in SYMBOLS),
            "Independent client reservations invalid",
        )
        return deepcopy(
            dict(
                cash_units=self.cash,
                positions=self.positions,
                reserved_cash_units=reserved,
                reserved_sell_shares=sells,
                outstanding_buy_shares=buys,
                open_orders=len(active),
                reconciliation_required=False,
                orders=[self.orders[k] for k in sorted(self.orders)],
            )
        )


def metrics(w, fills, events):
    buy = w["side"] == "buy"
    sign = 1 if buy else -1
    n = w["requested_shares"]
    arrival2 = 200 * SCALE
    terminal2 = arrival2 + sign * 2 * MOVES[w["path"]][-1][1] * (SCALE // 100)
    filled = sum(f["quantity"] for f in fills)
    left = n - filled
    notional = sum(f["quantity"] * f["price_units"] for f in fills)
    fees = sum(f["fee_units"] for f in fills)
    spread = sum(
        sign * f["quantity"] * (2 * f["price_units"] - f["bid_units"] - f["ask_units"])
        for f in fills
    )
    timing = sum(
        sign * f["quantity"] * (f["bid_units"] + f["ask_units"] - arrival2)
        for f in fills
    )
    executed = sign * (2 * notional - filled * arrival2)
    opportunity = sign * left * (terminal2 - arrival2)
    require(executed == spread + timing, "Independent cost decomposition failed")
    ack = next(
        e
        for e in events
        if e["kind"] == "report"
        and e["activity"]["report"]["order_id"] == "measured"
        and e["activity"]["report"]["report_kind"] == "accepted"
    )
    canceled = next(
        (
            e
            for e in events
            if e["kind"] == "report"
            and e["activity"]["report"]["order_id"] == "measured"
            and e["activity"]["report"]["report_kind"] == "canceled"
        ),
        None,
    )
    ratio = lambda x, y: Decimal(x) / Decimal(y)
    return dict(
        requested_quantity=n,
        filled_quantity=filled,
        unfilled_quantity=left,
        fill_fraction=ratio(filled, n),
        fill_notional_units=notional,
        arrival_midpoint_twice_units=arrival2,
        terminal_midpoint_twice_units=terminal2,
        executed_shortfall_twice_units=executed,
        spread_component_twice_units=spread,
        timing_component_twice_units=timing,
        unfilled_opportunity_twice_units=opportunity,
        fees_twice_units=2 * fees,
        total_shortfall_twice_units=executed + opportunity + 2 * fees,
        executed_shortfall_bps=(
            ratio(10000 * executed, filled * arrival2) if filled else None
        ),
        total_shortfall_bps=ratio(
            10000 * (executed + opportunity + 2 * fees), n * arrival2
        ),
        decision_to_dispatch_seconds=0,
        acknowledgement_source_delay_seconds=ack["activity"]["at_seconds"] - BASE - 10,
        acknowledgement_receipt_delay_seconds=ack["at_seconds"] - 10,
        first_fill_source_delay_seconds=(
            fills[0]["source_seconds"] - 10 if fills else None
        ),
        first_fill_receipt_delay_seconds=(
            fills[0]["receipt_seconds"] - 10 if fills else None
        ),
        completion_source_delay_seconds=(
            fills[-1]["source_seconds"] - 10 if not left else None
        ),
        completion_receipt_delay_seconds=(
            fills[-1]["receipt_seconds"] - 10 if not left else None
        ),
        completion_censored=bool(left),
        completion_censor_reason=(
            "not_censored"
            if not left
            else "terminal_cancellation" if canceled else "observation_horizon"
        ),
        completion_source_observation_seconds=(
            fills[-1]["source_seconds"] - 10
            if not left
            else canceled["activity"]["at_seconds"] - BASE - 10 if canceled else 20
        ),
        completion_observation_seconds=(
            fills[-1]["receipt_seconds"] - 10
            if not left
            else canceled["at_seconds"] - 10 if canceled else 20
        ),
        completion_horizon_seconds=20,
        usd_scale=SCALE,
    )


class CaseOracle:
    def __init__(self, w, p):
        self.w, self.p = w, p
        self.identity = w["id"] + "_" + p["id"]
        self.broker = BrokerAudit(
            dict(
                kind="header",
                schema_version=5,
                model_contract="synthetic_portfolio_broker_v1",
                policy_sha256=BROKER_POLICY,
                account_id=self.identity,
                sequence=0,
                previous_sha256="0" * 64,
            )
        )
        self.client = ClientLedger()
        self.commands = []
        self.events = []
        self.pending = []
        self.fills = []
        self.cursor = 0
        self.cancel_dispatch = self.cancel_at = self.complete = None
        self.sent = self.completed = False
        self.cancel_outcome = "not_requested"

    def command(self, payload, at):
        command = dict(
            payload, at_seconds=BASE + at, command_id="C" + str(len(self.commands) + 1)
        )
        self.broker.apply(command)
        self.commands.append(
            deepcopy(
                dict(
                    command=command,
                    reply=dict(
                        status="ok", activity_cursor=len(self.broker.activities)
                    ),
                    state=self.broker.snapshot(),
                )
            )
        )
        for activity in self.broker.activities[self.cursor :]:
            r = activity["report"]
            due = at + (0 if r["order_id"] == "seed" else self.w["receipt_seconds"])
            self.pending.append(dict(activity=activity, due=due, delivered=False))
            if r["report_kind"] == "fill" and r["order_id"] == "measured":
                q = self.broker.quotes[self.w["symbol"]]
                self.fills.append(
                    dict(
                        quantity=r["fill_shares"],
                        price_units=r["fill_price_units"],
                        fee_units=r["fee_units"],
                        bid_units=q["bid_units"],
                        ask_units=q["ask_units"],
                        quote_sequence=q["sequence"],
                        quote_source_seconds=q["source_at_seconds"] - BASE,
                        source_seconds=at,
                        receipt_seconds=due,
                    )
                )
        self.cursor = len(self.broker.activities)

    def quote(self, seq, source, bid, ask, size):
        return dict(
            kind="quote",
            quote=dict(
                symbol=self.w["symbol"],
                sequence=seq,
                source_at_seconds=BASE + source,
                bid_units=bid,
                ask_units=ask,
                bid_shares=size,
                ask_shares=size,
            ),
        )

    def submit(self, order, at):
        self.client.submit(order)
        self.events.append(
            dict(
                kind="submit", at_seconds=at, order=order, state=self.client.snapshot()
            )
        )

    def deliver(self, at):
        for item in self.pending:
            if item["delivered"] or item["due"] != at:
                continue
            r = item["activity"]["report"]
            self.client.receive(r)
            item["delivered"] = True
            self.events.append(
                deepcopy(
                    dict(
                        kind="report",
                        at_seconds=at,
                        activity=item["activity"],
                        state=self.client.snapshot(),
                    )
                )
            )
            if r["order_id"] == "measured" and r["report_kind"] in {
                "canceled",
                "cancel_rejected",
            }:
                self.cancel_outcome = (
                    "canceled"
                    if r["report_kind"] == "canceled"
                    else "rejected_after_fill"
                )

    def cancellations(self, at):
        if self.cancel_at == at and not self.sent:
            self.command(
                dict(kind="cancel", order_id="measured", cancel_id="deadline"), at
            )
            self.sent = True
        if self.complete == at and not self.completed:
            self.command(dict(kind="complete_cancels"), at)
            self.completed = True

    def run(self):
        w, p = self.w, self.p
        self.command(dict(kind="session", date="2023-01-03"), 0)

        def order(identity, side, price):
            return dict(
                id=identity,
                symbol=w["symbol"],
                side=side,
                shares=w["requested_shares"],
                limit_units=price,
                fee_per_share_ceiling_units=FEE,
            )

        if w["side"] == "sell":
            seed = order("seed", "buy", 100 * SCALE)
            self.command(
                self.quote(1, 1, 100 * SCALE, 100 * SCALE, w["requested_shares"]), 1
            )
            self.submit(seed, 1)
            self.command(dict(kind="submit", order=seed, time_in_force="day"), 1)
            self.deliver(1)
        baseline = self.client.snapshot()
        buy = w["side"] == "buy"
        sign = 1 if buy else -1
        half = w["spread_cents"] * (SCALE // 100) // 2
        limit = 100 * SCALE + (half if buy else -half) * (
            1 if p["aggressive_limit"] else -1
        )
        measured = order("measured", w["side"], limit)
        by_time = dict(MOVES[w["path"]])
        for at in range(10, 31):
            self.command(dict(kind="advance"), at)
            if at in by_time:
                mid = 100 * SCALE + sign * by_time[at] * (SCALE // 100)
                self.command(
                    self.quote(
                        list(by_time).index(at) + 2,
                        at - w["source_age_seconds"],
                        mid - half,
                        mid + half,
                        w["displayed_shares"],
                    ),
                    at,
                )
            if at == 10:
                self.submit(measured, at)
            if at == 10 + w["outbound_seconds"]:
                self.command(
                    dict(kind="submit", order=measured, time_in_force="day"), at
                )
            self.cancellations(at)
            self.deliver(at)
            if at == 10 + p["cancel_after_seconds"] and self.client.orders["measured"][
                "status"
            ] not in {"filled", "canceled", "rejected"}:
                self.client.cancel()
                self.events.append(
                    dict(kind="cancel", at_seconds=at, state=self.client.snapshot())
                )
                self.cancel_dispatch = at
                self.cancel_at = at + w["outbound_seconds"]
                self.complete = self.cancel_at + w["cancel_processing_seconds"]
                self.cancellations(at)
                self.deliver(at)
        require(
            all(item["delivered"] for item in self.pending),
            "Independent common horizon left pending receipts",
        )
        final = self.client.snapshot()
        broker = self.broker.snapshot()
        require(
            final["cash_units"] == broker["cash_units"]
            and final["positions"] == broker["positions"]
            and final["open_orders"] == 0,
            "Independent ledgers do not reconcile",
        )
        return dict(
            id=self.identity,
            workload=w,
            policy=p,
            baseline_client=baseline,
            final_client=final,
            final_broker=broker,
            broker_commands=self.commands,
            client_events=self.events,
            fills=self.fills,
            limit_units=limit,
            cancel_dispatch_seconds=self.cancel_dispatch,
            cancel_broker_seconds=self.cancel_at,
            cancel_completion_seconds=self.complete,
            cancel_outcome=self.cancel_outcome,
            reconciled=True,
            metrics=metrics(w, self.fills, self.events),
        )


class ReportMarkup(HTMLParser):
    def __init__(self, html):
        super().__init__(convert_charrefs=True)
        self.cells = {}
        self.labels = {}
        self.policies = {}
        self.panels = set()
        self.label = None
        self.policy = None
        self.cell = None
        self.columns = []
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "section" and "data-panel" in a:
            require(a["data-panel"] not in self.panels, "Duplicate report panel")
            self.panels.add(a["data-panel"])
        if tag == "rect" and "data-case" in a:
            require(a["data-case"] not in self.cells, "Duplicate heatmap case")
            self.cells[a["data-case"]] = {
                k: float(a[k])
                for k in ["data-fill", "data-shortfall", "x", "y", "width", "height"]
            }
        if tag == "text" and "data-label" in a:
            require(a["data-label"] not in self.labels, "Duplicate heatmap label")
            self.label = a["data-label"]
            self.labels[self.label] = ""
        if tag == "tr" and "data-policy" in a:
            self.policy = a["data-policy"]
            self.columns = []
        if tag == "td" and self.policy is not None:
            self.cell = ""

    def handle_data(self, value):
        if self.label is not None:
            self.labels[self.label] += value
        if self.cell is not None:
            self.cell += value

    def handle_endtag(self, tag):
        if tag == "text":
            self.label = None
        if tag == "td" and self.cell is not None:
            self.columns.append(self.cell.strip())
            self.cell = None
        if tag == "tr" and self.policy is not None:
            require(self.policy not in self.policies, "Duplicate policy table row")
            self.policies[self.policy] = self.columns
            self.policy = None


class CostExecutionVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()

    def run(self, self_check=False):
        directory = self.directory
        summary = json.loads((directory / "summary.json").read_text())
        require(
            summary["kind"] == "cost_execution_study"
            and summary["schema_version"] == 1
            and summary["evidence_origin"] == "synthetic",
            "Study identity differs",
        )
        require(
            summary["case_count"] == summary["reconciled_cases"] == 680
            and summary["paired_comparison_count"] == 510,
            "Study counts differ",
        )
        require(
            not any(
                summary[k]
                for k in [
                    "network_requests",
                    "external_broker_events",
                    "native_child_processes",
                    "historical_strategy_scored",
                    "reserved_strategy_inputs_opened",
                ]
            ),
            "Study scope differs",
        )
        require(
            summary["policy_sha256"] == POLICY
            and summary["broker_policy_sha256"] == BROKER_POLICY,
            "Registered identities differ",
        )
        same(summary["plan"], expected_plan(), "plan")
        require(
            hashlib.sha256(
                json.dumps(
                    summary["plan"], sort_keys=True, separators=(",", ":")
                ).encode()
            ).hexdigest()
            == summary["plan_sha256"],
            "Plan identity differs",
        )
        require(len(summary["artifacts"]) == 1, "Unexpected study artifacts")
        artifact = summary["artifacts"][0]
        require(artifact["path"] == "execution_cases.jsonl", "Unexpected case path")
        path = directory / artifact["path"]
        require(
            path.stat().st_size == artifact["bytes"]
            and path.stat().st_size <= 128 * 1024 * 1024
            and hashlib.sha256(path.read_bytes()).hexdigest() == artifact["sha256"],
            "Case archive differs",
        )
        expected_matrix = [(w, p) for w in workloads() for p in POLICIES]
        rows = []
        first = None
        with path.open() as stream:
            for index, line in enumerate(stream):
                require(
                    index < 680 and len(line.encode()) <= 128 * 1024,
                    "Case count/record bound exceeded",
                )
                saved = json.loads(line)
                w, p = expected_matrix[index]
                with localcontext() as context:
                    context.prec = 50
                    expected = CaseOracle(w, p).run()
                    same(saved, expected, saved["id"])
                rows.append(saved)
                if first is None:
                    first = (saved, expected)
        require(len(rows) == 680, "Case archive incomplete")
        comparisons = []
        grouped = defaultdict(list)
        for row in rows:
            grouped[row["policy"]["id"]].append(row["metrics"])
        lookup = {r["id"]: r for r in rows}
        for row in rows:
            if row["policy"]["id"] == "cross_5s":
                continue
            base = lookup[row["workload"]["id"] + "_cross_5s"]
            comparisons.append(
                dict(
                    case_id=row["id"],
                    reference_case_id=base["id"],
                    workload_id=row["workload"]["id"],
                    policy_id=row["policy"]["id"],
                    fill_fraction_difference=row["metrics"]["fill_fraction"]
                    - base["metrics"]["fill_fraction"],
                    total_shortfall_bps_difference=row["metrics"]["total_shortfall_bps"]
                    - base["metrics"]["total_shortfall_bps"],
                )
            )
        same(summary["comparisons"], comparisons, "paired comparisons")
        policies = []
        for p in POLICIES:
            values = grouped[p["id"]]
            requested = sum(r["requested_quantity"] for r in values)
            filled = sum(r["filled_quantity"] for r in values)
            total = sum(r["total_shortfall_twice_units"] for r in values)
            denom = sum(
                r["requested_quantity"] * r["arrival_midpoint_twice_units"]
                for r in values
            )
            policies.append(
                dict(
                    policy_id=p["id"],
                    cases=len(values),
                    requested_quantity=requested,
                    filled_quantity=filled,
                    censored_cases=sum(r["completion_censored"] for r in values),
                    mean_fill_fraction=math.fsum(r["fill_fraction"] for r in values)
                    / len(values),
                    quantity_weighted_fill_fraction=filled / requested,
                    mean_total_shortfall_bps=math.fsum(
                        r["total_shortfall_bps"] for r in values
                    )
                    / len(values),
                    notional_weighted_total_shortfall_bps=10000 * total / denom,
                )
            )
        same(summary["policy_summaries"], policies, "policy summaries")
        report = directory / "report.html"
        require(
            hashlib.sha256(report.read_bytes()).hexdigest() == summary["report_sha256"],
            "Report identity differs",
        )
        markup = ReportMarkup(report.read_text())
        require(
            markup.panels
            == {s + "/" + side for s in SYMBOLS for side in ["buy", "sell"]},
            "Report panels differ",
        )
        require(
            set(markup.cells) == set(markup.labels) == set(lookup),
            "Static matrix lost cases",
        )
        for row in rows:
            cell = markup.cells[row["id"]]
            m = row["metrics"]
            w = row["workload"]
            p = row["policy"]
            group = [
                x
                for x in workloads()
                if x["symbol"] == w["symbol"] and x["side"] == w["side"]
            ]
            r = next(i for i, x in enumerate(group) if x["id"] == w["id"])
            c = next(i for i, x in enumerate(POLICIES) if x["id"] == p["id"])
            coords = {
                "x": 235 + 200 * c,
                "y": 37 + 41 * r,
                "height": 34,
                "width": 190 * m["fill_fraction"],
                "data-fill": m["fill_fraction"],
                "data-shortfall": m["total_shortfall_bps"],
            }
            require(
                all(abs(cell[k] - v) <= 1e-9 for k, v in coords.items()),
                "Heatmap dimensions or data differ",
            )
            label = re.fullmatch(
                r"([-0-9.]+)% \| ([-0-9.]+) bp", markup.labels[row["id"]]
            )
            require(label is not None, "Heatmap label missing")
            require(
                abs(float(label[1]) - m["fill_fraction"] * 100) <= 0.050000001
                and abs(float(label[2]) - m["total_shortfall_bps"]) <= 0.000500001,
                "Heatmap label differs",
            )
        require(
            set(markup.policies) == {p["id"] for p in POLICIES},
            "Policy table missing rows",
        )
        for row in policies:
            cells = markup.policies[row["policy_id"]]
            expected = [
                row["policy_id"],
                170,
                100 * row["mean_fill_fraction"],
                100 * row["quantity_weighted_fill_fraction"],
                row["censored_cases"],
                row["mean_total_shortfall_bps"],
                row["notional_weighted_total_shortfall_bps"],
            ]
            require(
                len(cells) == 7
                and cells[0] == expected[0]
                and all(
                    abs(float(a) - b) <= 0.000000501
                    for a, b in zip(cells[1:], expected[1:])
                ),
                "Displayed policy summary differs",
            )
        if self_check:
            self.corruptions(*first)
        with zipfile.ZipFile(
            directory / "review.zip", "w", zipfile.ZIP_DEFLATED
        ) as archive:
            for name in ["summary.json", "report.html", "execution_cases.jsonl"]:
                archive.write(directory / name, name)
        print(
            "Verified 680 registered cases, independent broker/client ledgers, 510 matched contrasts, fee-inclusive costs and all 680 heatmap cells."
        )
        print("Review bundle: " + str(directory / "review.zip"))

    @staticmethod
    def corruptions(saved, expected):
        mutants = []
        for key in [
            "total_shortfall_twice_units",
            "fees_twice_units",
            "filled_quantity",
        ]:
            row = deepcopy(saved)
            row["metrics"][key] += 1
            mutants.append(row)
        row = deepcopy(saved)
        row["final_client"]["cash_units"] += 1
        mutants.append(row)
        row = deepcopy(saved)
        row["broker_commands"][2]["command"]["quote"]["source_at_seconds"] += 1
        mutants.append(row)
        row = deepcopy(saved)
        row["client_events"][-1]["at_seconds"] += 1
        mutants.append(row)
        for row in mutants:
            try:
                same(row, expected, "constructed corruption")
            except ValueError:
                pass
            else:
                raise ValueError(
                    "Semantic corruption accepted after bypassing artifact hashes"
                )
        print("Rejected six semantic corruptions without relying on artifact hashes.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    try:
        CostExecutionVerifier(args.directory).run(args.self_check)
    except (
        ValueError,
        KeyError,
        IndexError,
        TypeError,
        OSError,
        zipfile.BadZipFile,
    ) as error:
        raise SystemExit(
            "Synthetic execution verification failed: " + str(error)
        ) from None


if __name__ == "__main__":
    main()
