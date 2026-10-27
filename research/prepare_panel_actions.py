"""Prepare immutable issuer cash records and free full-horizon corporate-action cross-checks."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
from urllib.parse import quote
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
        return self.collect_resource(f"sealed_sources/{symbol}{suffix}", self.urls[symbol])

    def collect_resource(self, relative: str, url: str) -> dict:
        path = self.directory / relative
        receipt = path.with_suffix(path.suffix + ".provenance.json")
        if not path.exists():
            if receipt.exists():
                raise ValueError("Source provenance exists without its source file")
            request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 AlgorithmicTradingRuntime/1.0"})
            with urllib.request.urlopen(request, timeout=45) as stream:
                data = stream.read(20 * 1024 * 1024 + 1)
            if len(data) > 20 * 1024 * 1024:
                raise ValueError("Action source exceeds its 20-MiB bound")
            record = {"file": relative, "url": url, "sha256": hashlib.sha256(data).hexdigest(),
                      "retrieved_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")}
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open("xb") as stream:
                stream.write(data)
            with receipt.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(json.dumps(record, indent=2) + "\n")
        record = json.loads(receipt.read_text())
        if record["file"] != relative or record["url"] != url or hashlib.sha256(path.read_bytes()).hexdigest() != record["sha256"]:
            raise ValueError("Action source provenance differs")
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
        policy_file = Path(__file__).resolve().parent / "extension_source_resolution.json"
        policy = json.loads(policy_file.read_bytes())
        policy_sha = hashlib.sha256(policy_file.read_bytes()).hexdigest()
        records, distributions, coverage = [], [], []
        for symbol in ("SPY", "QQQ", "IWM", "TLT"):
            print(f"{symbol}: authenticating issuer source; existing cached files are retained.", flush=True)
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
            print(f"{symbol}: retained {len(bounded)} issuer cash records.")
        gld = sources.collect_resource("sealed_sources/GLD.html", policy["gld_issuer_url"])
        if policy["gld_issuer_statement"] not in (self.directory / gld["file"]).read_text(encoding="utf-8"):
            raise ValueError("GLD issuer income statement is unavailable")
        records.append(gld)
        coverage.append({"symbol": "GLD", "cash_complete": True, "splits_complete": False,
                         "source_files": [gld["file"]], "coverage_locator": "issuer income statement plus full-horizon event cross-check"})
        for row in coverage:
            symbol = row["symbol"]
            print(f"{symbol}: authenticating 2016-2025 secondary event coverage.", flush=True)
            record = sources.collect_resource(f"sealed_sources/{symbol}_events.json",
                                              policy["secondary_event_url"].replace("{symbol}", quote(symbol, safe="")))
            record.update(kind="secondary_events", symbol=symbol)
            records.append(record)
            body = json.loads((self.directory / record["file"]).read_bytes())
            chart = body["chart"]
            if chart["error"] is not None or len(chart["result"]) != 1:
                raise ValueError("Secondary event response is incomplete")
            result = chart["result"][0]
            expected_months = {f"{year}-{month:02}" for year in range(2016, 2026) for month in range(1, 13)}
            months = [datetime.fromtimestamp(t, timezone.utc).strftime("%Y-%m") for t in result["timestamp"]]
            if (result["meta"]["symbol"] != symbol or result["meta"]["currency"] != "USD"
                    or result["meta"]["dataGranularity"] != "1mo"
                    or len(months) != policy["secondary_months"] or set(months) != expected_months):
                raise ValueError("Secondary event symbol, currency or horizon mismatch")
            events = result.get("events", {})
            if events.get("splits"):
                raise ValueError(f"{symbol}: nonempty secondary split history requires reconciliation")
            if symbol == "GLD" and (events.get("dividends") or events.get("capitalGains")):
                raise ValueError("GLD secondary history contradicts zero cash distributions")
            row["splits_complete"] = True
            row["source_files"].append(record["file"])
            row["coverage_locator"] += "; 120 monthly coverage timestamps and zero returned split events"
        result = {"schema_version": 2, "evidence_origin": "provider", "first_ex_date": "2016-01-01", "last_ex_date": "2025-12-31",
                  "source_resolution_sha256": policy_sha, "sources": records, "coverage": coverage,
                  "cash_distributions": sorted(distributions, key=lambda r: (r["symbol"], r["ex_date"])), "splits": []}
        # The original incomplete reference and every existing raw receipt remain intact.
        path = self.directory / "reference_resolved.json"
        text = json.dumps(result, indent=2) + "\n"
        if path.exists():
            if path.read_text() != text:
                raise ValueError("Existing resolved reference differs; reviewed evidence is never overwritten")
        else:
            with path.open("x", encoding="utf-8", newline="\n") as stream:
                stream.write(text)
        print("Action reference ready: issuer cash authority, five full-horizon split cross-checks and GLD cash coverage.")
        print("Next: run panel_finalize. Existing prices and the original issuer reference are retained.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=Path(__file__).resolve().parents[1] / "scratch/datasets/extension_panel_2016_2025/issuer")
    args = parser.parse_args()
    PanelIssuerPreparation(args.directory.resolve()).run()


if __name__ == "__main__":
    main()
