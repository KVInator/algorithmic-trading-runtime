"""Authenticate cached quotes and independently check the descriptive cost comparison."""

from __future__ import annotations
import argparse
from collections import defaultdict
from copy import deepcopy
from decimal import Decimal, localcontext
from html.parser import HTMLParser
import hashlib
import json
from pathlib import Path
import zipfile
from verify_cost_quotes import QuoteVerifier, require, digest, POLICY

COSTS = (0, 0.5, 1, 2, 5)


class Marks(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.marks = []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        self.marks.append((tag, dict(attrs)))


def compare(actual, expected):
    if isinstance(expected, dict):
        require(actual.keys() == expected.keys(), "Quote comparison fields differ")
        for k, v in expected.items():
            compare(actual[k], v)
    elif isinstance(expected, list):
        require(len(actual) == len(expected), "Quote comparison length differs")
        for a, b in zip(actual, expected):
            compare(a, b)
    elif isinstance(expected, Decimal):
        require(
            actual is not None
            and abs(Decimal(str(actual)) - expected) < Decimal("1e-10"),
            "Quote cost arithmetic differs",
        )
    else:
        require(actual == expected, "Quote comparison identity/status differs")


def expected(sample):
    rows = []
    coverage = defaultdict(list)
    totals = defaultdict(int)
    with localcontext() as ctx:
        ctx.prec = 50
        for w in sample["windows"]:
            key = w["symbol"] + "/" + w["date"][:4]
            totals[key] += 1
            h = None
            differences = []
            if w["status"] == "eligible":
                m = w["measurement"]
                h = (
                    Decimal(10000)
                    * (m["ask_units"] - m["bid_units"])
                    / Decimal(m["ask_units"] + m["bid_units"])
                )
                coverage[key].append(h)
                differences = [
                    dict(
                        reference_cost_bps=c,
                        crossing_minus_reference_bps=h - Decimal(str(c)),
                    )
                    for c in COSTS
                ]
            rows.append(
                {
                    k: w[k]
                    for k in (
                        "id",
                        "symbol",
                        "date",
                        "decision_minute",
                        "delay_minutes",
                        "status",
                    )
                }
                | dict(crossing_cost_bps=h, reference_differences=differences)
            )
        groups = []
        for key in sorted(totals):
            v = coverage[key]
            groups.append(
                dict(
                    group=key,
                    planned=totals[key],
                    eligible=len(v),
                    mean_crossing_bps=sum(v) / len(v) if v else None,
                    minimum=min(v) if v else None,
                    maximum=max(v) if v else None,
                    reference_coverage=[
                        dict(
                            reference_cost_bps=c,
                            eligible_at_or_below_reference=sum(
                                h <= Decimal(str(c)) for h in v
                            ),
                        )
                        for c in COSTS
                    ],
                )
            )
        pairs = []
        for a in rows:
            if a["delay_minutes"] != 1:
                continue
            b = next(
                b
                for b in rows
                if all(a[k] == b[k] for k in ("symbol", "date", "decision_minute"))
                and b["delay_minutes"] == 2
            )
            pairs.append(
                dict(
                    one_minute_id=a["id"],
                    two_minute_id=b["id"],
                    two_minus_one_crossing_bps=(
                        None
                        if a["crossing_cost_bps"] is None
                        or b["crossing_cost_bps"] is None
                        else b["crossing_cost_bps"] - a["crossing_cost_bps"]
                    ),
                )
            )
    return dict(windows=rows, groups=groups, delay_pairs=pairs)


def verify(run, self_check=False):
    s = json.loads((run / "summary.json").read_text())
    sample = json.loads((run / "sample.json").read_text())
    require(
        s["kind"] == "quote_reference_comparison"
        and s["policy_sha256"] == POLICY
        and s["network_requests"] == 0
        and not s["historical_strategy_scored"]
        and not s["reserved_strategy_inputs_opened"],
        "Quote comparison scope differs",
    )
    require(
        hashlib.sha256(
            json.dumps(
                sample, sort_keys=True, separators=(",", ":"), ensure_ascii=False
            ).encode()
        ).hexdigest()
        == s["sample_sha256"],
        "Comparison sample identity differs",
    )
    QuoteVerifier(run, run / "sample.json").verify(bundle=False, verify_report=False)
    auth = s["authentication"]
    if auth["mode"] == "raw_archive_read_only_replay":
        require(
            digest(Path(auth["sample_file"])) == auth["sample_file_sha256"],
            "Original quote summary differs",
        )
        require(
            json.loads(Path(auth["sample_file"]).read_text()) == sample,
            "Original quote projection differs",
        )
    else:
        require(
            auth["mode"] == "constructed_archive"
            and sample["evidence_origin"] == "synthetic",
            "Unrecognized quote provenance",
        )
    e = expected(sample)
    for key in e:
        compare(s[key], e[key])
    require(
        digest(run / "report.html") == s["report_sha256"],
        "Quote comparison report changed",
    )
    marks = Marks((run / "report.html").read_text()).marks
    svg = {a["data-symbol"]: a for t, a in marks if t == "svg"}
    circles = {a["data-group"]: a for t, a in marks if t == "circle"}
    na = {a["data-na"] for t, a in marks if "data-na" in a}
    require(
        len(svg) == 5 and len(circles) == sum(g["eligible"] > 0 for g in e["groups"]),
        "Quote plot coverage differs",
    )
    for symbol in sample["plan"]["symbols"]:
        groups = [g for g in e["groups"] if g["group"].startswith(symbol + "/")]
        top = max(
            [Decimal(5)] + [g["mean_crossing_bps"] for g in groups if g["eligible"]]
        ) * Decimal("1.15")
        require(
            abs(Decimal(svg[symbol]["data-top"]) - top) < Decimal("1e-9"),
            "Quote plot scale differs",
        )
        for i, g in enumerate(groups):
            if not g["eligible"]:
                require(g["group"] in na, "Unavailable quote group lacks NA mark")
                continue
            a = circles[g["group"]]
            require(
                abs(float(a["cx"]) - (95 + 105 * i)) < 1e-9
                and abs(Decimal(a["cy"]) - (260 - 220 * g["mean_crossing_bps"] / top))
                < Decimal("1e-9"),
                "Quote plot coordinates differ",
            )
    if self_check:
        for key in ("crossing_cost_bps", "reference_differences"):
            bad = deepcopy(s["windows"])
            r = next(r for r in bad if r["crossing_cost_bps"] is not None)
            if key == "crossing_cost_bps":
                r[key] += 1
            else:
                r[key][1]["crossing_minus_reference_bps"] += 1
            try:
                compare(bad, e["windows"])
            except ValueError:
                pass
            else:
                raise ValueError("Quote semantic mutation accepted")
        print("Rejected two quote-cost semantic corruptions independently of hashes.")
    with zipfile.ZipFile(run / "review.zip", "w", zipfile.ZIP_DEFLATED) as z:
        for name in ("summary.json", "report.html"):
            z.write(run / name, name)
        if sample["evidence_origin"] == "synthetic":
            z.write(run / "sample.json", "sample.json")
    print(
        f"Verified cached raw quote evidence, {len(e['windows'])} windows, {len(e['delay_pairs'])} delay pairs and all static marks."
    )
    print("Review bundle:", run / "review.zip")


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("run", type=Path)
    p.add_argument("--self-check", action="store_true")
    a = p.parse_args()
    try:
        verify(a.run.resolve(), a.self_check)
    except (ValueError, KeyError, OSError, StopIteration) as e:
        raise SystemExit("Quote reference verification failed: " + str(e))
