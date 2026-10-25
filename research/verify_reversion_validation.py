"""Independent arithmetic and artifact checks for a synthetic reversion report."""

import csv
from decimal import Decimal, localcontext
import hashlib
import json
import math
from pathlib import Path
import sys


class ReversionValidationVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.summary = self.load("summary.json")

    def load(self, name):
        path = (self.directory / name).resolve()
        if path.parent != self.directory:
            raise ValueError("report file must be a direct child")
        return json.loads(path.read_text(encoding="utf-8"))

    @staticmethod
    def close(actual, expected):
        if not math.isclose(actual, expected, rel_tol=1e-11, abs_tol=1e-15):
            raise ValueError(f"numerical mismatch: {actual} versus {expected}")

    def verify(self):
        summary = self.summary
        assert summary["study_status"] == "synthetic_only"
        assert summary["historical_model_fits"] == summary["historical_strategy_scores"] == 0
        for artifact in summary["artifacts"]:
            path = (self.directory / artifact["path"]).resolve()
            assert path.parent == self.directory
            assert hashlib.sha256(path.read_bytes()).hexdigest() == artifact["sha256"]
        inventory = (self.directory / "source_inventory.txt").read_bytes()
        assert hashlib.sha256(inventory).hexdigest() == summary["source_sha256"]
        sequences = {}
        with (self.directory / "reference_sessions.csv").open(newline="", encoding="utf-8") as stream:
            for row in csv.DictReader(stream):
                session = sequences.setdefault(row["case"], {}).setdefault(int(row["session"]), [])
                assert int(row["observation"]) == len(session)
                session.append(Decimal(row["residual"]))
        for case in summary["cases"]:
            trials = self.load(case["trials_file"])
            assert len(trials) == summary["trials_per_case"]
            counts = [sum(t["status"] == status for t in trials) for status in summary["status_order"]]
            assert counts == case["status_counts"]
            errors = [t["slope"] - case["latent_slope"] for t in trials if t["slope"] is not None]
            self.close(case["slope_bias"], math.fsum(errors) / len(errors))
            self.close(case["slope_rmse"], math.sqrt(math.fsum(e * e for e in errors) / len(errors)))
            self.verify_fit(sequences[case["label"]], trials[0])
            self.verify_ranges(case, trials)
        print(f"Verified {len(summary['cases'])} synthetic cases: Decimal OLS, all trial aggregates, "
              "coverage denominators, failure counts and artifact identities")

    def verify_fit(self, sessions, fitted):
        with localcontext() as context:
            context.prec = 60
            pairs = [(s[i-1], s[i]) for s in sessions.values() for i in range(1, len(s))]
            n = Decimal(len(pairs))
            mx = sum(x for x, _ in pairs) / n
            my = sum(y for _, y in pairs) / n
            xx = sum((x-mx)**2 for x, _ in pairs)
            xy = sum((x-mx)*(y-my) for x, y in pairs)
            slope = xy / xx
            intercept = my - slope*mx
            variance = sum((y-intercept-slope*x)**2 for x, y in pairs) / (n-2)
            assert fitted["pairs"] == len(pairs)
            self.close(fitted["slope"], float(slope))
            self.close(fitted["intercept"], float(intercept))
            self.close(fitted["innovation_variance"], float(variance))
            if fitted["status"] == "admissible":
                self.close(fitted["equilibrium"], float(intercept/(1-slope)))
                self.close(fitted["half_life_minutes"], float(-Decimal(5)*Decimal(2).ln()/slope.ln()))

    def verify_ranges(self, case, trials):
        phi = case["latent_slope"]
        truths = {"slope": phi, "equilibrium": None, "half_life_minutes": None, "expected_log_move": None}
        if 0 < phi < 1:
            kappa = -math.log(phi)/5
            truths.update(equilibrium=self.summary["latent_equilibrium"], half_life_minutes=math.log(2)/kappa,
                          expected_log_move=(self.summary["latent_equilibrium"]-self.summary["current_for_move"])
                          * math.exp(-kappa)*(-math.expm1(-kappa*30)))
        for index, aggregate in enumerate(case["interval_validation"]):
            selected = trials[:self.summary["coverage_trials_per_case"]]
            assert aggregate["trials"] == len(selected)
            ranges = [t["uncertainty"][index] for t in selected]
            assert aggregate["inadmissible_refits"] == sum(r["replicates"]-r["admissible_refits"] for r in ranges)
            assert aggregate["coefficient_refits"] == sum(r["coefficient_refits"] for r in ranges)
            for r in ranges:
                assert sum(r["status_counts"]) == r["replicates"]
                assert r["status_counts"][0] == r["admissible_refits"]
                assert (r["slope"] is not None) == (r["coefficient_refits"] == r["replicates"])
                for field in ["equilibrium", "half_life_minutes", "expected_log_move"]:
                    assert (r["conditional_"+field] is not None) == (r["admissible_refits"] >= 20)
            for field, truth in truths.items():
                key = field if field == "slope" else "conditional_"+field
                available = [r[key] for r in ranges if r[key] is not None and truth is not None]
                covered = sum(r["lower"] <= truth <= r["upper"] for r in available)
                statistic = aggregate[field]
                assert statistic["available"] == len(available) and statistic["covered"] == covered
                if available:
                    p = covered/len(available)
                    self.close(statistic["coverage_given_available"], p)
                    self.close(statistic["monte_carlo_standard_error"], math.sqrt(p*(1-p)/len(available)))
                else:
                    assert statistic["coverage_given_available"] is None
                    assert statistic["monte_carlo_standard_error"] is None


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: python research/verify_reversion_validation.py <report-directory>")
    ReversionValidationVerifier(sys.argv[1]).verify()
