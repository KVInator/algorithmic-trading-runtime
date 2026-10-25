"""Independent synthetic matcher, Decimal cost/timing oracle and journal verification."""

from collections import defaultdict
import csv
from decimal import Decimal, localcontext
import hashlib
import json
import datetime
from pathlib import Path
import sys

from verify_paper_execution import PaperExecutionVerifier
from verify_paper_transport import PaperTransportVerifier


class ExecutionMeasurementVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()

    def path(self, relative):
        path = (self.directory / relative).resolve()
        assert path.is_relative_to(self.directory) and path != self.directory
        return path

    @staticmethod
    def same(actual, expected):
        assert set(actual) == set(expected), (
            set(actual) - set(expected),
            set(expected) - set(actual),
        )
        for key, value in expected.items():
            if value is None or isinstance(value, bool):
                assert actual[key] is value, (key, actual[key], value)
            elif isinstance(value, Decimal):
                assert abs(Decimal(str(actual[key])) - value) <= Decimal("1e-9"), (
                    key,
                    actual[key],
                    value,
                )
            else:
                assert actual[key] == value, (key, actual[key], value)

    @staticmethod
    def expected(case):
        w, p = case["workload"], case["policy"]
        quotes = case["quotes"]
        assert [q["at_seconds"] for q in quotes] == [10, 12, 15, 18, 21, 24, 30]
        changes = {
            "steady": [0, 0, 0, 0, 0, 0, 0],
            "reverting": [0, 0, -2, -4, 0, 2, 0],
            "adverse": [0, 2, 4, 6, 8, 10, 12],
        }[w["path"]]
        sign = 1 if w["side"] == "buy" else -1
        for index, (q, change) in enumerate(zip(quotes, changes)):
            mid = 10000 + sign * change
            assert q == dict(
                sequence=index + 2,
                at_seconds=[10, 12, 15, 18, 21, 24, 30][index],
                bid_cents=mid - w["spread_cents"] // 2,
                ask_cents=mid + w["spread_cents"] // 2,
                bid_shares=w["quote_size"],
                ask_shares=w["quote_size"],
            )
        assert (
            case["decision_seconds"] == 10
            and case["dispatch_seconds"] == 11
            and case["horizon_seconds"] == 30
        )
        crossing = p["aggressive_limit"]
        touch = "ask_cents" if (sign == 1) == crossing else "bid_cents"
        limit = quotes[0][touch]
        assert case["limit_cents"] == limit
        submitted = 11 + w["outbound_delay_seconds"]
        cancelled = (
            11
            + p["cancel_after_seconds"]
            + w["outbound_delay_seconds"]
            + w["cancel_processing_seconds"]
        )
        by_time = {q["at_seconds"]: q for q in quotes}
        live, remaining, available, quote = False, 10, 0, None
        fills = []
        # Independently simulate finite quote budgets. Source quotes precede a
        # submission or cancel confirmation at the same second.
        for at in sorted(set(by_time) | {submitted, cancelled}):
            if at in by_time:
                quote = by_time[at]
                available = w["quote_size"]
            if at == submitted:
                live = True
            if live and remaining and available:
                price = quote["ask_cents" if sign == 1 else "bid_cents"]
                if price <= limit if sign == 1 else price >= limit:
                    quantity = min(remaining, available)
                    fills.append(
                        dict(
                            quantity=quantity,
                            price_cents=price,
                            bid_cents=quote["bid_cents"],
                            ask_cents=quote["ask_cents"],
                            quote_sequence=quote["sequence"],
                            source_seconds=at,
                            receipt_seconds=at + w["delivery_delay_seconds"],
                        )
                    )
                    available -= quantity
                    remaining -= quantity
            if at == cancelled:
                live = False
        assert fills == case["fills"]
        filled = 10 - remaining
        arrival2 = quotes[0]["bid_cents"] + quotes[0]["ask_cents"]
        terminal2 = quotes[-1]["bid_cents"] + quotes[-1]["ask_cents"]
        notional = sum(f["quantity"] * f["price_cents"] for f in fills)
        spread2 = sum(
            sign
            * f["quantity"]
            * (2 * f["price_cents"] - f["bid_cents"] - f["ask_cents"])
            for f in fills
        )
        timing2 = sum(
            sign * f["quantity"] * (f["bid_cents"] + f["ask_cents"] - arrival2)
            for f in fills
        )
        executed2 = sign * (2 * notional - filled * arrival2)
        opportunity2 = sign * remaining * (terminal2 - arrival2)
        assert executed2 == spread2 + timing2
        result = dict(
            requested_quantity=10,
            filled_quantity=filled,
            unfilled_quantity=remaining,
            fill_fraction=Decimal(filled) / 10,
            fill_notional_cents=notional,
            arrival_midpoint_twice_cents=arrival2,
            terminal_midpoint_twice_cents=terminal2,
            executed_shortfall_twice_cents=executed2,
            spread_component_twice_cents=spread2,
            timing_component_twice_cents=timing2,
            unfilled_opportunity_twice_cents=opportunity2,
            fees_twice_cents=0,
            total_shortfall_twice_cents=executed2 + opportunity2,
            executed_shortfall_bps=(
                Decimal(10000 * executed2) / (filled * arrival2) if filled else None
            ),
            total_shortfall_bps=Decimal(10000 * (executed2 + opportunity2))
            / (10 * arrival2),
            decision_to_dispatch_seconds=1,
            acknowledgement_source_delay_seconds=w["outbound_delay_seconds"],
            acknowledgement_receipt_delay_seconds=w["outbound_delay_seconds"]
            + w["delivery_delay_seconds"],
            first_fill_source_delay_seconds=(
                fills[0]["source_seconds"] - 11 if fills else None
            ),
            first_fill_receipt_delay_seconds=(
                fills[0]["receipt_seconds"] - 11 if fills else None
            ),
            completion_source_delay_seconds=(
                fills[-1]["source_seconds"] - 11 if not remaining else None
            ),
            completion_receipt_delay_seconds=(
                fills[-1]["receipt_seconds"] - 11 if not remaining else None
            ),
            completion_censored=bool(remaining),
            completion_observation_seconds=(
                fills[-1]["receipt_seconds"] - 11
                if not remaining
                else (
                    case["cancel_confirmation_receipt_seconds"] - 11
                    if case["cancel_outcome"] == "canceled"
                    else 19
                )
            ),
            completion_source_observation_seconds=(
                fills[-1]["source_seconds"] - 11
                if not remaining
                else (
                    case["cancel_confirmation_source_seconds"] - 11
                    if case["cancel_outcome"] == "canceled"
                    else 19
                )
            ),
            completion_censor_reason=(
                "not_censored"
                if not remaining
                else (
                    "terminal_cancellation"
                    if case["cancel_outcome"] == "canceled"
                    else "observation_horizon"
                )
            ),
            completion_horizon_seconds=19,
        )
        return result

    def verify(self):
        summary = json.loads(self.path("summary.json").read_text())
        assert (
            summary["schema_version"] == 1 and summary["evidence_origin"] == "synthetic"
        )
        assert (
            summary["quote_feed"] == "synthetic_top_of_book_v1"
            and summary["clock_resolution_seconds"] == 1
        )
        assert (
            summary["network_requests_sent"] == 0
            and summary["broker_events_observed"] == 0
            and summary["fees"] == "zero"
        )
        artifacts = summary["artifacts"]
        names = [a["path"] for a in artifacts]
        assert len(names) == len(set(names))
        for a in artifacts:
            assert (
                hashlib.sha256(self.path(a["path"]).read_bytes()).hexdigest()
                == a["sha256"]
            )
        assert (
            hashlib.sha256(self.path("source_inventory.txt").read_bytes()).hexdigest()
            == summary["source_sha256"]
        )
        evidence = json.loads(self.path("measurement_evidence.json").read_text())
        assert len(evidence["cases"]) == summary["case_count"]
        assert len(evidence["comparisons"]) == summary["paired_comparison_count"]
        grouped, policy_metrics = defaultdict(list), defaultdict(list)
        captures = PaperTransportVerifier(self.directory)
        ledgers, journal_inputs, fills = 0, 0, 0
        for c in evidence["cases"]:
            self.same(c["metrics"], self.expected(c))
            grouped[c["workload"]["id"]].append(c)
            policy_metrics[c["policy"]["id"]].append(c["metrics"])
            parent = self.path(c["case"])
            ledger = PaperExecutionVerifier(parent)
            ledger.journal("runtime.journal")
            ledgers += 1
            journal_inputs += ledger.inputs
            captures.capture(c["case"] + "/capture/capture.journal")
            for state in [c["state"], *[f["state"] for f in c["frames"]]]:
                account, state_hash, tip = ledger.frames["runtime.journal"][
                    state["durable_records"]
                ]
                assert account == state["account"] and tip == state["journal_sha256"]
                values = {
                    k: v
                    for k, v in state.items()
                    if k not in ("durable_records", "journal_sha256")
                }
                assert ledger.digest(ledger.encoded(values)) == state_hash
            journal_messages = []
            stream_bodies = []
            for line in (parent / "runtime.journal").read_bytes().splitlines()[1:]:
                command = json.loads(line.rsplit(b"\t", 1)[0])["command"]
                if command["kind"] != "stream":
                    continue
                stream_bodies.append(command["raw"])
                data = json.loads(command["raw"])
                if (
                    data["stream"] != "trade_updates"
                    or data["data"]["order"]["client_order_id"] != "measured-order"
                ):
                    continue
                origin = datetime.datetime.fromisoformat("2023-01-03T14:30:00+00:00")
                source = int(
                    (
                        datetime.datetime.fromisoformat(
                            data["data"]["timestamp"].replace("Z", "+00:00")
                        )
                        - origin
                    ).total_seconds()
                )
                received = int(
                    (
                        datetime.datetime.fromisoformat(
                            command["received_at"].replace("Z", "+00:00")
                        )
                        - origin
                    ).total_seconds()
                )
                journal_messages.append(
                    dict(
                        event=data["data"]["event"],
                        raw=command["raw"],
                        source_seconds=source,
                        receipt_seconds=received,
                    )
                )
            assert journal_messages == c["events"]
            captured_bodies = []
            for line in (parent / "capture/capture.journal").read_bytes().splitlines():
                item = json.loads(line.rsplit(b"\t", 1)[0])
                if item["kind"] == "stream":
                    captured_bodies.append(
                        (parent / "capture" / item["body_file"]).read_text()
                    )
            assert stream_bodies == captured_bodies
            cancel_events = [
                e
                for e in c["events"]
                if e["event"] in ("canceled", "order_cancel_rejected")
            ]
            expected_cancel = (
                "not_requested"
                if not cancel_events
                else (
                    "canceled"
                    if cancel_events[-1]["event"] == "canceled"
                    else "rejected_after_fill"
                )
            )
            assert c["cancel_outcome"] == expected_cancel
            for field in [
                "cancel_dispatch_seconds",
                "cancel_source_seconds",
                "cancel_confirmation_source_seconds",
                "cancel_confirmation_receipt_seconds",
            ]:
                assert (c[field] is None) == (not cancel_events)
            if cancel_events:
                assert (
                    c["cancel_dispatch_seconds"]
                    == c["dispatch_seconds"] + c["policy"]["cancel_after_seconds"]
                )
                assert (
                    c["cancel_source_seconds"]
                    == c["cancel_dispatch_seconds"]
                    + c["workload"]["outbound_delay_seconds"]
                )
                assert (
                    c["cancel_confirmation_source_seconds"]
                    == cancel_events[-1]["source_seconds"]
                )
                assert (
                    c["cancel_confirmation_receipt_seconds"]
                    == cancel_events[-1]["receipt_seconds"]
                )
            broker = c["broker"]
            measured = [f for f in broker["fills"] if f["order_id"] == "measured-order"]
            assert [
                (f["quantity"], f["price_cents"], f["quote_sequence"]) for f in measured
            ] == [
                (f["quantity"], f["price_cents"], f["quote_sequence"])
                for f in c["fills"]
            ]
            used = defaultdict(int)
            quotes = {q["sequence"]: q for q in broker["quotes"]}
            for f in broker["fills"]:
                side = "ask" if f["side"] == "buy" else "bid"
                q = quotes[f["quote_sequence"]]
                used[q["sequence"], side] += f["quantity"]
                assert (
                    used[q["sequence"], side] <= q[side + "_shares"]
                    and f["price_cents"] == q[side + "_cents"]
                )
            raw_fills = []
            for e in c["events"]:
                d = json.loads(e["raw"])["data"]
                assert d["order"]["client_order_id"] == "measured-order"
                assert (
                    e["receipt_seconds"]
                    == e["source_seconds"] + c["workload"]["delivery_delay_seconds"]
                )
                assert d["event"] == e["event"]
                if d["event"] in ("fill", "partial_fill"):
                    raw_fills.append(
                        (
                            int(d["qty"]),
                            Decimal(d["price"]) * 100,
                            e["source_seconds"],
                            e["receipt_seconds"],
                        )
                    )
            assert raw_fills == [
                (
                    f["quantity"],
                    Decimal(f["price_cents"]),
                    f["source_seconds"],
                    f["receipt_seconds"],
                )
                for f in c["fills"]
            ]
            account = c["state"]["account"]
            assert Decimal(account["cash_units"]) / 10**9 == Decimal(
                broker["account"]["cash"]
            )
            assert account["position_shares"] == sum(
                int(p["qty"]) for p in broker["positions"]
            )
            assert (
                account["reserved_cash_units"]
                == account["reserved_sell_shares"]
                == account["open_orders"]
                == 0
            )
            fills += len(measured)
        expected_comparisons = []
        for workload, cases in grouped.items():
            assert len(cases) == 4 and len({c["policy"]["id"] for c in cases}) == 4
            assert all(
                c["quotes"] == cases[0]["quotes"]
                and c["workload"] == cases[0]["workload"]
                for c in cases
            )
            baseline = next(
                c["metrics"] for c in cases if c["policy"]["id"] == "cross_11s"
            )
            for c in cases:
                if c["policy"]["id"] == "cross_11s":
                    continue
                expected_comparisons.append(
                    dict(
                        workload=workload,
                        candidate=c["policy"]["id"],
                        baseline="cross_11s",
                        fill_fraction_difference=Decimal(
                            str(c["metrics"]["fill_fraction"])
                        )
                        - Decimal(str(baseline["fill_fraction"])),
                        total_shortfall_difference_twice_cents=c["metrics"][
                            "total_shortfall_twice_cents"
                        ]
                        - baseline["total_shortfall_twice_cents"],
                    )
                )
        for a, b in zip(evidence["comparisons"], expected_comparisons):
            self.same(a, b)
        expected_sensitivities = []
        for c in evidence["cases"]:
            w = c["workload"]
            base_id = w["path"] + "_" + w["side"] + "_base"
            if w["id"] == base_id or base_id not in grouped:
                continue
            baseline = next(
                b["metrics"] for b in grouped[base_id] if b["policy"] == c["policy"]
            )
            m = c["metrics"]
            expected_sensitivities.append(
                dict(
                    workload=w["id"],
                    baseline_workload=base_id,
                    policy=c["policy"]["id"],
                    fill_fraction_difference=Decimal(str(m["fill_fraction"]))
                    - Decimal(str(baseline["fill_fraction"])),
                    total_shortfall_difference_twice_cents=m[
                        "total_shortfall_twice_cents"
                    ]
                    - baseline["total_shortfall_twice_cents"],
                    acknowledgement_receipt_difference_seconds=m[
                        "acknowledgement_receipt_delay_seconds"
                    ]
                    - baseline["acknowledgement_receipt_delay_seconds"],
                )
            )
        assert (
            len(expected_sensitivities)
            == len(evidence["sensitivities"])
            == summary["assumption_comparison_count"]
        )
        for actual, expected in zip(evidence["sensitivities"], expected_sensitivities):
            self.same(actual, expected)
        for saved in evidence["policy_summaries"]:
            metrics = policy_metrics[saved["policy"]]
            completed = [
                m["completion_receipt_delay_seconds"]
                for m in metrics
                if not m["completion_censored"]
            ]
            firsts = [
                m["first_fill_receipt_delay_seconds"]
                for m in metrics
                if m["first_fill_receipt_delay_seconds"] is not None
            ]
            cases = [
                c for c in evidence["cases"] if c["policy"]["id"] == saved["policy"]
            ]
            denominator = sum(
                m["requested_quantity"] * m["arrival_midpoint_twice_cents"]
                for m in metrics
            )
            wanted = dict(
                canceled_cases=sum(c["cancel_outcome"] == "canceled" for c in cases),
                cancel_rejected_cases=sum(
                    c["cancel_outcome"] == "rejected_after_fill" for c in cases
                ),
                spread_component_bps=Decimal(
                    10000 * sum(m["spread_component_twice_cents"] for m in metrics)
                )
                / denominator,
                timing_component_bps=Decimal(
                    10000 * sum(m["timing_component_twice_cents"] for m in metrics)
                )
                / denominator,
                unfilled_opportunity_bps=Decimal(
                    10000 * sum(m["unfilled_opportunity_twice_cents"] for m in metrics)
                )
                / denominator,
                policy=saved["policy"],
                case_count=len(metrics),
                completed_cases=len(completed),
                censored_cases=len(metrics) - len(completed),
                first_fill_cases=len(firsts),
                fill_fraction=Decimal(sum(m["filled_quantity"] for m in metrics))
                / sum(m["requested_quantity"] for m in metrics),
                total_shortfall_bps=Decimal(
                    10000 * sum(m["total_shortfall_twice_cents"] for m in metrics)
                )
                / sum(
                    m["requested_quantity"] * m["arrival_midpoint_twice_cents"]
                    for m in metrics
                ),
                mean_acknowledgement_receipt_seconds=Decimal(
                    sum(m["acknowledgement_receipt_delay_seconds"] for m in metrics)
                )
                / len(metrics),
                mean_completion_receipt_seconds=(
                    Decimal(sum(completed)) / len(completed) if completed else None
                ),
                mean_first_fill_receipt_seconds=(
                    Decimal(sum(firsts)) / len(firsts) if firsts else None
                ),
            )
            self.same(saved, wanted)
        with self.path("execution_metrics.csv").open(newline="") as f:
            rows = list(csv.DictReader(f))
        assert len(rows) == len(evidence["cases"])
        for row, c in zip(rows, evidence["cases"]):
            assert row["case"] == c["case"]
            for key, value in row.items():
                if key not in c["metrics"]:
                    continue
                expected = c["metrics"][key]
                if expected is None:
                    assert value == ""
                elif isinstance(expected, str):
                    assert value == expected
                elif isinstance(expected, bool):
                    assert value == str(expected).lower()
                else:
                    assert abs(Decimal(value) - Decimal(str(expected))) <= Decimal(
                        "1e-9"
                    )
        print(
            f"Verified {len(artifacts)} artifact hashes, {captures.records} captures, {ledgers} Decimal ledgers and {journal_inputs} journal inputs"
        )
        print(
            f"Verified {len(grouped)} matched workloads, {len(evidence['cases'])} policy cases, {fills} measured fills and {len(expected_comparisons)} paired comparisons"
        )
        print(
            "Synthetic matching, signed costs, censoring, source/receipt times, liquidity budgets, policy summaries and assumption sensitivities agree"
        )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: python -B research/verify_execution_measurement.py <report-directory>"
        )
    with localcontext() as context:
        context.prec = 80
        ExecutionMeasurementVerifier(sys.argv[1]).verify()
