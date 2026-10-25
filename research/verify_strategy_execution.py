"""Independent integer-ledger, signal and artifact verification for execution replay."""

from decimal import Decimal, localcontext
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys


class StrategyExecutionVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.summary = self.load("summary.json")
        self.evidence = self.load("execution_evidence.json")
        self.inputs = self.load("inputs.json")
        self.checked_frames = 0
        self.checked_journal_inputs = 0

    def path(self, name):
        path = (self.directory / name).resolve()
        assert path.parent == self.directory
        return path

    def load(self, name):
        return json.loads(self.path(name).read_text())

    def verify(self):
        for a in self.summary["artifacts"]:
            assert hashlib.sha256(self.path(a["path"]).read_bytes()).hexdigest() == a["sha256"]
        assert (
            hashlib.sha256(self.path("source_inventory.txt").read_bytes()).hexdigest()
            == self.summary["source_sha256"]
        )
        cases = self.evidence["cases"]
        assert len(cases) == self.summary["case_count"] and all(
            c["passed"] for c in self.evidence["checks"]
        )
        for case in cases:
            states, initial = self.journal(case["journal_file"])
            requests = {
                o["request"]["order"]["id"]: o["request"]["order"]
                for f in case["frames"]
                for o in f["orders"]
            }
            previous = 0
            for f in case["frames"]:
                assert previous <= f["at_seconds"]
                previous = f["at_seconds"]
                assert f["client"] == states[f["journal_records"]]
                cash, shares = initial, 0
                for fill in case["fills"][: f["broker_fill_count"]]:
                    sign = -1 if fill["side"] == "buy" else 1
                    cash += sign * fill["shares"] * fill["price_cents"] - fill["fee_cents"]
                    shares -= sign * fill["shares"]
                    q = next(q for q in case["quotes"] if q["sequence"] == fill["quote_sequence"])
                    assert fill["price_cents"] == q["ask_cents" if sign == -1 else "bid_cents"]
                assert (
                    cash == f["broker"]["cash_cents"] and shares == f["broker"]["position_shares"]
                )
                active, quantities = {}, {}
                for report in case["reports"][: f["broker_report_count"]]:
                    key = report["order_id"]
                    if report["report_kind"] == "accepted":
                        active[key] = True
                    elif report["report_kind"] == "fill":
                        quantities[key] = quantities.get(key, 0) + report["fill_shares"]
                        if quantities[key] == requests[key]["shares"]:
                            active[key] = False
                    elif report["report_kind"] in ["canceled", "rejected"]:
                        active[key] = False
                expected = {
                    k: quantities.get(k, 0) for k, open_order in active.items() if open_order
                }
                observed = {
                    o["request"]["order"]["id"]: o["filled_shares"]
                    for o in f["broker"]["open_orders"]
                }
                assert expected == observed
                if f["label"].startswith("Client restart"):
                    assert not f["client"]["connected"] and f["client"]["reconciliation_required"]
                if f["target"]:
                    self.verify_target(case, f["target"])
                self.checked_frames += 1
            if case["parity"]:
                self.parity(case, initial)
        for design in ["momentum", "continuation_price", "continuation_volume", "reversion"]:
            a = next(c for c in cases if c["label"] == design + " / partial")
            b = next(c for c in cases if c["label"] == design + " / restart")
            assert a["fills"] == b["fills"] and a["reports"] == b["reports"]
            assert a["frames"][-1]["client"] == b["frames"][-1]["client"]
        print(
            f"Verified {len(cases)} cases, {self.checked_frames} saved frames, "
            f"{self.checked_journal_inputs} chained journal inputs, independent integer accounting, "
            "strategy target directions, matched/rounding parity and artifact identities"
        )

    def journal(self, name):
        previous = "0" * 64
        requests, filled, active, executions = {}, {}, {}, set()
        states = {}
        connected = True
        reconciliation = False
        for index, line in enumerate(self.path(name).read_bytes().splitlines()):
            data, digest = line.rsplit(b"\t", 1)
            assert hashlib.sha256(data).hexdigest() == digest.decode()
            row = json.loads(data)
            assert row["sequence"] == index and row["previous_sha256"] == previous
            previous = digest.decode()
            if index == 0:
                assert row["schema_version"] == 2
                initial = cash = row["configuration"]["initial_cash_cents"]
                shares = 0
                continue
            command, outcome = row["input"], row["trace"]["outcome"]
            if outcome == "applied":
                kind = command["kind"]
                if kind == "submit":
                    order = command["order"]
                    key = order["id"]
                    requests[key] = order
                    filled[key] = 0
                    active[key] = True
                elif kind == "report":
                    key = command["order_id"]
                    if command["report_kind"] == "fill":
                        assert command["execution_id"] not in executions
                        executions.add(command["execution_id"])
                        sign = -1 if requests[key]["side"] == "buy" else 1
                        cash += (
                            sign * command["fill_shares"] * command["fill_price_cents"]
                            - command["fee_cents"]
                        )
                        shares -= sign * command["fill_shares"]
                        filled[key] += command["fill_shares"]
                        if filled[key] == requests[key]["shares"]:
                            active[key] = False
                    elif command["report_kind"] in ["canceled", "rejected"]:
                        active[key] = False
                elif kind == "disconnect":
                    connected = False
                    reconciliation = True
                elif kind == "reconnect":
                    connected = True
                    reconciliation = True
                elif kind == "reconcile":
                    assert command["cash_cents"] == cash and command["position_shares"] == shares
                    reconciliation = False
            if outcome == "quarantined":
                reconciliation = True
            reserve_cash = reserve_shares = outstanding = open_orders = 0
            for key, order in requests.items():
                if not active[key]:
                    continue
                open_orders += 1
                remaining = order["shares"] - filled[key]
                if order["side"] == "buy":
                    outstanding += remaining
                    reserve_cash += remaining * (
                        order["limit_cents"] + order["fee_per_share_ceiling_cents"]
                    )
                else:
                    reserve_shares += remaining
            state = {
                "cash_cents": cash,
                "position_shares": shares,
                "reserved_cash_cents": reserve_cash,
                "reserved_sell_shares": reserve_shares,
                "outstanding_buy_shares": outstanding,
                "open_orders": open_orders,
                "connected": connected,
                "reconciliation_required": reconciliation,
            }
            assert state == row["trace"]["account"], (name, index, state, row["trace"]["account"])
            assert 0 <= reserve_cash <= cash and 0 <= reserve_shares <= shares
            states[index] = state
            self.checked_journal_inputs += 1
        return states, initial

    def verify_target(self, case, target):
        inputs = self.inputs["historical" if case["historical_proxy"] else "synthetic"]
        design = case["design"]
        elapsed = (target["decision_at_seconds"] - inputs["current_start"]) // 60
        cash = case["label"].endswith(" / cash")
        bar_key = (
            "momentum"
            if design == "momentum"
            else "reversion" if design == "reversion" else "continuation"
        )
        bars = inputs[bar_key + ("_cash" if cash else "_long")]
        if elapsed == (180 if design == "reversion" else 385):
            assert target["target_shares"] == 0
            return
        prefix = bars[:elapsed]
        assert prefix[-1][0] + 60 == target["decision_at_seconds"]
        if design == "momentum":
            expected = prefix[-1][4] > prefix[0][1]
        elif design.startswith("continuation"):
            move = prefix[-1][4] / prefix[0][1] - 1
            scale = statistics.mean(
                abs(s["bars"][elapsed - 1][4] / s["bars"][0][1] - 1) for s in inputs["history"]
            )
            price = move > scale
            volume = sum(b[5] for b in prefix)
            median = statistics.median(
                sum(b[5] for b in s["bars"][:elapsed]) for s in inputs["history"]
            )
            # Holding condition is price-only; identify executed inventory at the
            # quote immediately preceding this newly created target.
            preceding = next(
                f
                for f in case["frames"]
                if f["at_seconds"] == target["decision_at_seconds"]
                and (not f["target"] or f["target"]["id"] != target["id"])
            )
            long_position = preceding["client"]["position_shares"] > 0
            expected = price and (
                long_position or design == "continuation_price" or volume > median
            )
        else:

            def residuals(data):
                anchor = math.fsum(b[4] * b[5] for b in data[:30]) / math.fsum(
                    b[5] for b in data[:30]
                )
                return [math.log(data[29 + 5 * k][4]) - math.log(anchor) for k in range(25)]

            history = [residuals(s["bars"]) for s in inputs["history"]]
            pairs = [(s[k - 1], s[k]) for s in history for k in range(1, 25)]
            mx = statistics.mean(x for x, y in pairs)
            my = statistics.mean(y for x, y in pairs)
            phi = math.fsum((x - mx) * (y - my) for x, y in pairs) / math.fsum(
                (x - mx) ** 2 for x, y in pairs
            )
            expected = False
            if 0 < phi < 1:
                theta = (my - phi * mx) / (1 - phi)
                current = residuals(prefix)[-1]
                kappa = -math.log(phi) / 5
                move = (theta - current) * math.exp(-kappa) * (-math.expm1(-30 * kappa))
                expected = current < theta and move > math.log1p(0.0001) - math.log1p(-0.0001)
        assert target["target_shares"] == (10 if expected else 0), (case["label"], target)

    @staticmethod
    def parity(case, initial):
        p = case["parity"]
        buy = next(f for f in case["fills"] if f["side"] == "buy")
        sell = next(f for f in case["fills"] if f["side"] == "sell")
        cost = Decimal(1) / 10000 if case["label"].endswith("aligned-fee") else Decimal(0)
        with localcontext() as context:
            context.prec = 50
            quantity = Decimal(initial) / (Decimal(buy["price_cents"]) * (1 + cost))
            final_usd = quantity * Decimal(sell["price_cents"]) * (1 - cost) / 100
            assert math.isclose(p["fractional_entry_shares"], float(quantity), rel_tol=1e-12)
            assert math.isclose(p["fractional_final_equity_usd"], float(final_usd), rel_tol=1e-12)
            actual = Decimal(case["frames"][-1]["client"]["cash_cents"]) / 100
            assert math.isclose(
                p["integer_minus_fractional_usd"], float(actual - final_usd), abs_tol=1e-10
            )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: python research/verify_strategy_execution.py <report-directory>")
    StrategyExecutionVerifier(sys.argv[1]).verify()
