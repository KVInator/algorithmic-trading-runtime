"""Independently verify frozen decisions, observed opens, costs and paired curves."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from decimal import Decimal, localcontext
from html.parser import HTMLParser
import json
import math
from pathlib import Path
import zipfile

from verify_historical_panel import PanelVerifier, require
from verify_panel_replay import ReplayVerifier, clock, same, seconds


POLICY = "827dee82278dbabf0f5d8fd6d0980afe07cd01c876847507df38080a587dc2ca"
SYMBOLS = ("SPY", "QQQ", "IWM", "TLT", "GLD")
POLICIES = (
    "momentum",
    "momentum_always_long",
    "continuation_price",
    "continuation_volume",
    "reversion_candidate",
    "reversion_sign_only",
    "reversion_drift",
    "reversion_always_long",
)
COSTS = (0, 0.5, 1, 2, 5)
PAIRS = {
    "momentum_increment": ("momentum", "momentum_always_long"),
    "continuation_increment": ("continuation_volume", "continuation_price"),
    "reversion_sign_increment": ("reversion_candidate", "reversion_sign_only"),
    "reversion_drift_increment": ("reversion_candidate", "reversion_drift"),
}


def statistics(values):
    if not values:
        return None
    wealth = peak = 1.0
    drawdown = 0.0
    for value in values:
        wealth *= 1 + value
        peak = max(peak, wealth)
        drawdown = max(drawdown, 1 - wealth / peak)
    return dict(
        mean=math.fsum(values) / len(values),
        eligible_date_product=wealth - 1,
        eligible_date_maximum_drawdown=drawdown,
    )


def reference_path(primary, bars, opening, delay):
    entry = None
    trades = []
    resolved, reason = True, "resolved"
    for decision in primary["decisions"]:
        target = decision["target_long"]
        if target is None:
            resolved, reason = False, "primary_target_unavailable"
            break
        if target == (entry is not None):
            continue
        minute = int((seconds(decision["at"]) - opening) // 60) + delay
        if minute not in bars:
            resolved, reason = False, "execution_open_unavailable"
            break
        if target:
            entry = decision["at"], bars[minute][0]
        else:
            start, price = entry
            trades.append(
                dict(
                    entry_decision=start,
                    entry_at=clock(seconds(start) + delay * 60),
                    exit_decision=decision["at"],
                    exit_at=clock(seconds(decision["at"]) + delay * 60),
                    entry_price=price,
                    exit_price=bars[minute][0],
                )
            )
            entry = None
    require(
        not resolved or entry is None,
        "Unliquidated reference cannot be reported as an intraday result",
    )
    return resolved, reason, trades


def charge(trades, cost):
    # Arbitrary-precision cash/share arithmetic is independent of both native
    # LongCashAccount and its binary floating-point return kernel.
    with localcontext() as context:
        context.prec = 50
        rate = Decimal(str(cost)) / 10000
        cash = gross = Decimal(1)
        turnover = fees = Decimal(0)
        for trade in trades:
            buy, sell = Decimal(str(trade["entry_price"])), Decimal(
                str(trade["exit_price"])
            )
            shares = cash / (buy * (1 + rate))
            purchase, sale = shares * buy, shares * sell
            fees += (purchase + sale) * rate
            turnover += purchase + sale
            cash = sale * (1 - rate)
            gross *= sell / buy
        return dict(
            gross=float(gross - 1),
            net=float(cash - 1),
            traded_notional_per_initial_equity=float(turnover),
            cost_per_initial_equity=float(fees),
        )


def verify_curve_lines(actual, expected):
    # Native SVG vertices use fixed six-decimal serialization. Its maximum
    # rounding error is half a canvas micro-unit; the extra 1e-9 covers binary
    # evaluation/parsing of the same coordinates, not financial calculations.
    tolerance = 0.5e-6 + 1e-9
    require(len(actual) == len(expected), "Cost curve line count differs")
    require(
        all(
            len(point) == 2 and all(math.isfinite(v) for v in point)
            for line in [*actual, *expected]
            for point in line
        ),
        "Cost curve contains an invalid coordinate",
    )
    edges = [
        [
            i
            for i, saved in enumerate(actual)
            if len(saved) == len(reference)
            and all(
                abs(a - b) <= tolerance
                for saved_point, reference_point in zip(saved, reference)
                for a, b in zip(saved_point, reference_point)
            )
        ]
        for reference in expected
    ]
    matched = [None] * len(actual)

    def assign(reference, visited):
        for index in edges[reference]:
            if index in visited:
                continue
            visited.add(index)
            if matched[index] is None or assign(matched[index], visited):
                matched[index] = reference
                return True
        return False

    # One-to-one matching preserves multiplicity for coincident curves and
    # avoids greedy mismatches when valid rounding intervals overlap.
    require(
        all(assign(i, set()) for i in range(len(expected))),
        "Cost curve lines differ from verified coordinates",
    )


def verify_curve_numerics():
    checks = 0
    x = [85.0, 145.0, 205.0, 325.0, 685.0]
    # Retained Windows report markers straddle the old four-decimal buckets.
    cases = [
        [67.0068319707, 81.0254812028, 95.0419836276, 123.0685499304, 207.0967741935],
        [57.9032258065, 72.1095969537, 86.3137501094, 114.7154044448, 199.8671875973],
        [-0.0000499304, -0.0000501094, 0.0000499304, 0.0000501094, 0.0],
    ]
    expected = [list(zip(x, values)) for values in cases]
    saved = [[(a, float(f"{b:.6f}")) for a, b in line] for line in expected]
    for actual, reference in (
        (saved, expected),
        (saved[::-1], expected),
        ([saved[0], saved[0]], [expected[0], expected[0]]),
    ):
        verify_curve_lines(actual, reference)
        checks += 1
    # A flexible first match must be reassigned to preserve the rigid second.
    delta = 0.5e-6 + 1e-9
    verify_curve_lines(
        [[(0.0, 0.75 * delta)], [(0.0, -0.75 * delta)]],
        [[(0.0, 0.0)], [(0.0, 1.5 * delta)]],
    )
    checks += 1
    bad = [
        [saved[0], saved[0], saved[2]],
        saved[:-1],
        [*saved, saved[0]],
        [saved[0][:-1], *saved[1:]],
        [[*saved[0], saved[0][-1]], *saved[1:]],
        [saved[0][::-1], *saved[1:]],
    ]
    for value in [
        expected[0][0][1] + 1e-6,
        expected[0][0][1] + 0.001,
        math.nan,
        math.inf,
        -math.inf,
    ]:
        bad.append([[(x[0], value), *saved[0][1:]], *saved[1:]])
    for actual in bad:
        try:
            verify_curve_lines(actual, expected)
        except ValueError:
            checks += 1
        else:
            raise ValueError("Altered or incomplete SVG curve was accepted")
    print(
        f"Curve serialization checks passed: {checks}; financial tolerances unchanged."
    )


class Markup(HTMLParser):
    def __init__(self, html):
        super().__init__(convert_charrefs=True)
        self.points, self.tables = {}, {}
        self.details = set()
        self.polylines = []
        self.key = self.cell = None
        self.cells = []
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "section" and "data-detail" in a:
            require(
                a["data-detail"] not in self.details,
                "Duplicate instrument/policy report panel",
            )
            self.details.add(a["data-detail"])
        if tag == "circle" and "data-series" in a:
            key = a["data-series"], int(a["data-delay"]), float(a["data-cost"])
            require(key not in self.points, "Duplicate cost curve point")
            self.points[key] = tuple(float(a[k]) for k in ("data-mean-bp", "cx", "cy"))
        if tag == "polyline":
            self.polylines.append(
                [tuple(map(float, p.split(","))) for p in a["points"].split()]
            )
        if tag == "tr" and "data-case" in a:
            self.key, self.cells = a["data-case"], []
            require(self.key not in self.tables, "Duplicate cost table case")
        if tag == "td" and self.key is not None:
            self.cell = ""

    def handle_data(self, data):
        if self.cell is not None:
            self.cell += data

    def handle_endtag(self, tag):
        if tag == "td" and self.cell is not None:
            self.cells.append(self.cell.strip())
            self.cell = None
        if tag == "tr" and self.key is not None:
            self.tables[self.key] = self.cells
            self.key = None


class CostDelayVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        summary_file = self.directory / "summary.json"
        require(
            summary_file.stat().st_size <= 16 * 1024 * 1024, "Summary exceeds its bound"
        )
        self.summary = json.loads(summary_file.read_text(encoding="utf-8"))
        self.series = defaultdict(
            lambda: dict(
                values=[], active=0, trades=0, turnover=0.0, fees=0.0, minutes=0.0
            )
        )
        self.pairs = defaultdict(list)
        self.daily_pairs = defaultdict(dict)
        self.coverage = defaultdict(lambda: Counter())
        self.first_record = None

    def record(self, record, bars, opening):
        day = record["primary"]
        symbol, date = day["symbol"], day["date"]
        available = day["scored_period"] and day["common_operational"]
        if symbol == "SPY" and day["scored_period"]:
            self.coverage[date[:4]]["calendar_dates"] += 1
            self.coverage[date[:4]]["common_dates"] += day["common_operational"]
        expected = {(p["policy"], delay) for p in day["policies"] for delay in (1, 2)}
        require(
            len(record["paths"]) == len(expected)
            and {(p["policy"], p["delay_minutes"]) for p in record["paths"]}
            == expected,
            "Cost/delay path matrix differs",
        )
        returns = {}
        for saved in record["paths"]:
            primary = next(p for p in day["policies"] if p["policy"] == saved["policy"])
            delay = saved["delay_minutes"]
            resolved, reason, trades = reference_path(primary, bars, opening, delay)
            require(
                saved["resolved"] == resolved
                and saved["reason"] == reason
                and saved["outcome_available"] == (available and resolved),
                "Cost/delay missingness differs",
            )
            require(
                not available or resolved, "Common mask includes unresolved execution"
            )
            require(
                len(saved["completed_trades"]) == len(trades),
                "Frozen trade count differs",
            )
            for actual, expected_trade in zip(saved["completed_trades"], trades):
                require(
                    set(actual) == set(expected_trade), "Trade evidence fields differ"
                )
                for key, value in expected_trade.items():
                    if key.endswith("price"):
                        same(actual[key], value, "Reference open differs")
                    else:
                        require(actual[key] == value, "Delayed reference clock differs")
            minutes = sum(
                int((seconds(t["exit_at"]) - seconds(t["entry_at"])) // 60)
                for t in trades
            )
            require(
                saved["minutes_held"] == (minutes if available and resolved else None),
                "Exposure duration differs",
            )
            require(
                [c["cost_bps"] for c in saved["costs"]] == list(COSTS),
                "Cost grid changed or was selected",
            )
            for case in saved["costs"]:
                cost = case["cost_bps"]
                actual_values = charge(trades, cost)
                require(
                    set(case) == {"cost_bps", *actual_values},
                    "Cost evidence fields differ",
                )
                for key, value in actual_values.items():
                    same(
                        case[key],
                        value if available and resolved else None,
                        "Cost arithmetic/mask differs: " + key,
                    )
                if available and resolved:
                    key = symbol, saved["policy"], delay, cost
                    series = self.series[key]
                    series["values"].append(case["net"])
                    series["active"] += bool(trades)
                    series["trades"] += len(trades)
                    series["turnover"] += case["traded_notional_per_initial_equity"]
                    series["fees"] += case["cost_per_initial_equity"]
                    series["minutes"] += minutes
                    returns[saved["policy"], delay, cost] = case["net"]
        if available:
            for endpoint, (candidate, control) in PAIRS.items():
                for delay in (1, 2):
                    for cost in COSTS:
                        value = (
                            returns[candidate, delay, cost]
                            - returns[control, delay, cost]
                        )
                        self.pairs[symbol, endpoint, delay, cost].append(value)
                        self.daily_pairs[date, endpoint, delay, cost][symbol] = value
            if self.first_record is None:
                self.first_record = record, bars, opening

    def aggregates(self):
        s = self.summary
        expected_series = {
            (sym, p, delay, cost)
            for sym in SYMBOLS
            for p in POLICIES
            for delay in (1, 2)
            for cost in COSTS
        }
        require(
            s["series_count"] == 400
            and len(s["series"]) == 400
            and {
                (r["symbol"], r["policy"], r["delay_minutes"], r["cost_bps"])
                for r in s["series"]
            }
            == expected_series,
            "Instrument/policy/cost/delay summary matrix differs",
        )
        for row in s["series"]:
            values = self.series[
                row["symbol"], row["policy"], row["delay_minutes"], row["cost_bps"]
            ]
            n = len(values["values"])
            require(
                row["dates"] == n
                and row["active_dates"] == values["active"]
                and row["completed_trades"] == values["trades"],
                "Return/exposure counts differ",
            )
            stats = statistics(values["values"])
            if stats is None:
                require(
                    row["statistics"] is None,
                    "Unavailable series was assigned a return",
                )
            else:
                require(
                    set(row["statistics"]) == set(stats), "Statistics fields differ"
                )
                for key, value in stats.items():
                    same(
                        row["statistics"][key],
                        value,
                        "Eligible-date statistic differs: " + key,
                    )
            for key, field in (
                ("mean_traded_notional_per_initial_equity", "turnover"),
                ("mean_cost_per_initial_equity", "fees"),
                ("mean_minutes_held", "minutes"),
            ):
                same(
                    row[key],
                    values[field] / n if n else None,
                    "Exposure/cost summary differs",
                )
        for (_, endpoint, delay, cost), values in sorted(self.daily_pairs.items()):
            require(
                set(values) == set(SYMBOLS),
                "Equal-weight contrast omitted an instrument",
            )
            self.pairs["equal_weight", endpoint, delay, cost].append(
                math.fsum(values.values()) / 5
            )
        expected_pairs = {
            (sym, p, delay, cost)
            for sym in (*SYMBOLS, "equal_weight")
            for p in PAIRS
            for delay in (1, 2)
            for cost in COSTS
        }
        require(
            s["paired_count"] == 240
            and len(s["paired"]) == 240
            and {
                (r["symbol"], r["endpoint"], r["delay_minutes"], r["cost_bps"])
                for r in s["paired"]
            }
            == expected_pairs,
            "Paired summary matrix differs",
        )
        for row in s["paired"]:
            values = self.pairs[
                row["symbol"], row["endpoint"], row["delay_minutes"], row["cost_bps"]
            ]
            require(row["dates"] == len(values), "Paired denominator differs")
            if values:
                require(
                    set(row["statistics"]) == {"mean"},
                    "A paired contrast was compounded as an account",
                )
                same(
                    row["statistics"]["mean"],
                    math.fsum(values) / len(values),
                    "Paired mean differs",
                )
            else:
                require(
                    row["statistics"] is None,
                    "Empty paired contrast was assigned a result",
                )
        coverage = [
            dict(year=year, **count) for year, count in sorted(self.coverage.items())
        ]
        require(s["coverage"] == coverage, "Original-calendar coverage differs")

    def report(self):
        s = self.summary
        path = self.directory / "report.html"
        require(
            PanelVerifier.digest(path) == s["report_sha256"], "Report identity differs"
        )
        html = path.read_text(encoding="utf-8")
        require(
            "Evidence origin: " + s["evidence_origin"] in html,
            "Report origin is not visible",
        )
        require(
            "not a continuous account" in html and "shared-capital portfolio" in html,
            "Reference-path interpretation is missing",
        )
        markup = Markup(html)
        require(
            markup.details
            == {sym + "/" + policy for sym in SYMBOLS for policy in POLICIES},
            "Static detail panels are missing",
        )
        expected_points = {}
        for rows, paired in (
            (s["series"], False),
            ([r for r in s["paired"] if r["symbol"] == "equal_weight"], True),
        ):
            groups = defaultdict(list)
            for row in rows:
                if row["statistics"] is not None:
                    groups[
                        row["symbol"], row["endpoint" if paired else "policy"]
                    ].append(row)
            for (symbol, name), group in groups.items():
                ys = [0, *[10000 * r["statistics"]["mean"] for r in group]]
                low, high = min(ys), max(ys)
                padding = max(0.1, (high - low) * 0.12)
                low, high = low - padding, high + padding
                for row in group:
                    v = 10000 * row["statistics"]["mean"]
                    expected_points[
                        symbol + "/" + name, row["delay_minutes"], row["cost_bps"]
                    ] = (
                        v,
                        85 + 120 * row["cost_bps"],
                        225 - (v - low) * 185 / (high - low),
                    )
        require(
            set(markup.points) == set(expected_points), "Static cost curves lost points"
        )
        for key, expected in expected_points.items():
            require(
                all(abs(a - b) <= 2e-6 for a, b in zip(markup.points[key], expected)),
                "Cost curve coordinate/value differs",
            )
        # Check actual line vertices as well as its independently labelled points.
        lines = []
        for name, delay in sorted({(key[0], key[1]) for key in expected_points}):
            lines.append([expected_points[name, delay, cost][1:] for cost in COSTS])
        verify_curve_lines(markup.polylines, lines)
        require(len(markup.tables) == 400, "Static cost table lost cases")
        for row in s["series"]:
            key = (
                row["symbol"]
                + "/"
                + row["policy"]
                + "/"
                + str(row["delay_minutes"])
                + "/"
                + str(float(row["cost_bps"]))
            )
            cells = markup.tables.get(key)
            require(
                cells is not None and len(cells) == 7,
                "Cost table case identity differs",
            )
            require(
                int(cells[1]) == row["dates"] and int(cells[2]) == row["active_dates"],
                "Displayed denominators differ",
            )
            if row["statistics"] is None:
                require(cells[3:] == ["NA"] * 4, "Unavailable series was drawn as zero")
            else:
                values = (
                    10000 * row["statistics"]["mean"],
                    100 * row["statistics"]["eligible_date_product"],
                    100 * row["statistics"]["eligible_date_maximum_drawdown"],
                    row["mean_traded_notional_per_initial_equity"],
                )
                require(
                    all(abs(float(a) - b) <= 6e-7 for a, b in zip(cells[3:], values)),
                    "Displayed return/exposure values differ",
                )

    def run(self, report_only=False, self_check=False):
        s = self.summary
        require(
            s["schema_version"] == 1
            and s["kind"] == "cost_delay_comparison"
            and s["policy_sha256"] == POLICY,
            "Cost/delay contract differs",
        )
        require(
            s["evidence_origin"] in {"provider", "synthetic"}
            and s["evidence_origin"] == s["provenance"]["evidence_origin"],
            "Evidence origin differs",
        )
        require(
            s["costs_bps_per_side"] == list(COSTS)
            and s["delays_minutes"] == [1, 2]
            and s["primary_cost_bps"] == 1
            and s["primary_delay_minutes"] == 1,
            "Registered cost/delay settings differ",
        )
        require(
            s["series_count"] == len(s["series"]) == 400
            and len(
                {
                    (r["symbol"], r["policy"], r["delay_minutes"], r["cost_bps"])
                    for r in s["series"]
                }
            )
            == 400
            and s["paired_count"] == len(s["paired"]) == 240,
            "Saved summary dimensions differ",
        )
        require(
            not any(
                s[k]
                for k in (
                    "signals_refitted_for_costs",
                    "reserved_strategy_inputs_opened",
                    "network_requests",
                    "broker_events",
                    "shared_capital_replayed",
                    "daily_risk_accounts_replayed",
                    "synthetic_execution_cases_executed",
                    "simultaneous_inference_completed",
                )
            )
            and s["primary_reference_parity_passed"] is True
            and s["path_interpretation"]
            == "eligible_date_product_not_continuous_account",
            "Scope claims differ",
        )
        if not report_only:
            replay = ReplayVerifier(self.directory)
            require(
                len(s["artifacts"]) == 5
                and {Path(r["path"]).stem for r in s["artifacts"]} == set(SYMBOLS),
                "Instrument artifacts differ",
            )
            for reference in s["artifacts"]:
                path = self.directory / reference["path"]
                require(
                    path.parent == self.directory
                    and path.stat().st_size == reference["bytes"]
                    and path.stat().st_size <= 128 * 1024 * 1024
                    and PanelVerifier.digest(path) == reference["sha256"],
                    "Cost/delay artifact changed",
                )
                counts = replay.instrument(
                    path.stem, path, record_member="primary", observer=self.record
                )
                row = next(r for r in s["instruments"] if r["symbol"] == path.stem)
                require(
                    all(
                        row[k] == v for k, v in counts.items() if k != "risk_forecasts"
                    ),
                    "Instrument/calendar summary differs",
                )
            for values in replay.common.values():
                require(
                    len(values) == 5
                    and all(c == all(x[0] for x in values) for _, c in values),
                    "Paired common mask differs",
                )
            self.aggregates()
            if self_check:
                self.corruption_checks()
                verify_curve_numerics()
        elif self_check:
            raise ValueError("Self-check requires local source and path evidence")
        self.report()
        with zipfile.ZipFile(
            self.directory / "review.zip", "w", zipfile.ZIP_DEFLATED
        ) as archive:
            for name in ("summary.json", "report.html"):
                archive.write(self.directory / name, name)
            if s["evidence_origin"] == "synthetic" and not report_only:
                for reference in s["artifacts"]:
                    archive.write(self.directory / reference["path"], reference["path"])
        print(
            "Verified cost/delay "
            + (
                "report representation only"
                if report_only
                else "sources, causal decisions, all reference paths and Decimal cash arithmetic"
            )
            + ": 400 series, 240 paired comparisons and static plots."
        )
        if report_only:
            print(
                "Raw observations and aggregate path calculations were not reauthenticated by this report-only check."
            )
        print("Review bundle: " + str(self.directory / "review.zip"))

    def corruption_checks(self):
        require(
            self.first_record is not None,
            "No eligible fixture record for corruption checks",
        )
        original, bars, opening = self.first_record

        def mutation(field, value):
            row = json.loads(json.dumps(original))
            path = next(
                p for p in row["paths"] if p["policy"] == "momentum_always_long"
            )
            if field == "net":
                path["costs"][2]["net"] += 0.001
            elif field == "fee":
                path["costs"][2]["cost_per_initial_equity"] += 0.001
            elif field == "price":
                path["completed_trades"][0]["entry_price"] += 1
            else:
                path[field] = value
            return row

        cases = [
            mutation("net", None),
            mutation("fee", None),
            mutation("price", None),
            mutation("delay_minutes", 3),
            mutation("outcome_available", False),
            mutation("minutes_held", 390),
        ]
        for row in cases:
            try:
                CostDelayVerifier(self.directory).record(row, bars, opening)
            except (ValueError, KeyError, TypeError):
                pass
            else:
                raise ValueError(
                    "Semantic corruption was accepted after bypassing artifact hashes"
                )
        print("Rejected six semantic corruptions independently of artifact hashes.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--report-only", action="store_true")
    parser.add_argument("--self-check", action="store_true")
    args = parser.parse_args()
    try:
        CostDelayVerifier(args.directory).run(args.report_only, args.self_check)
    except (ValueError, KeyError, OSError, TypeError, IndexError) as error:
        raise SystemExit("Cost/delay verification failed: " + str(error)) from None


if __name__ == "__main__":
    main()
