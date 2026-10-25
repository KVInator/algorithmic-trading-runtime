"""Verify the complete offline demonstration through independent saved-state oracles."""

import hashlib
import json
from pathlib import Path
import sys

from verify_paper_broker import PaperBrokerVerifier
from verify_strategy_execution import StrategyExecutionVerifier


class ProjectDemoVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()

    def verify(self):
        summary = json.loads((self.directory / "summary.json").read_text())
        assert (
            summary["evidence_origin"] == "synthetic"
            and summary["network_requests"] == 0
        )
        assert summary["cases"] == 24 and summary["checks"] == 259
        for artifact in summary["artifacts"]:
            path = (self.directory / artifact["path"]).resolve()
            assert path.is_relative_to(self.directory)
            assert hashlib.sha256(path.read_bytes()).hexdigest() == artifact["sha256"]
        StrategyExecutionVerifier(self.directory / "strategies").verify()
        PaperBrokerVerifier(self.directory / "broker").verify()
        print(
            f"Verified complete offline demo: {len(summary['artifacts'])} artifact hashes, 24 cases"
        )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: python -B research/verify_project_demo.py <demo-directory>"
        )
    ProjectDemoVerifier(sys.argv[1]).verify()
