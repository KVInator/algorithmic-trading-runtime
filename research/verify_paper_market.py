"""Independent Decimal, nanosecond and IANA-timezone checks of saved market probes."""

import calendar
from datetime import datetime
from decimal import Decimal, ROUND_CEILING, ROUND_FLOOR, localcontext
import hashlib
import json
from pathlib import Path
import re
import sys
from zoneinfo import ZoneInfo

from verify_paper_transport import PaperTransportVerifier


def instant(value: str) -> int:
    match = re.fullmatch(
        r"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(?:\.(\d{1,9}))?(Z|[+-]\d{2}:\d{2})",
        value,
    )
    assert match
    whole, fraction, offset = match.groups()
    seconds = calendar.timegm(datetime.fromisoformat(whole).timetuple())
    if offset != "Z":
        minutes = int(offset[1:3]) * 60 + int(offset[4:])
        seconds -= minutes * 60 * (-1 if offset[0] == "-" else 1)
    return seconds * 10**9 + int((fraction or "").ljust(9, "0"))


class PaperMarketVerifier:
    def __init__(self, directory: str | Path):
        self.directory = Path(directory).resolve()
        self.capture = PaperTransportVerifier(self.directory)
        self.quote_count = 0
        self.session_count = 0

    def calendar_rows(self, case: dict, records: list[dict], folder: Path) -> list[dict]:
        response = next(
            r for r in records if r["kind"] == "market_response" and "/v2/calendar?" in r["detail"]
        )
        assert response["status"] == 200
        assert response["detail"] == (
            "GET https://paper-api.alpaca.markets/v2/calendar?start="
            + case["first"]
            + "&end="
            + case["last"]
            + "&date_type=TRADING"
        )
        raw = json.loads((folder / response["body_file"]).read_text())
        assert case["enrolled"] is False
        rows = []
        for source in raw:
            date = source["date"]
            assert case["first"] <= date <= case["last"]
            if rows:
                assert date > rows[-1]["date"]
            clocks = []
            for field in ("open", "close"):
                local = datetime.fromisoformat(date + "T" + source[field])
                assert local.weekday() < 5
                clock = local.replace(tzinfo=ZoneInfo("America/New_York"))
                clocks.append(clock.astimezone(ZoneInfo("UTC")).isoformat().replace("+00:00", "Z"))
            regular = source["open"] in ("09:30", "09:30:00") and source["close"] in (
                "16:00",
                "16:00:00",
            )
            rows.append(dict(date=date, open_utc=clocks[0], close_utc=clocks[1], regular=regular))
        assert rows == case["sessions"]
        candidates = [
            r
            for r in rows
            if r["regular"] and instant(r["open_utc"]) > instant(case["received"]["utc"])
        ][:5]
        assert candidates == case["candidate_sessions"]
        assert case["five_candidates_available"] == (len(candidates) == 5)
        assert case["received"] == {
            "utc": response["received_at"],
            "monotonic_ms": response["monotonic_ms"],
        }
        self.session_count += len(rows)
        return rows

    def quote(self, case: dict, sessions: list[dict], records: list[dict], folder: Path) -> None:
        response = next(
            r
            for r in records
            if r["kind"] == "market_response" and "/quotes/latest?" in r["detail"]
        )
        assert response["status"] == 200
        raw = json.loads((folder / response["body_file"]).read_text(), parse_float=Decimal)
        assert raw["symbol"] == case["symbol"] == "SPY"
        assert case["requested_feed"] == "sip" and case["currency"] == "USD"
        q = raw["quote"]
        assert all(
            isinstance(q[k], (int, Decimal)) and not isinstance(q[k], bool) for k in ("bp", "ap")
        )
        assert all(isinstance(q[k], int) and not isinstance(q[k], bool) for k in ("bs", "as"))
        assert Decimal(case["bid"]) == Decimal(q["bp"])
        assert Decimal(case["ask"]) == Decimal(q["ap"])
        assert q["bs"] == case["bid_size_round_lots"]
        assert q["as"] == case["ask_size_round_lots"]
        for source, saved in (
            ("bx", "bid_exchange"),
            ("ax", "ask_exchange"),
            ("z", "tape"),
            ("c", "conditions"),
            ("t", "source_at"),
        ):
            assert q[source] == case[saved]
        received, evaluated = case["received"], case["evaluated"]
        assert received == {
            "utc": response["received_at"],
            "monotonic_ms": response["monotonic_ms"],
        }
        at, arrival, source = instant(evaluated["utc"]), instant(received["utc"]), instant(q["t"])
        age = at - source
        assert age == case["source_age_ns"]
        denied = []
        if (
            received["monotonic_ms"] < 0
            or evaluated["monotonic_ms"] < received["monotonic_ms"]
            or at < arrival
        ):
            denied.append("receipt_clock_regression")
        else:
            elapsed = evaluated["monotonic_ms"] - received["monotonic_ms"]
            if elapsed > 2000:
                denied.append("receipt_too_old")
            elif abs(at - arrival - elapsed * 10**6) > 250000000:
                denied.append("receipt_clock_disagreement")
        if age < 0:
            denied.append("future_source_timestamp")
        elif age > 2 * 10**9:
            denied.append("source_age_over_two_seconds")
        bid, ask = Decimal(q["bp"]), Decimal(q["ap"])
        if min(bid, ask) < 1:
            denied.append("price_below_one_usd")
        if bid >= ask:
            denied.append("crossed_or_locked")
        if min(q["bs"], q["as"]) <= 0:
            denied.append("nonpositive_displayed_size")
        session = next(
            (s for s in sessions if instant(s["open_utc"]) <= at < instant(s["close_utc"])), None
        )
        if session is None:
            denied.append("outside_calendar_session")
        elif not session["regular"]:
            denied.append("shortened_session_excluded")
        elif not instant(session["open_utc"]) <= source < instant(session["close_utc"]):
            denied.append("quote_outside_current_session")
        if not denied and max(bid // 1, ask // 1) > (2**63 - 1 - 100) // 100:
            denied.append("limit_cent_range_unsupported")
        assert denied == case["denials"]
        assert case["quote_usable"] == (not denied)
        limits = (
            [None, None, None]
            if denied
            else [
                int((bid * 100).to_integral_value(rounding=ROUND_FLOOR)),
                int((ask * 100).to_integral_value(rounding=ROUND_CEILING)),
                int((bid * 100).to_integral_value(rounding=ROUND_FLOOR)),
            ]
        )
        assert limits == [
            case[k] for k in ("passive_buy_cents", "marketable_buy_cents", "marketable_sell_cents")
        ]
        self.quote_count += 1

    def verify(self) -> None:
        summary = json.loads(self.capture.path("summary.json").read_text())
        assert (
            summary["schema_version"] == 1
            and summary["model_contract"] == "paper_quote_calendar_v1"
        )
        assert summary["order_submissions"] == 0 and summary["session_enrollment_created"] is False
        assert summary["network_requests_sent"] == (
            None if summary["native_read_only_probe"] else 0
        )
        assert summary["service_access_observed"] == (
            summary["native_read_only_probe"] and summary["status"] == "read_only_access_verified"
        )
        names = [a["path"] for a in summary["artifacts"]]
        assert len(names) == len(set(names))
        for artifact in summary["artifacts"]:
            assert (
                hashlib.sha256(self.capture.path(artifact["path"]).read_bytes()).hexdigest()
                == artifact["sha256"]
            )
            if artifact["path"].endswith("capture.journal"):
                self.capture.capture(artifact["path"])
        assert (
            hashlib.sha256(self.capture.path("source_inventory.txt").read_bytes()).hexdigest()
            == summary["source_sha256"]
        )
        evidence = json.loads(self.capture.path("market_evidence.json").read_text())
        assert len(evidence["checks"]) == summary["checked_assertions"]
        assert len(evidence["cases"]) == summary["case_count"]
        cases = (
            [(None, evidence["cases"])]
            if summary["native_read_only_probe"]
            else [(c["case"], c["observations"]) for c in evidence["cases"]]
        )
        if summary["status"] == "read_only_access_verified":
            assert [c["case"] for c in evidence["cases"]] == [
                "current_calendar",
                "latest_sip_quote",
            ]
            assert summary["request_attempts"] == 2
        attempts = 0
        for name, observations in cases:
            folder = self.directory / (name or "") / "capture"
            journal = folder / "capture.journal"
            records = (
                [json.loads(line.rsplit(b"\t", 1)[0]) for line in journal.read_bytes().splitlines()]
                if journal.exists()
                else []
            )
            assert all(
                (folder / r["body_file"]).stat().st_size <= 65536
                for r in records
                if r["kind"] == "market_response"
            )
            attempts += sum(r["kind"] == "market_request" for r in records)
            sessions = []
            for case in observations:
                if case["case"] == "current_calendar":
                    sessions = self.calendar_rows(case, records, folder)
                elif case["case"] == "latest_sip_quote":
                    self.quote(case, sessions, records, folder)
                else:
                    raise AssertionError("unsupported market observation")
        assert attempts == summary["request_attempts"]
        print(
            f"Verified {len(names)} artifact hashes, {self.capture.records} capture records, "
            f"{self.session_count} session conversions and {self.quote_count} exact quote assessments"
        )
        print("Read-only access and saved quote gates do not establish order-launcher readiness")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: python research/verify_paper_market.py <report-directory>")
    with localcontext() as context:
        context.prec = 100
        PaperMarketVerifier(sys.argv[1]).verify()
