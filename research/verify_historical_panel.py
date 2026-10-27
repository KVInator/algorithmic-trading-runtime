"""Independently authenticate a panel archive and export coverage-only review evidence."""

from __future__ import annotations

import argparse
import csv
from datetime import date, timedelta
from decimal import Decimal
import hashlib
import io
import json
from pathlib import Path
import zipfile

from verify_historical_capability import numeric, require, timestamp_ns, utc_clock


class PanelVerifier:
    # Fixed exchange-date oracle, separate from the production holiday algorithm.
    holidays = {
        2016: "01-01 01-18 02-15 03-25 05-30 07-04 09-05 11-24 12-26",
        2017: "01-02 01-16 02-20 04-14 05-29 07-04 09-04 11-23 12-25",
        2018: "01-01 01-15 02-19 03-30 05-28 07-04 09-03 11-22 12-05 12-25",
        2019: "01-01 01-21 02-18 04-19 05-27 07-04 09-02 11-28 12-25",
        2020: "01-01 01-20 02-17 04-10 05-25 07-03 09-07 11-26 12-25",
        2021: "01-01 01-18 02-15 04-02 05-31 07-05 09-06 11-25 12-24",
        2022: "01-17 02-21 04-15 05-30 06-20 07-04 09-05 11-24 12-26",
        2023: "01-02 01-16 02-20 04-07 05-29 06-19 07-04 09-04 11-23 12-25",
        2024: "01-01 01-15 02-19 03-29 05-27 06-19 07-04 09-02 11-28 12-25",
        2025: "01-01 01-09 01-20 02-17 04-18 05-26 06-19 07-04 09-01 11-27 12-25",
    }
    short_dates = {
        2016: "11-25", 2017: "07-03 11-24", 2018: "07-03 11-23 12-24",
        2019: "07-03 11-29 12-24", 2020: "11-27 12-24", 2021: "11-26",
        2022: "11-25", 2023: "07-03 11-24", 2024: "07-03 11-29 12-24", 2025: "07-03 11-28 12-24",
    }
    symbols = ["SPY", "QQQ", "IWM", "TLT", "GLD"]

    def __init__(self, directory: Path):
        self.directory = directory.resolve()
        self.response_files: set[str] = set()

    @staticmethod
    def digest(path: Path) -> str:
        result = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                result.update(block)
        return result.hexdigest()

    def path(self, name: str) -> Path:
        relative = Path(name)
        require(not relative.is_absolute() and all(p not in {".", ".."} for p in relative.parts), "Invalid panel artifact path")
        require("\\" not in name and ":" not in name, "Invalid panel artifact separator")
        result = self.directory / relative
        require(result.resolve().is_relative_to(self.directory) and not any(p.is_symlink() for p in [result, *result.parents]), "Panel artifact escaped its root")
        return result

    @staticmethod
    def load(path: Path, maximum: int = 16 * 1024 * 1024):
        require(path.stat().st_size <= maximum, "Panel artifact exceeded its byte bound")
        return json.loads(path.read_bytes(), parse_float=Decimal)

    def checked(self, reference):
        path = self.path(reference["file"])
        require(self.digest(path) == reference["sha256"], "Panel artifact hash mismatch")
        return self.load(path)

    def response(self, reference, expected_path: str, expected_host: str, reserved: bool):
        require(1 <= len(reference["attempts"]) <= 9, "Panel attempt bound exceeded")
        previous_status = None
        selected = None
        for index, attempt in enumerate(reference["attempts"]):
            require(attempt["file"].endswith(f"/attempt_{index}.json"), "Panel attempt chain mismatch")
            require(attempt["file"].startswith("sealed/" if reserved else "responses/"), "Panel reserved boundary mismatch")
            require(previous_status is None or previous_status in {429, 500, 502, 503, 504}, "Attempt after terminal HTTP response")
            envelope = self.checked(attempt)
            require(envelope["schema_version"] == 1 and envelope["host"] == expected_host and envelope["request_path"] == expected_path, "Panel request provenance mismatch")
            body = bytes.fromhex(envelope["body_hex"])
            require(body.hex() == envelope["body_hex"], "Noncanonical archived response encoding")
            require(len(body) <= 65536 and hashlib.sha256(body).hexdigest() == envelope["body_sha256"], "Panel response body hash/bound mismatch")
            timestamp_ns(envelope["retrieved_utc"])
            previous_status = envelope["http_status"]
            self.response_files.add(attempt["file"])
            selected = envelope
        require(reference["file"] == reference["attempts"][-1]["file"] and reference["sha256"] == reference["attempts"][-1]["sha256"], "Selected response identity mismatch")
        require(selected["http_status"] == 200, "Panel page is not complete")
        return json.loads(bytes.fromhex(selected["body_hex"]), parse_float=Decimal)

    def calendar(self, first: str, last: str):
        current, finish = date.fromisoformat(first), date.fromisoformat(last)
        days = []
        while current <= finish:
            if current.weekday() < 5 and current.strftime("%m-%d") not in self.holidays[current.year].split():
                close = "13:00" if current.strftime("%m-%d") in self.short_dates[current.year].split() else "16:00"
                days.append({"date": current.isoformat(), "open": "09:30", "close": close})
            current += timedelta(days=1)
        return days

    def verify(self):
        contract = self.load(self.path("dataset_contract.json"))
        manifest = self.load(self.path("acquisition_manifest.json"))
        require(contract["symbols"] == self.symbols and contract["schema_version"] == 1, "Panel universe/schema mismatch")
        origin = contract["evidence_origin"]
        require(origin in {"provider", "synthetic"} and manifest["evidence_origin"] == origin, "Panel origin mismatch")
        require(manifest["protocol_sha256"] == contract["protocol_sha256"], "Panel protocol identity mismatch")
        require(contract["protocol_sha256"] == self.digest(Path(__file__).resolve().parent / "extension_protocol.json"), "Panel is not governed by this registered protocol")
        require(self.checked(manifest["dataset_contract"]) == contract, "Panel contract reference mismatch")
        for field in ("strategy_scored", "research_inputs_ready", "issuer_reconciled"):
            require(manifest[field] is False, "Acquisition manifest makes a premature completion claim")
        require(len(manifest["months"]) == len(contract["ranges"]), "Panel month count mismatch")
        if origin == "provider":
            require(len(contract["ranges"]) == 120 and contract["ranges"][0]["first"] == "2016-01-01" and contract["ranges"][-1]["last"] == "2025-12-31", "Provider panel is not the registered full plan")
            import calendar
            expected_ranges = [{"first": f"{y}-{m:02}-01", "last": f"{y}-{m:02}-{calendar.monthrange(y, m)[1]}"} for y in range(2016, 2026) for m in range(1, 13)]
            require(contract["ranges"] == expected_ranges and manifest["full_price_plan_authenticated"] is True, "Provider panel months are incomplete")
        coverage = []
        years = set()
        for monthly_ref, period in zip(manifest["months"], contract["ranges"]):
            month = self.checked(monthly_ref)
            years.add(int(period["first"][:4]))
            path = f"/v2/calendar?start={period['first']}&end={period['last']}&date_type=TRADING"
            body = self.response(month["calendar"], path, "paper-api.alpaca.markets", period["first"] >= "2024")
            expected = self.calendar(period["first"], period["last"])
            require([{k: r[k][:5] if k in {"open", "close"} else r[k] for k in ("date", "open", "close")} for r in body] == expected, "Panel calendar differs from independent date/clock table")
            require(len(month["checkpoints"]) == len(expected) * 5, "Panel checkpoint count mismatch")
            for index, day in enumerate(expected):
                daily = []
                for offset, symbol in enumerate(self.symbols):
                    record = self.checked(month["checkpoints"][index * 5 + offset])
                    daily.append(self.session(record, day, symbol, contract["protocol_sha256"]))
                common = all(r["intraday_eligible"] for r in daily)
                for row in daily:
                    row["common_intraday_eligible"] = common
                coverage.extend(daily)
        expected_intervals = {(sym, y) for sym in self.symbols for y in years | ({2026} if 2025 in years else set())}
        observed_intervals = set()
        actions = []
        ids = set()
        for reference in manifest["actions"]:
            record = self.checked(reference)
            pair = record["symbol"], int(record["first_process_date"][:4])
            require(pair in expected_intervals and pair not in observed_intervals, "Panel action interval duplicated or unexpected")
            observed_intervals.add(pair)
            actions.append(self.actions(record, ids))
        require(observed_intervals == expected_intervals, "Panel action interval missing")
        return {"schema_version": 1, "kind": "historical_panel_independent_review", "evidence_origin": origin,
                "protocol_sha256": contract["protocol_sha256"], "acquisition_manifest_sha256": self.digest(self.path("acquisition_manifest.json")),
                "response_attempts_verified": len(self.response_files), "coverage": coverage, "action_coverage": actions,
                "strategy_scored": False, "research_inputs_ready": False, "issuer_reconciled": False}

    def session(self, record, day, symbol, protocol):
        require(record["symbol"] == symbol and record["date"] == day["date"] and record["protocol_sha256"] == protocol, "Panel session identity mismatch")
        value = date.fromisoformat(day["date"])
        opening = utc_clock(value, 9, 30)
        closing = utc_clock(value, int(day["close"][:2]), 0)
        grid = list(range(opening, closing, 60 * 10**9))
        observations, token = [], None
        tokens = set()
        require(1 <= len(record["pages"]) <= 16, "Panel session page bound exceeded")
        from urllib.parse import quote
        for page_ref in record["pages"]:
            from datetime import datetime, timezone
            def text(ns):
                return datetime.fromtimestamp(ns // 10**9, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
            path = (f"/v2/stocks/bars?symbols={symbol}&timeframe=1Min&start={quote(text(opening), safe='')}"
                    f"&end={quote(text(closing - 60 * 10**9), safe='')}&limit=100&feed=sip&adjustment=raw&sort=asc&currency=USD&asof=-")
            if token is not None:
                path += "&page_token=" + quote(token, safe="")
            body = self.response(page_ref, path, "data.alpaca.markets", day["date"] >= "2024")
            require(isinstance(body["bars"], dict) and set(body["bars"]) <= {symbol}, "Unexpected panel bar symbol")
            bars = body["bars"].get(symbol, [])
            require(len(bars) <= 100, "Panel bar page row bound exceeded")
            for bar in bars:
                at = timestamp_ns(bar["t"])
                require(at in grid and (not observations or at > observations[-1]), "Panel bar chronology/grid mismatch")
                o, h, low, c = (numeric(bar, k) for k in ("o", "h", "l", "c"))
                numeric(bar, "v")
                require(low > 0 and low <= o <= h and low <= c <= h, "Panel OHLC observation invalid")
                observations.append(at)
            token = body["next_page_token"]
            require(token is None or isinstance(token, str) and 0 < len(token) <= 4096 and token not in tokens, "Panel pagination token invalid")
            if token is not None:
                tokens.add(token)
        require(token is None, "Panel pagination unfinished")
        observed = set(observations)
        missing = [at for at in grid if at not in observed]
        require([timestamp_ns(t) for t in record["missing_minutes_utc"]] == missing, "Panel missing-minute mask mismatch")
        regular = day["close"] == "16:00"
        diagnostic = symbol == "SPY" and day["date"] == "2023-01-03"
        expected = {"symbol": symbol, "date": day["date"], "regular": regular, "expected_minutes": len(grid),
                    "observed_minutes": len(observations), "missing_minutes": len(missing), "open_available": opening in observed,
                    "close_available": closing - 60 * 10**9 in observed, "intraday_eligible": regular and not missing and not diagnostic,
                    "reserved": day["date"] >= "2024"}
        for k, v in expected.items():
            if k != "missing_minutes":
                require(record[k] == v, "Panel derived session field mismatch")
        require(record["complete_grid"] == (not missing) and record["diagnostic_excluded"] == diagnostic, "Panel session mask mismatch")
        require(timestamp_ns(record["open_utc"]) == opening and timestamp_ns(record["close_utc"]) == closing, "Panel UTC calendar mismatch")
        return expected

    def actions(self, record, identities):
        from urllib.parse import quote
        counters = dict.fromkeys(("records", "cash_dividends", "complete_payment_fields", "splits", "complete_split_fields"), 0)
        token = None
        tokens = set()
        unsupported = []
        require(1 <= len(record["pages"]) <= 32, "Panel action page bound exceeded")
        for page in record["pages"]:
            path = f"/v1/corporate-actions?symbols={record['symbol']}&start={record['first_process_date']}&end={record['last_process_date']}&limit=100&data_quality=complete&sort=asc"
            if token is not None:
                path += "&page_token=" + quote(token, safe="")
            body = self.response(page, path, "data.alpaca.markets", record["first_process_date"] >= "2024")
            page_count = 0
            for kind, values in body["corporate_actions"].items():
                for row in values:
                    page_count += 1
                    require(row["symbol"] == record["symbol"] and row["id"] not in identities, "Panel action identity/symbol mismatch")
                    identities.add(row["id"])
                    process = date.fromisoformat(row["process_date"])
                    require(record["first_process_date"] <= process.isoformat() <= record["last_process_date"], "Panel action process range mismatch")
                    counters["records"] += 1
                    if kind == "cash_dividends":
                        counters["cash_dividends"] += 1
                        try:
                            ex, pay, rec = (date.fromisoformat(row[k]) for k in ("ex_date", "payable_date", "record_date"))
                            counters["complete_payment_fields"] += numeric(row, "rate") > 0 and pay >= ex and rec >= ex
                        except (ValueError, TypeError, KeyError):
                            pass
                    elif kind in {"forward_splits", "reverse_splits", "unit_splits"}:
                        counters["splits"] += 1
                        try:
                            date.fromisoformat(row["ex_date"])
                            counters["complete_split_fields"] += numeric(row, "new_rate") > 0 and numeric(row, "old_rate") > 0
                        except (ValueError, TypeError, KeyError):
                            pass
                if values and kind not in {"cash_dividends", "forward_splits", "reverse_splits", "unit_splits"}:
                    unsupported.append(kind)
            require(page_count <= 100, "Panel action row bound exceeded")
            token = body["next_page_token"]
            require(token is None or isinstance(token, str) and 0 < len(token) <= 4096 and token not in tokens, "Panel action token invalid")
            if token is not None:
                tokens.add(token)
        require(token is None and all(record[k] == v for k, v in counters.items()), "Panel action counts/pagination mismatch")
        require(record.get("unsupported_types", []) == unsupported and record["issuer_reconciled"] is False, "Panel unsupported action policy mismatch")
        return {k: v for k, v in record.items() if k != "pages"}

    def export(self, summary, directory: Path):
        directory.mkdir(parents=True, exist_ok=True)
        text = json.dumps(summary, indent=2, default=str) + "\n"
        path = directory / "summary.json"
        if path.exists():
            require(path.read_text() == text, "Existing panel review differs")
        else:
            with path.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(text)
        csv_path = directory / "coverage.csv"
        buffer = io.StringIO(newline="")
        writer = csv.DictWriter(buffer, fieldnames=list(summary["coverage"][0]))
        writer.writeheader()
        writer.writerows(summary["coverage"])
        table = buffer.getvalue().encode("utf-8")
        if csv_path.exists():
            require(csv_path.read_bytes() == table, "Existing panel review table differs")
        else:
            with csv_path.open("xb") as stream:
                stream.write(table)
        archive = directory / "review.zip"
        if not archive.exists():
            with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as stream:
                for name in ("summary.json", "coverage.csv"):
                    stream.write(directory / name, name)
        else:
            with zipfile.ZipFile(archive) as stream:
                require(set(stream.namelist()) == {"summary.json", "coverage.csv"}, "Existing panel ZIP contains unexpected files")
                for name in stream.namelist():
                    require(stream.read(name) == (directory / name).read_bytes(), "Existing panel ZIP differs")
        return archive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    directory = args.directory.resolve()
    if (directory / "checks.json").exists():
        for child in ("complete", "interrupted", "outage", "history"):
            result = PanelVerifier(directory / child).verify()
            require(result["evidence_origin"] == "synthetic", "Constructed panel origin mismatch")
        print("Verified four constructed panel archives, raw evidence, calendar grids and masks.")
    else:
        if (directory / "dataset_contract.json").is_file() and not (directory / "acquisition_manifest.json").exists():
            raise SystemExit(
                f"Panel acquisition is incomplete: {directory / 'acquisition_manifest.json'} is missing.\n"
                "Run panel_acquire.exe again to resume saved responses. Wait for its final Status and Visual report "
                "before running this verifier. Keep the existing dataset directory."
            )
        verifier = PanelVerifier(directory)
        summary = verifier.verify()
        bundle = verifier.export(summary, directory / "review")
        print(f"Panel archive verified: {summary['response_attempts_verified']} retained HTTP attempts.")
        print(f"Share only: {bundle}")


if __name__ == "__main__":
    try:
        main()
    except FileNotFoundError as error:
        raise SystemExit(f"Panel verification failed: required file not found: {error.filename}") from None
    except (ValueError, KeyError, TypeError, OSError, zipfile.BadZipFile) as error:
        raise SystemExit(f"Panel verification failed: {type(error).__name__}; inspect local evidence.") from None
