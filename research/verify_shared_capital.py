"""Independent integer ledger and admission audit of synthetic schema-4 journals.

Python arbitrary-precision divmod checks the native portable allocation kernel.
No native executable, private market inputs or network connection is required.
"""

import argparse
from copy import deepcopy
import hashlib
import json
import math
from pathlib import Path
import re
import zipfile

from verify_panel_replay import fit, residual, same, seconds, verify_reversion_coefficients

SYMBOLS = ["GLD", "IWM", "QQQ", "SPY", "TLT"]
FAMILIES = ["momentum", "reversion", "volume_conditioned_continuation"]
OWNERS = [(f, s) for f in FAMILIES for s in SYMBOLS]
TERMINAL = {"filled", "canceled", "rejected"}
ZERO = "0" * 64
PROTOCOL = "269d6e677a3e62a21ede6e88659edb2f7a998bfe0137e62b13458e411f0c55ee"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def owner(row):
    return row["family"], row["symbol"]


def allocate(quantity, weights):
    total = sum(weights.values())
    require(quantity >= 0 and total > 0, "Invalid allocation weights")
    pairs = {k: divmod(quantity * v, total) for k, v in weights.items()}
    result = {k: pair[0] for k, pair in pairs.items()}
    for k in sorted(weights, key=lambda k: (-pairs[k][1], k))[:quantity - sum(result.values())]:
        result[k] += 1
    return result


def frames(path, incomplete=False):
    data = path.read_bytes()
    require(len(data) <= 32 * 1024**2, "Segment exceeds byte bound")
    lines = data.splitlines(keepends=True)
    if incomplete:
        require(lines and not lines[-1].endswith(b"\n"), "Expected incomplete tail")
        require(len(lines.pop()) <= 65536, "Incomplete tail exceeds frame bound")
    result = []
    for line in lines:
        require(line.endswith(b"\n") and len(line) <= 65536, "Incomplete or oversized frame")
        payload, digest = line[:-1].rsplit(b"\t", 1)
        require(hashlib.sha256(payload).hexdigest().encode() == digest, "Frame checksum differs")
        result.append((json.loads(payload), digest.decode()))
    require(0 < len(result) <= 10001, "Missing header or excessive records")
    return result


def integer_fields(value):
    """Financial state must never pass through floating-point JSON conversion."""
    if isinstance(value, dict):
        for k, v in value.items():
            if k.endswith("_units") or k.endswith("_shares") or k.endswith("_seconds") or k in {"shares", "remaining", "generation", "sequence"}:
                require(type(v) is int and -(2**63) <= v < 2**63, "Non-integral or unrepresentable field: " + k)
            if k in {"long_target", "acknowledged", "connected", "reconciliation_required", "sealed"}:
                require(type(v) is bool, "Non-boolean state: " + k)
            integer_fields(v)
    elif isinstance(value, list):
        for item in value:
            integer_fields(item)


class StrategyAudit:
    """Check the declared constructed price fixture and its primary adapter."""

    def __init__(self, rows):
        require(sorted(r["symbol"] for r in rows) == SYMBOLS, "Missing primary strategy evidence")
        self.rows = {r["symbol"]: r for r in rows}
        def history_close(m):
            return 100 * math.exp(.002 * math.exp(-m / 50) + .00001 * math.sin(.7 * m))
        prior = {m: (100 if m == 0 else history_close(m - 1), history_close(m), 1000) for m in range(390)}
        current = dict(prior)
        for m in range(30, 390):
            close = 100 - (m - 29) / 120 if m < 150 else 99 + (m - 149) / 35
            current[m] = (current[m - 1][1], close, 3000)
        for m in range(30):
            current[m] = (*current[m][:2], 3000)
        fitted = fit([residual(prior)] * 20)
        x = residual(current)[-1]
        a, b = fitted["intercept"], fitted["slope"]
        expected_move = (a - (1 - b) * x) * b ** .2 * math.fsum(b ** j for j in range(6))
        reversion_long = x < fitted["equilibrium"] and expected_move > math.log1p(.0001) - math.log1p(-.0001)
        for row in rows:
            require(row["date"] < "2024" and row["reversion"]["fit_status"] == fitted["status"], "Constructed fit status differs")
            verify_reversion_coefficients(row["reversion"], fitted, "M4 constructed prior")
            same(row["reversion"]["current"], residual(current)[-1], "Constructed current residual differs")
            same(row["reversion"]["expected_move"], expected_move, "Constructed forecast differs")
            same(row["momentum_signal"], (current[359][1] - 100) / 100, "Constructed momentum signal differs")
            for path in row["policies"]:
                if path["policy"] in {"momentum", "reversion_candidate"}:
                    wanted = current[359][1] > 100 if path["policy"] == "momentum" else reversion_long
                    require([d["target_long"] for d in path["decisions"]] == [wanted, False], "Constructed primary target differs")
                if path["policy"] != "continuation_volume":
                    continue
                for d in path["decisions"]:
                    if d["features"] is None:
                        continue
                    m = (seconds(d["at"]) - seconds(row["session_open_utc"])) // 60
                    for k, v in dict(move=(current[m - 1][1] - 100) / 100, scale=abs((prior[m - 1][1] - 100) / 100),
                                     volume=m * 3000, median_volume=m * 1000, relative_volume=3).items():
                        same(d["features"][k], v, "Constructed continuation feature differs: " + k)

    def verify(self, command, owners):
        expected = []
        names = {"momentum": "momentum", "continuation_volume": "volume_conditioned_continuation", "reversion_candidate": "reversion"}
        for symbol, row in self.rows.items():
            for path in row["policies"]:
                if path["policy"] not in names:
                    continue
                family = names[path["policy"]]
                for d in path["decisions"]:
                    if seconds(d["at"]) != command["at_seconds"]:
                        continue
                    target = d["target_long"]
                    if family == "volume_conditioned_continuation" and d["features"] is not None:
                        f = d["features"]
                        target = f["move"] > f["scale"] and (owners[(family, symbol)]["shares"] > 0 or f["relative_volume"] > 1)
                    expected.append(dict(family=family, symbol=symbol, long_target=target))
        require(sorted(command["signals"], key=owner) == sorted(expected, key=owner), "Shared adapter differs from primary features and actual ownership")


class AccountAudit:
    """Replay observable inputs; independently verify every resulting checkpoint."""

    def __init__(self, header):
        self.config = header["configuration"]
        c = self.config
        require(header["schema_version"] == 4 and header["model_contract"] == "shared_capital_v1", "Wrong journal contract")
        require(c["symbols"] == SYMBOLS and c["families"] == FAMILIES, "Wrong owner universe")
        require([c[k] for k in ("usd_scale", "initial_cash_units", "owner_budget_denominator", "family_budget_denominator", "instrument_budget_denominator")] == [10**9, 10**14, 15, 3, 5], "Wrong registered budgets")
        flat = header["flat_checkpoint"]
        self.cash, self.at = flat["cash_units"], flat["at_seconds"]
        self.owners = {owner(r): {"family": r["family"], "symbol": r["symbol"], "shares": 0, "desired_shares": 0,
                                 "cash_flow_units": r["cash_flow_units"], "decision_at_seconds": 0, "expires_at_seconds": 0}
                       for r in flat["owner_cash_flows"]}
        require(sorted(self.owners) == OWNERS, "Flat checkpoint owner coverage differs")
        self.generation = header["generation"]
        if self.generation == 0:
            require(self.cash == 10**14 and self.at == 0 and all(r["cash_flow_units"] == 0 for r in self.owners.values()), "Root checkpoint invents capital")
        self.connected, self.reconciliation, self.sealed = False, True, False
        self.quotes, self.orders, self.batches, self.events, self.executions, self.cancels = {}, {}, {}, {}, {}, {}
        self.order_count = self.cancel_count = 0

    def active(self):
        return {k: v for k, v in self.orders.items() if v["status"] not in TERMINAL}

    def mark(self, symbol):
        q = self.quotes[symbol]
        return q["bid_units"] + (q["ask_units"] - q["bid_units"]) // 2

    def equity(self):
        return self.cash + sum(r["shares"] * self.mark(k[1]) for k, r in self.owners.items() if r["shares"])

    def exposure(self, selected=lambda k: True):
        held = sum(r["shares"] * self.mark(k[1]) for k, r in self.owners.items() if selected(k) and r["shares"])
        pending = sum(q * (max(self.mark(k[1]), o["order"]["limit_units"]) + o["order"]["fee_per_share_ceiling_units"])
                      for o in self.active().values() if o["order"]["side"] == "buy" for k, q in o["claims"].items() if selected(k))
        return held + pending

    def desired(self, k):
        r = self.owners[k]
        return r["shares"] if self.at > r["expires_at_seconds"] else r["desired_shares"]

    def reserved(self):
        return sum((o["order"]["shares"] - o["filled_shares"]) * (o["order"]["limit_units"] + o["order"]["fee_per_share_ceiling_units"])
                   for o in self.active().values() if o["order"]["side"] == "buy")

    def targets(self, i):
        canonical = deepcopy(i)
        canonical["quotes"].sort(key=lambda r: r["symbol"])
        canonical["signals"].sort(key=owner)
        if i["id"] in self.batches:
            if self.batches[i["id"]] == canonical:
                return "duplicate"
            self.connected, self.reconciliation = False, True
            return "quarantined"
        c, at = self.config, i["at_seconds"]
        valid = (at > 0 and at >= self.at and re.fullmatch(r"[A-Za-z0-9_-]{1,64}", i["id"])
                 and sorted(q["symbol"] for q in i["quotes"]) == SYMBOLS and len(i["signals"]) <= 15
                 and len({owner(r) for r in i["signals"]}) == len(i["signals"]))
        valid = valid and all(q["bid_units"] > c["fee_per_share_ceiling_units"] and q["ask_units"] >= q["bid_units"]
                              and 0 < q["source_at_seconds"] <= at and at - q["source_at_seconds"] <= c["maximum_quote_age_seconds"] for q in i["quotes"])
        valid = valid and all(owner(r) in self.owners and at > self.owners[owner(r)]["decision_at_seconds"] for r in i["signals"])
        if not valid:
            return "denied"
        self.at, self.quotes = at, {q["symbol"]: q for q in i["quotes"]}
        nav = self.equity()
        for r in i["signals"]:
            k = owner(r)
            self.owners[k].update(desired_shares=(nav // 15) // (self.quotes[k[1]]["ask_units"] + c["fee_per_share_ceiling_units"]) if r["long_target"] else 0,
                                  decision_at_seconds=at, expires_at_seconds=at + c["dispatch_window_seconds"])
        self.batches[i["id"]] = canonical
        return "applied"

    def report(self, r):
        if r["event_id"] in self.events:
            return ("duplicate" if self.events[r["event_id"]] == r else "quarantined"), []
        if r["order_id"] not in self.orders:
            return "quarantined", []
        o = self.orders[r["order_id"]]
        request, kind = o["order"], r["report_kind"]
        if r["cancel_id"] and self.cancels.get(r["cancel_id"]) != r["order_id"]:
            return "quarantined", []
        fills, outcome = [], "applied"
        if kind == "fill":
            signature = [r[k] for k in ("order_id", "fill_shares", "fill_price_units", "fee_units")]
            if r["execution_id"] in self.executions:
                outcome = "duplicate" if self.executions[r["execution_id"]] == signature else "quarantined"
                if outcome == "duplicate":
                    self.events[r["event_id"]] = r
                return outcome, []
            q, price, fee = r["fill_shares"], r["fill_price_units"], r["fee_units"]
            if (o["status"] in TERMINAL or not r["execution_id"] or q <= 0 or price <= 0 or fee < 0
                    or q > request["shares"] - o["filled_shares"] or fee > q * request["fee_per_share_ceiling_units"]
                    or (request["side"] == "buy" and price > request["limit_units"])
                    or (request["side"] == "sell" and price < request["limit_units"])):
                return "quarantined", []
            quantities = allocate(q, o["claims"])
            fees = allocate(fee, quantities)
            for k, quantity in sorted(quantities.items()):
                if not quantity:
                    continue
                signed = quantity if request["side"] == "buy" else -quantity
                self.owners[k]["shares"] += signed
                self.owners[k]["cash_flow_units"] -= signed * price + fees[k]
                self.cash -= signed * price + fees[k]
                o["claims"][k] -= quantity
                fills.append(dict(family=k[0], symbol=k[1], shares=quantity, fee_units=fees[k]))
            o["filled_shares"] += q
            o["acknowledged"] = True
            if o["filled_shares"] == request["shares"]:
                o["status"], o["pending_cancel_id"] = "filled", ""
            elif o["status"] != "pending_cancel":
                o["status"] = "partially_filled"
            self.executions[r["execution_id"]] = signature
        elif kind == "accepted":
            if o["status"] == "rejected":
                return "quarantined", []
            o["acknowledged"] = True
            if o["status"] == "pending_new":
                o["status"] = "working"
            else:
                outcome = "obsolete"
        elif kind == "cancel_rejected":
            if o["status"] in TERMINAL or r["cancel_id"] != o["pending_cancel_id"]:
                outcome = "obsolete"
            else:
                o.update(status="partially_filled" if o["filled_shares"] else "working", acknowledged=True, pending_cancel_id="")
        else:
            if r["cumulative_filled_shares"] != o["filled_shares"]:
                return "quarantined", []
            if kind == "rejected":
                if o["status"] == "rejected":
                    outcome = "obsolete"
                elif o["acknowledged"] or o["filled_shares"] or o["status"] not in {"pending_new", "pending_cancel"}:
                    return "quarantined", []
                else:
                    o.update(status="rejected", pending_cancel_id="")
            elif kind == "canceled":
                if o["status"] in {"canceled", "filled"}:
                    outcome = "obsolete"
                elif o["status"] == "rejected":
                    return "quarantined", []
                else:
                    o.update(status="canceled", pending_cancel_id="")
            else:
                raise ValueError("Unknown report kind")
        self.events[r["event_id"]] = r
        return outcome, fills

    def effects(self, result, checkpoint, may_route):
        effects = result["orders"] or result["cancels"] or result["transfers"]
        require(not effects or (self.connected and not self.reconciliation), "Closed gate dispatched a command")
        require(not effects or may_route, "Non-routing input dispatched a command")
        expected_cancels = []
        if may_route and self.connected and not self.reconciliation:
            nav = self.equity()
            for identity, o in sorted(self.active().items()):
                side, symbol = o["order"]["side"], o["order"]["symbol"]
                changed = any(r["shares"] + (1 if side == "buy" else -1) * o["claims"].get(k, 0) != self.desired(k)
                              for k, r in self.owners.items() if k[1] == symbol)
                if side == "buy":
                    changed = changed or self.exposure() > nav or self.exposure(lambda k: k[1] == symbol) > nav // 5
                    changed = changed or any(q and (self.exposure(lambda key: key == k) > nav // 15
                                                   or self.exposure(lambda key: key[0] == k[0]) > nav // 3) for k, q in o["claims"].items())
                if changed and o["status"] != "pending_cancel":
                    expected_cancels.append(identity)
        require([c["order_id"] for c in result["cancels"]] == expected_cancels, "Required cancellation omitted or unrelated cancellation added")
        for t in result["transfers"]:
            a, b, q, price = owner(t["from"]), owner(t["to"]), t["shares"], t["price_units"]
            require(a != b and a[1] == b[1] and q > 0 and price == self.mark(a[1]), "Invalid internal cross")
            require(not any(o["order"]["symbol"] == a[1] for o in self.active().values()), "Reserved inventory crossed internally")
            require(q <= self.owners[a]["shares"] - self.desired(a) and q <= self.desired(b) - self.owners[b]["shares"], "Cross exceeds owner intent")
            nav = self.equity()
            require(q * price <= max(0, nav // 15 - self.exposure(lambda k: k == b)) and q * price <= max(0, nav // 3 - self.exposure(lambda k: k[0] == b[0])), "Internal recipient exceeds budget")
            self.owners[a]["shares"] -= q
            self.owners[b]["shares"] += q
            self.owners[a]["cash_flow_units"] += q * price
            self.owners[b]["cash_flow_units"] -= q * price
        for cancel in result["cancels"]:
            o = self.active()[cancel["order_id"]]
            self.cancel_count += 1
            require(cancel["cancel_id"] == cancel["order_id"] + "-c" + str(self.cancel_count) and o["status"] != "pending_cancel", "Cancel identity/state differs")
            self.cancels[cancel["cancel_id"]] = cancel["order_id"]
            o.update(status="pending_cancel", pending_cancel_id=cancel["cancel_id"])
        require(not result["orders"] or all(0 <= self.at - q["source_at_seconds"] <= self.config["maximum_quote_age_seconds"] for q in self.quotes.values()), "Stale quote dispatched entry")
        for request in result["orders"]:
            symbol, side = request["symbol"], request["side"]
            self.order_count += 1
            require(request["id"] == f"S4-{self.generation}-{self.order_count}", "Order namespace/counter differs")
            require(len(self.active()) < 5 and not any(o["order"]["symbol"] == symbol for o in self.active().values()), "Multiple open orders per symbol")
            require(request["limit_units"] == self.quotes[symbol]["ask_units" if side == "buy" else "bid_units"] and request["fee_per_share_ceiling_units"] == self.config["fee_per_share_ceiling_units"], "Order terms differ from quote/configuration")
            nav, price = self.equity(), request["limit_units"] + request["fee_per_share_ceiling_units"]
            if side == "sell":
                claims = {k: r["shares"] - self.desired(k) for k, r in self.owners.items() if k[1] == symbol and r["shares"] > self.desired(k)}
            else:
                wanted = {}
                for k, r in self.owners.items():
                    if k[1] != symbol or r["shares"] >= self.desired(k):
                        continue
                    q = min(self.desired(k) - r["shares"], max(0, nav // 15 - self.exposure(lambda key: key == k)) // price,
                            max(0, nav // 3 - self.exposure(lambda key: key[0] == k[0])) // price)
                    if q:
                        wanted[k] = q
                affordable = min((self.cash - self.reserved()) // price,
                                 max(0, nav // 5 - self.exposure(lambda k: k[1] == symbol)) // price,
                                 max(0, nav - self.exposure()) // price)
                claims = allocate(min(sum(wanted.values()), affordable), wanted)
            require(request["shares"] == sum(claims.values()) > 0, "Quantity violates exact shared admission")
            saved = next(o for o in checkpoint["open_orders"] if o["order"]["id"] == request["id"])
            require({owner(r): r["remaining"] for r in saved["claims"]} == claims, "Dispatch-time frozen allocation differs")
            self.orders[request["id"]] = dict(order=request, filled_shares=0, status="pending_new", acknowledged=False, pending_cancel_id="", claims=claims)

    def snapshot(self):
        nav = self.equity()
        positions = []
        for s in SYMBOLS:
            q = sum(r["shares"] for k, r in self.owners.items() if k[1] == s)
            buys = sells = 0
            for o in self.active().values():
                if o["order"]["symbol"] == s:
                    remaining = o["order"]["shares"] - o["filled_shares"]
                    if o["order"]["side"] == "buy":
                        buys += remaining
                    else:
                        sells += remaining
            require(q >= sells >= 0, "Unfunded sell reservation")
            positions.append(dict(symbol=s, shares=q, outstanding_buy_shares=buys, reserved_sell_shares=sells))
        require(self.cash >= self.reserved() >= 0, "Borrowing or unbacked cash reservation")
        require(self.cash == self.config["initial_cash_units"] + sum(r["cash_flow_units"] for r in self.owners.values()), "Owner flows do not conserve global cash")
        require(all(r["shares"] >= 0 for r in self.owners.values()), "Negative ownership")
        breaches = []
        if self.quotes:
            breaches += [f"owner:{k[0]}/{k[1]}" for k in OWNERS if self.exposure(lambda key: key == k) > nav // 15]
            breaches += [f"family:{f}" for f in FAMILIES if self.exposure(lambda k: k[0] == f) > nav // 3]
            breaches += [f"symbol:{s}" for s in SYMBOLS if self.exposure(lambda k: k[1] == s) > nav // 5]
            if self.exposure() > nav:
                breaches.append("gross")
        orders = []
        for _, o in sorted(self.active().items()):
            row = deepcopy(o)
            row["claims"] = [dict(family=k[0], symbol=k[1], remaining=q) for k, q in sorted(o["claims"].items())]
            orders.append(row)
        return dict(cash_units=self.cash, reserved_cash_units=self.reserved(), equity_units=nav, connected=self.connected,
                    reconciliation_required=self.reconciliation, positions=positions, owners=list(self.owners.values()),
                    quotes=[q for _, q in sorted(self.quotes.items())], open_orders=orders, budget_breaches=breaches,
                    at_seconds=self.at, generation=self.generation, sealed=self.sealed)

    def apply(self, record):
        require(not self.sealed, "Input follows sealed segment")
        i, result, saved = record["input"], record["result"], record["checkpoint"]
        kind, expected_fills = i["kind"], []
        if kind == "targets":
            outcome = self.targets(i)
        elif i["at_seconds"] <= 0 or i["at_seconds"] < self.at:
            outcome = "quarantined" if kind in {"report", "reconcile"} else "denied"
            if outcome == "quarantined":
                self.connected, self.reconciliation = False, True
        else:
            self.at = i["at_seconds"]
            outcome = "applied"
            if kind in {"disconnect", "reconnect"}:
                self.connected, self.reconciliation = kind == "reconnect", True
            elif kind == "reconcile":
                expected = [{"order": o["order"], "filled_shares": o["filled_shares"]} for o in self.active().values()]
                positions = {s: sum(r["shares"] for k, r in self.owners.items() if k[1] == s) for s in SYMBOLS}
                matches = self.connected and i["cash_units"] == self.cash and i["positions"] == positions and sorted(i["open_orders"], key=lambda o: o["order"]["id"]) == sorted(expected, key=lambda o: o["order"]["id"])
                self.reconciliation = not matches
                outcome = "applied" if matches else "quarantined"
            elif kind == "seal":
                require(not self.active() and all(r["shares"] == r["desired_shares"] == 0 for r in self.owners.values()), "Active segment handover")
                self.sealed, self.connected, self.reconciliation = True, False, True
            elif kind == "report":
                outcome, expected_fills = self.report(i["report"])
                if outcome == "quarantined":
                    self.reconciliation = True
            else:
                raise ValueError("Unknown input kind")
        require(result["outcome"] == outcome, "Saved outcome differs from independent transition")
        require(result["fills"] == expected_fills, "Fill/fee allocation differs")
        if outcome in {"denied", "duplicate", "quarantined"}:
            require(not any(result[k] for k in ("orders", "cancels", "transfers")), "Rejected/duplicate input dispatched effects")
        self.effects(result, saved, kind in {"targets", "report"} and outcome in {"applied", "obsolete"})
        actual = self.snapshot()
        require(actual == saved, "Checkpoint differs: " + ", ".join(k for k in actual if actual[k] != saved.get(k)))


class SharedCapitalVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()

    def path(self, relative):
        path = (self.directory / relative).resolve()
        require(path.is_relative_to(self.directory), "Artifact escaped run directory")
        return path

    def verify(self, bundle=True):
        summary = json.loads(self.path("summary.json").read_text(encoding="utf-8"))
        require(summary["kind"] == "shared_capital_diagnostic" and summary["evidence_origin"] == "synthetic", "Wrong evidence origin")
        require(summary["network_requests"] == summary["external_broker_events"] == 0 and not summary["historical_strategy_scored"] and not summary["reserved_strategy_inputs_opened"], "Evidence scope differs")
        require(summary["registered_protocol_sha256"] == PROTOCOL, "Registered protocol identity differs")
        strategy = StrategyAudit(summary["evidence"]["strategy_records"])
        paths = [a["path"] for a in summary["artifacts"]]
        require(len(paths) == len(set(paths)), "Duplicate artifact")
        for a in summary["artifacts"]:
            data = self.path(a["path"]).read_bytes()
            require(len(data) == a["bytes"] and hashlib.sha256(data).hexdigest() == a["sha256"], "Artifact identity differs: " + a["path"])
        views, finals, count = [], {}, 0
        require(len({c["name"] for c in summary["cases"]}) == len(summary["cases"]), "Duplicate case name")
        for case in summary["cases"]:
            previous, last, events = None, None, []
            for segment in case["segments"]:
                require(segment["path"] in paths, "Segment absent from artifact inventory")
                records = frames(self.path(segment["path"]))
                header, digest = records[0]
                integer_fields(header)
                require(header["kind"] == "header" and header["sequence"] == 0 and header["previous_sha256"] == ZERO, "Header chain differs")
                require(header["configuration"] == case["configuration"] and all(header[k] == segment[k] for k in ("generation", "previous_segment_sha256")), "Segment identity differs")
                if previous is None:
                    require(header["generation"] == 0 and header["previous_segment_sha256"] == ZERO, "Missing root segment")
                else:
                    require(previous["sealed"] and header["generation"] == previous["generation"] + 1 and header["previous_segment_sha256"] == last, "Successor lacks sealed predecessor")
                    flat = header["flat_checkpoint"]
                    require(flat["cash_units"] == previous["cash_units"] and flat["at_seconds"] == previous["at_seconds"] and flat["owner_cash_flows"] == [dict(family=r["family"], symbol=r["symbol"], cash_flow_units=r["cash_flow_units"]) for r in previous["owners"]], "Handover lost owner P&L or global capital")
                audit = AccountAudit(header)
                for seq, (record, next_digest) in enumerate(records[1:], 1):
                    require(record["sequence"] == seq and record["previous_sha256"] == digest and record["kind"] == "input", "Sequence/hash chain differs")
                    try:
                        integer_fields(record)
                        if case["name"] == "strategy_integration" and record["input"]["kind"] == "targets":
                            strategy.verify(record["input"], audit.owners)
                        audit.apply(record)
                    except (ValueError, KeyError, StopIteration) as error:
                        raise ValueError(f"{case['name']}, sequence {seq}: {error}") from error
                    digest = next_digest
                    events.append(record)
                    count += 1
                previous, last = audit.snapshot(), digest
            require(previous == case["final_state"], "Final state differs: " + case["name"])
            finals[case["name"]] = previous
            views.append(dict(name=case["name"], configuration=case["configuration"], events=events))
        for rejected in summary["evidence"]["rejected_files"]:
            path, failure = self.path(rejected["path"]), rejected["failure"]
            if failure == "empty_unpublished_successor":
                require(path.read_bytes() == b"", "Rejected successor was published")
            elif failure == "incomplete_tail":
                frames(path, incomplete=True)
            elif failure == "checksum":
                rejected_checksum = False
                try:
                    frames(path)
                except ValueError:
                    rejected_checksum = True
                require(rejected_checksum, "Claimed corruption is valid")
            else:
                raise ValueError("Unknown rejected artifact class: " + failure)
        for pair in summary["evidence"]["financial_parity"]:
            def financial(name):
                return {k: v for k, v in finals[name].items() if k not in {"connected", "reconciliation_required", "at_seconds", "generation", "sealed"}}
            require(financial(pair["left"]) == financial(pair["right"]), "Recovery changes financial state")
        page = self.path("report.html").read_text(encoding="utf-8")
        match = re.search(r"const cases=(\[.*?\]);\s*const choose=", page, re.S)
        require(match is not None and json.loads(match[1]) == views, "Displayed events differ from audited journals")
        if bundle:
            with zipfile.ZipFile(self.path("review.zip"), "w", zipfile.ZIP_DEFLATED) as archive:
                for relative in ["summary.json", *paths]:
                    archive.write(self.path(relative), relative)
        return len(summary["cases"]), count


def self_check(directory):
    """Semantic mutations bypass hashes deliberately to exercise ledger checks."""
    source = frames(directory / "global_resources/0.journal")
    def changed(records, mutation):
        for record, _ in records[1:]:
            if mutation(record):
                return
        raise ValueError("Mutation had no applicable evidence")
    def cash(r):
        r["checkpoint"]["cash_units"] += 1
        return True
    def shares(r):
        r["checkpoint"]["owners"][0]["shares"] += 1
        return True
    def claim(r):
        if not r["checkpoint"]["open_orders"]:
            return False
        r["checkpoint"]["open_orders"][0]["claims"][0]["remaining"] += 1
        return True
    def fee(r):
        if not r["result"]["fills"]:
            return False
        r["result"]["fills"][0]["fee_units"] += 1
        return True
    def order(r):
        if not r["result"]["orders"]:
            return False
        r["result"]["orders"][0]["shares"] += 1
        return True
    def reconcile(r):
        if r["input"]["kind"] != "reconcile":
            return False
        r["input"]["cash_units"] += 1
        return True
    def future(r):
        if r["input"]["kind"] != "targets":
            return False
        r["input"]["quotes"][0]["source_at_seconds"] += 1
        return True
    for mutation in (cash, shares, claim, fee, order, reconcile, future):
        records = deepcopy(source)
        changed(records, mutation)
        audit, rejected = AccountAudit(records[0][0]), False
        try:
            for r, _ in records[1:]:
                audit.apply(r)
        except ValueError:
            rejected = True
        require(rejected, "Semantic corruption accepted: " + mutation.__name__)
    keys = [("a", "SPY"), ("b", "SPY"), ("c", "SPY")]
    require(list(allocate(5, dict.fromkeys(keys, 66)).values()) == [2, 2, 1], "Tie allocation differs")
    require(list(allocate(3000000000001, dict.fromkeys(keys, 1666666666666)).values()) == [1000000000001, 1000000000000, 1000000000000], "Large product allocation differs")
    print("Independent self-checks passed: seven semantic corruptions rejected; exact tie and large-product allocations verified.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--self-check", action="store_true", help="Also reject deliberate semantic corruptions")
    args = parser.parse_args()
    try:
        cases, events = SharedCapitalVerifier(args.directory).verify()
        if args.self_check:
            self_check(args.directory)
    except (ValueError, KeyError, OSError, TypeError, StopIteration) as error:
        raise SystemExit(f"Shared-capital verification failed: {error}") from error
    print(f"Verified {cases} synthetic cases and {events} durable transitions: cash, ownership, allocation, admission and recovery.")
    print(f"Review bundle: {args.directory.resolve() / 'review.zip'}")


if __name__ == "__main__":
    main()
