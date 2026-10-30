"""Verify integrated evaluation with existing independent causal and ledger oracles."""

from __future__ import annotations
import argparse
from copy import deepcopy
from decimal import localcontext
from html.parser import HTMLParser
import json
import math
from pathlib import Path
import zipfile
from evaluation_scope import EvaluationScope
from verify_cost_delay import CostDelayVerifier
from verify_panel_accounts import verify as verify_accounts
from verify_stability_extension import ExtensionVerifier
from verify_stability import StabilityVerifier, decode, require, same
from verify_inference_calibration import POLICY, PROTOCOL, CalibrationModel, digest

ROOT = Path(__file__).resolve().parents[1]


class CoverageMarks(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.marks = {}
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if tag == "rect" and "data-endpoint" in a:
            require(a["data-endpoint"] not in self.marks, "Duplicate coverage mark")
            self.marks[a["data-endpoint"]] = a


class EvaluationVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.summary = decode((self.directory / "summary.json").read_bytes())
        self.scope = EvaluationScope(self.summary["evaluation_scope"])

    def contract(self, s):
        require(
            s["kind"] == "panel_evaluation"
            and s["schema_version"] == 1
            and s["validation_protocol_sha256"] == POLICY
            and s["protocol_sha256"] == PROTOCOL,
            "Evaluation registration differs",
        )
        require(
            s["evaluation_scope"] == self.scope.document
            and s["evidence_origin"]
            == ("synthetic" if self.scope.synthetic else "provider"),
            "Source/capability differs",
        )
        require(
            s["primary_series"] == 77
            and s["secondary_and_matched_series"] == 345
            and s["registered_variants"] == 14,
            "Evaluation matrix differs",
        )
        require(
            not any(
                s[k]
                for k in (
                    "network_requests",
                    "external_broker_events",
                    "rules_selected_from_validation",
                    "parameters_changed",
                    "strategy_advancement_authorized",
                )
            ),
            "Scope/advancement differs",
        )
        require(
            s["validation_strategy_inputs_opened"]
            == (not self.scope.synthetic and self.scope.year == 2024)
            and s["final_strategy_inputs_opened"]
            == (not self.scope.synthetic and self.scope.year == 2025),
            "Access claim differs",
        )
        require(
            s["calibration_size_excess_detected"]
            == self.scope.document["calibration_size_excess_detected"],
            "Calibration warning lost",
        )

    def calibration(self):
        s = self.summary
        d = Path(s["calibration_directory"])
        c = decode((d / "summary.json").read_bytes())
        require(
            c["kind"] == "controlled_inference_calibration"
            and c["validation_protocol_sha256"] == POLICY
            and c["protocol_sha256"] == PROTOCOL,
            "Calibration registration differs",
        )
        if self.scope.year == 2024:
            require(
                digest(d / "summary.json") == self.scope.document["authority_sha256"],
                "Calibration authority changed",
            )
        require(
            c["is_check"] == self.scope.synthetic
            and c["controlled_calibration_completed"] == (not self.scope.synthetic),
            "Constructed calibration cannot authorize provider input",
        )
        if not self.scope.synthetic:
            r = decode((d / "verification.json").read_bytes())
            require(
                r["kind"] == "controlled_inference_calibration_verification"
                and r["status"] == "verified"
                and r["summary_sha256"] == digest(d / "summary.json")
                and r["validation_protocol_sha256"] == POLICY
                and r["full_registered_experiment"]
                and r["outer_moment_checks"] == 20000
                and r["independent_full_bootstrap_replays"] == 40,
                "Full independent calibration receipt missing or stale",
            )
        for a in c["artifacts"]:
            p = d / a["path"]
            require(
                p.resolve().parent == d.resolve()
                and p.stat().st_size == a["bytes"]
                and digest(p) == a["sha256"],
                "Calibration evidence changed",
            )
        same(
            c["nominal_size_excess_detected"],
            s["calibration_size_excess_detected"],
            "Carried warning",
        )

    def children(self):
        out = {}
        for row in self.summary["children"]:
            p = self.directory / row["path"]
            require(
                p.resolve().parent == self.directory
                and digest(p / "summary.json") == row["summary_sha256"],
                "Child identity/path differs",
            )
            s = decode((p / "summary.json").read_bytes())
            require(
                s["evaluation_scope"] == self.scope.document
                and s["source_sha256"] == self.summary["source_sha256"],
                "Child stage/build differs",
            )
            require(s["kind"] not in out, "Duplicate child")
            out[s["kind"]] = p
        require(
            set(out)
            == {
                "cost_delay_comparison",
                "panel_account_study",
                "stability_extension_study",
                "stability_primary_diagnostics",
            },
            "Integrated child matrix differs",
        )
        return out

    def verify_runner(self):
        s = self.summary
        if self.scope.year == 2025:
            freeze = Path(s["final_freeze_directory"]).resolve()
            record = decode((freeze / "freeze.json").read_bytes())
            runner = freeze / record["runner"]
            require(
                runner.resolve().parent == freeze,
                "Frozen runner escapes its directory",
            )
        else:
            runner = (
                ROOT
                / "scratch/native"
                / s["build_configuration"]
                / (
                    "panel_evaluation.exe"
                    if s["compiler"].startswith("MSVC")
                    else "panel_evaluation"
                )
            )
        require(
            runner.is_file() and digest(runner) == s["runner_sha256"],
            "Evaluated runner is missing or changed",
        )
        return runner

    def self_check_contracts(self):
        """Reject changed access claims before reading expensive source evidence."""
        checks = 0
        for key, value in [
            ("rules_selected_from_validation", True),
            ("parameters_changed", True),
            ("strategy_advancement_authorized", True),
            ("network_requests", 1),
            (
                "final_strategy_inputs_opened",
                not self.summary["final_strategy_inputs_opened"],
            ),
            ("registered_variants", 13),
        ]:
            require(
                value != self.summary[key], "Semantic mutation is unchanged: " + key
            )
            changed = deepcopy(self.summary)
            changed[key] = value
            try:
                self.contract(changed)
            except ValueError:
                checks += 1
            else:
                raise ValueError("Semantic mutation accepted: " + key)
        for key, value in [
            ("unit_reference_first", f"{self.scope.year - 1}-01-01"),
            ("last_input", f"{self.scope.year + 1}-12-31"),
            ("first_scored", f"{self.scope.year - 1}-01-01"),
        ]:
            require(
                value != self.scope.document[key],
                "Cohort mutation is unchanged: " + key,
            )
            changed = deepcopy(self.scope.document)
            changed[key] = value
            try:
                EvaluationScope(changed)
            except ValueError:
                checks += 1
            else:
                raise ValueError("Cohort mutation accepted: " + key)
        return checks

    def run(self, self_check=False):
        self.contract(self.summary)
        checks = self.self_check_contracts() if self_check else 0
        if self_check:
            print(
                f"Evaluation access/cohort checks passed: {checks}; authenticating saved evidence.",
                flush=True,
            )
        if self.scope.year == 2025:
            freeze = Path(self.summary["final_freeze_directory"])
            verify_freeze(freeze)
            f = decode((freeze / "freeze.json").read_bytes())
            require(
                f["evidence_origin"] == "provider"
                and digest(freeze / "freeze.json")
                == self.scope.document["authority_sha256"]
                and f["runner_sha256"] == self.summary["runner_sha256"],
                "Final authority/executable differs",
            )
        self.calibration()
        s = self.summary
        d = self.directory
        require(
            len(s["artifacts"]) == 3
            and {a["path"] for a in s["artifacts"]}
            == {"plan.json", "access_checks.json", "source_inventory.txt"},
            "Artifact inventory differs",
        )
        for a in s["artifacts"]:
            p = d / a["path"]
            require(
                p.parent == d
                and p.stat().st_size == a["bytes"]
                and digest(p) == a["sha256"],
                "Artifact changed",
            )
        same(
            decode((d / "plan.json").read_bytes()),
            CalibrationModel().policy,
            "Validation specification",
        )
        require(
            digest(d / "source_inventory.txt") == s["source_sha256"],
            "Source inventory differs",
        )
        self.verify_runner()
        children = self.children()
        print("Authenticating all evaluation decisions and cost paths", flush=True)
        CostDelayVerifier(children["cost_delay_comparison"], scope=self.scope).run()
        with localcontext() as context:
            context.prec = 50
            verify_accounts(children["panel_account_study"], scope=self.scope)
        ExtensionVerifier(children["stability_extension_study"], scope=self.scope).run()
        primary = StabilityVerifier(
            children["stability_primary_diagnostics"], scope=self.scope
        )
        primary.verify_sources()
        primary.verify_math()
        same(s["evaluation_dates"], primary.dates, "Evaluation dates")
        require(
            s["source_calendar_dates"] == len(primary.market["dates"]),
            "Source calendar differs",
        )
        family = primary.summary["inference"][3]
        matched = decode(
            (children["stability_extension_study"] / "summary.json").read_bytes()
        )["matched_inference"][3]
        for key, value in dict(
            primary_family_available=family["family_available"],
            primary_coverage_adequate=family["all_endpoints_adequate"],
            matched_family_available=matched["family_available"],
            matched_coverage_adequate=matched["all_endpoints_adequate"],
        ).items():
            same(s[key], value, "Family/coverage qualification")
        full = family["family_available"] and family["all_endpoints_adequate"]
        expected = (
            "constructed_pipeline_check_no_market_claim"
            if self.scope.synthetic
            else (
                "descriptive_only_controlled_size_excess"
                if s["calibration_size_excess_detected"]
                else (
                    "input_limited_complete_family_unavailable"
                    if not full
                    else "registered_bounds_with_assumption_qualifications_no_automatic_advancement"
                )
            )
        )
        require(s["assessment"] == expected, "Qualification overstates evidence")
        text = (d / "report.html").read_text()
        require(digest(d / "report.html") == s["report_sha256"], "Report changed")
        marks = CoverageMarks(text).marks
        require(
            set(marks) == {e["id"] for e in family["endpoints"]},
            "Coverage matrix differs",
        )
        for e in family["endpoints"]:
            bar = marks[e["id"]]
            require(
                math.isclose(
                    float(bar["width"]),
                    500 * e["observed_dates"] / e["calendar_dates"],
                    rel_tol=5e-11,
                    abs_tol=1e-8,
                )
                and int(bar["data-observed"]) == e["observed_dates"],
                "Coverage geometry/count differs",
            )
        for c in s["children"]:
            require(f'href="{c["path"]}/report.html"' in text, "Report link missing")
        if self_check:
            original = primary.summary["inference"][3]["endpoints"][0]["estimate"]
            primary.summary["inference"][3]["endpoints"][0]["estimate"] = 1.0
            try:
                primary.verify_inference_result(3)
            except ValueError:
                checks += 1
            else:
                raise ValueError("Changed primary estimate accepted")
            primary.summary["inference"][3]["endpoints"][0]["estimate"] = original
            print(
                f"Evaluation semantic checks passed: {checks}; artifact hashes bypassed.",
                flush=True,
            )
        receipt = dict(
            kind="panel_evaluation_verification",
            status="verified",
            summary_sha256=digest(d / "summary.json"),
            validation_protocol_sha256=POLICY,
            source_scope=(
                "independent_analytic_source_reconstruction"
                if self.scope.synthetic
                else "independent_raw_provider_authentication"
            ),
            primary_series=77,
            secondary_and_matched_series=345,
            registered_variants=14,
            evaluation_dates=len(primary.dates),
            semantic_checks=checks,
            calibration_size_excess_detected=s["calibration_size_excess_detected"],
            final_strategy_inputs_opened=s["final_strategy_inputs_opened"],
        )
        if self.scope.year == 2025:
            receipt["verifier_sha256"] = digest(Path(__file__))
            receipt["runner_source"] = "verified_M8_frozen_copy"
            maintenance = freeze / "maintenance_verification.json"
            if maintenance.exists():
                note = decode(maintenance.read_bytes())
                require(
                    note["kind"] == "payment_date_implementation_maintenance"
                    and note["status"] == "verified"
                    and note["replacement_summary_sha256"]
                    == f["validation_summary_sha256"]
                    and note["original_freeze_sha256"]
                    == digest(Path(note["original_freeze_directory"]) / "freeze.json"),
                    "Maintenance parity receipt differs",
                )
                receipt["maintenance_verification_sha256"] = digest(maintenance)
        (d / "verification.json").write_text(json.dumps(receipt, indent=2) + "\n")
        with zipfile.ZipFile(d / "review.zip", "w", zipfile.ZIP_DEFLATED) as z:
            if self.scope.year == 2025:
                z.write(freeze / "freeze.json", "frozen_record.json")
                if maintenance.exists():
                    z.write(maintenance, "maintenance_verification.json")
            for name in [
                "summary.json",
                "report.html",
                "verification.json",
                "plan.json",
                "access_checks.json",
                "source_inventory.txt",
            ]:
                z.write(d / name, name)
            for c in s["children"]:
                child = d / c["path"]
                cs = decode((child / "summary.json").read_bytes())
                names = {"summary.json", "report.html"} | {
                    a["path"]
                    for a in cs["artifacts"]
                    if not a["path"].endswith(".jsonl")
                    or a["path"] == "observations.jsonl"
                }
                for name in names:
                    z.write(child / name, f'{c["path"]}/{name}')
        print(
            f"Verified integrated {self.scope.year} evaluation: 77 primary and 345 secondary/matched series, {len(primary.dates)} dates; {expected}."
        )
        print("Review bundle:", d / "review.zip")
        return receipt


def verify_freeze(directory):
    d = Path(directory).resolve()
    f = decode((d / "freeze.json").read_bytes())
    require(
        f["kind"] == "panel_evaluation_freeze"
        and f["validation_protocol_sha256"] == POLICY
        and f["extension_protocol_sha256"] == PROTOCOL,
        "Freeze registration differs",
    )
    require(
        f["study_manifest_sha256"]
        == "edeee4275c9a01df3859f6c5090073c2b496df31e625e3bf6eed79bee7a8715c",
        "Frozen input registration differs",
    )
    v = Path(f["validation_directory"])
    s = decode((v / "summary.json").read_bytes())
    r = decode((v / "verification.json").read_bytes())
    require(
        digest(v / "summary.json")
        == f["validation_summary_sha256"]
        == r["summary_sha256"]
        and digest(v / "verification.json") == f["validation_receipt_sha256"]
        and r["status"] == "verified",
        "Frozen validation changed",
    )
    require(
        s["evaluation_scope"]["year"] == 2024
        and s["evidence_origin"] == f["evidence_origin"]
        and s["source_sha256"] == f["source_sha256"],
        "Frozen source scope differs",
    )
    p = d / f["runner"]
    require(
        p.parent == d
        and digest(p) == f["runner_sha256"] == s["runner_sha256"]
        and digest(d / "source_inventory.txt")
        == f["source_inventory_sha256"]
        == f["source_sha256"],
        "Frozen executable/inventory changed",
    )
    require(
        not any(
            f[k]
            for k in (
                "rules_selected_from_validation",
                "parameters_changed",
                "final_strategy_inputs_opened",
            )
        ),
        "Freeze contains selection or final access",
    )
    c = Path(f["calibration_directory"])
    require(
        digest(c / "summary.json")
        == f["calibration_summary_sha256"]
        == s["evaluation_scope"]["authority_sha256"]
        and f["calibration_size_excess_detected"]
        == s["calibration_size_excess_detected"],
        "Frozen calibration changed",
    )
    (d / "verification.json").write_text(
        json.dumps(
            dict(
                kind="panel_evaluation_freeze_verification",
                status="verified",
                freeze_sha256=digest(d / "freeze.json"),
                runner_sha256=f["runner_sha256"],
                final_strategy_inputs_opened=False,
            ),
            indent=2,
        )
        + "\n"
    )
    print(
        "Verified immutable executable/specification freeze; 2025 strategy inputs opened: false."
    )
    print("Freeze:", d / "freeze.json")


def compare_validation(directory, original_freeze):
    original_freeze = Path(original_freeze).resolve()
    verify_freeze(original_freeze)
    frozen = decode((original_freeze / "freeze.json").read_bytes())
    original = Path(frozen["validation_directory"])
    replacement = Path(directory).resolve()
    old, new = [
        decode((p / "summary.json").read_bytes()) for p in (original, replacement)
    ]
    for p, s in ((original, old), (replacement, new)):
        receipt = decode((p / "verification.json").read_bytes())
        require(
            receipt["status"] == "verified"
            and receipt["summary_sha256"] == digest(p / "summary.json")
            and s["kind"] == "panel_evaluation"
            and s["evaluation_scope"]["year"] == 2024
            and not s["final_strategy_inputs_opened"],
            "Maintenance requires two verified 2024 evaluations",
        )
    for key in (
        "evaluation_scope",
        "validation_protocol_sha256",
        "protocol_sha256",
        "compiler",
        "build_configuration",
        "calibration_directory",
        "evaluation_dates",
        "source_calendar_dates",
        "assessment",
    ):
        same(new[key], old[key], "Maintenance " + key)
    children = [{c["name"]: c for c in s["children"]} for s in (old, new)]
    require(
        len(children[0]) == len(children[1]) == 4
        and children[0].keys() == children[1].keys(),
        "Maintenance child matrix differs",
    )
    count = 0
    for name in children[0]:
        paths = [
            base / table[name]["path"]
            for base, table in ((original, children[0]), (replacement, children[1]))
        ]
        summaries = []
        for p, table in zip(paths, children):
            require(
                digest(p / "summary.json") == table[name]["summary_sha256"],
                "Maintenance child identity differs",
            )
            summaries.append(decode((p / "summary.json").read_bytes()))
        ignored = {"source_sha256", "report_sha256", "artifacts", "sources"}
        same(
            *[{k: v for k, v in s.items() if k not in ignored} for s in summaries],
            "Maintenance financial summary " + name,
        )
        artifacts = [{a["path"]: a for a in s["artifacts"]} for s in summaries]
        require(
            artifacts[0].keys() == artifacts[1].keys(), "Maintenance artifacts differ"
        )
        for path in artifacts[0]:
            for p, table in zip(paths, artifacts):
                require(
                    digest(p / path) == table[path]["sha256"],
                    "Maintenance artifact changed",
                )
            require(
                artifacts[0][path]["sha256"] == artifacts[1][path]["sha256"],
                "Maintenance financial records differ: " + name + "/" + path,
            )
            count += 1
    note = dict(
        kind="payment_date_implementation_maintenance",
        status="verified",
        original_freeze_directory=str(original_freeze),
        original_freeze_sha256=digest(original_freeze / "freeze.json"),
        original_summary_sha256=digest(original / "summary.json"),
        replacement_summary_sha256=digest(replacement / "summary.json"),
        identical_financial_artifacts=count,
        financial_summaries=4,
        final_strategy_inputs_opened=False,
    )
    (replacement / "maintenance_verification.json").write_text(
        json.dumps(note, indent=2) + "\n"
    )
    print(
        f"Maintenance parity passed: {count} byte-identical financial artifacts and four scientific summaries; no 2025 strategy scoring."
    )


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--self-check", action="store_true")
    mode = p.add_mutually_exclusive_group()
    mode.add_argument("--freeze", action="store_true")
    mode.add_argument("--compare-validation", type=Path, metavar="ORIGINAL_FREEZE_DIR")
    a = p.parse_args()
    try:
        if a.compare_validation:
            compare_validation(a.directory, a.compare_validation)
        elif a.freeze:
            verify_freeze(a.directory)
        else:
            EvaluationVerifier(a.directory).run(a.self_check)
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
        print("Panel evaluation verification failed:", e)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
