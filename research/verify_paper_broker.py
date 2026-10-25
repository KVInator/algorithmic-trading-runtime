"""Independent Decimal broker ledger, quote budgets and capture/journal checks."""

from decimal import Decimal, localcontext
import json
from pathlib import Path
import sys

from verify_paper_transport import PaperTransportVerifier


class PaperBrokerVerifier:
    def __init__(self, directory):
        self.directory = Path(directory)

    def verify(self):
        PaperTransportVerifier(self.directory).verify()
        summary = json.loads((self.directory / "summary.json").read_text())
        assert summary["evidence_origin"] == "synthetic"
        evidence = json.loads((self.directory / "transport_evidence.json").read_text())
        fills_checked = 0
        for case in evidence["cases"]:
            cash, position = Decimal("2000"), 0
            quantities, executions = {}, set()
            economic_fills = []
            messages = case["trade_history"]
            for raw in messages:
                data = json.loads(raw)["data"]
                if data["event"] not in ("fill", "partial_fill"):
                    continue
                order = data["order"]
                execution = data["execution_id"]
                assert execution not in executions
                executions.add(execution)
                qty, price = int(data["qty"]), Decimal(data["price"])
                assert qty > 0 and price > 0
                identity = order["client_order_id"]
                quantities[identity] = quantities.get(identity, 0) + qty
                assert (
                    quantities[identity]
                    == int(order["filled_qty"])
                    <= int(order["qty"])
                )
                if order["side"] == "buy":
                    assert price <= Decimal(order["limit_price"])
                    cash -= qty * price
                    position += qty
                else:
                    assert price >= Decimal(order["limit_price"])
                    cash += qty * price
                    position -= qty
                assert (
                    cash >= 0
                    and position >= 0
                    and position == int(data["position_qty"])
                )
                economic_fills.append((identity, order["side"], qty, price))
                fills_checked += 1
            broker = case["broker"]
            assert cash == Decimal(broker["account"]["cash"])
            assert position == sum(int(p["qty"]) for p in broker["positions"])
            account = case["state"]["account"]
            assert Decimal(account["cash_units"]) / 10**9 == cash
            assert account["position_shares"] == position
            quotes = {q["sequence"]: q for q in broker["quotes"]}
            used = {}
            assert len(broker["fills"]) == len(executions)
            assert economic_fills == [
                (
                    f["order_id"],
                    f["side"],
                    f["quantity"],
                    Decimal(f["price_cents"]) / 100,
                )
                for f in broker["fills"]
            ]
            for fill in broker["fills"]:
                q = quotes[fill["quote_sequence"]]
                side = "ask" if fill["side"] == "buy" else "bid"
                key = (q["sequence"], side)
                used[key] = used.get(key, 0) + fill["quantity"]
                assert used[key] <= q[side + "_shares"]
                assert fill["price_cents"] == q[side + "_cents"]
            assert broker["submissions"] == len(broker["orders"])
        print(
            f"Verified {len(evidence['cases'])} synthetic broker cases, {fills_checked} incremental fills and quote liquidity budgets"
        )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: python research/verify_paper_broker.py <report-directory>"
        )
    with localcontext() as context:
        context.prec = 80
        PaperBrokerVerifier(sys.argv[1]).verify()
