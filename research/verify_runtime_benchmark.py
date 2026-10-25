"""Independent benchmark quantiles, throughput, fixture economics and journal checks."""

from decimal import Decimal, localcontext
import csv
import hashlib
import json
from pathlib import Path
import sys

from verify_paper_execution import PaperExecutionVerifier


class RuntimeBenchmarkVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()

    def path(self, relative):
        path = (self.directory / relative).resolve()
        assert path.is_relative_to(self.directory) and path != self.directory
        return path

    @staticmethod
    def digest(data):
        return hashlib.sha256(data).hexdigest()

    @staticmethod
    def distribution(values):
        assert values and all(isinstance(v, int) and v >= 0 for v in values)
        ordered = sorted(values)
        return dict(
            count=len(values),
            min_ns=ordered[0],
            max_ns=ordered[-1],
            **{
                f"p{q}_ns": ordered[(q * len(values) + 99) // 100 - 1]
                for q in (50, 95, 99)
            },
        )

    def legacy(self, path, workload, recovered):
        expected = [dict(kind="disconnect"), *workload["typed_inputs"]]
        outcomes = ["applied", *workload["expected_outcomes"]]
        if recovered:
            expected.append(dict(kind="disconnect"))
            outcomes.append("applied")
        cash, position, connected, reconciliation = 10000000, 0, True, False
        orders, executions = {}, set()
        previous = "0" * 64
        lines = path.read_bytes().splitlines()
        assert len(lines) == len(expected) + 1
        for index, line in enumerate(lines):
            payload, checksum = line.rsplit(b"\t", 1)
            assert self.digest(payload) == checksum.decode()
            row = json.loads(payload)
            assert row["sequence"] == index and row["previous_sha256"] == previous
            previous = checksum.decode()
            if index == 0:
                assert (
                    row["schema_version"] == 2
                    and row["configuration"]["initial_cash_cents"] == cash
                )
                continue
            c = row["input"]
            assert (
                c == expected[index - 1]
                and row["trace"]["outcome"] == outcomes[index - 1]
            )
            kind = c["kind"]
            if kind == "disconnect":
                connected, reconciliation = False, True
            elif kind == "reconnect":
                connected, reconciliation = True, True
            elif kind == "reconcile":
                assert c["cash_cents"] == cash and c["position_shares"] == position
                reconciliation = False
            elif kind == "submit":
                o = c["order"]
                assert o["id"] not in orders
                orders[o["id"]] = dict(
                    side=o["side"], qty=o["shares"], filled=0, limit=o["limit_cents"]
                )
            elif kind == "report" and c["report_kind"] == "fill":
                o = orders[c["order_id"]]
                if c["execution_id"] in executions:
                    assert row["trace"]["outcome"] == "duplicate"
                else:
                    executions.add(c["execution_id"])
                    sign = 1 if o["side"] == "buy" else -1
                    cash -= (
                        sign * c["fill_shares"] * c["fill_price_cents"] + c["fee_cents"]
                    )
                    position += sign * c["fill_shares"]
                    o["filled"] += c["fill_shares"]
            outstanding = sum(
                o["qty"] - o["filled"] for o in orders.values() if o["side"] == "buy"
            )
            reserved = sum(
                (o["qty"] - o["filled"]) * o["limit"]
                for o in orders.values()
                if o["side"] == "buy"
            )
            sell_reserved = sum(
                o["qty"] - o["filled"] for o in orders.values() if o["side"] == "sell"
            )
            account = dict(
                cash_cents=cash,
                position_shares=position,
                reserved_cash_cents=reserved,
                reserved_sell_shares=sell_reserved,
                outstanding_buy_shares=outstanding,
                open_orders=sum(o["filled"] < o["qty"] for o in orders.values()),
                connected=connected,
                reconciliation_required=reconciliation,
            )
            assert row["trace"]["account"] == account
        assert cash == 10000000 + workload["round_trips"] and position == 0
        assert connected is not recovered and reconciliation is recovered
        return len(lines) - 1

    def verify(self):
        summary = json.loads(self.path("summary.json").read_text())
        assert (
            summary["schema_version"] == 1
            and summary["evidence_origin"] == "measured_processing_synthetic_inputs"
        )
        assert summary["release_reference"] == (
            summary["build_configuration"] == "Release"
        )
        names = [a["path"] for a in summary["artifacts"]]
        assert len(names) == len(set(names))
        for a in summary["artifacts"]:
            assert self.digest(self.path(a["path"]).read_bytes()) == a["sha256"]
        assert (
            self.digest(self.path("source_inventory.txt").read_bytes())
            == summary["source_sha256"]
        )
        data = json.loads(self.path("benchmark_evidence.json").read_text())
        if "build_settings" in summary:
            assert summary["build_settings"] == data["build_settings"]
            assert summary["build_settings"]["cxx_standard"] == 20
        assert data["clock_probe"]["count"] == 1000
        rows, workloads = data["rows"], {w["round_trips"]: w for w in data["workloads"]}
        assert len(rows) == summary["rows"]
        for n, w in workloads.items():
            assert (
                len(w["typed_inputs"]) == 7 * n + 2
                and len(w["paper_commands"]) == 7 * n + 3
            )
            assert (
                w["expected_outcomes"].count("duplicate")
                == w["paper_expected_outcomes"].count("duplicate")
                == n
            )
            # Independent analytical order count/economics, beyond agreement between engines.
            assert len(w["orders"]) == 2 * n and all(
                o["shares"] == o["filled"] == 1 and o["status"] == "filled"
                for o in w["orders"]
            )
        group_modes = {}
        for r in rows:
            n, mode = r["round_trips"], r["mode"]
            group_modes.setdefault((n, r["trial"]), set()).add(mode)
            w = workloads[n]
            recovery, paper = mode.endswith("recovery"), mode.startswith("paper_")
            assert r["sample_unit"] == ("recovery_operation" if recovery else "input")
            expected_count = 1 if recovery else 7 * n + (3 if paper else 2)
            assert len(r["latencies_ns"]) == expected_count
            assert self.distribution(r["latencies_ns"]) == r["latency"]
            assert sum(r["latencies_ns"]) == r["elapsed_ns"] > 0
            expected_rate = Decimal(10**9 * expected_count) / r["elapsed_ns"]
            assert abs(Decimal(str(r["operations_per_second"])) - expected_rate) <= max(
                Decimal("1e-6"), expected_rate * Decimal("1e-12")
            )
            assert r["append_latency"] == (
                self.distribution(r["append_sync_ns"]) if r["append_sync_ns"] else None
            )
            if r["append_sync_ns"]:
                assert len(r["append_sync_ns"]) == expected_count
                assert all(
                    a <= b for a, b in zip(r["append_sync_ns"], r["latencies_ns"])
                )
            assert r["cash_cents"] == 10000000 + n and r["position_shares"] == 0
            expected_orders = sorted(w["orders"], key=lambda o: o["id"])
            assert (
                self.digest(
                    json.dumps(
                        expected_orders, sort_keys=True, separators=(",", ":")
                    ).encode()
                )
                == r["terminal_orders_sha256"]
            )
            assert r["recovered_routing_closed"] is recovery
            for resource in (r["resources_before"], r["resources_after"]):
                for v in resource.values():
                    assert v is None or isinstance(v, int) and v >= 0
            if r["journal_records"]:
                journal = self.path(r["directory"] + "/" + mode + ".journal")
                raw = journal.read_bytes()
                assert (
                    len(raw) == r["journal_bytes"]
                    and self.digest(raw) == r["journal_sha256"]
                )
                assert (
                    max(len(line) + 1 for line in raw.splitlines())
                    == r["largest_frame_bytes"]
                    <= summary["limits"]["frame_bytes"]
                )
                assert (
                    len(raw) <= summary["limits"]["journal_bytes"]
                    and r["journal_records"] < summary["limits"]["input_records"]
                )
                if paper:
                    v = PaperExecutionVerifier(journal.parent)
                    v.journal(journal.name)
                    commands = [
                        json.loads(line.rsplit(b"\t", 1)[0])["command"]
                        for line in raw.splitlines()[1:]
                    ]
                    wanted = [dict(kind="disconnect"), *w["paper_commands"]]
                    if recovery:
                        wanted.append(dict(kind="disconnect"))
                    assert commands == wanted
                    assert v.inputs == r["journal_records"]
                else:
                    assert self.legacy(journal, w, recovery) == r["journal_records"]
                if recovery:
                    source = self.path(
                        r["directory"]
                        + "/"
                        + ("paper_sync.journal" if paper else "journal_sync.journal")
                    )
                    assert raw.startswith(source.read_bytes())
            else:
                assert (
                    mode in ("core", "copy_candidate")
                    and r["journal_bytes"] == 0
                    and r["journal_sha256"] is None
                )
        modes = {
            "core",
            "copy_candidate",
            "journal_memory",
            "journal_sync",
            "journal_recovery",
            "paper_memory",
            "paper_sync",
            "paper_recovery",
        }
        assert all(v == modes for v in group_modes.values())
        assert len(rows) == len(group_modes) * len(modes)
        for n, trial in group_modes:
            for prefix in ("journal", "paper"):
                memory = self.path(
                    f"n{n}_trial{trial}/{prefix}_memory.journal"
                ).read_bytes()
                synced = self.path(
                    f"n{n}_trial{trial}/{prefix}_sync.journal"
                ).read_bytes()
                assert memory == synced
        with self.path("benchmark_metrics.csv").open(newline="") as f:
            csv_rows = list(csv.DictReader(f))
        assert len(csv_rows) == len(rows)
        for c, r in zip(csv_rows, rows):
            for key in ("mode", "sample_unit"):
                assert c[key] == r[key]
            for key in (
                "round_trips",
                "trial",
                "elapsed_ns",
                "journal_bytes",
                "journal_records",
                "largest_frame_bytes",
            ):
                assert int(c[key]) == r[key]
            assert int(c["count"]) == r["latency"]["count"]
            assert Decimal(c["operations_per_second"]) == Decimal(
                str(r["operations_per_second"])
            )
            for key in ("p50_ns", "p95_ns", "p99_ns", "max_ns"):
                assert int(c[key]) == r["latency"][key]
            for column, group, field in (
                ("rss_before_bytes", "resources_before", "resident_bytes"),
                ("rss_after_bytes", "resources_after", "resident_bytes"),
                ("process_peak_bytes", "resources_after", "peak_resident_bytes"),
            ):
                expected = r[group][field]
                assert c[column] == ("" if expected is None else str(expected))
        print(
            f"Verified {len(summary['artifacts'])} artifact hashes, {len(rows)} trial rows and {len(group_modes)} complete mode groups"
        )
        print(
            "Quantiles, measured rates, nested append timings, analytical balances, duplicate fills, journal replay and recovery gates agree"
        )
        print(
            "Timing values and OS memory readings remain observations, not independently reproducible constants"
        )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: python -B research/verify_runtime_benchmark.py <report-directory>"
        )
    with localcontext() as context:
        context.prec = 80
        RuntimeBenchmarkVerifier(sys.argv[1]).verify()
