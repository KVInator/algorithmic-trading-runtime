"""Independent SPY-MR-01 report verification. Requires NumPy for bulk refits."""

import csv
from datetime import datetime, timezone
from decimal import Decimal, localcontext
import hashlib
import json
import math
from pathlib import Path
import sys
from zoneinfo import ZoneInfo

import numpy as np
from block_sampling import BlockSampler


class SessionSampler(BlockSampler):
    """NumPy multiplicities for the registered historical reversion oracle."""

    def weights(self, replicates):
        result = np.zeros((replicates, self.size))
        for row in result:
            for start, length in self.blocks():
                row[start:start+length] += 1
        return result


class ReversionStudyVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.summary = self.load("summary.json")
        self.observations = self.csv("observations.csv")
        self.features = self.csv("features.csv")

    def load(self, name):
        path = (self.directory/name).resolve()
        assert path.parent == self.directory
        return json.loads(path.read_text())

    def csv(self, name):
        with (self.directory/name).open(newline="") as stream:
            return list(csv.DictReader(stream))

    @staticmethod
    def close(a, b, derived=False):
        assert math.isclose(float(a), float(b), rel_tol=1e-9 if derived else 1e-11,
                            abs_tol=1e-12 if derived else 1e-14), (a, b)

    def verify(self):
        assert SessionSampler(1, 1, 5489).integer() == 14514284786278117030
        s = self.summary
        assert s["study_status"] == "registered_development_scored" and s["revision"] == 1
        assert s["manifest_sha256"] == "70fdd01093b9026cc4e673def25718e547c0052b728163bac9bdf30b9c25126e"
        assert len(self.observations) == 496 and len(self.features) == 476 and s["market_account_cases"] == 40
        assert [f["date"] for f in self.features] == [o["date"] for o in self.observations[20:]]
        assert all("2022-01-01" <= o["date"] <= "2023-12-31" for o in self.observations)
        for artifact in s["artifacts"]:
            path = (self.directory/artifact["path"]).resolve()
            assert path.parent == self.directory
            assert hashlib.sha256(path.read_bytes()).hexdigest() == artifact["sha256"]
        assert hashlib.sha256((self.directory/"source_inventory.txt").read_bytes()).hexdigest() == s["source_sha256"]
        cash = self.csv("cash.csv")
        assert len(cash) == 476 and [r["date"] for r in cash] == [f["date"] for f in self.features]
        assert all(float(r["gross_return"]) == float(r["net_return"]) == 0 and float(r["equity"]) == 1 for r in cash)
        self.verify_priors()
        values = self.verify_accounts()
        self.verify_outcomes(values)
        self.verify_parameter_ranges()
        self.verify_gates()
        print("Verified 476 prior-only Decimal fits/decisions, forty account paths, annual metrics, "
              "both paired outcome bootstraps, all parameter ranges, gates and artifact identities; NumPy", np.__version__)

    def verify_priors(self):
        for i, f in enumerate(self.features):
            history = self.observations[i:i+20]
            assert f["oldest_history_start"][:10] == history[0]["date"]
            assert f["latest_history_start"][:10] == history[-1]["date"] < f["date"]
            noon = datetime.fromisoformat(f["date"]+"T12:00:00").replace(tzinfo=ZoneInfo("America/New_York"))
            assert datetime.fromisoformat(f["decision_at"].replace("Z", "+00:00")) == noon
            complete = all(o["anchor_price"] for o in history)
            assert bool(int(f["history_complete"])) == complete
            if not complete:
                assert not int(f["candidate_active"]) and not int(f["drift_active"])
                continue
            with localcontext() as context:
                context.prec = 60
                pairs = [(Decimal(o[f"x{k-1}"]), Decimal(o[f"x{k}"])) for o in history for k in range(1, 25)]
                x = sum(a for a, b in pairs)/480
                y = sum(b for a, b in pairs)/480
                xx = sum((a-x)**2 for a, b in pairs)
                xy = sum((a-x)*(b-y) for a, b in pairs)
                phi = xy/xx
                intercept = y-phi*x
                variance = sum((b-intercept-phi*a)**2 for a, b in pairs)/478
                self.close(f["slope"], phi)
                self.close(f["intercept"], intercept)
                self.close(f["innovation_variance"], variance)
                drift = float(sum(b-a for a, b in pairs)/80)
                self.close(f["drift_log_move"], drift)
                admissible = 0 < phi < 1 and abs(phi-1) > Decimal(64)*Decimal(2)**-52 and variance > 0
                assert (f["fit_status"] == "admissible") == admissible
                current = self.observations[i+20]
                assert bool(f["current_residual"]) == bool(current["anchor_price"])
                if current["anchor_price"]:
                    residual = float(current["x24"])
                    self.close(f["current_residual"], residual)
                    expected = None
                    if admissible:
                        theta = float(intercept/(1-phi))
                        kappa = -math.log(float(phi))/5
                        expected = (theta-residual)*math.exp(-kappa)*(-math.expm1(-kappa*30))
                        self.close(f["equilibrium"], theta, True)
                        self.close(f["half_life_minutes"], math.log(2)/kappa, True)
                        self.close(f["expected_log_move"], expected)
                    hurdle = math.log1p(.0001)-math.log1p(-.0001)
                    assert bool(int(f["candidate_active"])) == bool(expected is not None and residual < theta and expected > hurdle)
                    assert bool(int(f["sign_only_active"])) == (residual < 0)
                assert bool(int(f["drift_active"])) == (drift > math.log1p(.0001)-math.log1p(-.0001))
                assert int(f["always_long_active"]) == 1

    def verify_accounts(self):
        n = len(self.features)
        values = np.full((n, 88), np.nan)
        records = {}
        for policy in ["candidate", "sign_only", "drift", "always_long"]:
            rows = self.csv(f"accounts_{policy}.csv")
            assert len(rows) == n*10
            for row in rows:
                records.setdefault(int(row["case_index"]), []).append(row)
        for case in self.summary["cases"]:
            i = case["case_index"]
            rows = records[i]
            gross, net, active = [], [], []
            wealth = peak = 1.0
            for d, row in enumerate(rows):
                f = self.features[d]
                assert row["date"] == f["date"]
                a = bool(int(f[case["policy"]+"_active"]))
                c = case["cost_bps_per_side"]/10000
                ratio = float(row["exit_price"])/float(row["entry_price"])
                g, r = (ratio-1, ratio*(1-c)/(1+c)-1) if a else (0.0, 0.0)
                self.close(row["gross_return"], g)
                self.close(row["net_return"], r)
                self.close(row["equity_before"], wealth)
                self.close(row["fractional_quantity"], wealth/(float(row["entry_price"])*(1+c)) if a else 0)
                wealth *= 1+r
                peak = max(peak, wealth)
                self.close(row["equity_after"], wealth)
                self.close(row["drawdown"], 1-wealth/peak)
                assert int(row["holding_minutes"]) == (30 if a else 0) and int(row["turnover"]) == (2 if a else 0)
                at = datetime.fromisoformat(f["decision_at"].replace("Z", "+00:00"))
                entry = datetime.fromisoformat(row["entry_time"].replace("Z", "+00:00"))
                exit_at = datetime.fromisoformat(row["exit_time"].replace("Z", "+00:00"))
                assert (entry-at).total_seconds() == 60*case["delay_minutes"] and (exit_at-entry).total_seconds() == 1800
                gross.append(g); net.append(r); active.append(a)
                values[d, i] = r; values[d, 40+i] = g
            self.check_period(case["overall"], rows, gross, net, active)
            for year, stats in case["years"].items():
                indices = [k for k, row in enumerate(rows) if row["date"].startswith(year)]
                self.check_period(stats, [rows[k] for k in indices], [gross[k] for k in indices],
                                  [net[k] for k in indices], [active[k] for k in indices])
        for p in range(1, 4):
            values[:, 78+2*p] = values[:, 2]-values[:, 10*p+2]
            values[:, 79+2*p] = values[:, 42]-values[:, 40+10*p+2]
        primary = records[2]
        for d, f in enumerate(self.features):
            target = math.log(float(primary[d]["exit_price"]))-math.log(float(primary[d]["entry_price"]))
            self.close(f["realized_log_move"], target)
            if f["expected_log_move"] and f["drift_log_move"]:
                e = target-float(f["expected_log_move"]); z = target-float(f["drift_log_move"])
                values[d, 86:88] = z*z-e*e, abs(z)-abs(e)
                self.close(f["squared_loss_contrast"], values[d, 86]); self.close(f["absolute_loss_contrast"], values[d, 87])
            else:
                assert not f["squared_loss_contrast"] and not f["absolute_loss_contrast"]
        return values

    def check_period(self, stats, rows, gross, net, active):
        assert stats["sessions"] == len(net) and stats["active_dates"] == sum(active)
        self.close(stats["mean_daily_turnover"], 2*sum(active)/len(active))
        for name, data in [("gross", gross), ("net", net)]:
            out = stats[name]; wealth = peak = 1.0; drawdown = 0
            for v in data:
                wealth *= 1+v; peak = max(peak, wealth); drawdown = max(drawdown, 1-wealth/peak)
            mean = math.fsum(data)/len(data)
            self.close(out["mean"], mean); self.close(out["compounded"], wealth-1)
            self.close(out["maximum_daily_close_drawdown"], drawdown)
            if len(data) > 1:
                sd = math.sqrt(math.fsum((v-mean)**2 for v in data)/(len(data)-1))
                if sd > 0:
                    self.close(out["sample_standard_deviation"], sd)
                    self.close(out["annualized_daily_sharpe"], mean/sd*math.sqrt(252))
        positive = math.fsum(v for v in net if v > 0)
        winners = sorted([(v, row["date"]) for v, row in zip(net, rows) if v > 0], key=lambda x: (-x[0], x[1]))[:5]
        self.close(stats["net_sum"], math.fsum(net)); self.close(stats["positive_return_sum"], positive)
        self.close(stats["top_five_net_sum"], math.fsum(v for v, _ in winners))
        assert [w["date"] for w in stats["largest_five_winners"]] == [d for _, d in winners]
        if positive:
            self.close(stats["top_five_share_of_positive_returns"], math.fsum(v for v, _ in winners)/positive)

    def verify_outcomes(self, values):
        finite = np.isfinite(values)
        for i, metric in enumerate(self.summary["metrics"]):
            assert metric["available"] == int(finite[:, i].sum())
            if metric["available"]:
                self.close(metric["mean"], values[finite[:, i], i].mean())
            else:
                assert metric["mean"] is None
        assert [b["block_length"] for b in self.summary["outcome_uncertainty"]] == [5, 10]
        for boot in self.summary["outcome_uncertainty"]:
            assert boot["replicates"] == 10000 and boot["seed"] == 20261023
            weights = SessionSampler(len(values), boot["block_length"], boot["seed"]).weights(10000)
            counts = weights @ finite.astype(float)
            sums = weights @ np.where(finite, values, 0)
            for i, record in enumerate(boot["metrics"]):
                undefined = int((counts[:, i] == 0).sum())
                assert record["undefined_replicates"] == undefined
                if undefined:
                    assert record["range"] is None
                else:
                    lo, hi = np.quantile(sums[:, i]/counts[:, i], [.025, .975])
                    self.close(record["range"]["lower"], lo); self.close(record["range"]["upper"], hi)

    def verify_parameter_ranges(self):
        ranges = self.load("parameter_ranges.json")
        expected = {(f["date"], b) for f in self.features if int(f["history_complete"]) for b in [1, 5]}
        assert len(ranges) == len(expected) and {(r["date"], r["block_length"]) for r in ranges} == expected
        for row in ranges:
            index = next(i for i, f in enumerate(self.features) if f["date"] == row["date"])
            history = np.array([[float(o[f"x{k}"]) for k in range(25)] for o in self.observations[index:index+20]])
            assert row["replicates"] == 499 and row["seed"] == 20261023+int(row["date"].replace("-", ""))
            w = SessionSampler(20, row["block_length"], row["seed"]).weights(499)
            x, y = history[:, :-1], history[:, 1:]
            sx, sy = w @ x.sum(axis=1), w @ y.sum(axis=1)
            xx = w @ (x*x).sum(axis=1)-sx*sx/480
            xy = w @ (x*y).sum(axis=1)-sx*sy/480
            phi = xy/xx; a = (sy-phi*sx)/480
            status = np.where(phi <= 0, 3, np.where(abs(phi-1) <= 64*np.finfo(float).eps, 4, np.where(phi > 1, 5, 0)))
            # Direct residual SSE per source session, then the bootstrap weights.
            for b in range(499):
                if np.sum(w[b, :, None]*(y-a[b]-phi[b]*x)**2) == 0 and status[b] == 0:
                    status[b] = 6
            assert row["status_counts"] == np.bincount(status, minlength=7).tolist()
            valid = status == 0
            assert row["admissible_refits"] == int(valid.sum()) and row["coefficient_refits"] == 499
            comparisons = {"slope": phi}
            if valid.sum() >= 20:
                theta = a[valid]/(1-phi[valid]); kappa = -np.log(phi[valid])/5
                comparisons.update(conditional_equilibrium=theta, conditional_half_life_minutes=np.log(2)/kappa)
                current = self.features[index]["current_residual"]
                if current:
                    comparisons["conditional_expected_log_move"] = (theta-float(current))*np.exp(-kappa)*(-np.expm1(-kappa*30))
            for field in ["slope", "conditional_equilibrium", "conditional_half_life_minutes", "conditional_expected_log_move"]:
                if field in comparisons:
                    lo, hi = np.quantile(comparisons[field], [.025, .975])
                    self.close(row[field]["lower"], lo, True); self.close(row[field]["upper"], hi, True)
                else:
                    assert row[field] is None

    def verify_gates(self):
        s = self.summary; primary = s["cases"][2]; p = primary["overall"]; metrics = s["metrics"]
        expected = {"at_least_100_active_dates": p["active_dates"] >= 100,
                    "positive_primary_gross": p["gross"]["mean"] > 0, "positive_primary_net": p["net"]["mean"] > 0,
                    "positive_one_minute_2bp_net": s["cases"][3]["overall"]["net"]["mean"] > 0,
                    "positive_two_minute_1bp_net": s["cases"][7]["overall"]["net"]["mean"] > 0,
                    "top_five_share_at_most_half": p["top_five_share_of_positive_returns"] is not None and p["top_five_share_of_positive_returns"] <= .5,
                    "positive_net_sum_without_top_five": p["net_sum"]-p["top_five_net_sum"] > 0}
        for year in ["2022", "2023"]:
            expected["positive_net_"+year] = year in primary["years"] and primary["years"][year]["net"]["mean"] > 0
        for i in [80, 82]: expected["positive_"+metrics[i]["label"]] = metrics[i]["mean"] > 0
        for i in [81, 83]: expected["nonnegative_"+metrics[i]["label"]] = metrics[i]["mean"] >= 0
        for b in s["outcome_uncertainty"]:
            for i in [2, 80, 82]:
                r = b["metrics"][i]["range"]
                expected["positive_lower_"+metrics[i]["label"]+"_block_"+str(b["block_length"])] = r is not None and r["lower"] > 0
        assert {g["label"]: g["passed"] for g in s["economic_gates"]} == expected
        status = "no_gross_edge" if p["gross"]["mean"] <= 0 else "cost_dominated" if p["net"]["mean"] <= 0 else \
            "passes_development_screens" if all(expected.values()) else "failed_registered_screens"
        assert s["assessment"] == status
        forecast = {"positive_squared_loss_improvement": metrics[86]["mean"] is not None and metrics[86]["mean"] > 0,
                    "positive_absolute_loss_improvement": metrics[87]["mean"] is not None and metrics[87]["mean"] > 0}
        for b in s["outcome_uncertainty"]:
            for i in [86, 87]:
                r = b["metrics"][i]["range"]
                forecast["positive_lower_"+metrics[i]["label"]+"_block_"+str(b["block_length"])] = r is not None and r["lower"] > 0
        assert {g["label"]: g["passed"] for g in s["forecast_gates"]} == forecast


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: python research/verify_reversion_study.py <report-directory>")
    ReversionStudyVerifier(sys.argv[1]).verify()
