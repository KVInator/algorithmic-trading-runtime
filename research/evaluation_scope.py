"""Explicit verifier scope; default readers remain barred from later prices."""

from pathlib import Path
from verify_historical_panel import PanelVerifier, require

POLICY = "587ff9244737e15d61ab91c4dff4a19bfd9cbbe546788e742cc684388e251b54"


class EvaluationScope:
    def __init__(self, document):
        self.document = document
        self.year = document["year"]
        self.synthetic = document["evidence_origin"] == "synthetic"
        require(
            self.year in (2024, 2025)
            and document["stage"]
            == ("M8_validation" if self.year == 2024 else "M9_final"),
            "Evaluation stage differs",
        )
        self.first = "2022-01-01" if self.synthetic else "2016-01-01"
        self.last = f"{self.year}-12-31"
        self.scored_first = f"{self.year}-01-01"
        self.unit_first = PanelVerifier(Path(".")).calendar(
            f"{self.year-2}-01-01", f"{self.year-1}-12-31"
        )[-252]["date"]
        require(
            document["first_input"] == self.first
            and document["last_input"] == self.last
            and document["first_scored"] == self.scored_first
            and document["unit_reference_first"] == self.unit_first,
            "Evaluation calendar/cohort differs",
        )
        require(
            len(document["authority_sha256"]) == 64
            and document["fresh_evaluation_capital"]
            and document["primary_rules_replaced"] is False,
            "Evaluation authority/rules differ",
        )

    def scored(self, date):
        return self.scored_first <= date <= self.last

    def authorized(self, date):
        return self.first <= date <= self.last

    def unit(self, date):
        return self.unit_first <= date <= self.last

    def check(self, s):
        require(
            s.get("evaluation_scope") == self.document
            and s.get("validation_protocol_sha256") == POLICY
            and s["reserved_strategy_inputs_opened"] is True,
            "Scoped evidence differs",
        )


def scored(date, scope=None):
    return scope.scored(date) if scope else "2016-12-31" < date < "2024"


def authorized(date, scope=None):
    return scope.authorized(date) if scope else "2016-01-01" <= date < "2024"


def scope_claim(s, scope=None):
    if scope:
        scope.check(s)
        return True
    return (
        s.get("reserved_strategy_inputs_opened") is False
        and "evaluation_scope" not in s
    )
