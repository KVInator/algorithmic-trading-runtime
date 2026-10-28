"""Independently authenticate a panel archive and export coverage-only review evidence."""

from __future__ import annotations

import argparse
import csv
from datetime import date, datetime, timedelta, timezone
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
        self.development_gap_intervals: list[dict] = []
        self.missing_grids: dict[tuple[str, str], set[int]] = {}

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

    def session(self, record, day, symbol, protocol, captured: list | None = None):
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
                if captured is not None:
                    captured.append(bar)
            token = body["next_page_token"]
            require(token is None or isinstance(token, str) and 0 < len(token) <= 4096 and token not in tokens, "Panel pagination token invalid")
            if token is not None:
                tokens.add(token)
        require(token is None, "Panel pagination unfinished")
        observed = set(observations)
        missing = [at for at in grid if at not in observed]
        self.missing_grids[symbol, day["date"]] = {(at - opening) // (60 * 10**9) for at in missing}
        require([timestamp_ns(t) for t in record["missing_minutes_utc"]] == missing, "Panel missing-minute mask mismatch")
        regular = day["close"] == "16:00"
        if day["date"] < "2024":
            runs = []
            for at in missing:
                if not runs or at != runs[-1][-1] + 60 * 10**9:
                    runs.append([])
                runs[-1].append(at)
            for run in runs:
                def clock(ns):
                    return datetime.fromtimestamp(ns // 10**9, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
                self.development_gap_intervals.append({"symbol": symbol, "date": day["date"], "regular": regular,
                    "start_utc": clock(run[0]), "end_exclusive_utc": clock(run[-1] + 60 * 10**9),
                    "missing_minutes": len(run), "cause": "undetermined"})
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

    def verify_input_policy(self, summary: dict, actual: dict) -> int:
        policy_file = Path(__file__).resolve().parent / "extension_input_policy.json"
        policy = self.load(policy_file)
        require(actual["policy_sha256"] == self.digest(policy_file) and actual["parent_protocol_sha256"] == summary["protocol_sha256"], "Input amendment identity mismatch")
        require(policy["parent_protocol_sha256"] == summary["protocol_sha256"], "Input amendment parent differs")
        decisions = (set(policy["continuation"]["decision_minutes"]) | set(policy["momentum"]["exit_decision_minutes"]) |
                     {policy["momentum"]["decision_minute"], policy["continuation"]["liquidation_minute"], policy["reversion"]["decision_minute"]} |
                     {policy["reversion"]["decision_minute"] + h for h in policy["reversion"]["holding_minutes"]})
        required = {policy["mandatory_open_minute"]} | {t + d for t in decisions for d in policy["execution_delays_minutes"]}
        require(actual["execution_minute_slots"] == sorted(required), "Input amendment execution slots differ from registered decisions")
        by_date, features = {}, {}
        for row in summary["coverage"]:
            key = row["symbol"], row["date"]
            missing = self.missing_grids[key]
            diagnostic = key == ("SPY", "2023-01-03")
            operational = row["regular"] and not diagnostic and not required.intersection(missing)
            by_date.setdefault(row["date"], []).append((row, operational))
            count = features.setdefault((row["symbol"], row["date"][:4]), [0, 0, 0, 0])
            if row["regular"] and not diagnostic:
                anchor = policy["reversion"]["anchor_minutes"]
                samples = set(range(anchor - 1, policy["reversion"]["decision_minute"], policy["reversion"]["primary_observation_step_minutes"]))
                count[0] += 1
                count[1] += not missing.intersection(policy["momentum"]["feature_minute_slots"])
                count[2] += not missing.intersection(set(range(anchor)) | samples)
                count[3] += sum(not missing.intersection(range(t)) for t in policy["continuation"]["decision_minutes"])
        require(len(actual["feature_coverage"]) == len(features), "Input amendment feature count differs")
        for row in actual["feature_coverage"]:
            expected = features[row["symbol"], row["year"]]
            require([row[k] for k in ["regular_non_diagnostic_dates", "momentum_feature_dates", "reversion_feature_dates", "continuation_available_prefixes"]] == expected, "Input amendment feature availability differs")
            require(row["continuation_possible_prefixes"] == expected[0] * 12, "Input amendment prefix denominator differs")
        contract = self.load(Path(__file__).resolve().parent / "extension_protocol.json")
        rules = contract["eligibility"]
        years = sorted({d[:4] for d in by_date})
        require(len(actual["year_coverage"]) == len(years), "Input amendment year count differs")
        qualified = 0
        for row, year in zip(actual["year_coverage"], years):
            dates = [v for d, v in by_date.items() if d.startswith(year)]
            regular = sum(v[0][0]["regular"] for v in dates)
            common = sum(all(flag for _, flag in v) for v in dates)
            strict = sum(v[0][0]["common_intraday_eligible"] for v in dates)
            fraction = Decimal(common) / regular if regular else Decimal(0)
            qualifies = "2016" < year < "2024" and common >= rules["minimum_common_development_dates_per_qualified_year"] and fraction >= rules["minimum_common_development_calendar_coverage"]
            qualified += qualifies
            require([row[k] for k in ["year", "regular_dates", "common_operational_dates", "parent_complete_grid_dates", "qualifies_development_year"]] == [year, regular, common, strict, qualifies], "Input amendment annual coverage differs")
            require(abs(row["operational_fraction"] - fraction) < Decimal("1e-15"), "Input amendment annual fraction differs")
            require(row["known_halt_context_dates"] == sum(v[0][0]["regular"] and d in policy["halt_context_dates"] for d, v in by_date.items() if d.startswith(year)), "Input amendment halt context differs")
        require(actual["qualified_operational_development_years"] == qualified and actual["minimum_development_years"] == rules["minimum_scored_years"], "Input amendment adequacy count differs")
        require(actual["operational_year_gate_passed"] == (qualified >= rules["minimum_scored_years"]), "Input amendment adequacy decision differs")
        require(actual["research_inputs_ready"] is False and actual["model_fit_availability_evaluated"] is False and actual["replay_adoption_pending"] is True, "Input amendment makes a premature replay claim")
        return qualified

    def verify_closing_recovery(self, summary: dict, actual: dict) -> set[tuple[str, str]]:
        if actual.get("present") is False:
            return set()
        root = self.directory / "resolutions/closing_marks"
        verifier = PanelVerifier(root)
        manifest = self.load(root / "manifest.json")
        policy = self.load(Path(__file__).resolve().parent / "extension_input_policy.json")
        require(actual["manifest_sha256"] == self.digest(root / "manifest.json"), "Closing recovery manifest hash differs")
        require(manifest["acquisition_manifest_sha256"] == summary["acquisition_manifest_sha256"] and manifest["protocol_sha256"] == summary["protocol_sha256"] and manifest["evidence_origin"] == summary["evidence_origin"], "Closing recovery provenance differs")
        require(manifest["input_policy_sha256"] == self.digest(Path(__file__).resolve().parent / "extension_input_policy.json"), "Closing recovery policy differs")
        missing = {(r["symbol"], r["date"]) for r in summary["coverage"] if not r["close_available"]}
        planned = {(r["symbol"], r["date"]) for r in policy["closing_recovery_plan"]} & missing
        require({(r["symbol"], r["date"]) for r in manifest["records"]} == planned and len(manifest["records"]) == len(planned), "Closing recovery contains unexpected endpoints")
        recovered = set()
        from urllib.parse import quote
        for row in manifest["records"]:
            require(row["date"] < "2024-01-01", "Reserved closing recovery is not authorized")
            original = next(r for r in summary["coverage"] if (r["symbol"], r["date"]) == (row["symbol"], row["date"]))
            close = utc_clock(date.fromisoformat(row["date"]), 16 if original["regular"] else 13, 0)
            opening = close - 60 * 10**9
            require(timestamp_ns(row["start_utc"]) == opening and timestamp_ns(row["end_exclusive_utc"]) == close, "Closing recovery shifted the required clock")
            path = (f"/v2/stocks/bars?symbols={row['symbol']}&timeframe=1Min&start={quote(row['start_utc'], safe='')}"
                    f"&end={quote(row['start_utc'], safe='')}&limit=100&feed=sip&adjustment=raw&sort=asc&currency=USD&asof=-")
            require(len(row["pages"]) == 1, "Closing recovery must use one bounded page")
            body = verifier.response(row["pages"][0], path, "data.alpaca.markets", False)
            require(body["next_page_token"] is None and set(body["bars"]) <= {row["symbol"]}, "Closing recovery response is paginated or has another symbol")
            bars = body["bars"].get(row["symbol"], [])
            require(len(bars) <= 1 and row["recovered"] == bool(bars), "Closing recovery invented a mark")
            for bar in bars:
                require(timestamp_ns(bar["t"]) == opening, "Closing recovery contains a different minute")
                o, h, low, c = (numeric(bar, key) for key in ["o", "h", "l", "c"])
                numeric(bar, "v")
                require(0 < low <= o <= h and low <= c <= h, "Closing recovery has invalid OHLC")
                recovered.add((row["symbol"], row["date"]))
        require(actual["required"] == len(planned) and actual["recovered"] == len(recovered) and actual["complete"] == (planned == recovered), "Closing recovery counts differ")
        return recovered

    def verify_action_resolution(self, assessment: dict, policy: dict) -> None:
        actions = assessment["actions"]
        if not actions.get("issuer_reconciled"):
            return
        reference = (self.directory / "issuer/reference_resolved.json" if assessment["evidence_origin"] == "provider"
                     else self.directory.parent / "issuer/resolved.json")
        require(self.digest(reference) == actions["issuer_reference_sha256"], "Resolved action reference identity differs")
        doc = self.load(reference)
        require(doc["schema_version"] == 2 and doc["source_resolution_sha256"] == assessment["source_resolution_sha256"], "Action source policy differs")
        sources = {}
        secondary = {}
        issuer_gld = False
        months = {f"{year}-{month:02}" for year in range(2016, 2026) for month in range(1, 13)}
        for source in doc["sources"]:
            path = (reference.parent / source["file"]).resolve()
            require(path.is_relative_to(reference.parent.resolve()) and not path.is_symlink(), "Action source escaped its root")
            require(source["file"] not in sources and self.digest(path) == source["sha256"], "Action source identity differs")
            sources[source["file"]] = source
            if source.get("kind") == "secondary_events":
                symbol = source["symbol"]
                require(symbol in self.symbols and symbol not in secondary and source["url"] == policy["secondary_event_url"].replace("{symbol}", symbol), "Secondary source provenance differs")
                chart = self.load(path)["chart"]
                require(chart["error"] is None and len(chart["result"]) == 1, "Secondary event response failed")
                result = chart["result"][0]
                require(result["meta"]["symbol"] == symbol and result["meta"]["currency"] == "USD" and result["meta"]["dataGranularity"] == "1mo", "Secondary instrument metadata differs")
                stamps = [datetime.fromtimestamp(t, timezone.utc).strftime("%Y-%m") for t in result["timestamp"]]
                require(len(stamps) == 120 and set(stamps) == months, "Secondary event horizon incomplete")
                events = result.get("events", {})
                require(not events.get("splits"), "Secondary split history contradicts zero-event coverage")
                if symbol == "GLD":
                    require(not events.get("dividends") and not events.get("capitalGains"), "GLD cash coverage contradiction")
                secondary[symbol] = source["file"]
            elif source["url"] == policy["gld_issuer_url"]:
                issuer_gld = policy["gld_issuer_statement"] in path.read_text(encoding="utf-8")
        require(set(secondary) == set(self.symbols) and issuer_gld, "Incomplete action cross-check coverage")
        for row in doc["coverage"]:
            require(row["cash_complete"] and row["splits_complete"] and secondary[row["symbol"]] in row["source_files"], "Action coverage declaration differs")
        canonical_file = self.directory / "sealed/accounting_actions.json"
        require(self.digest(canonical_file) == actions["canonical_actions_sha256"], "Canonical action identity differs")
        canonical = self.load(canonical_file)
        expected = sorted(doc["cash_distributions"], key=lambda r: (r["symbol"], r["ex_date"]))
        require(canonical["cash_distributions"] == expected and canonical["splits"] == doc["splits"] == [], "Canonical cash authority differs")
        require(len({(r["symbol"], r["ex_date"]) for r in expected}) == len(expected) and all(r["symbol"] != "GLD" for r in expected), "Canonical cash event duplicated or GLD contradiction")
        require(canonical["source_resolution_sha256"] == assessment["source_resolution_sha256"], "Canonical source policy identity differs")

    def verify_registration(self, summary: dict, assessment: dict) -> None:
        registered = assessment["registration"]
        manifest_file = self.directory / "study_manifest.json"
        require(self.digest(manifest_file) == registered["study_manifest_sha256"], "Study registration identity differs")
        manifest = self.load(manifest_file)
        for key in ("protocol_sha256", "acquisition_manifest_sha256", "source_resolution_sha256", "evidence_origin"):
            require(manifest[key] == assessment[key], "Study registration provenance differs")
        require(manifest["input_policy_sha256"] == assessment["input_policy_assessment"]["policy_sha256"], "Registered input policy differs")
        require(manifest["closing_recovery_manifest_sha256"] == assessment["closing_mark_recovery"].get("manifest_sha256"), "Registered recovery identity differs")
        require(manifest["source_sha256"] == assessment["source_sha256"], "Registered source inventory differs")
        require(registered["strategy_scored"] is False and registered["reserved_strategy_inputs_opened"] is False, "Registration review overstates completion")
        for key in ("issuer_reference_sha256", "canonical_actions_sha256"):
            require(manifest[key] == assessment["actions"][key], "Registered action provenance differs")
        require(manifest["research_inputs_ready"] is True and registered["research_inputs_ready"] is True and manifest["strategy_scored"] is False and manifest["reserved_strategy_inputs_opened"] is False, "Registration completion or reserved claims differ")
        require(manifest["unavailable_daily_endpoints"] == assessment["remaining_daily_endpoints"], "Registered unavailable daily mask differs")
        for file, field in (("vintage_bridge.json", "vintage_bridge_sha256"), ("eligibility_masks.json", "eligibility_masks_sha256")):
            require(self.digest(self.directory / file) == manifest[field], "Registered artifact identity differs")
        require(registered["vintage_bridge_sha256"] == manifest["vintage_bridge_sha256"], "Bridge identity differs")
        masks = self.load(self.directory / "eligibility_masks.json")["rows"]
        require(len(masks) == len(summary["coverage"]), "Registered mask row count differs")
        recovered = self.verify_closing_recovery(summary, assessment["closing_mark_recovery"])
        consecutive = dict.fromkeys(self.symbols, 0)
        for row, mask in zip(summary["coverage"], masks):
            symbol, day = row["symbol"], row["date"]
            for key, value in row.items():
                require(mask[key] == value, "Original coverage was changed during registration")
            available = row["close_available"] or (symbol, day) in recovered
            source = "unavailable" if not available else "authenticated_closing_recovery" if (symbol, day) in recovered else "original_archive"
            require(mask["daily_close_available"] == available and mask["daily_close_source"] == source, "Daily close provenance differs")
            expected = day > "2016-12-31" and consecutive[symbol] >= 253 and row["open_available"] and available
            require(mask["daily_risk_inputs_ready"] == expected, "Consecutive daily history crossed an unavailable mark")
            consecutive[symbol] = consecutive[symbol] + 1 if available else 0

    def verify_readiness(self, summary: dict, directory: Path) -> Path:
        assessment = self.load(directory / "summary.json")
        require(assessment["kind"] == "historical_panel_readiness" and assessment["schema_version"] == 1, "Readiness schema mismatch")
        for field in ("protocol_sha256", "acquisition_manifest_sha256", "evidence_origin"):
            require(assessment[field] == summary[field], "Readiness provenance mismatch")
        for field in ("research_inputs_ready", "strategy_scored", "reserved_strategy_inputs_opened", "vintage_bridge_verified"):
            require(assessment[field] is False, "Readiness report makes a premature completion claim")
        contract = self.load(Path(__file__).resolve().parent / "extension_protocol.json")
        rules = contract["eligibility"]
        rows = summary["coverage"]
        years = []
        for year in sorted({r["date"][:4] for r in rows if contract["archive"]["initialization_last"] < r["date"] <= contract["archive"]["development_last"]}):
            regular = [r for r in rows if r["date"].startswith(year) and r["symbol"] == "SPY" and r["regular"]]
            if not regular:
                continue
            common = sum(r["common_intraday_eligible"] for r in regular)
            fraction = Decimal(common) / len(regular)
            qualifies = common >= rules["minimum_common_development_dates_per_qualified_year"] and fraction >= rules["minimum_common_development_calendar_coverage"]
            years.append(dict(year=year, common_dates=common, regular_dates=len(regular), coverage_fraction=fraction, qualifies=qualifies))
        require(len(assessment["development_years"]) == len(years), "Readiness year count mismatch")
        for actual, expected in zip(assessment["development_years"], years):
            for key, value in expected.items():
                require(abs(actual[key] - value) < Decimal("1e-15") if key == "coverage_fraction" else actual[key] == value, "Readiness annual coverage mismatch")
        endpoints = [{k: row[k] for k in ("symbol", "date", "open_available", "close_available")}
                     for row in rows if not row["open_available"] or not row["close_available"]]
        qualified = sum(y["qualifies"] for y in years)
        applies = summary["evidence_origin"] == "provider"
        passes = not endpoints and (not applies or qualified >= rules["minimum_scored_years"])
        require(assessment["missing_daily_endpoints"] == endpoints and assessment["qualified_development_years"] == qualified, "Readiness endpoint/year gate mismatch")
        require(assessment["minimum_development_years"] == rules["minimum_scored_years"] and assessment["year_gate_applies"] == applies, "Readiness threshold mismatch")
        require(assessment["coverage_gates_passed"] == passes, "Readiness coverage decision mismatch")
        require(assessment["development_gap_intervals"] == self.development_gap_intervals, "Readiness gap intervals differ from authenticated bars")
        require(assessment["reserved_gapped_symbol_days_details_omitted"] == sum(r["reserved"] and r["missing_minutes"] > 0 for r in rows), "Readiness reserved gap count mismatch")
        actions = assessment["actions"]
        for comparison in actions.get("development_cash_discrepancies", []):
            require(comparison["symbol_ex_date"].split("/")[1] < "2024-01-01", "Reserved cash detail leaked into readiness review")
        require(assessment["input_gates_passed"] == (passes and actions["issuer_reconciled"]), "Readiness combined gate mismatch")
        if "input_policy_assessment" in assessment:
            qualified = self.verify_input_policy(summary, assessment["input_policy_assessment"])
            recovered = self.verify_closing_recovery(summary, assessment["closing_mark_recovery"])
            remaining = [r for r in endpoints if not r["open_available"] or (r["symbol"], r["date"]) not in recovered]
            require(assessment["remaining_daily_endpoints"] == remaining, "Remaining daily endpoint mask differs")
            missing_allowed = False
            if "source_resolution_sha256" in assessment:
                policy_file = Path(__file__).resolve().parent / "extension_source_resolution.json"
                require(self.digest(policy_file) == assessment["source_resolution_sha256"], "Source resolution identity differs")
                policy = self.load(policy_file)
                require(policy["parent_input_policy_sha256"] == assessment["input_policy_assessment"]["policy_sha256"], "Source resolution parent differs")
                allowlist = {(r["symbol"], r["date"]) for r in policy["unavailable_close_allowlist"]}
                recovery = assessment["closing_mark_recovery"]
                missing_allowed = not remaining or ("manifest_sha256" in recovery and recovery["required"] == len(allowlist)
                    and len(remaining) <= len(allowlist) and all(r["open_available"] and not r["close_available"]
                        and (r["symbol"], r["date"]) in allowlist for r in remaining))
                require(assessment["unavailable_daily_marks_accepted"] == missing_allowed, "Unregistered daily marks were waived")
                require(assessment["daily_mark_handling"] == policy["unavailable_close_handling"], "Daily mark convention differs")
                self.verify_action_resolution(assessment, policy)
            active = (not remaining or missing_allowed) and (not applies or qualified >= rules["minimum_scored_years"])
            require(assessment["active_coverage_gates_passed"] == active and assessment["active_input_gates_passed"] == (active and actions["issuer_reconciled"]), "Active input gates differ")
        if "registration" in assessment:
            self.verify_registration(summary, assessment)
        report = directory / "report.html"
        require(report.is_file() and report.stat().st_size <= 16 * 1024 * 1024, "Readiness HTML missing or oversized")
        archive = directory / "review.zip"
        if not archive.exists():
            with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as stream:
                for name in ("summary.json", "report.html"):
                    stream.write(directory / name, name)
        else:
            with zipfile.ZipFile(archive) as stream:
                require(stream.namelist() == ["summary.json", "report.html"], "Readiness ZIP contains unexpected files")
                for name in stream.namelist():
                    require(stream.read(name) == (directory / name).read_bytes(), "Existing readiness ZIP differs")
        return archive

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
    parser.add_argument("--readiness", type=Path, help="Verify native readiness coverage/gaps and bundle its summary and HTML")
    args = parser.parse_args()
    directory = args.directory.resolve()
    if (directory / "checks.json").exists():
        checks = PanelVerifier.load(directory / "checks.json")
        for child in ("complete", "interrupted", "outage", "history", *(["diagnosis"] if "readiness_reports" in checks else []), *checks.get("additional_archives", [])):
            verifier = PanelVerifier(directory / child)
            result = verifier.verify()
            require(result["evidence_origin"] == "synthetic", "Constructed panel origin mismatch")
            if child in checks.get("readiness_reports", {}):
                verifier.verify_readiness(result, directory / checks["readiness_reports"][child])
        print("Verified constructed panel archives, raw evidence, calendar grids, masks and available readiness reports.")
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
        if args.readiness:
            bundle = verifier.verify_readiness(summary, args.readiness.resolve())
            print(f"Readiness coverage and gap checks passed. Share readiness evidence: {bundle}")


if __name__ == "__main__":
    try:
        main()
    except FileNotFoundError as error:
        raise SystemExit(f"Panel verification failed: required file not found: {error.filename}") from None
    except (ValueError, KeyError, TypeError, OSError, zipfile.BadZipFile) as error:
        raise SystemExit(f"Panel verification failed: {type(error).__name__}; inspect local evidence.") from None
