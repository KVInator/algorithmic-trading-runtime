"""Controlled one-year precision design; no historical observations or strategy fitting."""

from __future__ import annotations

import csv
from dataclasses import dataclass
import json
import math
from pathlib import Path
import random
import statistics


@dataclass(frozen=True)
class PrecisionCase:
    sessions: int
    daily_sd_basis_points: float
    ar1: float

    def mean_standard_error(self) -> float:
        n, phi = self.sessions, self.ar1
        variance_sum = n + 2 * math.fsum((n - lag) * phi**lag for lag in range(1, n))
        return self.daily_sd_basis_points * math.sqrt(variance_sum) / n


class PrecisionDesign:
    def __init__(self, specification: dict):
        self.specification = specification

    def run(self) -> list[dict]:
        inference = self.specification["inference"]
        plan = inference["precision_design"]
        normal = statistics.NormalDist()
        # Conservative planning envelope, not the final max-T implementation.
        critical = normal.inv_cdf(1 - inference["family_alpha"] / len(self.specification["primary_endpoints"]))
        target = self.specification["primary_endpoints"][0]["material_increment"] * 10000
        rows = []
        generator = random.Random(inference["seed"])
        for sd in plan["daily_noise_basis_points"]:
            for phi in plan["ar1"]:
                case = PrecisionCase(plan["sessions"], sd, phi)
                se = case.mean_standard_error()
                means = []
                for _ in range(2000):
                    previous = generator.gauss(0, sd)
                    observations = [previous]
                    innovation = sd * math.sqrt(1 - phi * phi)
                    for _ in range(case.sessions - 1):
                        previous = phi * previous + generator.gauss(0, innovation)
                        observations.append(previous)
                    means.append(math.fsum(observations) / case.sessions)
                observed_se = statistics.stdev(means)
                if abs(observed_se / se - 1) > 0.08:
                    raise ValueError("Controlled AR(1) mean variance does not agree with its analytical reference")
                expected_power = normal.cdf(target / se - critical)
                observed_power = sum(m + target > critical * se for m in means) / len(means)
                if abs(observed_power - expected_power) > 0.03:
                    raise ValueError("Controlled power does not agree with its Gaussian reference")
                rows.append({"sessions": case.sessions, "daily_sd_basis_points": sd, "ar1": phi,
                             "mean_se_basis_points": se, "simulated_mean_se_basis_points": observed_se,
                             "excess_above_null_for_80_percent_power_basis_points": (critical + normal.inv_cdf(plan["power"])) * se,
                             "total_increment_for_80_percent_power_basis_points": target + (critical + normal.inv_cdf(plan["power"])) * se,
                             "power_at_boundary_plus_material_increment": expected_power, "simulated_power": observed_power,
                             "trials": len(means)})
        return rows


def main():
    root = Path(__file__).resolve().parents[1]
    specification = json.loads((root / "research/extension_protocol.json").read_text())
    rows = PrecisionDesign(specification).run()
    for index in range(1000):
        directory = root / "scratch/runs" / f"extension_precision_{index}"
        try:
            directory.mkdir(parents=True)
            break
        except FileExistsError:
            continue
    else:
        raise ValueError("No unused precision run directory")
    with (directory / "precision.csv").open("x", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print("Controlled precision design: nine cases, 2,000 trials each; analytical variance and power checks passed.")
    print(f"Evidence: {directory / 'precision.csv'}")
    print("Gaussian AR(1) planning calculations do not establish empirical power or validate final block-bootstrap coverage.")


if __name__ == "__main__":
    main()
