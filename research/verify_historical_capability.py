"""Independently check capability evidence locally and export a price-free review ZIP."""

from __future__ import annotations

from collections import Counter
import csv
from datetime import date, datetime, timedelta, timezone
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import sys
from urllib.parse import parse_qs, urlsplit
import zipfile


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def timestamp_ns(text):
    match = re.fullmatch(r"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)(?:\.(\d{1,9}))?(Z|[+-]\d\d:\d\d)", text)
    require(match is not None, "Invalid timestamp syntax")
    base, fraction, offset = match.groups()
    point = datetime.fromisoformat(base + ("+00:00" if offset == "Z" else offset))
    seconds = int((point - datetime(1970, 1, 1, tzinfo=timezone.utc)).total_seconds())
    return seconds * 10**9 + int((fraction or "0").ljust(9, "0"))


def utc_clock(day, hour, minute):
    # This independent calendar arithmetic is bounded to the post-2007 rule.
    march = date(day.year, 3, 1)
    november = date(day.year, 11, 1)
    spring = march + timedelta(days=(6 - march.weekday()) % 7 + 7)
    autumn = november + timedelta(days=(6 - november.weekday()) % 7)
    offset = 4 if spring <= day < autumn else 5
    point = datetime(day.year, day.month, day.day, hour, minute, tzinfo=timezone.utc)
    return timestamp_ns((point + timedelta(hours=offset)).isoformat().replace("+00:00", "Z"))


def numeric(row, key):
    value = row[key]
    require(isinstance(value, (int, Decimal)) and not isinstance(value, bool), "Invalid numeric field type")
    value = Decimal(value)
    require(value.is_finite() and value >= 0, "Invalid numeric field value")
    return value


def action_date(row, key):
    value = row[key]
    require(isinstance(value, str) and len(value) == 10, "Invalid action date")
    return date.fromisoformat(value)


class CapabilityVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.summary = json.loads((self.directory / "summary.json").read_text(encoding="utf-8"))
        self.responses = []

    def verify(self, bundle=True):
        s = self.summary
        require(s["schema_version"] == 1 and s["kind"] == "historical_capability", "Unknown capability schema")
        require(s["evidence_origin"] in {"synthetic", "provider"}, "Unknown evidence origin")
        require(s["symbols"] == ["SPY", "QQQ", "IWM", "TLT", "GLD"], "Unexpected universe")
        require(2016 <= s["first_year"] <= s["last_year"] <= 2025, "Unexpected study bounds")
        for flag in ("strategy_scored", "full_history_verified", "research_inputs_ready", "corporate_actions_reconciled"):
            require(s[flag] is False, "Unsupported completion claim")
        attempts = s["attempts"]
        require(len(attempts) == s["request_attempts"] <= 5000, "Request bound/count mismatch")
        require(s["network_requests"] == (len(attempts) if s["evidence_origin"] == "provider" else 0), "Network count mismatch")
        total_bytes = 0
        for index, item in enumerate(attempts):
            expected_file = ("sealed/" if item["year"] >= 2024 else "responses/") + f"{index}.json"
            require(item["raw_file"] == expected_file, "Raw evidence escaped its declared boundary")
            path = self.directory / expected_file
            require(not path.is_symlink() and digest(path) == item["sha256"], "Raw response identity mismatch")
            require(path.stat().st_size == item["bytes"] <= 65536, "Response size mismatch")
            total_bytes += item["bytes"]
            require(json.loads((self.directory / "index" / f"{index}.json").read_text()) == item, "Attempt index mismatch")
            require(item["host"] == ("paper-api.alpaca.markets" if item["key"] == "calendar" else "data.alpaca.markets"), "Unexpected host")
            query = parse_qs(urlsplit(item["request_path"]).query)
            kind = item["key"].split("/")[0]
            expected_path = {"calendar": "/v2/calendar", "bars": "/v2/stocks/bars", "quotes": "/v2/stocks/quotes",
                             "trades": "/v2/stocks/trades", "actions": "/v1/corporate-actions"}[kind]
            require(urlsplit(item["request_path"]).path == expected_path, "Unexpected request endpoint")
            if item["key"] != "calendar":
                require(query["symbols"] == [item["key"].split("/")[1]], "Request symbol mismatch")
            if item["key"].split("/")[0] in {"bars", "quotes", "trades"}:
                require(query["feed"] == ["sip"] and query["asof"] == ["-"] and query["currency"] == ["USD"], "Feed/mapping convention changed")
                require(query["start"][0][:4] == str(item["year"]) and query["end"][0][:4] == str(item["year"]), "Request year mismatch")
            if item["http_status"] == 200:
                self.responses.append((item, query, json.loads(path.read_text(), parse_float=Decimal)))
        require(total_bytes <= 64 * 1024 * 1024, "Evidence byte bound exceeded")
        selected = self.verify_calendars()
        expected_pairs = {(symbol, year) for symbol in s["symbols"] for year in range(s["first_year"], s["last_year"] + 1)}
        require(len(s["rows"]) == len(expected_pairs), "Capability row count mismatch")
        actual_pairs = set()
        for row in s["rows"]:
            pair = row["symbol"], row["year"]
            require(pair not in actual_pairs, "Duplicate capability row")
            actual_pairs.add(pair)
            dates = selected.get(row["year"], [])
            require([b["date"] for b in row["bar_samples"]] == dates, "Calendar/sample date mismatch")
            for sample in row["bar_samples"]:
                self.verify_bars(row, sample)
            complete = len(dates) == 2 and all(b["status"] == "complete_sample" for b in row["bar_samples"])
            require(row["sampled_bars_complete"] == complete, "Sample completeness mismatch")
            for kind in ("quotes", "trades"):
                self.verify_ticks(row, kind)
            self.verify_actions(row)
        require(actual_pairs == expected_pairs, "Incomplete instrument/year matrix")
        candidates = [year for year in range(s["first_year"], s["last_year"] + 1)
                      if all(r["sampled_bars_complete"] for r in s["rows"] if r["year"] >= year)]
        require(s["provisional_common_price_start"] == (candidates[0] if candidates else None), "Common-start claim mismatch")
        all_bars = all(row["sampled_bars_complete"] for row in s["rows"])
        require(s["status"] == ("sampled_price_route_ready" if all_bars else "input_access_review_required"), "Route status mismatch")
        require({p.name for p in (self.directory / "review").iterdir()} == {"summary.json", "capability.csv", "report.html"}, "Unexpected review bundle contents")
        for name in ("summary.json", "capability.csv", "report.html"):
            require((self.directory / name).read_bytes() == (self.directory / "review" / name).read_bytes(), "Review content mismatch")
        for item in s["review_artifacts"]:
            require(item["path"] in {"capability.csv", "report.html"} and digest(self.directory / item["path"]) == item["sha256"], "Review artifact hash mismatch")
        with (self.directory / "capability.csv").open(newline="", encoding="utf-8") as stream:
            table = list(csv.DictReader(stream))
        require(len(table) == len(s["rows"]), "CSV row count mismatch")
        for record, row in zip(table, s["rows"]):
            expected = {"symbol": row["symbol"], "year": str(row["year"]),
                        "sampled_bars_complete": "1" if row["sampled_bars_complete"] else "0",
                        "quotes": row["quotes"]["status"], "trades": row["trades"]["status"], "actions": row["actions"]["status"]}
            expected.update({key: str(row["actions"][key]) for key in ("cash_dividends", "complete_payment_fields", "splits", "complete_split_fields")})
            require(record == expected, "CSV value mismatch")
        if bundle:
            self.export_bundle()

    def matches(self, key, year):
        return [(item, query, body) for item, query, body in self.responses if item["key"] == key and item["year"] == year]

    def verify_calendars(self):
        selected = {}
        expected = {(year, month) for year in range(self.summary["first_year"], self.summary["last_year"] + 1) for month in ("01", "12")}
        actual = set()
        for item in self.summary["calendar_samples"]:
            pair = item["year"], item["month"]
            require(pair not in actual, "Duplicate calendar sample")
            actual.add(pair)
            if item["status"] != "sampled_calendar_valid":
                continue
            pages = [p for p in self.matches("calendar", item["year"]) if p[1]["start"][0][5:7] == item["month"]]
            require(len(pages) == 1, "Calendar evidence missing")
            body = pages[0][2]
            require(isinstance(body, list) and 1 <= len(body) <= 31, "Invalid calendar size")
            previous, regular = None, []
            for row in body:
                day = date.fromisoformat(row["date"])
                require(day.year == item["year"] and f"{day.month:02}" == item["month"] and day.weekday() < 5, "Invalid calendar date")
                require(previous is None or day > previous, "Nonascending calendar")
                previous = day
                opening, closing = row["open"][:5], row["close"][:5]
                require("09:30" <= opening < closing <= "16:00", "Invalid calendar clocks")
                if opening == "09:30" and closing == "16:00":
                    regular.append(row["date"])
            chosen = regular[0] if item["month"] == "01" else regular[-1]
            require(chosen == item["selected_date"] and len(body) == item["sessions"], "Calendar selection mismatch")
            selected.setdefault(item["year"], []).append(chosen)
        require(actual == expected, "Incomplete calendar matrix")
        return selected

    def verify_bars(self, row, sample):
        if sample["status"] not in {"complete_sample", "gapped_sample"}:
            return
        pages = [p for p in self.matches("bars/" + row["symbol"], row["year"]) if p[1]["start"][0][:10] == sample["date"]]
        require(len(pages) == sample["pages"] and len(pages) <= 16, "Bar pagination count mismatch")
        day = date.fromisoformat(sample["date"])
        start, end = utc_clock(day, 9, 30), utc_clock(day, 16, 0)
        expected_grid = list(range(start, end, 60 * 10**9))
        observed, next_token, tokens = [], None, set()
        for _, query, body in pages:
            require(query.get("page_token", [None])[0] == next_token, "Broken bar page chain")
            require(query["timeframe"] == ["1Min"] and query["adjustment"] == ["raw"], "Bar convention mismatch")
            require(timestamp_ns(query["start"][0]) == start and timestamp_ns(query["end"][0]) == end - 60 * 10**9, "Bar request interval mismatch")
            groups = body["bars"]
            require(isinstance(groups, dict) and (not groups or list(groups) == [row["symbol"]]), "Unexpected bar symbol")
            values = groups.get(row["symbol"], [])
            require(isinstance(values, list) and len(values) <= 100, "Invalid bar page size")
            for value in values:
                at = timestamp_ns(value["t"])
                require(at in expected_grid and (not observed or at > observed[-1]), "Bar chronology/grid mismatch")
                o, h, l, c = (numeric(value, key) for key in ("o", "h", "l", "c"))
                numeric(value, "v")
                require(l > 0 and l <= o <= h and l <= c <= h, "Invalid OHLC observation")
                observed.append(at)
            next_token = body["next_page_token"]
            require(next_token is None or isinstance(next_token, str) and 0 < len(next_token) <= 4096 and next_token not in tokens, "Invalid bar token")
            if next_token is not None:
                tokens.add(next_token)
        missing = len(set(expected_grid) - set(observed))
        require(next_token is None and sample["expected_minutes"] == 390 and sample["observed_minutes"] == len(observed), "Bar coverage count mismatch")
        require(sample["missing_minutes"] == missing and sample["status"] == ("complete_sample" if missing == 0 else "gapped_sample"), "Bar gap classification mismatch")

    def verify_ticks(self, row, kind):
        item = row[kind]
        if item["status"] not in {"accessible_empty", "accessible_sample"}:
            return
        pages = self.matches(kind + "/" + row["symbol"], row["year"])
        require(len(pages) == 1, "Tick first-page evidence missing")
        _, query, body = pages[0]
        groups = body[kind]
        require(isinstance(groups, dict) and (not groups or list(groups) == [row["symbol"]]), "Unexpected tick symbol")
        values = groups.get(row["symbol"], [])
        require(isinstance(values, list) and len(values) <= 100, "Invalid tick sample size")
        start, end = timestamp_ns(query["start"][0]), timestamp_ns(query["end"][0])
        require(0 < end - start <= 60 * 10**9, "Tick window bound mismatch")
        previous, usable = None, 0
        for value in values:
            at = timestamp_ns(value["t"])
            require(start <= at <= end and (previous is None or at >= previous), "Tick chronology mismatch")
            previous = at
            if kind == "quotes":
                bp, ap, bs, ask_size = (numeric(value, key) for key in ("bp", "ap", "bs", "as"))
                usable += bp > 0 and ap >= bp and bs > 0 and ask_size > 0
            else:
                price, size = numeric(value, "p"), numeric(value, "s")
                usable += price > 0 and size > 0
        token = body["next_page_token"]
        require(token is None or isinstance(token, str) and 0 < len(token) <= 4096, "Invalid tick continuation")
        require(item["records"] == len(values) and item["usable_records"] == usable, "Tick count mismatch")
        require(item["status"] == ("accessible_sample" if values else "accessible_empty"), "Tick availability mismatch")
        require(item["more_pages"] == (token is not None) and item["pagination_exhausted"] == (token is None) and item["full_window_verified"] is False, "Tick scope mismatch")

    def verify_actions(self, row):
        item = row["actions"]
        require(item["issuer_reconciled"] is False and item["ex_date_coverage_verified"] is False, "Unsupported action completeness claim")
        if item["status"] not in {"accessible_empty", "accessible_records", "accessible_incomplete_fields"}:
            return
        counts = Counter(records=0, cash_dividends=0, complete_payment_fields=0, splits=0, complete_split_fields=0)
        types, seen, tokens, token = Counter(), set(), set(), None
        for _, query, body in self.matches("actions/" + row["symbol"], row["year"]):
            require(query.get("page_token", [None])[0] == token, "Broken action page chain")
            groups = body["corporate_actions"]
            require(isinstance(groups, dict), "Invalid action groups")
            page_count = 0
            for kind, values in groups.items():
                require(isinstance(values, list), "Invalid action group")
                types[kind] += len(values)
                for value in values:
                    page_count += 1
                    require(value["symbol"] == row["symbol"] and value["id"] and value["id"] not in seen, "Action identity mismatch")
                    seen.add(value["id"])
                    process = action_date(value, "process_date")
                    require(process.year == row["year"], "Action process-year mismatch")
                    counts["records"] += 1
                    if kind == "cash_dividends":
                        counts["cash_dividends"] += 1
                        try:
                            ex, pay, record = (action_date(value, key) for key in ("ex_date", "payable_date", "record_date"))
                            counts["complete_payment_fields"] += numeric(value, "rate") > 0 and pay >= ex and record >= ex
                        except (KeyError, ValueError, TypeError):
                            pass
                    elif kind in {"forward_splits", "reverse_splits", "unit_splits"}:
                        counts["splits"] += 1
                        try:
                            action_date(value, "ex_date")
                            counts["complete_split_fields"] += numeric(value, "old_rate") > 0 and numeric(value, "new_rate") > 0
                        except (KeyError, ValueError, TypeError):
                            pass
            require(page_count <= 100, "Action page bound exceeded")
            token = body["next_page_token"]
            require(token is None or isinstance(token, str) and 0 < len(token) <= 4096 and token not in tokens, "Invalid action token")
            if token is not None:
                tokens.add(token)
        require(token is None and item["pagination_exhausted"] is True, "Actions not exhausted")
        require(all(item[key] == value for key, value in counts.items()) and item["types"] == dict(types), "Action availability counts mismatch")
        expected = "accessible_empty" if counts["records"] == 0 else "accessible_records"
        if counts["cash_dividends"] != counts["complete_payment_fields"] or counts["splits"] != counts["complete_split_fields"]:
            expected = "accessible_incomplete_fields"
        require(item["status"] == expected, "Action availability classification mismatch")

    def export_bundle(self):
        path = self.directory / "review.zip"
        names = ("summary.json", "capability.csv", "report.html")
        if path.exists():
            with zipfile.ZipFile(path) as archive:
                require(set(archive.namelist()) == set(names), "Existing review ZIP has unexpected contents")
                for name in names:
                    require(archive.read(name) == (self.directory / "review" / name).read_bytes(), "Existing review ZIP differs")
            return
        with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED) as archive:
            for name in names:
                archive.write(self.directory / "review" / name, name)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: python -B research/verify_historical_capability.py <printed-directory>")
    directory = Path(sys.argv[1]).resolve()
    if (directory / "checks.json").exists():
        checks = json.loads((directory / "checks.json").read_text())
        require(checks["evidence_origin"] == "synthetic" and checks["network_requests"] == 0, "Invalid constructed-check origin")
        children = sorted(p for p in directory.iterdir() if p.is_dir() and (p / "summary.json").exists())
        require(len(children) == 5, "Constructed scenario bundle missing")
        for child in children:
            CapabilityVerifier(child).verify(bundle=False)
        print(f"Verified {len(children)} constructed capability bundles and their raw evidence; no market results inspected.")
    else:
        CapabilityVerifier(directory).verify()
        print(f"Capability evidence verified. Share only: {directory / 'review.zip'}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, zipfile.BadZipFile) as error:
        raise SystemExit(f"Capability verification failed: {type(error).__name__}; review local evidence.") from None
