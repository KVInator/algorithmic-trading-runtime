"""Independent controlled-family generation, moments, reference bootstrap and MC audit."""

from __future__ import annotations
import argparse
from collections import defaultdict
from copy import deepcopy
from html.parser import HTMLParser
import hashlib
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile
from block_sampling import BlockSampler
from verify_stability import (
    ENDPOINTS,
    JointInference,
    OriginalEndpoint,
    decode,
    require,
    same,
)

ROOT = Path(__file__).resolve().parents[1]
POLICY = "587ff9244737e15d61ab91c4dff4a19bfd9cbbe546788e742cc684388e251b54"
PROTOCOL = "269d6e677a3e62a21ede6e88659edb2f7a998bfe0137e62b13458e411f0c55ee"


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for b in iter(lambda: f.read(1024**2), b""):
            h.update(b)
    return h.hexdigest()


class CalibrationModel:
    def __init__(self):
        p = ROOT / "research/validation_protocol.json"
        require(digest(p) == POLICY, "Validation policy changed")
        self.policy = decode(p.read_bytes())
        self.c = self.policy["calibration"]
        self.definitions = decode(
            (ROOT / "research/extension_protocol.json").read_bytes()
        )["primary_endpoints"]
        self.cases = [
            (f"noise_{s}_phi_{p}", s, p, False)
            for s in self.c["daily_noise_basis_points"]
            for p in self.c["ar1"]
        ]
        m = self.c["additional_mask_case"]
        self.cases.append(
            ("fixed_exogenous_mask", m["daily_noise_basis_points"], m["ar1"], True)
        )

    def sample(self, index, trial):
        _, noise, phi, masked = self.cases[index]
        c = self.c
        rng = BlockSampler(
            252,
            20,
            c["generation_seed"]
            + index * c["case_seed_stride"]
            + trial * c["trial_seed_stride"],
        )

        def normal():
            u = ((rng.integer() >> 12) + 0.5) * 2**-52
            v = ((rng.integer() >> 12) + 0.5) * 2**-52
            return math.sqrt(-2 * math.log(u)) * math.cos(2 * math.pi * v)

        state = [normal() for _ in range(8)]
        sigma = noise / 10000
        result = [
            dict(id=e["id"], cer=j == 4, material=e["material_increment"], days=[])
            for j, e in enumerate(self.definitions)
        ]
        missing = set(c["additional_mask_case"]["missing_indices"]) if masked else set()
        for t in range(252):
            if t:
                state = [phi * x + math.sqrt(1 - phi * phi) * normal() for x in state]
            b = 0.5 * sigma * state[7]
            for j, e in enumerate(result):
                z = math.sqrt(0.5) * (state[0] + state[j + 1])
                mu = (
                    e["material"] / 252 + 1.5 * sigma * sigma
                    if j == 4
                    else e["material"]
                )
                a = mu + 20 * sigma * z if j == 5 else b + mu + sigma * z
                e["days"].append(None if t in missing else [a, 0.0 if j == 5 else b])
        return result

    def shifts(self, index):
        _, noise, phi, _ = self.cases[index]
        variance = (
            252 + 2 * math.fsum((252 - k) * phi**k for k in range(1, 252))
        ) / 252**2
        shift = (
            (self.c["planning_critical"] + self.c["planning_power_quantile"])
            * noise
            / 10000
            * math.sqrt(variance)
        )
        return [shift] * 4 + [shift * 252, shift * 20]


def binomial(k, n, p, z):
    f = k / n
    den = 1 + z * z / n
    center = (f + z * z / (2 * n)) / den
    half = z * math.sqrt(f * (1 - f) / n + z * z / (4 * n * n)) / den
    tail = (
        1.0
        if k == 0
        else math.fsum(
            math.exp(
                math.lgamma(n + 1)
                - math.lgamma(j + 1)
                - math.lgamma(n - j + 1)
                + j * math.log(p)
                + (n - j) * math.log1p(-p)
            )
            for j in range(k, n + 1)
        )
    )
    return dict(
        events=k,
        trials=n,
        frequency=f,
        mc_standard_error=math.sqrt(f * (1 - f) / n),
        wilson_lower=max(0.0, center - half),
        wilson_upper=min(1.0, center + half),
        size_excess_binomial_p=min(1.0, tail),
    )


class Marks(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.elements = []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        self.elements.append((tag, dict(attrs)))


class CalibrationVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.summary = decode((self.directory / "summary.json").read_bytes())
        self.model = CalibrationModel()

    def trial(self, r, input_, index):
        require([x["id"] for x in r["endpoints"]] == list(ENDPOINTS), "Family changed")
        require(
            r["all_endpoints_adequate"] and r["family_available"],
            "Unexpected controlled-family unavailability; retain and investigate",
        )
        shifts = self.model.shifts(index)
        rejected = False
        for j, (e, m) in enumerate(zip(r["endpoints"], map(OriginalEndpoint, input_))):
            same(e["estimate"], m.estimate, "Outer estimate")
            same(e["hac_standard_error"], m.error(r["block_length"]), "Outer HAC")
            same(
                e["family_lower"],
                m.estimate - r["family_critical"] * e["hac_standard_error"],
                "Bound arithmetic",
            )
            same(e["true_boundary"], input_[j]["material"], "Population boundary")
            same(e["alternative_shift"], shifts[j], "Planning shift")
            flag = e["family_lower"] > e["true_boundary"]
            power = e["family_lower"] + shifts[j] > e["true_boundary"]
            require(
                e["boundary_exceeded"] == flag and e["alternative_detected"] == power,
                "Detection differs",
            )
            rejected |= flag
        require(r["family_boundary_exceeded"] == rejected, "Family indicator differs")

    def aggregate(self, rows, index, block):
        c = self.model.cases[index]
        n = len(rows)
        z = self.model.c["mc_z"]
        size = binomial(sum(x["family_boundary_exceeded"] for x in rows), n, 0.05, z)
        endpoints = [
            dict(
                id=id,
                boundary_rejection=binomial(
                    sum(x["endpoints"][j]["boundary_exceeded"] for x in rows),
                    n,
                    0.05,
                    z,
                ),
                alternative_detection=binomial(
                    sum(x["endpoints"][j]["alternative_detected"] for x in rows),
                    n,
                    0.8,
                    z,
                ),
            )
            for j, id in enumerate(ENDPOINTS)
        ]
        return dict(
            case=c[0],
            daily_noise_basis_points=float(c[1]),
            ar1=float(c[2]),
            fixed_mask=c[3],
            block_length=block,
            available_trials=sum(x["family_available"] for x in rows),
            trials=n,
            family_boundary_exceedance=size,
            size_excess_adjusted_alpha=0.05 / 20,
            size_excess_detected=size["size_excess_binomial_p"] <= 0.05 / 20,
            endpoint_diagnostics=endpoints,
            nominal_market_coverage_certified=False,
        )

    def plots(self, expected):
        d = self.directory
        s = self.summary
        report = (d / "report.html").read_text()
        require(digest(d / "report.html") == s["report_sha256"], "Report changed")
        marks = Marks(report)
        require(
            [int(a["data-block"]) for tag, a in marks.elements if tag == "svg"]
            == [20, 40],
            "Plot panels differ",
        )
        for block in (20, 40):
            rows = [r for r in expected if r["block_length"] == block]
            limit = max(
                [0.15]
                + [1.1 * r["family_boundary_exceedance"]["wilson_upper"] for r in rows]
            )
            svg = next(
                a
                for tag, a in marks.elements
                if tag == "svg" and int(a["data-block"]) == block
            )
            require(
                math.isclose(
                    float(svg["data-limit"]), limit, rel_tol=1e-10, abs_tol=1e-10
                ),
                "Axis differs",
            )
            for i, r in enumerate(rows):
                occurrence = 0 if block == 20 else 1
                b = r["family_boundary_exceedance"]
                case = r["case"]
                line = [
                    a
                    for tag, a in marks.elements
                    if tag == "line" and a.get("data-case") == case
                ][occurrence]
                dot = [
                    a
                    for tag, a in marks.elements
                    if tag == "circle" and a.get("data-case") == case
                ][occurrence]
                values = [
                    (line["x1"], 400 + 500 * b["wilson_lower"] / limit),
                    (line["x2"], 400 + 500 * b["wilson_upper"] / limit),
                    (dot["cx"], 400 + 500 * b["frequency"] / limit),
                    (dot["cy"], 40 + 30 * i),
                ]
                require(
                    all(
                        math.isclose(float(a), b, rel_tol=5e-11, abs_tol=1e-8)
                        for a, b in values
                    ),
                    "Plot coordinate differs",
                )
        for r in expected:
            if r["block_length"] == 40:
                for e in r["endpoint_diagnostics"]:
                    require(
                        f'data-power="{r["case"]}/{e["id"]}">{100*e["alternative_detection"]["frequency"]:.2f}%'
                        in report,
                        "Detection table differs",
                    )

    def export_verified(self):
        d = self.directory
        s = self.summary
        r = decode((d / "verification.json").read_bytes())
        require(
            r["status"] == "verified"
            and r["summary_sha256"] == digest(d / "summary.json"),
            "Figure requires verified summary",
        )
        text = (d / "report.html").read_text()
        require(
            digest(d / "report.html") == s["report_sha256"], "Verified report changed"
        )
        plots = re.findall(r'(<svg data-block="(20|40)".*?</svg>)', text, re.S)
        require([int(b) for _, b in plots] == [20, 40], "Native panels differ")
        root = ET.Element(
            "svg",
            {
                "xmlns": "http://www.w3.org/2000/svg",
                "viewBox": "0 0 1100 1040",
                "role": "img",
                "aria-label": "Controlled family boundary exceedance and Monte Carlo intervals",
            },
        )
        ET.SubElement(root, "style").text = (
            "text{font:13px system-ui;fill:#19364c}.heading{font-size:22px;font-weight:700}.subheading{font-size:17px;font-weight:600}"
        )
        ET.SubElement(
            root, "rect", {"width": "1100", "height": "1040", "fill": "white"}
        )
        ET.SubElement(root, "text", {"x": "20", "y": "28", "class": "heading"}).text = (
            "Controlled simultaneous inference calibration"
            + (" | engineering check" if s["is_check"] else "")
        )
        ET.SubElement(root, "text", {"x": "20", "y": "53"}).text = (
            f"{s['trials_per_case']:,} outer trials per case; 95% Wilson Monte Carlo intervals; dashed line = 5% reference"
        )
        lookup = {x["case"]: x for x in s["cases"]}
        for i, (raw, block) in enumerate(plots):
            ET.SubElement(
                root, "text", {"x": "20", "y": str(85 + 460 * i), "class": "subheading"}
            ).text = f"Block {block} | complete six-endpoint material-boundary family"
            panel = ET.fromstring(raw)
            panel.set("x", "0")
            panel.set("y", str(95 + 460 * i))
            panel.set("width", "1100")
            panel.set("height", "410")
            for node in panel.iter("text"):
                if node.text in lookup:
                    c = lookup[node.text]
                    node.text = (
                        f"{c['daily_noise_basis_points']:g} bp noise; phi={c['ar1']:g}"
                        + ("; fixed mask" if c["fixed_mask"] else "")
                    )
                elif node.get("x") == "900" and (node.text or "").endswith("%"):
                    node.text = f"{float(node.text[:-1]):.1f}%"
            root.append(panel)
        ET.SubElement(root, "text", {"x": "20", "y": "1000"}).text = (
            f"Constructed Gaussian AR(1) evidence. Size-excess cells: {s['size_excess_cells']} / 20. No market-wide coverage claim."
        )
        ET.SubElement(
            root, "text", {"x": "20", "y": "1025", "style": "font-size:10px"}
        ).text = ("Source summary SHA-256: " + r["summary_sha256"])
        ET.SubElement(root, "metadata").text = json.dumps(
            dict(
                summary_sha256=r["summary_sha256"],
                validation_protocol_sha256=POLICY,
                native_coordinates=True,
            ),
            sort_keys=True,
        )
        (d / "calibration.svg").write_text(ET.tostring(root, encoding="unicode") + "\n")
        r["figure_sha256"] = digest(d / "calibration.svg")
        (d / "verification.json").write_text(json.dumps(r, indent=2) + "\n")

    def run(self, self_check=False):
        s = self.summary
        d = self.directory
        n = 4 if s["is_check"] else 1000
        require(
            s["kind"] == "controlled_inference_calibration"
            and s["validation_protocol_sha256"] == POLICY
            and s["protocol_sha256"] == PROTOCOL,
            "Calibration identity differs",
        )
        require(
            s["case_count"] == 10
            and s["trials_per_case"] == n
            and s["joint_family_evaluations"] == 20 * n
            and s["controlled_calibration_completed"] == (not s["is_check"]),
            "Trial matrix differs",
        )
        require(
            not any(
                s[k]
                for k in (
                    "network_requests",
                    "market_coverage_certified",
                    "validation_strategy_inputs_opened",
                    "final_strategy_inputs_opened",
                )
            ),
            "Scope differs",
        )
        names = {
            "trials.jsonl",
            "reference_trials.jsonl",
            "kernel_checks.json",
            "source_inventory.txt",
            "plan.json",
        }
        require(
            len(s["artifacts"]) == 5 and {a["path"] for a in s["artifacts"]} == names,
            "Inventory differs",
        )
        for a in s["artifacts"]:
            p = d / a["path"]
            require(
                p.parent == d
                and p.stat().st_size == a["bytes"]
                and a["bytes"] <= 256 * 1024**2
                and digest(p) == a["sha256"],
                "Artifact changed",
            )
        require(
            digest(d / "source_inventory.txt") == s["source_sha256"],
            "Source inventory differs",
        )
        same(
            decode((d / "plan.json").read_bytes()), self.model.policy, "Registered plan"
        )
        grouped = defaultdict(list)
        records = {}
        lines = iter(map(decode, (d / "trials.jsonl").read_bytes().splitlines()))
        for index, c in enumerate(self.model.cases):
            for trial in range(n):
                sample = self.model.sample(index, trial)
                for block in (20, 40):
                    r = next(lines)
                    require(
                        (r["case"], r["trial"], r["block_length"])
                        == (c[0], trial, block),
                        "Trial identity/order differs",
                    )
                    self.trial(r, sample, index)
                    grouped[index, block].append(r)
                    records[index, trial, block] = r
            print(
                f"Independent controlled moments | {c[0]} | {n} outer trials",
                flush=True,
            )
        require(next(lines, None) is None, "Unexpected trials")
        refs = list(
            map(decode, (d / "reference_trials.jsonl").read_bytes().splitlines())
        )
        require(len(refs) == 20, "Reference matrix differs")
        mapping = {}
        count = 0
        for index, c in enumerate(self.model.cases):
            for trial in (0, n - 1):
                ref = refs[count // 2]
                require(
                    (ref["case"], ref["trial"]) == (c[0], trial),
                    "Reference identity differs",
                )
                sample = self.model.sample(index, trial)
                same(ref["endpoints"], sample, "Independent Gaussian generation")
                for k, block in enumerate((20, 40)):
                    result = JointInference(sample).run(block)
                    same(ref["inference"][k], result, "Independent complete bootstrap")
                    r = records[index, trial, block]
                    same(
                        r["family_critical"],
                        result["family_critical"],
                        "Reference critical",
                    )
                    require(
                        r["sampling_sha256"] == result["sampling_sha256"],
                        "Mapping differs",
                    )
                    mapping[block] = result["sampling_sha256"]
                    count += 1
                print(
                    f"Independent complete-family bootstrap | {c[0]} | trial {trial}",
                    flush=True,
                )
        require(
            all(
                x["sampling_sha256"] == mapping[x["block_length"]]
                for x in records.values()
            ),
            "Cached mappings differ",
        )
        expected = [
            self.aggregate(grouped[i, b], i, b) for i in range(10) for b in (20, 40)
        ]
        same(s["cases"], expected, "Monte Carlo aggregation")
        excess = sum(x["size_excess_detected"] for x in expected)
        require(
            s["size_excess_cells"] == excess
            and s["unavailable_families"] == 0
            and s["nominal_size_excess_detected"] == (not s["is_check"] and excess > 0),
            "Qualification differs",
        )
        self.plots(expected)
        checks = 0
        if self_check:
            for field, change in [
                ("estimate", lambda x: x + 1e-4),
                ("hac_standard_error", lambda x: x * 2),
                ("family_lower", lambda x: x + 1e-4),
                ("true_boundary", lambda x: x + 1e-4),
                ("alternative_shift", lambda x: x * 2),
                ("boundary_exceeded", lambda x: not x),
            ]:
                mutated = deepcopy(records[0, 0, 20])
                mutated["endpoints"][0][field] = change(mutated["endpoints"][0][field])
                try:
                    self.trial(mutated, self.model.sample(0, 0), 0)
                except ValueError:
                    checks += 1
                else:
                    raise ValueError("Semantic corruption accepted: " + field)
        receipt = dict(
            kind="controlled_inference_calibration_verification",
            status="verified",
            summary_sha256=digest(d / "summary.json"),
            validation_protocol_sha256=POLICY,
            full_registered_experiment=not s["is_check"],
            outer_moment_checks=20 * n,
            independent_full_bootstrap_replays=count,
            reference_scope="first_and_last_outer_trial_per_case_and_block_remaining_trials_moments_HAC_bounds_aggregates_verified",
            semantic_checks=checks,
        )
        (d / "verification.json").write_text(json.dumps(receipt, indent=2) + "\n")
        self.export_verified()
        with zipfile.ZipFile(d / "review.zip", "w", zipfile.ZIP_DEFLATED) as z:
            for name in names | {
                "summary.json",
                "report.html",
                "verification.json",
                "calibration.svg",
            }:
                z.write(d / name, name)
        print(
            f"Verified controlled calibration: {20*n} joint families, {count} full reference bootstrap replays; size-excess cells: {excess}."
        )
        print("Review bundle:", d / "review.zip")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--self-check", action="store_true")
    a = p.parse_args()
    try:
        CalibrationVerifier(a.directory).run(a.self_check)
        return 0
    except (
        ValueError,
        KeyError,
        OSError,
        IndexError,
        TypeError,
        StopIteration,
        ArithmeticError,
    ) as e:
        print("Inference calibration verification failed:", e)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
