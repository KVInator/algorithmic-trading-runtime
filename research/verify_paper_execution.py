"""Independent Decimal ledger and chained-input checks for schema-3 paper reports."""

from decimal import Decimal, localcontext
import hashlib
import json
from pathlib import Path
import sys


class PaperExecutionVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.frames = {}
        self.inputs = 0

    @staticmethod
    def digest(data):
        return hashlib.sha256(data).hexdigest()

    @staticmethod
    def encoded(value):
        return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()

    @staticmethod
    def units(value):
        scaled = Decimal(str(value)) * 10**9
        assert scaled == scaled.to_integral_value()
        assert -(2**63) <= scaled < 2**63
        return int(scaled)

    def path(self, name):
        result = (self.directory / name).resolve()
        assert result.parent == self.directory
        return result

    def journal(self, name):
        previous = "0" * 64
        orders, executions, checkpoints = {}, {}, {}
        position = 0
        for sequence, line in enumerate(self.path(name).read_bytes().splitlines()):
            payload, digest = line.rsplit(b"\t", 1)
            assert self.digest(payload) == digest.decode()
            row = json.loads(payload)
            assert row["sequence"] == sequence and row["previous_sha256"] == previous
            previous = digest.decode()
            if sequence == 0:
                assert row["schema_version"] == 3
                assert row["model_contract"] == "paper_whole_share_nanodollar_v1"
                config = row["configuration"]
                assert config["fee_policy"] == "paper_zero_fees_v1"
                assert config["money_unit"] == "USD_1e-9"
                cash = Decimal(config["initial_cash_usd"])
                continue
            command, outcome = row["command"], row["outcome"]
            kind = command["kind"]
            if outcome == "applied" and kind == "submit":
                order = command["order"]
                assert order["id"] not in orders
                orders[order["id"]] = dict(
                    side=order["side"],
                    quantity=order["shares"],
                    filled=0,
                    limit=Decimal(order["limit_cents"]) / 100,
                    fee_ceiling=Decimal(order["fee_per_share_ceiling_cents"]) / 100,
                    active=True,
                )
            if kind == "stream":
                try:
                    message = json.loads(command["raw"])
                except json.JSONDecodeError:
                    assert outcome == "quarantined"
                    message = {}
                if message.get("stream") == "trade_updates":
                    data = message["data"]
                    event = data["event"]
                    if outcome == "applied" and event in ["fill", "partial_fill"]:
                        key = data["execution_id"].lower()
                        assert key not in executions
                        order = orders[data["order"]["client_order_id"]]
                        quantity, price = Decimal(data["qty"]), Decimal(data["price"])
                        assert quantity == quantity.to_integral_value() and quantity > 0
                        assert price > 0 and order["active"]
                        assert (
                            price <= order["limit"]
                            if order["side"] == "buy"
                            else price >= order["limit"]
                        )
                        assert quantity <= order["quantity"] - order["filled"]
                        sign = 1 if order["side"] == "buy" else -1
                        cash -= sign * quantity * price
                        position += sign * int(quantity)
                        order["filled"] += int(quantity)
                        order["active"] = order["filled"] != order["quantity"]
                        assert order["filled"] == int(data["order"]["filled_qty"])
                        assert position == int(data["position_qty"])
                        executions[key] = (data["order"]["client_order_id"], quantity, price)
                    elif outcome == "duplicate" and event in ["fill", "partial_fill"]:
                        assert executions[data["execution_id"].lower()] == (
                            data["order"]["client_order_id"],
                            Decimal(data["qty"]),
                            Decimal(data["price"]),
                        )
                    elif outcome == "applied" and event in ["canceled", "expired", "rejected"]:
                        order = orders[data["order"]["client_order_id"]]
                        assert order["filled"] == int(data["order"]["filled_qty"])
                        order["active"] = False
            reserved_cash = sum(
                (o["quantity"] - o["filled"]) * (o["limit"] + o["fee_ceiling"])
                for o in orders.values()
                if o["active"] and o["side"] == "buy"
            )
            reserved_sells = sum(
                o["quantity"] - o["filled"]
                for o in orders.values()
                if o["active"] and o["side"] == "sell"
            )
            outstanding = sum(
                o["quantity"] - o["filled"]
                for o in orders.values()
                if o["active"] and o["side"] == "buy"
            )
            account = row["account"]
            assert account["cash_units"] == self.units(cash)
            assert account["position_shares"] == position
            assert account["reserved_cash_units"] == self.units(reserved_cash)
            assert account["reserved_sell_shares"] == reserved_sells
            assert account["outstanding_buy_shares"] == outstanding
            assert account["open_orders"] == sum(o["active"] for o in orders.values())
            assert 0 <= reserved_cash <= cash and 0 <= reserved_sells <= position
            assert position + outstanding <= config["maximum_position_shares"]
            if kind == "disconnect" or (outcome == "quarantined" and kind == "stream"):
                assert not account["connected"] and account["reconciliation_required"]
            if kind == "reconcile" and outcome == "applied":
                assert account["connected"] and not account["reconciliation_required"]
            checkpoints[sequence] = (account, row["state_sha256"], digest.decode())
            self.inputs += 1
        self.frames[name] = checkpoints

    def verify(self):
        summary = json.loads(self.path("summary.json").read_text())
        assert summary["money_unit"] == "USD_1e-9" and summary["fee_policy"] == "paper_zero_fees_v1"
        assert summary["network_requests_sent"] == summary["broker_events_observed"] == 0
        for artifact in summary["artifacts"]:
            name = artifact["path"]
            assert self.digest(self.path(name).read_bytes()) == artifact["sha256"]
            if name.endswith(".journal"):
                self.journal(name)
        assert (
            self.digest(self.path("source_inventory.txt").read_bytes()) == summary["source_sha256"]
        )
        evidence = json.loads(self.path("bridge_evidence.json").read_text())
        assert len(evidence["checks"]) == summary["checked_assertions"]
        for frame in evidence["frames"]:
            state = frame["state"]
            account, state_hash, tip = self.frames[frame["case"] + ".journal"][
                state["durable_records"]
            ]
            assert account == state["account"] and tip == state["journal_sha256"]
            values = {
                k: v for k, v in state.items() if k not in ["durable_records", "journal_sha256"]
            }
            assert self.digest(self.encoded(values)) == state_hash
        print(
            f"Verified {len(self.frames)} journals, {self.inputs} inputs and {len(evidence['frames'])} saved frames; "
            "independent Decimal cash, inventory, reservations, deduplication and artifact identities"
        )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: python research/verify_paper_execution.py <report-directory>")
    with localcontext() as context:
        context.prec = 80
        PaperExecutionVerifier(sys.argv[1]).verify()
