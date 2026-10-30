"""Independent saved-export, causal-attribution and joint-inference oracle.

Standard library only. Provider raw archives are outside this verification scope.
"""

from __future__ import annotations
import argparse
from collections import defaultdict, deque
from copy import deepcopy
import hashlib
from html.parser import HTMLParser
import json
import math
from pathlib import Path
import statistics
import zipfile
from block_sampling import BlockSampler
from verify_historical_panel import PanelVerifier

PROTOCOL = "269d6e677a3e62a21ede6e88659edb2f7a998bfe0137e62b13458e411f0c55ee"
POLICY = "827dee82278dbabf0f5d8fd6d0980afe07cd01c876847507df38080a587dc2ca"
STUDY = "edeee4275c9a01df3859f6c5090073c2b496df31e625e3bf6eed79bee7a8715c"
SYMBOLS = ("SPY", "QQQ", "IWM", "TLT", "GLD")
INTRADAY = (
    "momentum",
    "momentum_always_long",
    "continuation_price",
    "continuation_volume",
    "reversion_candidate",
    "reversion_sign_only",
    "reversion_drift",
    "reversion_always_long",
)
RISK = ("intraday_risk", "daily_only", "static_risk_budget", "passive_equity")
SHARED = (
    "candidate",
    "momentum_control",
    "continuation_control",
    "reversion_sign_control",
    "reversion_drift_control",
)
ENDPOINTS = (
    "momentum_increment",
    "continuation_increment",
    "reversion_sign_increment",
    "reversion_drift_increment",
    "risk_cer_increment",
    "risk_qlike_improvement",
)
FIELDS = ("net", "gross", "exposure", "turnover", "cost", "holding_minutes", "trades")


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def same(actual, expected, path="value"):
    if isinstance(expected, dict):
        require(
            isinstance(actual, dict) and actual.keys() == expected.keys(),
            f"{path}: keys differ",
        )
        for key in expected:
            same(actual[key], expected[key], f"{path}.{key}")
    elif isinstance(expected, list):
        require(
            isinstance(actual, list) and len(actual) == len(expected),
            f"{path}: lengths differ",
        )
        for i, (a, b) in enumerate(zip(actual, expected)):
            same(a, b, f"{path}[{i}]")
    elif isinstance(expected, float):
        require(
            isinstance(actual, (int, float))
            and not isinstance(actual, bool)
            and math.isfinite(actual)
            and math.isclose(actual, expected, rel_tol=2e-10, abs_tol=2e-13),
            f"{path}: {actual} != {expected}",
        )
    else:
        require(
            type(actual) is type(expected) and actual == expected,
            f"{path}: {actual} != {expected}",
        )


def unique_object(pairs):
    output = {}
    for key, value in pairs:
        require(key not in output, "Duplicate JSON key")
        output[key] = value
    return output


def decode(raw):
    return json.loads(
        raw,
        object_pairs_hook=unique_object,
        parse_constant=lambda _: (_ for _ in ()).throw(ValueError("Nonfinite JSON")),
    )


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def canonical(value):
    return sha(
        json.dumps(
            value, sort_keys=True, separators=(",", ":"), allow_nan=False
        ).encode()
    )


def quantile(values, p):
    ordered = sorted(values)
    x = (len(ordered) - 1) * p
    i, j = math.floor(x), math.ceil(x)
    return ordered[i] + (x - i) * (ordered[j] - ordered[i])


class OriginalEndpoint:
    """Independent influence calculation and centered block sufficient statistics."""

    def __init__(self, endpoint):
        self.endpoint = endpoint
        self.valid = [p for p in endpoint["days"] if p is not None]
        self.n = len(self.valid)
        self.a = statistics.fmean(p[0] for p in self.valid) if self.n else 0
        self.b = statistics.fmean(p[1] for p in self.valid) if self.n else 0
        if self.valid and all(p[0] == self.valid[0][0] for p in self.valid):
            self.a = self.valid[0][0]
        if self.valid and all(p[1] == self.valid[0][1] for p in self.valid):
            self.b = self.valid[0][1]
        self.prefix = [[0.0] * 5]
        for pair in endpoint["days"]:
            previous = self.prefix[-1]
            if pair is None:
                self.prefix.append(previous.copy())
            else:
                a, b = pair[0] - self.a, pair[1] - self.b
                self.prefix.append(
                    [x + y for x, y in zip(previous, (1, a, b, a * a, b * b))]
                )
        self.estimate = self.statistic(self.prefix[-1])

    def statistic(self, m):
        n, sa, sb, sqa, sqb = m
        if n < (2 if self.endpoint["cer"] else 1):
            return None
        result = self.a - self.b + (sa - sb) / n
        if self.endpoint["cer"]:
            va, vb = (sqa - sa * sa / n) / (n - 1), (sqb - sb * sb / n) / (n - 1)
            if va < 0 or vb < 0:
                return None
            result = 252 * (result - 1.5 * (va - vb))
        return result if math.isfinite(result) else None

    def error(self, block):
        if self.n < 2 or self.estimate is None:
            return None
        total = len(self.endpoint["days"])
        residuals = []
        variance_difference = (self.prefix[-1][3] - self.prefix[-1][4]) / self.n
        for pair in self.endpoint["days"]:
            if pair is None:
                residuals.append(0)
                continue
            a, b = pair[0] - self.a, pair[1] - self.b
            value = a - b
            if self.endpoint["cer"]:
                value = 252 * (
                    value
                    - 1.5
                    * self.n
                    / (self.n - 1)
                    * (a * a - b * b - variance_difference)
                )
            residuals.append(value * total / self.n)
        variance = math.fsum(x * x for x in residuals)
        variance += math.fsum(
            2
            * (1 - lag / block)
            * math.fsum(residuals[i] * residuals[i - lag] for i in range(lag, total))
            for lag in range(1, block)
        )
        return (
            math.sqrt(variance) / total
            if variance > 0 and math.isfinite(variance)
            else None
        )


class JointInference:
    def __init__(self, endpoints):
        require(
            [x["id"] for x in endpoints] == list(ENDPOINTS), "Endpoint order differs"
        )
        self.endpoints = endpoints
        self.total = len(endpoints[0]["days"])
        self.moments = [OriginalEndpoint(e) for e in endpoints]
        require(
            all(len(e["days"]) == self.total for e in endpoints),
            "Endpoint calendar differs",
        )

    def run(self, block):
        errors = [m.error(block) for m in self.moments]
        usable = all(x is not None for x in errors)
        sampler = BlockSampler(self.total, block, 290026)
        mapping = hashlib.sha256()
        draws = [[] for _ in self.endpoints]
        undefined = [0] * 6
        maxima, undefined_family = [], 0
        for _ in range(10000):
            blocks = sampler.blocks()
            mapping.update(
                (
                    "".join(f"{start}:{length};" for start, length in blocks) + "\n"
                ).encode()
            )
            values = []
            for j, moment in enumerate(self.moments):
                p = moment.prefix
                sums = [
                    math.fsum(
                        p[start + length][k] - p[start][k] for start, length in blocks
                    )
                    for k in range(5)
                ]
                value = moment.statistic(sums)
                values.append(value)
                if value is None:
                    undefined[j] += 1
                else:
                    draws[j].append(value)
            if usable and all(v is not None for v in values):
                maxima.append(
                    max(
                        (v - m.estimate) / se
                        for v, m, se in zip(values, self.moments, errors)
                    )
                )
            else:
                undefined_family += 1
        simultaneous = block in (20, 40)
        available = simultaneous and usable and undefined_family <= 100 and bool(maxima)
        critical = quantile(maxima, 0.95) if available else None
        rows = []
        coverage = True
        for j, (endpoint, moment, error) in enumerate(
            zip(self.endpoints, self.moments, errors)
        ):
            adequate = moment.n >= 200 and moment.n / self.total >= 0.8
            coverage &= adequate
            marginal = block in ((5, 10) if j < 4 else (20, 40))
            interval = marginal and bool(draws[j]) and undefined[j] <= 100
            lower = moment.estimate - critical * error if critical is not None else None
            rows.append(
                dict(
                    id=endpoint["id"],
                    observed_dates=moment.n,
                    calendar_dates=self.total,
                    estimate=moment.estimate,
                    hac_standard_error=error,
                    undefined_resamples=undefined[j],
                    marginal_requested=marginal,
                    marginal_lower=quantile(draws[j], 0.025) if interval else None,
                    marginal_upper=quantile(draws[j], 0.975) if interval else None,
                    family_lower=lower,
                    material_increment=endpoint["material"],
                    adequate_coverage=adequate,
                    bound_above_material=(
                        lower > endpoint["material"] if lower is not None else None
                    ),
                )
            )
        return dict(
            block_length=block,
            replicates=10000,
            seed=290026,
            sampling_sha256=mapping.hexdigest(),
            endpoints=rows,
            family_requested=simultaneous,
            family_available=available,
            family_critical=critical,
            undefined_family_resamples=undefined_family,
            all_endpoints_adequate=coverage,
            advancement_assessed=False,
            assessment=(
                "input_limited"
                if not coverage
                else (
                    "family_unavailable_or_not_requested"
                    if not available
                    else "development_bounds_pending_risk_matching_and_inference_validation"
                )
            ),
        )


class PriorConditions:
    def __init__(self):
        self.returns = deque(maxlen=60)
        self.volatilities = deque(maxlen=252)
        self.previous = None

    def advance(self, date, current):
        vol = lo = hi = None
        label = "unavailable"
        if len(self.returns) == 60 and None not in self.returns:
            vol = statistics.stdev(self.returns) * math.sqrt(252)
        if (
            vol is not None
            and len(self.volatilities) == 252
            and None not in self.volatilities
        ):
            lo, hi = (quantile(self.volatilities, p) for p in (1 / 3, 2 / 3))
            label = "low" if vol <= lo else "high" if vol > hi else "middle"
        result = dict(
            date=date,
            prior_volatility=vol,
            lower_cutoff=lo,
            upper_cutoff=hi,
            cutoff_last_date=self.previous,
            label=label,
        )
        self.returns.append(current)
        self.volatilities.append(vol)
        self.previous = date
        return result


def duration_band(row):
    value = row["holding_minutes"]
    if value is None:
        return "unavailable"
    if value == 0:
        return "cash"
    require(0 < value <= 390, "Invalid holding time")
    return next(f"up_to_{upper}" for upper in (15, 30, 60, 120, 390) if value <= upper)


def concentration(rows, dates):
    valid = [(d, r["net"]) for r, d in zip(rows, dates) if r["net"] is not None]
    positive = sorted(((d, v) for d, v in valid if v > 0), key=lambda x: (-x[1], x[0]))
    total = math.fsum(v for _, v in valid)
    wins = math.fsum(v for _, v in positive)
    output = []
    for count, name in (
        (1, "top_1"),
        (5, "top_5"),
        (10, "top_10"),
        (math.ceil(0.01 * len(positive)), "top_one_percent_positive_dates"),
    ):
        removed = positive[:count]
        value = math.fsum(v for _, v in removed)
        output.append(
            dict(
                rule=name,
                positive_dates=len(positive),
                removed_dates=[d for d, _ in removed],
                removed_arithmetic_return=value,
                share_of_positive_sum=value / wins if wins else None,
                mean_after_zeroing=(
                    (total - value) / len(valid)
                    if valid and (name != "top_one_percent_positive_dates" or positive)
                    else None
                ),
            )
        )
    return output


def describe(rows):
    valid = [r for r in rows if r["net"] is not None]
    values = [r["net"] for r in valid]
    stats = None
    if values:
        require(all(math.isfinite(v) and v > -1 for v in values), "Invalid net return")
        wealth = peak = 1.0
        drawdown = 0.0
        for v in values:
            wealth *= 1 + v
            peak = max(peak, wealth)
            drawdown = max(drawdown, 1 - wealth / peak)
        stats = dict(
            mean=statistics.fmean(values),
            selected_observation_product=wealth - 1,
            selected_observation_drawdown=drawdown,
            sample_sd=statistics.stdev(values) if len(values) > 1 else None,
        )

    def sum_field(key):
        subset = [r[key] for r in rows if r[key] is not None]
        return math.fsum(subset) if subset else None

    exposures = [r["exposure"] for r in valid if r["exposure"] is not None]
    trades = [r["trades"] for r in valid if r["trades"] is not None]
    return dict(
        selected_calendar_dates=len(rows),
        observed_dates=len(valid),
        active_dates=sum(
            r["exposure"] is not None and r["exposure"] > 0 for r in valid
        ),
        statistics=stats,
        mean_exposure=statistics.fmean(exposures) if exposures else None,
        turnover_initial_equity_units=sum_field("turnover"),
        cost_initial_equity_units=sum_field("cost"),
        trade_count_or_mean_component_count=math.fsum(trades) if trades else None,
    )


def groups(series, dates):
    output = []
    for row in series:
        selections = defaultdict(list)
        for i, (day, date) in enumerate(zip(row["days"], dates)):
            selections["period", "all"].append(i)
            selections["year", date[:4]].append(i)
            selections["condition", day["condition"]].append(i)
            if row["layer"] == "intraday":
                selections["holding", duration_band(day)].append(i)
        for (dimension, group), indices in sorted(selections.items()):
            selected = [row["days"][i] for i in indices]
            output.append(
                dict(
                    **describe(selected),
                    series=row["id"],
                    dimension=dimension,
                    group=group,
                    concentration=concentration(selected, [dates[i] for i in indices]),
                )
            )
    return output


from evaluation_scope import scored, authorized, scope_claim


class SavedSource:
    def __init__(self, identity, scope=None):
        self.directory = Path(identity["directory"])
        raw = (self.directory / "summary.json").read_bytes()
        require(sha(raw) == identity["summary_sha256"], "M6 summary changed")
        self.summary = decode(raw)
        same(self.summary["artifacts"], identity["artifacts"], "M6 inventory")
        self.artifacts = {a["path"]: a for a in identity["artifacts"]}
        require(
            len(self.artifacts) == len(identity["artifacts"]), "M6 duplicate artifact"
        )
        require(
            self.summary["network_requests"] == 0 and scope_claim(self.summary, scope),
            "M6 scope differs",
        )
        require(
            sha((self.directory / "report.html").read_bytes())
            == self.summary["report_sha256"],
            "M6 report changed",
        )
        require(
            self.summary["policy_sha256"] == POLICY
            and self.summary["provenance"]["protocol_sha256"] == PROTOCOL,
            "M6 protocol differs",
        )

    def records(self, name):
        path = self.directory / name
        require(path.resolve().parent == self.directory.resolve(), "M6 path escape")
        artifact = self.artifacts[name]
        require(path.stat().st_size == artifact["bytes"], "M6 size differs")
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for line in stream:
                require(
                    len(line) <= 16 * 1024 * 1024 and line.endswith(b"\n"),
                    "M6 record truncated/oversized",
                )
                digest.update(line)
                yield decode(line)
        require(digest.hexdigest() == artifact["sha256"], "M6 artifact changed")


def empty_observation():
    return dict.fromkeys(FIELDS) | {"condition": "unavailable"}


def intraday_observations(record, scope=None):
    """Decode primary intraday outcomes against the independent exchange clock."""
    primary = record["primary"]
    date = primary["date"]
    require(authorized(date, scope), "Intraday input outside authorized window")
    sessions = PanelVerifier(Path(".")).calendar(date, date)
    require(len(sessions) == 1, "Intraday input is not an exchange session")
    observations = {policy: empty_observation() for policy in INTRADAY}
    if sessions[0]["close"] != "16:00":
        require(
            not primary["policies"]
            and not record["paths"]
            and not primary["common_operational"]
            and not primary["availability"]["operational"],
            "Shortened session contains intraday paths or an operational outcome",
        )
        return observations
    paths = [x for x in record["paths"] if x["delay_minutes"] == 1]
    require(
        len(paths) == 8 and {x["policy"] for x in paths} == set(INTRADAY),
        "M6 primary paths differ",
    )
    for path in paths:
        costs = [c for c in path["costs"] if c["cost_bps"] == 1]
        require(
            len(costs) == 1
            and path["outcome_available"]
            == (primary["common_operational"] and path["resolved"]),
            "M6 mask/charge differs",
        )
        cost = costs[0]
        require(
            (cost["net"] is not None) == path["outcome_available"],
            "M6 outcome missingness differs",
        )
        observations[path["policy"]].update(
            net=cost["net"],
            gross=cost["gross"],
            turnover=cost["traded_notional_per_initial_equity"],
            cost=cost["cost_per_initial_equity"],
            holding_minutes=(
                float(path["minutes_held"]) if path["outcome_available"] else None
            ),
            exposure=path["minutes_held"] / 390 if path["outcome_available"] else None,
            trades=(
                float(len(path["completed_trades"]))
                if path["outcome_available"]
                else None
            ),
        )
    return observations


class StabilityVerifier:
    def __init__(self, directory, scope=None):
        self.scope = scope
        self.directory = Path(directory).resolve()
        self.summary = decode((self.directory / "summary.json").read_bytes())
        self.artifacts = {a["path"]: a for a in self.summary["artifacts"]}
        require(
            len(self.artifacts) == len(self.summary["artifacts"]),
            "Duplicate M7 artifact",
        )
        require(
            set(self.artifacts)
            == {
                "observations.jsonl",
                "market.json",
                "plan.json",
                "inference_inputs.json",
            }
            | (
                {"kernel_checks.json"}
                if self.summary["evidence_origin"] == "synthetic"
                and "kernel_checks.json" in self.artifacts
                else set()
            ),
            "M7 inventory differs",
        )
        for name, item in self.artifacts.items():
            require(
                item["bytes"]
                <= (128 if name == "observations.jsonl" else 32) * 1024 * 1024,
                "Derived evidence exceeds bound",
            )
            path = self.directory / name
            require(
                path.resolve().parent == self.directory
                and path.stat().st_size == item["bytes"]
                and sha(path.read_bytes()) == item["sha256"],
                "M7 artifact identity differs",
            )
        self.series = [
            decode(line)
            for line in (self.directory / "observations.jsonl")
            .read_bytes()
            .splitlines()
        ]
        self.market = self.load("market.json")
        self.inputs = self.load("inference_inputs.json")
        self.plan = self.load("plan.json")
        self.report = (self.directory / "report.html").read_text(encoding="utf-8")
        require(
            sha(self.report.encode()) == self.summary["report_sha256"],
            "M7 report changed",
        )
        self.dates = self.summary["dates"]

    def load(self, name):
        return decode((self.directory / name).read_bytes())

    def verify_sources(self):
        costs, accounts = (
            SavedSource(self.summary["sources"][key], self.scope)
            for key in ("cost_delay", "accounts")
        )
        require(
            costs.summary["kind"] == "cost_delay_comparison"
            and accounts.summary["kind"] == "panel_account_study",
            "M6 kind differs",
        )
        require(
            costs.summary["primary_reference_parity_passed"],
            "M6 primary parity differs",
        )
        origin = self.summary["evidence_origin"]
        for source in (costs, accounts):
            require(
                source.summary["provenance"]["evidence_origin"] == origin,
                "M6 origins differ",
            )
        if origin == "provider":
            for key in (
                "study_manifest_sha256",
                "acquisition_manifest_sha256",
                "eligibility_masks_sha256",
            ):
                same(
                    costs.summary["provenance"][key],
                    accounts.summary["provenance"][key],
                    key,
                )
            require(
                accounts.summary["provenance"]["study_manifest_sha256"] == STUDY,
                "Unregistered source",
            )
        else:
            require(
                all(
                    source.summary["provenance"]["fixture"]
                    == (
                        "analytic_evaluation_2022_2024"
                        if self.scope
                        else "analytic_panel_300"
                    )
                    for source in (costs, accounts)
                ),
                "Unknown fixture",
            )
        same(
            accounts.summary["development_dates"],
            self.dates,
            "Source development dates",
        )
        observed = {s["id"]: s for s in self.series}
        calendar = self.market["dates"]
        index = {d: i for i, d in enumerate(self.dates)}
        for symbol in SYMBOLS:
            hashes = []
            logs = []
            for i, row in enumerate(costs.records(symbol + ".jsonl")):
                p = row["primary"]
                require(
                    i < len(calendar)
                    and p["date"] == calendar[i]
                    and p["symbol"] == symbol,
                    "M6 calendar differs",
                )
                hashes.append(canonical(p))
                logs.append(
                    p["measurement"]["log_return"] if p["measurement"] else None
                )
                if p["date"] not in index:
                    continue
                for policy, expected in intraday_observations(row, self.scope).items():
                    actual = observed[f"intraday/{symbol}/{policy}"]["days"][
                        index[p["date"]]
                    ]
                    same(
                        {key: actual[key] for key in FIELDS},
                        {key: expected[key] for key in FIELDS},
                        "Intraday extraction",
                    )
            require(len(hashes) == len(calendar), "Cost calendar truncated")
            same(logs, self.market["log_returns"][symbol], "Market returns")
            n = 0
            for i, primary in enumerate(accounts.records(symbol + ".jsonl")):
                require(
                    i < len(hashes) and canonical(primary) == hashes[i],
                    "M6 primary records differ",
                )
                n += 1
            require(n == len(calendar), "Account primary truncated")
            print(f"Authenticated saved M6 records | {symbol}", flush=True)
        forecasts = {}
        n = 0
        for i, row in enumerate(accounts.records("account_days.jsonl")):
            require(
                i < len(calendar) and row["date"] == calendar[i], "Account date differs"
            )
            n += 1
            date = row["date"]
            for f in row["forecasts"]:
                if f["delay"] != 1:
                    continue
                key = (f["symbol"], f["date"])
                require(
                    key not in forecasts and f["date"] in index,
                    "Forecast identity differs",
                )
                if f["next_date"] is None:
                    require(
                        date == self.dates[-1] == f["date"]
                        and f["control_minus_candidate_qlike"] is None,
                        "Forecast terminal differs",
                    )
                else:
                    require(
                        i > 0
                        and f["date"] == calendar[i - 1]
                        and f["next_date"] == date,
                        "Forecast horizon differs",
                    )
                forecasts[key] = f["control_minus_candidate_qlike"]
            if date not in index:
                require(
                    not row["risk"] and not row["shared"],
                    "Initialization accounts present",
                )
                continue
            seen = set()
            for account in row["risk"]:
                if account["delay"] != 1 or account["cost_bps"] != 1:
                    continue
                key = f"risk/{account['symbol']}/{account['account']}"
                require(key not in seen, "Duplicate risk account")
                seen.add(key)
                actual = observed[key]["days"][index[date]]
                v = empty_observation()
                v.update(
                    net=account["net_return"],
                    cost=math.fsum(e["cost"] for e in account["events"]) / 100000,
                    turnover=math.fsum(abs(e["notional"]) for e in account["events"])
                    / 100000,
                )
                if account["net_return"] is not None:
                    v["exposure"] = (
                        account["value"]["shares"]
                        * account["last_price"]
                        / account["value"]["liquid"]
                    )
                same(
                    {key: actual[key] for key in FIELDS},
                    {key: v[key] for key in FIELDS},
                    "Risk extraction",
                )
            require(len(seen) == 20, "Risk matrix differs")
            seen.clear()
            for account in row["shared"]:
                if account["delay"] != 1 or account["cost_bps"] != 1:
                    continue
                key = f"shared/all/{account['variant']}"
                require(key not in seen, "Duplicate shared account")
                seen.add(key)
                actual = observed[key]["days"][index[date]]
                v = empty_observation()
                v.update(
                    net=account["net_return"],
                    turnover=account["traded_mid_notional_units"] / 1e14,
                    cost=account["crossing_cost_units"] / 1e14,
                    exposure=(
                        account["maximum_gross_fraction"]
                        if account["net_return"] is not None
                        else None
                    ),
                )
                same(
                    {key: actual[key] for key in FIELDS},
                    {key: v[key] for key in FIELDS},
                    "Shared extraction",
                )
            require(len(seen) == 5, "Shared matrix differs")
            if n % 250 == 0:
                print(
                    f"Authenticated account calendar | {n}/{len(calendar)}", flush=True
                )
        require(
            n == len(calendar) and len(forecasts) == len(self.dates) * 5,
            "Forecast/calendar incomplete",
        )
        expected = []
        for date in self.dates:
            values = [forecasts[symbol, date] for symbol in SYMBOLS]
            expected.append(
                [math.fsum(values) / 5, 0.0] if None not in values else None
            )
        same(self.inputs["endpoints"][5]["days"], expected, "QLIKE common-date pairing")
        plan = accounts.directory / "plan.json"
        artifact = accounts.artifacts["plan.json"]
        require(
            plan.stat().st_size == artifact["bytes"]
            and sha(plan.read_bytes()) == artifact["sha256"],
            "M6 plan differs",
        )
        same(decode(plan.read_bytes()), accounts.summary["plan"], "M6 plan")

    def verify_math(self):
        s = self.summary
        require(
            s["kind"] == "stability_primary_diagnostics"
            and s["protocol_sha256"] == PROTOCOL
            and s["network_requests"] == 0,
            "M7 identity differs",
        )
        require(scope_claim(s, self.scope), "Primary scope differs")
        for key in (
            "advancement_assessed",
            "secondary_variants_executed",
            "risk_matching_executed",
            "inference_calibration_validated",
        ):
            require(s[key] is False, f"Unexpected {key}")
        same(self.dates, sorted(set(self.dates)), "Calendar ordering")
        require(
            all(scored(d, self.scope) for d in self.dates),
            "Reserved/initialization input scored",
        )
        same(self.inputs["dates"], self.dates, "Inference dates")
        calendar = self.market["dates"]
        reference = [
            r["date"]
            for r in PanelVerifier(Path(".")).calendar(
                self.scope.first if self.scope else "2016-01-01",
                self.scope.last if self.scope else "2023-12-31",
            )
        ]
        if self.summary["evidence_origin"] == "synthetic" and self.scope is None:
            reference = reference[:300]
        same(calendar, reference, "Independent exchange-session calendar")
        require(
            calendar[-len(self.dates) :] == self.dates
            and all(authorized(d, self.scope) for d in calendar)
            and sorted(set(calendar)) == calendar,
            "Market calendar differs",
        )
        require(
            set(self.market["log_returns"]) == set(SYMBOLS), "Market universe differs"
        )
        self.verify_attribution()
        self.verify_inference()
        self.verify_plan()
        self.verify_plots()

    def verify_attribution(self):
        s = self.summary
        calendar = self.market["dates"]
        market = deepcopy(self.market["log_returns"])
        market["equal_weight"] = [
            math.fsum(values) / 5 if None not in values else None
            for values in zip(*(market[s] for s in SYMBOLS))
        ]
        for symbol, returns in market.items():
            require(len(returns) == len(calendar), "Market length differs")
            state = PriorConditions()
            same(
                self.market["conditions"][symbol],
                [state.advance(d, r) for d, r in zip(calendar, returns)],
                f"{symbol} prior labels",
            )
        expected_ids = {
            f"{layer}/{symbol}/{policy}"
            for layer, policies in (("intraday", INTRADAY), ("risk", RISK))
            for symbol in SYMBOLS + ("equal_weight",)
            for policy in policies
        } | {f"shared/all/{p}" for p in SHARED}
        require(
            [x["id"] for x in self.series] == sorted(expected_ids),
            "77-series identity differs",
        )
        by_id = {x["id"]: x for x in self.series}
        offset = len(calendar) - len(self.dates)
        for row in self.series:
            require(
                row["id"] == f"{row['layer']}/{row['symbol']}/{row['policy']}"
                and len(row["days"]) == len(self.dates),
                "Series metadata differs",
            )
            condition_symbol = (
                "equal_weight" if row["layer"] == "shared" else row["symbol"]
            )
            for i, observation in enumerate(row["days"]):
                require(
                    set(observation) == set(FIELDS) | {"condition"},
                    "Observation schema differs",
                )
                require(
                    all(
                        v is None or isinstance(v, (int, float)) and math.isfinite(v)
                        for k, v in observation.items()
                        if k in FIELDS
                    ),
                    "Nonfinite observation",
                )
                require(
                    observation["condition"]
                    == self.market["conditions"][condition_symbol][offset + i]["label"],
                    "Attribution label differs",
                )
                if observation["net"] is None:
                    require(
                        all(
                            observation[k] is None
                            for k in ("gross", "exposure", "holding_minutes", "trades")
                        ),
                        "Missing date filled",
                    )
                if row["symbol"] == "equal_weight":
                    components = [
                        by_id[f"{row['layer']}/{symbol}/{row['policy']}"]["days"][i]
                        for symbol in SYMBOLS
                    ]
                    for key in FIELDS:
                        values = [v[key] for v in components]
                        value = math.fsum(values) / 5 if None not in values else None
                        same(observation[key], value, f"Pooled {key}")
        pairs = (
            ("momentum", "momentum_always_long"),
            ("continuation_volume", "continuation_price"),
            ("reversion_candidate", "reversion_sign_only"),
            ("reversion_candidate", "reversion_drift"),
            ("intraday_risk", "daily_only"),
        )
        for j, endpoint in enumerate(self.inputs["endpoints"]):
            require(
                endpoint["id"] == ENDPOINTS[j]
                and endpoint["cer"] == (j == 4)
                and endpoint["material"]
                == (5e-5 if j < 4 else 0.005 if j == 4 else 0.01),
                "Endpoint definition differs",
            )
            if j < 5:
                layer = "risk" if j == 4 else "intraday"
                a, b = (by_id[f"{layer}/equal_weight/{p}"]["days"] for p in pairs[j])
                values = [
                    (
                        [x["net"], y["net"]]
                        if x["net"] is not None and y["net"] is not None
                        else None
                    )
                    for x, y in zip(a, b)
                ]
                same(endpoint["days"], values, "Paired endpoint inputs")
            require(len(endpoint["days"]) == len(self.dates), "Endpoint length differs")
        same(s["groups"], groups(self.series, self.dates), "Attribution/concentration")
        require(
            s["group_count"] == len(s["groups"])
            and s["series_count"] == 77
            and s["primary_endpoint_count"] == 6
            and s["calendar_dates"] == len(self.dates),
            "Summary counts differ",
        )

    def verify_inference(self):
        s = self.summary
        self.expected_inference = []
        for index, block in enumerate((5, 10, 20, 40)):
            print(f"Independent joint full-calendar draws | block {block}", flush=True)
            expected = JointInference(self.inputs["endpoints"]).run(block)
            self.expected_inference.append(expected)
            self.verify_inference_result(index)
        if "kernel_checks.json" in self.artifacts:
            checks = self.load("kernel_checks.json")
            for i, block in enumerate((5, 10, 20, 40)):
                same(
                    checks["results"][i],
                    JointInference(checks["input"]).run(block),
                    f"Numerical fixture {block}",
                )
            if "session_calendar_checks" in checks:
                for case in checks["session_calendar_checks"]:
                    try:
                        observations = intraday_observations(case["record"])
                    except ValueError:
                        require(
                            case["accepted"] is False and case["observations"] is None,
                            "Native and independent session rejection differ",
                        )
                    else:
                        require(case["accepted"] is True, "Valid session was rejected")
                        same(
                            case["observations"],
                            observations,
                            "Native session calendar conversion",
                        )
            zero = deepcopy(checks["input"])
            for e in zero:
                e["days"] = [[0.0, 0.0] for _ in e["days"]]
            same(
                checks["zero_studentizer_result"],
                JointInference(zero).run(40),
                "Zero studentizer fixture",
            )

            for e in zero:
                e["days"] = [[0.003, 0.001] for _ in e["days"]]
            same(
                checks["constant_increment_result"],
                JointInference(zero).run(40),
                "Constant increment fixture",
            )
            same(
                checks["rejected_inputs"],
                [
                    "incomplete_family",
                    "reordered_family",
                    "changed_material_increment",
                    "unequal_calendars",
                ],
                "Rejected native families",
            )

    def verify_inference_result(self, index):
        same(
            self.summary["inference"][index],
            self.expected_inference[index],
            "Joint inference",
        )

    def verify_plan(self):
        p = self.plan
        require(
            p["protocol_sha256"] == PROTOCOL
            and p["stage"]
            == (
                "M7_primary_diagnostics"
                if self.scope is None
                else (
                    "M8_primary_validation"
                    if self.scope.year == 2024
                    else "M9_primary_final"
                )
            )
            and p["primary_cost_bps"] == p["primary_delay_minutes"] == 1,
            "M7 plan differs",
        )
        root = Path(__file__).resolve().parents[1]
        registered = (root / "research/extension_protocol.json").read_bytes()
        require(sha(registered) == PROTOCOL, "Local registered protocol differs")
        registered = decode(registered)
        same(p["inference"], registered["inference"], "Registered inference")
        same(p["stability"], registered["stability"], "Registered stability")
        for key in (
            "secondary_variants_executed",
            "risk_matching_executed",
            "inference_calibration_validated",
            "reserved_strategy_inputs_opened",
        ):
            expected = (
                self.scope is not None and key == "reserved_strategy_inputs_opened"
            )
            require(p[key] is expected, "Plan stage differs")
        if self.scope:
            self.scope.check(p)

    def verify_plots(self):
        parser = PlotParser()
        parser.feed(self.report)
        endpoints = self.summary["inference"][3]["endpoints"]
        require(
            len(parser.coverage) == 6 and len(parser.intervals) == 6,
            "Missing coverage/interval figure",
        )
        for e, bar in zip(endpoints, parser.coverage):
            require(bar["data-endpoint"] == e["id"], "Coverage identity differs")
            require(
                math.isclose(
                    float(bar["width"]),
                    620 * e["observed_dates"] / len(self.dates),
                    rel_tol=1e-10,
                    abs_tol=1e-8,
                ),
                "Coverage geometry differs",
            )
        for j, (endpoint, graph) in enumerate(zip(endpoints, parser.intervals)):
            attrs, marks = graph
            require(
                attrs["data-endpoint"] == endpoint["id"], "Interval identity differs"
            )
            lo, hi, scale = (
                float(attrs[k]) for k in ("data-low", "data-high", "data-scale")
            )
            require(
                scale == (10000 if j < 4 else 100 if j == 4 else 1) and hi > lo,
                "Interval units/range differ",
            )
            marginal = self.summary["inference"][1 if j < 4 else 3]["endpoints"][j]
            expected_marks = {
                k
                for k in ("estimate", "family_lower", "material_increment")
                if endpoint[k] is not None
            }
            if marginal["marginal_lower"] is not None:
                expected_marks.add("marginal")
            require(marks.keys() == expected_marks, "Interval missing/extra marks")

            def x(value):
                return 50 + 900 * (value * scale - lo) / (hi - lo)

            for key, a in marks.items():
                if key == "marginal":
                    coordinates = (
                        ("x1", marginal["marginal_lower"]),
                        ("x2", marginal["marginal_upper"]),
                    )
                else:
                    coordinates = (
                        ("cx" if key == "estimate" else "x1", endpoint[key]),
                    )
                for name, value in coordinates:
                    require(
                        math.isclose(
                            float(a[name]), x(value), rel_tol=1e-9, abs_tol=1e-7
                        ),
                        "Interval geometry differs",
                    )
        lookup = {
            (g["series"], g["dimension"], g["group"]): g for g in self.summary["groups"]
        }
        require(
            {d for d, _, _, _ in parser.cells}
            == {"instrument", "year", "condition", "holding"},
            "Missing attribution figure",
        )
        observed_cells = [
            (dimension, series, group) for dimension, series, group, _ in parser.cells
        ]
        expected_cells = {
            (dimension, f"intraday/equal_weight/{policy}", group)
            for dimension, columns in (
                ("year", sorted({d[:4] for d in self.dates})),
                ("condition", ("low", "middle", "high", "unavailable")),
                (
                    "holding",
                    (
                        "cash",
                        "up_to_15",
                        "up_to_30",
                        "up_to_60",
                        "up_to_120",
                        "up_to_390",
                        "unavailable",
                    ),
                ),
            )
            for policy in INTRADAY
            for group in columns
        }
        expected_cells |= {
            ("instrument", f"intraday/{symbol}/{policy}", symbol)
            for policy in INTRADAY
            for symbol in SYMBOLS + ("equal_weight",)
        }
        require(
            len(observed_cells) == len(set(observed_cells))
            and set(observed_cells) == expected_cells,
            "Attribution plot cells missing, duplicated or unexpected",
        )
        for dimension, series, group, value in parser.cells:
            row = lookup.get(
                (series, "period", "all")
                if dimension == "instrument"
                else (series, dimension, group)
            )
            expected = row["statistics"]["mean"] if row and row["statistics"] else None
            same(decode(value), expected, "Heatmap cell")
        require(len(parser.removals) == 32, "Missing concentration table")
        for series, rule, value in parser.removals:
            row = lookup[series, "period", "all"]
            expected = next(
                x["mean_after_zeroing"]
                for x in row["concentration"]
                if x["rule"] == rule
            )
            same(decode(value), expected, "Concentration cell")

    def self_check(self):
        """Semantic mutations intentionally bypass file hashes."""
        checks = 0

        def reject(run):
            nonlocal checks
            try:
                run()
            except (ValueError, KeyError, TypeError):
                checks += 1
            else:
                raise ValueError("Semantic corruption accepted")

        valid = next(
            x
            for x in self.summary["groups"]
            if x["statistics"] and x["concentration"][0]["removed_dates"]
        )
        old = valid["concentration"][0]["removed_dates"][0]
        valid["concentration"][0]["removed_dates"][0] = "2099-01-01"
        reject(self.verify_attribution)
        valid["concentration"][0]["removed_dates"][0] = old
        for index, key in ((0, "hac_standard_error"), (1, "marginal_lower")):
            row = self.summary["inference"][index]["endpoints"][0]
            old = row[key]
            row[key] = 999.0
            reject(lambda: self.verify_inference_result(index))
            row[key] = old
        row = next(r for s in self.series for r in s["days"] if r["net"] is None)
        row["net"] = 0.0
        reject(self.verify_attribution)
        row["net"] = None
        old = self.report
        self.report = self.report.replace('width="', 'width="999', 1)
        reject(self.verify_plots)
        self.report = old
        parser = PlotParser()
        parser.feed(self.report)
        removed = parser.cells[0]
        # Remove one real cell tag from a nonempty figure; remaining dimensions
        # still exist, so the exact cell inventory must detect the omission.
        self.report = self.report.replace(
            'data-value="' + removed[3] + '"', 'data-omitted="' + removed[3] + '"', 1
        )
        reject(self.verify_plots)
        self.report = old
        old = self.market["conditions"]["SPY"][0]["cutoff_last_date"]
        self.market["conditions"]["SPY"][0]["cutoff_last_date"] = "2099-01-01"
        reject(self.verify_attribution)
        self.market["conditions"]["SPY"][0]["cutoff_last_date"] = old
        old = self.inputs["endpoints"][4]["days"]
        self.inputs["endpoints"][4]["days"] = [[0.1, 0.0] for _ in self.dates]
        reject(self.verify_attribution)
        self.inputs["endpoints"][4]["days"] = old
        # A nonlinear CER must follow pooling; the two operations generally differ.
        a, b = ([0.02, -0.01, 0.015], [-0.01, 0.02, -0.015])
        cer = lambda x: 252 * (statistics.fmean(x) - 1.5 * statistics.variance(x))
        require(
            not math.isclose(
                cer([(x + y) / 2 for x, y in zip(a, b)]), (cer(a) + cer(b)) / 2
            ),
            "CER pooling counterexample failed",
        )
        checks += 1
        require(
            BlockSampler(1, 1, 5489).integer() == 14514284786278117030,
            "MT19937-64 reference differs",
        )
        checks += 1
        state = PriorConditions()
        dates = [f"session_{i:04d}" for i in range(320)]
        values = [math.sin(i * 0.31) * 0.01 for i in range(320)]
        rows = [state.advance(d, v) for d, v in zip(dates, values)]
        require(
            all(r["label"] == "unavailable" for r in rows[:312])
            and rows[312]["label"] != "unavailable",
            "Prior-history boundary differs",
        )
        changed = PriorConditions()
        altered = [
            changed.advance(d, 100 if i == 312 else v)
            for i, (d, v) in enumerate(zip(dates, values))
        ]
        same(
            altered[:313],
            rows[:313],
            "Current return leaked into current classification",
        )
        tie = concentration(
            [dict(net=0.1), dict(net=0.1), dict(net=None)],
            ["2017-01-04", "2017-01-03", "2017-01-05"],
        )
        require(
            tie[0]["removed_dates"] == ["2017-01-03"]
            and math.isclose(tie[0]["mean_after_zeroing"], 0.05),
            "Winner tie/denominator differs",
        )
        empty = concentration([dict(net=-0.1)], ["2017-01-03"])
        require(
            empty[-1]["mean_after_zeroing"] is None
            and empty[-1]["share_of_positive_sum"] is None,
            "No-positive concentration differs",
        )
        require(
            [
                duration_band(dict(holding_minutes=v))
                for v in (None, 0, 15, 15.1, 30, 60, 120, 390)
            ]
            == [
                "unavailable",
                "cash",
                "up_to_15",
                "up_to_30",
                "up_to_30",
                "up_to_60",
                "up_to_120",
                "up_to_390",
            ],
            "Holding boundaries differ",
        )
        checks += 5
        short = {
            "primary": {
                "date": "2017-07-03",
                "policies": [],
                "common_operational": False,
                "availability": {"operational": False},
            },
            "paths": [],
        }
        values = intraday_observations(short)
        require(
            set(values) == set(INTRADAY)
            and all(all(row[k] is None for k in FIELDS) for row in values.values()),
            "Shortened session was converted into cash",
        )
        regular = deepcopy(short)
        regular["primary"]["date"] = "2017-07-05"
        reject(lambda: intraday_observations(regular))
        short["primary"]["common_operational"] = True
        reject(lambda: intraday_observations(short))
        checks += 1
        print(
            f"Numerical/failure checks passed: {checks}; semantic mutations bypass artifact hashes."
        )

    def bundle(self):
        names = ["summary.json", "report.html", *self.artifacts]
        path = self.directory / "review.zip"
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
            for name in names:
                archive.write(self.directory / name, name)
        return path


class PlotParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.coverage, self.intervals, self.cells, self.removals = [], [], [], []
        self.chart = self.dimension = self.series = self.group = None
        self.graph = None

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if "data-chart" in a:
            self.chart = a["data-chart"]
            self.dimension = a.get("data-dimension")
            if self.chart == "interval":
                self.graph = (a, {})
                self.intervals.append(self.graph)
        if tag == "rect" and self.chart == "coverage":
            self.coverage.append(a)
        if self.chart == "interval" and "data-mark" in a:
            require(a["data-mark"] not in self.graph[1], "Duplicate interval mark")
            self.graph[1][a["data-mark"]] = a
        if "data-series" in a:
            self.series = a["data-series"]
        if "data-group" in a:
            self.group = a["data-group"]
        if self.chart == "heatmap" and "data-value" in a:
            self.cells.append(
                (self.dimension, self.series, self.group, a["data-value"])
            )
        if self.chart == "concentration" and "data-rule" in a:
            self.removals.append((self.series, a["data-rule"], a["data-value"]))

    def handle_endtag(self, tag):
        if tag in ("svg", "table"):
            self.chart = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--self-check", action="store_true")
    parser.add_argument(
        "--review-only",
        action="store_true",
        help="Verify derived evidence without reopening source M6 files",
    )
    args = parser.parse_args()
    try:
        verifier = StabilityVerifier(args.directory)
        if not args.review_only:
            verifier.verify_sources()
        verifier.verify_math()
        if args.self_check:
            verifier.self_check()
        print(
            f"Verified primary stability: 77 series, {len(verifier.dates)} dates, six endpoints and all four joint block lengths."
        )
        print(
            "Source scope: "
            + (
                "derived evidence only; saved M6 records were not reopened."
                if args.review_only
                else "saved M6 records authenticated; raw provider archive was not reauthenticated."
            )
        )
        print(f"Review bundle: {verifier.bundle()}")
        return 0
    except (
        ValueError,
        KeyError,
        IndexError,
        TypeError,
        OSError,
        ZeroDivisionError,
    ) as error:
        print(f"Stability verification failed: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
