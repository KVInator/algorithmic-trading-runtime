"""Independent capture-chain, artifact and Decimal-ledger checks for transport evidence."""

from decimal import localcontext
import hashlib
import json
from pathlib import Path
import sys

from verify_paper_execution import PaperExecutionVerifier


class PaperTransportVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.records = 0
        self.ledgers = 0
        self.inputs = 0

    @staticmethod
    def digest(data):
        return hashlib.sha256(data).hexdigest()

    def path(self, name):
        path = (self.directory / name).resolve()
        assert path.is_relative_to(self.directory) and path != self.directory
        return path

    def capture(self, name):
        previous = "0" * 64
        parent = self.path(name).parent
        for sequence, line in enumerate(self.path(name).read_bytes().splitlines()):
            payload, checksum = line.rsplit(b"\t", 1)
            assert self.digest(payload) == checksum.decode()
            row = json.loads(payload)
            assert row["sequence"] == sequence and row["previous_sha256"] == previous
            previous = checksum.decode()
            body_path = (parent / row["body_file"]).resolve()
            assert body_path.parent == parent and row["body_file"] == f"body_{sequence}.txt"
            body = body_path.read_bytes()
            assert self.digest(body) == row["body_sha256"] and len(body) <= 1024 * 1024
            assert isinstance(row["monotonic_ms"], int) and row["monotonic_ms"] >= 0
            assert row["received_at"].endswith("Z")
            if row["kind"] == "http_request":
                method, url = row["detail"].split(" ", 1)
                assert method in ["GET", "POST", "DELETE"]
                assert url.startswith("https://paper-api.alpaca.markets/v2/")
                assert "APCA-API" not in row["detail"]
                if method == "POST":
                    request = json.loads(body)
                    assert request["symbol"] == "SPY" and request["type"] == "limit"
                    assert request["time_in_force"] == "day" and not request["extended_hours"]
                else:
                    assert body == b""
            elif row["kind"] == "connection":
                assert body == b""
            else:
                assert row["kind"] in ["http_response", "stream"]
            self.records += 1

    def control(self, journal):
        state = dict(stopped=False, reason="", deadlines={})
        previous = None
        records = 0
        for sequence, line in enumerate(journal.read_bytes().splitlines()):
            payload, checksum = line.rsplit(b"\t", 1)
            assert self.digest(payload) == checksum.decode()
            row = json.loads(payload)
            if sequence == 0:
                assert row["schema_version"] == 1
                assert row["model_contract"] == "paper_control_stop_deadline_v1"
                assert row["order_tif"] == "DAY"
            else:
                assert row["sequence"] == sequence and row["previous_sha256"] == previous
                command = row["command"]
                if command["kind"] == "deadline":
                    assert not state["stopped"]
                    assert command["client_id"] not in state["deadlines"]
                    state["deadlines"][command["client_id"]] = command["expires_at"]
                else:
                    assert command["kind"] == "stop" and command["reason"]
                    if not state["stopped"]:
                        state["stopped"] = True
                        state["reason"] = command["reason"]
                assert row["state"] == state
                records += 1
            previous = checksum.decode()
        return state, records, previous

    def verify(self):
        summary = json.loads(self.path("summary.json").read_text())
        assert summary["schema_version"] == 1 and summary["real_order_submissions"] == 0
        native = summary["native_read_only_probe"]
        assert summary["network_requests_sent"] == (None if native else 0)
        assert summary["broker_order_events_observed"] == (None if native else 0)
        artifacts = summary["artifacts"]
        names = [entry["path"] for entry in artifacts]
        assert len(set(names)) == len(names)
        for entry in artifacts:
            assert self.digest(self.path(entry["path"]).read_bytes()) == entry["sha256"]
        assert (
            self.digest(self.path("source_inventory.txt").read_bytes()) == summary["source_sha256"]
        )
        evidence = json.loads(self.path("transport_evidence.json").read_text())
        assert len(evidence["checks"]) == summary["checked_assertions"]
        assert len(evidence["cases"]) == summary["case_count"]
        for name in names:
            if name.endswith("capture.journal"):
                self.capture(name)
        ledgers = {}
        for name in names:
            if name.endswith("runtime.journal"):
                journal = self.path(name)
                verifier = PaperExecutionVerifier(journal.parent)
                verifier.journal(journal.name)
                ledgers[journal] = verifier
                self.ledgers += 1
                self.inputs += verifier.inputs
        for case in evidence["cases"]:
            journal = self.path("runtime.journal" if native else case["case"] + "/runtime.journal")
            verifier = ledgers[journal]
            state = case["state"]
            account, state_hash, tip = verifier.frames[journal.name][state["durable_records"]]
            assert account == state["account"] and tip == state["journal_sha256"]
            values = {
                k: v for k, v in state.items() if k not in ["durable_records", "journal_sha256"]
            }
            assert self.digest(verifier.encoded(values)) == state_hash
            if "control" in case:
                policy, count, tip = self.control(journal.parent / "control.journal")
                saved = case["control"]
                assert all(saved[k] == policy[k] for k in policy)
                assert saved["durable_records"] == count and saved["journal_sha256"] == tip
                assert saved["execution"] == state
        print(
            f"Verified {len(artifacts)} artifact hashes, {self.records} capture records, "
            f"{self.ledgers} Decimal ledgers and {self.inputs} journal inputs"
        )
        if native:
            print("Read-only evidence only; no order-lifecycle or heartbeat acceptance implied")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: python research/verify_paper_transport.py <report-directory>")
    with localcontext() as context:
        context.prec = 80
        PaperTransportVerifier(sys.argv[1]).verify()
