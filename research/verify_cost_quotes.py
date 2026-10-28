"""Independently authenticate bounded old SIP quote windows and latest eligibility."""

import argparse
from collections import Counter
from decimal import Decimal
from html.parser import HTMLParser
import hashlib
import json
from pathlib import Path
import re
from urllib.parse import parse_qs, urlsplit
import zipfile

from verify_historical_capability import timestamp_ns, utc_clock
from datetime import date

POLICY = "827dee82278dbabf0f5d8fd6d0980afe07cd01c876847507df38080a587dc2ca"
STUDY = "edeee4275c9a01df3859f6c5090073c2b496df31e625e3bf6eed79bee7a8715c"
MASKS = "29413ab872c317cbef3724e7bdf25e5000a3c12ee536163ddc6592168129c173"
SYMBOLS = ["SPY", "QQQ", "IWM", "TLT", "GLD"]
MINUTES = [*range(30, 361, 30), 385]


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def units(value, scale):
    require(type(value) in {int, float, Decimal}, "Non-numeric quote field")
    exact = Decimal(str(value)) * scale
    require(
        exact >= 0 and exact == exact.to_integral_value() and exact <= 2**63 - 1,
        "Unrepresentable quote units",
    )
    return int(exact)


def classify(quote, instant):
    bid, ask, bs, az = (
        units(quote[k], scale) for k, scale in [("bp", 10**9), ("ap", 10**9), ("bs", 1), ("as", 1)]
    )
    status = (
        "missing_side"
        if min(bid, ask) <= 0
        else (
            "crossed"
            if ask < bid
            else (
                "locked"
                if ask == bid
                else (
                    "missing_size"
                    if min(bs, az) <= 0
                    else (
                        "missing_exchange"
                        if not all(isinstance(quote[k], str) and quote[k] for k in ["bx", "ax"])
                        else (
                            "nonregular"
                            if not isinstance(quote["c"], list)
                            or len(quote["c"]) not in (1, 2)
                            or any(c != "R" for c in quote["c"])
                            else "eligible"
                        )
                    )
                )
            )
        )
    )
    result = dict(
        status=status,
        source_timestamp=quote["t"],
        age_nanoseconds=instant - timestamp_ns(quote["t"]),
        bid_units=bid,
        ask_units=ask,
        bid_shares=100 * bs,
        ask_shares=100 * az,
    )
    require(max(result["bid_shares"], result["ask_shares"]) < 2**63, "Size multiplication overflow")
    if status == "eligible":
        spread = 20000 * (ask - bid) / (ask + bid)
        result.update(spread_bps=spread, half_spread_bps=spread / 2)
    return result


class ReportMarkup(HTMLParser):
    """Read static marks and tables without executing presentation JavaScript."""

    def __init__(self, html):
        super().__init__()
        self.version = None
        self.svg = None
        self.article = None
        self.coverage, self.statuses, self.panels, self.plots, self.table = {}, {}, {}, {}, {}
        self.row, self.cell = None, None
        self.feed(html)

    def handle_starttag(self, tag, pairs):
        a = dict(pairs)
        if tag == "meta" and a.get("name") == "quote-report-version":
            self.version = a.get("content")
        if tag == "svg":
            self.svg = a.get("id")
            if self.svg and self.svg.startswith("spread-"):
                self.plots[self.svg[7:]] = dict(limit=float(a["data-ymax"]), points={})
        if tag == "article" and a.get("class") == "sample-detail":
            self.article = a["data-key"]
            require(self.article not in self.panels, "Duplicate static detail")
            self.panels[self.article] = "hidden" in a
        if tag == "g" and self.svg == "coverage-chart" and "data-symbol" in a:
            key = (a["data-symbol"], a["data-year"])
            require(key not in self.coverage, "Duplicate coverage cell")
            self.coverage[key] = (int(a["data-eligible"]), int(a["data-total"]))
        if tag == "rect" and self.svg == "status-chart" and "data-status" in a:
            require(a["data-status"] not in self.statuses, "Duplicate status bar")
            self.statuses[a["data-status"]] = (int(a["data-count"]), float(a["width"]))
        if tag == "circle" and self.svg and self.svg.startswith("spread-"):
            points = self.plots[self.svg[7:]]["points"]
            require(a["data-window-id"] not in points, "Duplicate spread mark")
            points[a["data-window-id"]] = (float(a["cx"]), float(a["cy"]))
        if tag == "tr" and "data-window-id" in a:
            self.row = a["data-window-id"]
            require(self.row not in self.table, "Duplicate displayed window")
            self.table[self.row] = []
        if tag == "td" and self.row is not None:
            self.cell = []

    def handle_data(self, value):
        if self.cell is not None:
            self.cell.append(value)

    def handle_endtag(self, tag):
        if tag == "td" and self.cell is not None:
            self.table[self.row].append("".join(self.cell))
            self.cell = None
        if tag == "tr":
            self.row = None
        if tag == "svg":
            self.svg = None
        if tag == "article":
            self.article = None


class ReportVerifier:
    """Check saved-report representation; this does not authenticate raw HTTP."""

    def __init__(self, run):
        self.run = Path(run)
        self.summary = json.loads((self.run / "summary.json").read_text(encoding="utf-8"))

    def verify(self):
        s = self.summary
        require(s["kind"] == "cost_quote_sample" and s["policy_sha256"] == POLICY, "Report contract differs")
        rows = s["windows"]
        counts = Counter(r["status"] for r in rows)
        require(dict(counts) == s["status_counts"], "Saved coverage differs")
        require(len({r["id"] for r in rows}) == len(rows), "Duplicate saved window")
        for row in rows:
            if row["status"] != "eligible":
                continue
            m = row["measurement"]
            require(
                0 < m["bid_units"] < m["ask_units"] and min(m["bid_shares"], m["ask_shares"]) > 0,
                "Saved eligible geometry differs",
            )
            spread = 20000 * (m["ask_units"] - m["bid_units"]) / (m["ask_units"] + m["bid_units"])
            require(
                abs(m["spread_bps"] - spread) <= 1e-12 * max(1, spread)
                and abs(m["half_spread_bps"] - spread / 2) <= 1e-12 * max(1, spread),
                "Saved spread arithmetic differs",
            )
            require(
                m["age_nanoseconds"] == timestamp_ns(row["end"]) - timestamp_ns(m["source_timestamp"]),
                "Saved quote age differs",
            )
        html = (self.run / "report.html").read_text(encoding="utf-8")
        embedded = re.search(r"const data=(\{.*?\});\s*const symbol=", html, re.S)
        require(embedded is not None and json.loads(embedded[1]) == s, "Displayed quote records differ")
        markup = ReportMarkup(html)
        if markup.version is None:
            return len(rows), counts["eligible"], False
        require(markup.version == "2", "Unknown quote report version")
        expected = {}
        ordered = []
        for symbol in s["plan"]["symbols"]:
            for d in s["plan"]["sample_dates"]:
                year = d[:4]
                selected = [r for r in rows if r["symbol"] == symbol and r["date"].startswith(year)]
                expected[(symbol, year)] = (sum(r["status"] == "eligible" for r in selected), len(selected))
                ordered.append((symbol + "-" + year, selected))
        require(markup.coverage == expected, "Static coverage heatmap differs")
        require(set(markup.statuses) == set(counts), "Static status bars differ")
        for status, (n, width) in markup.statuses.items():
            require(
                n == counts[status] and abs(width - 970 * n / len(rows)) <= 0.00051,
                "Static status width/count differs",
            )
        require(
            set(markup.plots) == set(markup.panels) == {key for key, _ in ordered},
            "Static detail inventory differs",
        )
        initial = next(
            (key for key, selected in ordered if any(r["status"] == "eligible" for r in selected)),
            ordered[0][0],
        )
        require(
            [key for key, hidden in markup.panels.items() if not hidden] == [initial],
            "Initial detail lacks eligible records or is not unique",
        )
        require(set(markup.table) == {r["id"] for r in rows}, "Static table lost windows")
        for key, selected in ordered:
            eligible = [r for r in selected if r["status"] == "eligible"]
            plot = markup.plots[key]
            require(
                set(plot["points"]) == {r["id"] for r in eligible},
                "Static scatter includes ineligible or missing points",
            )
            if not eligible:
                continue
            limit = max(r["measurement"]["half_spread_bps"] for r in eligible) * 1.1
            require(abs(plot["limit"] - limit) <= 1e-12 * max(1, limit), "Static spread scale differs")
            for row in eligible:
                x, y = plot["points"][row["id"]]
                wanted_x = 100 + (row["decision_minute"] - 30) / 355 * 820
                wanted_y = 245 - row["measurement"]["half_spread_bps"] / plot["limit"] * 205
                require(
                    abs(x - wanted_x) <= 0.00051 and abs(y - wanted_y) <= 0.00051,
                    "Static spread coordinates differ",
                )
        for row in rows:
            cells = markup.table[row["id"]]
            require(
                len(cells) == 7
                and cells[:4]
                == [
                    row["end"],
                    f"{row['decision_minute']} / {row['delay_minutes']}",
                    row["status"],
                    f"{len(row['pages'])} / {row['records']}",
                ],
                "Static window table identity differs",
            )
            if row["status"] == "eligible":
                require(
                    abs(float(cells[4]) - row["measurement"]["half_spread_bps"]) <= 0.000000501,
                    "Static spread label differs",
                )
            else:
                require(cells[4] == "NA", "Ineligible spread displayed as a value")
            m = row["measurement"]
            if m is None:
                require(cells[5:] == ["NA", "NA"], "Unestimated geometry displayed as a value")
            else:
                require(
                    cells[5] == f"{m['bid_shares']} / {m['ask_shares']}"
                    and abs(float(cells[6]) - m["age_nanoseconds"] / 1e6) <= 0.000000501,
                    "Static size/age labels differ",
                )
        require(
            "No eligible quotes in this selection" in html or all(n for n, _ in expected.values()),
            "Missing empty-selection explanation",
        )
        return len(rows), counts["eligible"], True


class QuoteVerifier:
    def __init__(self, run, summary_file=None):
        self.run = Path(run).resolve()
        source = Path(summary_file) if summary_file is not None else self.run / "summary.json"
        self.summary = json.loads(source.read_text(encoding="utf-8"))
        self.dataset = Path(self.summary["dataset"]).resolve()

    def verify(self, bundle=True, window_observer=None, verify_report=True):
        s = self.summary
        require(s["kind"] == "cost_quote_sample" and s["policy_sha256"] == POLICY, "Quote contract differs")
        require(
            not s["historical_strategy_scored"] and not s["reserved_strategy_inputs_opened"],
            "Quote acquisition changed research scope",
        )
        require(digest(self.dataset / "plan.json") == s["plan_sha256"], "Retained quote plan differs")
        p = json.loads((self.dataset / "plan.json").read_text())
        require(
            p == s["plan"]
            and p["policy_sha256"] == POLICY
            and p["symbols"] == SYMBOLS
            and p["maximum_pages_per_window"] == 16
            and p["round_lot_shares"] == 100,
            "Quote plan fields differ",
        )
        require(
            s["evidence_origin"] == p["evidence_origin"]
            and p["evidence_origin"] in {"provider", "synthetic"},
            "Quote evidence origin differs",
        )
        require(
            s["network_requests_this_invocation"]
            == (s["request_callbacks_this_invocation"] if p["evidence_origin"] == "provider" else 0),
            "Network scope differs",
        )
        expected = []
        require(p["sample_dates"] == sorted(set(p["sample_dates"])), "Sample dates not unique/ordered")
        if p["evidence_origin"] == "provider":
            require(
                [int(d[:4]) for d in p["sample_dates"]] == list(range(2017, 2024)),
                "Provider sample years differ",
            )
            require(
                p["input_evidence"]["study_manifest_sha256"] == STUDY
                and p["input_evidence"]["eligibility_masks_sha256"] == MASKS,
                "M2 registration differs",
            )
            # The plan's original masks remain on the producer machine. Check
            # exact source selection when they are available at the fixed route.
            panel = Path(__file__).resolve().parents[1] / "scratch/datasets/extension_panel_2016_2025"
            require(
                digest(panel / "study_manifest.json") == STUDY
                and digest(panel / "eligibility_masks.json") == MASKS,
                "Quote selection inputs unavailable or changed",
            )
            masks = json.loads((panel / "eligibility_masks.json").read_text())
            grouped = {}
            for r in masks["rows"]:
                if "2017" <= r["date"] < "2024":
                    grouped.setdefault(r["date"], {})[r["symbol"]] = r["input_availability"]["operational"]
            chosen = [
                next(
                    d
                    for d, values in sorted(grouped.items())
                    if d.startswith(str(y)) and len(values) == 5 and all(values.values())
                )
                for y in range(2017, 2024)
            ]
            require(chosen == p["sample_dates"], "Outcome-independent quote dates differ")
        for d in p["sample_dates"]:
            require("2017" <= d < "2024", "Reserved or initialization date in quote plan")
            opening = utc_clock(date.fromisoformat(d), 9, 30)
            for symbol in SYMBOLS:
                for minute in MINUTES:
                    for delay in [1, 2]:
                        instant = opening + (minute + delay) * 60 * 10**9
                        expected.append(
                            (f"{symbol}/{d}/m{minute}d{delay}", symbol, d, minute, delay, instant)
                        )
        require(len(expected) == len(p["windows"]) == len(s["windows"]), "Quote matrix size differs")
        denied, counts, files = False, Counter(), set()
        for wanted, planned, row in zip(expected, p["windows"], s["windows"]):
            identity, symbol, d, minute, delay, instant = wanted
            require(
                all(
                    row[k] == planned[k] == v
                    for k, v in [
                        ("id", identity),
                        ("symbol", symbol),
                        ("date", d),
                        ("decision_minute", minute),
                        ("delay_minutes", delay),
                    ]
                ),
                "Window identity differs",
            )
            require(
                timestamp_ns(row["start"]) == timestamp_ns(planned["start"]) == instant - 10**9
                and timestamp_ns(row["end"]) == timestamp_ns(planned["end"]) == instant,
                "Quote interval differs",
            )
            if denied:
                require(
                    not row["pages"]
                    and row["records"] == 0
                    and row["measurement"] is None
                    and row["status"] == "not_attempted_after_denial",
                    "Denied route was repeatedly requested",
                )
                if window_observer:
                    window_observer(row, [])
                counts[row["status"]] += 1
                continue
            token, seen_tokens, previous, latest, ambiguous, n, status, measurement = (
                None,
                set(),
                None,
                None,
                False,
                0,
                "empty",
                None,
            )
            latest_records = []
            require(0 < len(row["pages"]) <= 16, "Unbounded or absent quote pages")
            for index, page in enumerate(row["pages"]):
                require(
                    0 < len(page["attempts"]) <= 9
                    and page["file"] == page["attempts"][-1]["file"]
                    and page["sha256"] == page["attempts"][-1]["sha256"],
                    "Attempt inventory differs",
                )
                response = None
                for attempt, receipt in enumerate(page["attempts"]):
                    relative = f"responses/{identity}/page_{index}/attempt_{attempt}.json"
                    require(receipt["file"] == relative, "Unexpected raw response path")
                    f = (self.dataset / relative).resolve()
                    require(
                        f.is_relative_to(self.dataset)
                        and not f.is_symlink()
                        and digest(f) == receipt["sha256"],
                        "Raw envelope identity differs",
                    )
                    if response is not None:
                        require(
                            response["http_status"] in {429, 500, 502, 503, 504},
                            "Attempt follows terminal response",
                        )
                    response = json.loads(f.read_text())
                    require(response["schema_version"] == 1, "Response envelope schema differs")
                    body = bytes.fromhex(response["body_hex"])
                    require(
                        len(body) <= 65536 and hashlib.sha256(body).hexdigest() == response["body_sha256"],
                        "Response bytes/hash differ",
                    )
                    query = parse_qs(urlsplit(response["request_path"]).query)
                    expected_query = dict(
                        symbols=[symbol],
                        start=[row["start"]],
                        end=[row["end"]],
                        limit=["100"],
                        feed=["sip"],
                        sort=["asc"],
                        currency=["USD"],
                        asof=["-"],
                    )
                    if token is not None:
                        expected_query["page_token"] = [token]
                    require(
                        response["host"] == "data.alpaca.markets"
                        and urlsplit(response["request_path"]).path == "/v2/stocks/quotes"
                        and query == expected_query,
                        "Request changed symbol/feed/token/interval",
                    )
                    timestamp_ns(response["retrieved_utc"])
                    files.add(relative)
                require(response is not None, "Missing retained response")
                if response["http_status"] != 200:
                    status = "http_" + str(response["http_status"])
                    denied = response["http_status"] in {401, 403}
                    require(index == len(row["pages"]) - 1, "Quote requests followed terminal HTTP failure")
                    break
                raw = json.loads(body, parse_float=Decimal)
                quotes = raw["quotes"]
                require(
                    isinstance(quotes, dict) and (not quotes or set(quotes) == {symbol}),
                    "Quote grouping differs",
                )
                values = quotes.get(symbol, [])
                require(isinstance(values, list) and len(values) <= 100, "Quote page count differs")
                for q in values:
                    at = timestamp_ns(q["t"])
                    require(
                        instant - 10**9 <= at <= instant and (previous is None or at >= previous),
                        "Quote is future, outside window or reordered",
                    )
                    if previous is None or at > previous:
                        latest, ambiguous = q, False
                    elif q != latest:
                        ambiguous = True
                    if window_observer:
                        if previous is None or at > previous:
                            latest_records.clear()
                        latest_records.append(q)
                    previous = at
                    n += 1
                token = raw["next_page_token"]
                require(
                    token is None or (isinstance(token, str) and len(token) <= 512),
                    "Invalid pagination token",
                )
                if not token:
                    require(index == len(row["pages"]) - 1, "Extra page after terminal token")
                    if latest is not None:
                        if ambiguous:
                            status = "ambiguous_latest"
                        else:
                            measurement = classify(latest, instant)
                            status = measurement["status"]
                    break
                require(token not in seen_tokens, "Pagination token cycled")
                seen_tokens.add(token)
                if index == 15:
                    status = "incomplete_page_limit"
                else:
                    require(index + 1 < len(row["pages"]), "Continued window truncated before page cap")
            require(row["records"] == n and row["status"] == status, "Derived quote count/status differs")
            actual = row["measurement"]
            if measurement is None:
                require(actual is None, "Incomplete or ambiguous quote received a measurement")
            else:
                require(set(actual) == set(measurement), "Quote measurement shape differs")
                for k, v in measurement.items():
                    require(
                        (
                            abs(actual[k] - v) <= 1e-12 * max(1, abs(v))
                            if isinstance(v, float)
                            else actual[k] == v
                        ),
                        "Quote measurement differs: " + k,
                    )
            if window_observer:
                window_observer(row, latest_records)
            counts[status] += 1
        require(
            dict(counts) == s["status_counts"] and len(files) == s["retained_http_attempts"],
            "Coverage/attempt inventory differs",
        )
        require(
            s["status"]
            == (
                "quote_sample_complete"
                if counts["eligible"] == len(expected)
                else "quote_sample_completed_with_unestimated_windows"
            ),
            "Sample status differs",
        )
        if verify_report:
            ReportVerifier(self.run).verify()
        if bundle:
            with zipfile.ZipFile(self.run / "review.zip", "w", zipfile.ZIP_DEFLATED) as z:
                for name in ["summary.json", "report.html"]:
                    z.write(self.run / name, name)
        return len(expected), len(files), counts


def metadata_field(quote, name):
    if name not in quote:
        return {"present": False}

    def normal(value):
        if isinstance(value, Decimal):
            return float(value)
        if isinstance(value, list):
            return [normal(v) for v in value]
        if isinstance(value, dict):
            return {k: normal(v) for k, v in value.items()}
        return value

    value = normal(quote[name])
    encoded = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    if len(encoded.encode()) > 512:
        kind = (
            "null"
            if value is None
            else (
                "boolean"
                if isinstance(value, bool)
                else (
                    "string"
                    if isinstance(value, str)
                    else (
                        "array"
                        if isinstance(value, list)
                        else "object" if isinstance(value, dict) else "number"
                    )
                )
            )
        )
        return dict(
            present=True,
            representation="sha256_only_over_512_bytes",
            json_type=kind,
            value_sha256=hashlib.sha256(encoded.encode()).hexdigest(),
        )
    return dict(present=True, value=value)


def diagnostic_window(row, records):
    status = row["status"]
    complete = not status.startswith("http_") and status not in {
        "incomplete_page_limit",
        "not_attempted_after_denial",
    }
    variants, changed = {}, set()
    identical, price_same, size_same, supported = True, True, True, True
    first_prices, first_sizes = None, None
    for q in records:
        metadata = dict(conditions=metadata_field(q, "c"), tape=metadata_field(q, "z"))
        key = json.dumps(metadata, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        variants.setdefault(key, {**metadata, "records": 0})["records"] += 1
        identical = identical and q == records[0]
        for name in set(q) | set(records[0]):
            if (name in q) != (name in records[0]) or (name in q and q[name] != records[0][name]):
                changed.add(name)
        try:
            result = classify(q, timestamp_ns(row["end"]))
            prices = [result["bid_units"], result["ask_units"]]
            sizes = [result["bid_shares"], result["ask_shares"]]
            if first_prices is None:
                first_prices, first_sizes = prices, sizes
            else:
                price_same = price_same and prices == first_prices
                size_same = size_same and sizes == first_sizes
        except (ValueError, KeyError, TypeError):
            supported = False
    relation = (
        "incomplete"
        if not complete
        else (
            "no_quote"
            if not records
            else (
                ("single_quote" if len(records) == 1 else "identical_duplicates")
                if identical
                else (
                    "unsupported_fields"
                    if not supported
                    else (
                        "price_conflict"
                        if not price_same
                        else "size_conflict" if not size_same else "metadata_only"
                    )
                )
            )
        )
    )
    return dict(
        id=row["id"],
        symbol=row["symbol"],
        date=row["date"],
        primary_status=status,
        complete=complete,
        latest_records=len(records),
        latest_relation=relation,
        price_consistent=price_same if complete and records and supported else None,
        size_consistent=size_same if complete and records and supported else None,
        changed_fields=sorted(changed),
        metadata_variants=[variants[k] for k in sorted(variants)],
        passes_if_only_condition_test_removed=status in {"eligible", "nonregular"},
    )


class DiagnosticMarkup(HTMLParser):
    def __init__(self, html):
        super().__init__()
        self.years = {}
        self.relations = {}
        self.table = {}
        self.row = None
        self.cell = None
        self.feed(html)

    def handle_starttag(self, tag, pairs):
        a = dict(pairs)
        if tag == "rect" and "data-year" in a:
            key = (a["data-year"], a["data-status"])
            require(key not in self.years, "Duplicate diagnostic year bar")
            self.years[key] = (int(a["data-count"]), float(a["width"]))
        if tag == "rect" and "data-relation" in a:
            require(a["data-relation"] not in self.relations, "Duplicate diagnostic conflict bar")
            self.relations[a["data-relation"]] = (int(a["data-count"]), float(a["width"]))
        if tag == "tr" and "data-frequency-index" in a:
            self.row = int(a["data-frequency-index"])
            require(self.row not in self.table, "Duplicate metadata frequency row")
            self.table[self.row] = []
        if tag == "td" and self.row is not None:
            self.cell = []

    def handle_data(self, value):
        if self.cell is not None:
            self.cell.append(value)

    def handle_endtag(self, tag):
        if tag == "td" and self.cell is not None:
            self.table[self.row].append("".join(self.cell))
            self.cell = None
        if tag == "tr":
            self.row = None


class DiagnosticVerifier:
    def __init__(self, run):
        self.run = Path(run)
        self.summary = json.loads((self.run / "summary.json").read_text(encoding="utf-8"))

    def verify(self, report_only=False, bundle=True):
        d = self.summary
        require(
            d["kind"] == "cost_quote_diagnostics"
            and d["evidence_origin"] in {"provider", "synthetic"}
            and d["diagnostic_rules_version"] == 1
            and d["policy_sha256"] == POLICY,
            "Diagnostic contract differs",
        )
        require(
            d["network_requests"] == 0
            and d["primary_replay_identical"]
            and not d["revised_eligibility_adopted"]
            and not d["historical_strategy_scored"]
            and not d["reserved_strategy_inputs_opened"],
            "Diagnostic scope differs",
        )
        if not report_only:
            source = Path(d["source_summary"])
            require(digest(source) == d["source_summary_sha256"], "Original quote summary identity differs")
            original = json.loads(source.read_text(encoding="utf-8"))
            observations = []
            oracle = QuoteVerifier(source.parent, summary_file=source)
            oracle.verify(
                bundle=False,
                window_observer=lambda row, quotes: observations.append(diagnostic_window(row, quotes)),
                verify_report=False,
            )

            # Compare semantic metadata inventories without relying on a language's
            # object-key order or Decimal/float textual serialization.
            def ordered(row):
                value = dict(row)
                value["metadata_variants"] = sorted(
                    value["metadata_variants"],
                    key=lambda v: json.dumps(v, sort_keys=True, ensure_ascii=False),
                )
                return value

            require(
                [ordered(r) for r in observations] == [ordered(r) for r in d["windows"]],
                "Latest condition/conflict observations differ from raw responses",
            )
            require(
                d["producer_source_sha256"] == original["source_sha256"]
                and d["evidence_origin"] == original["evidence_origin"]
                and d["plan_sha256"] == original["plan_sha256"]
                and d["primary_status_counts"] == original["status_counts"]
                and d["retained_http_attempts"] == original["retained_http_attempts"],
                "Original diagnostic provenance differs",
            )
        rows = d["windows"]
        require(rows and len({r["id"] for r in rows}) == len(rows), "Missing/duplicate diagnostic windows")
        primary = Counter(r["primary_status"] for r in rows)
        conflicts = Counter()
        frequencies = {}
        without = 0
        years = {}
        for row in rows:
            status = row["primary_status"]
            relation = row["latest_relation"]
            require(
                row["passes_if_only_condition_test_removed"] == (status in {"eligible", "nonregular"}),
                "Hypothetical count silently changed eligibility",
            )
            without += row["passes_if_only_condition_test_removed"]
            years.setdefault(row["date"][:4], Counter())[status] += 1
            require(
                sum(v["records"] for v in row["metadata_variants"]) == row["latest_records"],
                "Latest-record metadata inventory differs",
            )
            if status == "ambiguous_latest":
                require(
                    row["latest_records"] >= 2
                    and relation
                    in {"price_conflict", "size_conflict", "metadata_only", "unsupported_fields"},
                    "Ambiguous primary result silently resolved",
                )
                conflicts[relation] += 1
            if relation == "metadata_only":
                require(
                    row["price_consistent"] is True and row["size_consistent"] is True,
                    "Metadata-only conflict changes prices/sizes",
                )
            if relation == "size_conflict":
                require(
                    row["price_consistent"] is True and row["size_consistent"] is False,
                    "Size conflict changes prices",
                )
            if relation == "price_conflict":
                require(row["price_consistent"] is False, "Price conflict has consistent prices")
            if not row["complete"]:
                continue
            seen = set()
            for variant in row["metadata_variants"]:
                descriptor = dict(
                    year=row["date"][:4],
                    symbol=row["symbol"],
                    conditions=variant["conditions"],
                    tape=variant["tape"],
                )
                key = json.dumps(descriptor, sort_keys=True, ensure_ascii=False)
                require(key not in seen, "Duplicate per-window metadata variant")
                seen.add(key)
                target = frequencies.setdefault(key, {**descriptor, "windows": 0, "records": 0})
                target["windows"] += 1
                target["records"] += variant["records"]
        require(
            dict(primary) == d["primary_status_counts"]
            and dict(conflicts) == d["ambiguous_latest_relations"]
            and without == d["condition_test_removal_only_count"],
            "Diagnostic aggregate counts differ",
        )
        canon = lambda v: json.dumps(v, sort_keys=True, ensure_ascii=False)
        require(
            sorted(map(canon, frequencies.values())) == sorted(map(canon, d["metadata_frequencies"])),
            "Condition/tape frequency aggregation differs",
        )
        html = (self.run / "report.html").read_text(encoding="utf-8")
        require(digest(self.run / "report.html") == d["report_sha256"], "Diagnostic report hash differs")
        require(
            "Evidence origin: " + d["evidence_origin"] in html, "Diagnostic evidence origin is not displayed"
        )
        markup = DiagnosticMarkup(html)
        expected_bars = {}
        for year, counts in years.items():
            total = sum(counts.values())
            other = total - sum(counts[k] for k in ["eligible", "nonregular", "ambiguous_latest"])
            for status, n in [
                ("eligible", counts["eligible"]),
                ("nonregular", counts["nonregular"]),
                ("ambiguous_latest", counts["ambiguous_latest"]),
                ("other", other),
            ]:
                expected_bars[(year, status)] = (n, 830 * n / total)
        require(set(markup.years) == set(expected_bars), "Diagnostic year bars lost categories")
        for key, (n, width) in markup.years.items():
            require(
                n == expected_bars[key][0] and abs(width - expected_bars[key][1]) <= 0.001,
                "Diagnostic year bar width/count differs",
            )
        labels = {"price_conflict", "size_conflict", "metadata_only", "unsupported_fields"}
        require(set(markup.relations) == labels, "Diagnostic conflict bars differ")
        maximum = max([1, *conflicts.values()])
        for label, (n, width) in markup.relations.items():
            require(
                n == conflicts[label] and abs(width - 650 * n / maximum) <= 0.001,
                "Diagnostic conflict width/count differs",
            )
        require(len(markup.table) == len(d["metadata_frequencies"]), "Diagnostic frequency table lost rows")

        def field_label(value):
            if not value["present"]:
                return "missing field"
            return (
                json.dumps(value["value"], ensure_ascii=False, separators=(",", ":"), sort_keys=True)
                if "value" in value
                else "oversized metadata; SHA-256 " + value["value_sha256"]
            )

        for i, row in enumerate(d["metadata_frequencies"]):
            require(
                markup.table[i]
                == [
                    row["year"],
                    row["symbol"],
                    field_label(row["tape"]),
                    field_label(row["conditions"]),
                    str(row["windows"]),
                    str(row["records"]),
                ],
                "Diagnostic metadata table labels differ",
            )
        if bundle:
            with zipfile.ZipFile(self.run / "review.zip", "w", zipfile.ZIP_DEFLATED) as archive:
                for name in ["summary.json", "report.html"]:
                    archive.write(self.run / name, name)
        return len(rows), conflicts


def verify(path):
    s = json.loads((path / "summary.json").read_text())
    if s["kind"] == "cost_quote_diagnostics":
        n, conflicts = DiagnosticVerifier(path).verify()
        print(
            f"Verified cached quote diagnosis: {n} windows; raw responses and primary replay authenticated; conflicts: {dict(conflicts)}"
        )
        print(f"Review bundle: {path.resolve() / 'review.zip'}")
    elif s["kind"] == "cost_execution_check":
        require(
            s["network_requests"] == 0
            and s["evidence_origin"] == "synthetic"
            and s["synthetic_workloads_registered"] == 170
            and s["synthetic_account_cases_registered"] == 680
            and s["synthetic_account_cases_executed"] == 0,
            "Synthetic planning scope differs",
        )
        total = 0
        for c in s["cases"]:
            n, attempts, counts = QuoteVerifier(path / Path(c).parent).verify()
            total += n
        for case in s.get("diagnostic_cases", []):
            DiagnosticVerifier(path / Path(case).parent).verify()
        for c in s["rejected_datasets"]:
            raw = list((path / c).rglob("attempt_0.json"))
            require(raw, "Missing rejected evidence")
            if "tampered" in c:
                require(
                    any(
                        hashlib.sha256(bytes.fromhex(json.loads(p.read_text())["body_hex"])).hexdigest()
                        != json.loads(p.read_text())["body_sha256"]
                        for p in raw
                    ),
                    "Claimed tampering was not present",
                )
            elif "future" in c:
                d = json.loads(raw[0].read_text())
                r = json.loads(bytes.fromhex(d["body_hex"]))
                qs = next(iter(r["quotes"].values()))
                end = parse_qs(urlsplit(d["request_path"]).query)["end"][0]
                require(timestamp_ns(qs[0]["t"]) > timestamp_ns(end), "Claimed future source clock was valid")
        print(
            f"Verified {len(s['cases'])} constructed quote cases, {total} windows and rejected-evidence boundaries."
        )
        with zipfile.ZipFile(path / "review.zip", "w", zipfile.ZIP_DEFLATED) as archive:
            archive.write(path / "summary.json", "summary.json")
            for case in [*s["cases"], *s.get("diagnostic_cases", [])]:
                parent = Path(case).parent
                for name in ["summary.json", "report.html"]:
                    archive.write(path / parent / name, str(parent / name))
        print(f"Review bundle: {path.resolve() / 'review.zip'}")
    else:
        n, attempts, counts = QuoteVerifier(path).verify()
        print(
            f"Verified {n} fixed quote windows, {attempts} retained HTTP attempts; status counts: {dict(counts)}"
        )
        print(f"Review bundle: {path.resolve()/'review.zip'}")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument(
        "--report-only",
        action="store_true",
        help="Check saved report representation without accessing raw HTTP archives",
    )
    args = p.parse_args()
    try:
        if (
            args.report_only
            and json.loads((args.directory / "summary.json").read_text())["kind"] == "cost_quote_diagnostics"
        ):
            n, conflicts = DiagnosticVerifier(args.directory).verify(report_only=True)
            print(f"Verified diagnostic representation only: {n} windows; conflicts: {dict(conflicts)}")
            print("Raw HTTP archives were not reauthenticated by this report-only check.")
        elif args.report_only:
            n, eligible, static = ReportVerifier(args.directory).verify()
            print(
                f"Verified saved-report representation: {n} windows, {eligible} eligible; static plots: {static}."
            )
            print("Raw HTTP archives were not reauthenticated by this report-only check.")
        else:
            verify(args.directory)
    except (ValueError, KeyError, TypeError, OSError, StopIteration) as e:
        raise SystemExit(f"Quote verification failed: {e}") from e


if __name__ == "__main__":
    main()
