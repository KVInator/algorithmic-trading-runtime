"""Prepare cash-distribution evidence from free issuer sources; split coverage stays unverified."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
from decimal import Decimal
from html.parser import HTMLParser
import hashlib
import json
from pathlib import Path
import urllib.request

from extract_distribution_reference import IssuerDistributionExtractor


class IssuerSources:
    urls = {
        "SPY": IssuerDistributionExtractor.source_url,
        "IWM": "https://www.ishares.com/us/products/239710/ishares-russell-2000-etf",
        "TLT": "https://www.ishares.com/us/products/239454/ishares-20-year-treasury-bond-etf",
        "QQQ": "https://dng-api.invesco.com/cache/v1/accounts/en_US/shareclasses/46090E103/distribution?idType=cusip&productType=ETF",
    }

    def __init__(self, directory: Path):
        self.directory = directory

    def collect(self, symbol: str) -> dict:
        suffix = ".xlsx" if symbol == "SPY" else ".json" if symbol == "QQQ" else ".html"
        relative = f"sealed_sources/{symbol}{suffix}"
        path = self.directory / relative
        receipt = path.with_suffix(path.suffix + ".provenance.json")
        if not path.exists():
            if receipt.exists():
                raise ValueError("Issuer provenance exists without its source file")
            request = urllib.request.Request(self.urls[symbol], headers={"User-Agent": "AlgorithmicTradingRuntime/1.0"})
            with urllib.request.urlopen(request, timeout=45) as stream:
                data = stream.read(20 * 1024 * 1024 + 1)
            if len(data) > 20 * 1024 * 1024:
                raise ValueError("Issuer source exceeds its 20-MiB bound")
            record = {"file": relative, "url": self.urls[symbol], "sha256": hashlib.sha256(data).hexdigest(),
                      "retrieved_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open("xb") as stream:
                stream.write(data)
            with receipt.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(json.dumps(record, indent=2) + "\n")
        record = json.loads(receipt.read_text())
        if record["file"] != relative or record["url"] != self.urls[symbol] or hashlib.sha256(path.read_bytes()).hexdigest() != record["sha256"]:
            raise ValueError("Issuer source provenance differs")
        return record


class ISharesDistributionTable(HTMLParser):
    def __init__(self):
        super().__init__()
        self.tables = []

    def handle_starttag(self, tag, attrs):
        value = dict(attrs).get("componentprops")
        if value is None:
            return
        doc = json.loads(value)
        if doc.get("componentId") == "performance" and "containersByNameMap" in doc:
            table = doc["containersByNameMap"]["distributions"]["subContainersByNameMap"]["table"]
            self.tables.append(table["dataPointsByNameMap"])

    def extract(self, source: Path) -> list[dict]:
        self.feed(source.read_text(encoding="utf-8"))
        if len(self.tables) != 1:
            raise ValueError("Expected one identified issuer distribution table")
        columns = {key: self.tables[0][key]["value"] for key in ("exDate", "recordDate", "payableDate", "totalDistribution")}
        if len({len(v) for v in columns.values()}) != 1:
            raise ValueError("Issuer distribution columns have different lengths")
        def day(value):
            return datetime.strptime(str(value), "%Y%m%d").date().isoformat()
        return [{"ex_date": day(columns["exDate"][i]), "record_date": day(columns["recordDate"][i]),
                 "payable_date": day(columns["payableDate"][i]), "usd_per_share": columns["totalDistribution"][i],
                 "source_locator": f"performance.distributions.table row {i}"} for i in range(len(columns["exDate"]))]


class PanelIssuerPreparation:
    def __init__(self, directory: Path):
        self.directory = directory

    def run(self):
        sources = IssuerSources(self.directory)
        records, distributions, coverage = [], [], []
        for symbol in ("SPY", "QQQ", "IWM", "TLT"):
            record = sources.collect(symbol)
            records.append(record)
            source = self.directory / record["file"]
            if symbol == "SPY":
                table = IssuerDistributionExtractor().table(source)
                header = table[0][1]
                if any(header.get(k) != v for k, v in {"B": "TICKER", "C": "CUSIP", "D": "EX-DATE", "E": "RECORD DATE", "F": "PAYABLE DATE"}.items()):
                    raise ValueError("SSGA distribution header changed")
                rows = []
                for number, row in table[1:]:
                    if row.get("B") != "SPY":
                        continue
                    if row["C"] != "78462F103":
                        raise ValueError("SSGA security identifier mismatch")
                    def day(value):
                        return datetime.strptime(value, "%m/%d/%Y").date().isoformat()
                    rows.append({"ex_date": day(row["D"]), "record_date": day(row["E"]), "payable_date": day(row["F"]),
                                 "usd_per_share": sum(Decimal(row.get(k) or "0") for k in ("G", "H", "I")),
                                 "source_locator": f"dividend worksheet row {number}; cash and capital gains summed"})
            elif symbol in {"IWM", "TLT"}:
                rows = ISharesDistributionTable().extract(source)
            else:
                body = json.loads(source.read_text(), parse_float=Decimal)
                if body["cusip"] != "46090E103" or body["currencyCode"] != "USD":
                    raise ValueError("QQQ issuer identifier/currency mismatch")
                rows = [{"ex_date": r["exDate"], "record_date": r["recordDate"], "payable_date": r["payDate"],
                         "usd_per_share": r["distributionAmountPerUnit"], "source_locator": f"distributions row {i}"}
                        for i, r in enumerate(body["distributions"])]
            dates = [r["ex_date"] for r in rows]
            bounded = []
            for row in rows:
                if not "2016-01-01" <= row["ex_date"] <= "2025-12-31":
                    continue
                rate = Decimal(str(row["usd_per_share"]))
                if not rate.is_finite() or rate <= 0 or not row["ex_date"] <= row["record_date"] <= row["payable_date"]:
                    raise ValueError("Invalid issuer amount or chronology")
                bounded.append({**row, "usd_per_share": float(rate), "symbol": symbol, "source_file": record["file"]})
            if len({r["ex_date"] for r in bounded}) != len(bounded):
                raise ValueError("Issuer rows need an explicit same-date component reconciliation")
            distributions.extend(bounded)
            coverage.append({"symbol": symbol, "cash_complete": min(dates) < "2016-01-01" and max(dates) > "2025-12-31",
                             "splits_complete": False, "source_files": [record["file"]], "coverage_locator": "full returned issuer cash-distribution table; does not establish split history"})
            print(f"{symbol}: retained {len(bounded)} issuer cash records; split coverage unverified.")
        coverage.append({"symbol": "GLD", "cash_complete": False, "splits_complete": False, "source_files": [], "coverage_locator": "independent full-horizon evidence still required"})
        result = {"schema_version": 1, "evidence_origin": "provider", "first_ex_date": "2016-01-01", "last_ex_date": "2025-12-31",
                  "sources": records, "coverage": coverage, "cash_distributions": sorted(distributions, key=lambda r: (r["symbol"], r["ex_date"])), "splits": []}
        path = self.directory / "reference.json"
        text = json.dumps(result, indent=2) + "\n"
        if path.exists():
            if path.read_text() != text:
                raise ValueError("Existing issuer reference differs; reviewed evidence is never overwritten")
        else:
            with path.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(text)
        print("Cash reference prepared. GLD and independent split coverage remain unresolved; finalization stays blocked.")
        print("Keep issuer sources and the reference local; share only coverage/reconciliation summaries.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=Path(__file__).resolve().parents[1] / "scratch/datasets/extension_panel_2016_2025/issuer")
    args = parser.parse_args()
    PanelIssuerPreparation(args.directory.resolve()).run()


if __name__ == "__main__":
    main()
