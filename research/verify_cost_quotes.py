"""Independently authenticate bounded old SIP quote windows and latest eligibility."""

import argparse
from collections import Counter
from decimal import Decimal
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


class QuoteVerifier:
    def __init__(self, run):
        self.run = Path(run).resolve()
        self.summary = json.loads((self.run / "summary.json").read_text(encoding="utf-8"))
        self.dataset = Path(self.summary["dataset"]).resolve()

    def verify(self, bundle=True):
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
        page = (self.run / "report.html").read_text(encoding="utf-8")
        match = re.search(r"const data=(\{.*?\});\s*const symbol=", page, re.S)
        require(match is not None and json.loads(match[1]) == s, "Displayed quote records differ")
        if bundle:
            with zipfile.ZipFile(self.run / "review.zip", "w", zipfile.ZIP_DEFLATED) as z:
                for name in ["summary.json", "report.html"]:
                    z.write(self.run / name, name)
        return len(expected), len(files), counts


def verify(path):
    s = json.loads((path / "summary.json").read_text())
    if s["kind"] == "cost_execution_check":
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
            for case in s["cases"]:
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
    args = p.parse_args()
    try:
        verify(args.directory)
    except (ValueError, KeyError, TypeError, OSError, StopIteration) as e:
        raise SystemExit(f"Quote verification failed: {e}") from e


if __name__ == "__main__":
    main()
