"""Extract the bounded SPY distribution reference from the issuer's workbook."""

import argparse
from datetime import datetime
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET
from zipfile import ZipFile


class IssuerDistributionExtractor:
    namespace = {"s": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
    source_url = "https://www.ssga.com/library-content/products/fund-data/etfs/us/spdr-etf-historical-distributions.xlsx"

    def extract(self, source: Path, retrieved_utc: str) -> dict:
        datetime.strptime(retrieved_utc, "%Y-%m-%dT%H:%M:%SZ")
        with ZipFile(source) as archive:
            shared = ["".join(item.itertext()) for item in ET.fromstring(archive.read("xl/sharedStrings.xml"))]
            rows = ET.fromstring(archive.read("xl/worksheets/sheet1.xml")).findall(".//s:row", self.namespace)
            result = []
            for row in rows:
                values = {}
                for cell in row:
                    value = cell.find("s:v", self.namespace)
                    text = value.text if value is not None else "".join(cell.itertext())
                    if cell.get("t") == "s":
                        text = shared[int(text)]
                    values["".join(c for c in cell.attrib["r"] if c.isalpha())] = text.strip()
                if row.attrib["r"] == "1":
                    expected = {"B": "TICKER", "C": "CUSIP", "D": "EX-DATE", "E": "RECORD DATE",
                                "F": "PAYABLE DATE", "G": "DIVIDEND ($)", "H": "SHORT TERM CAPITAL GAIN ($)",
                                "I": "LONG TERM CAPITAL GAIN ($)"}
                    if any(values.get(key) != label for key, label in expected.items()):
                        raise ValueError("Issuer worksheet columns differ from the extraction contract")
                    continue
                if values.get("B") != "SPY":
                    continue
                ex = datetime.strptime(values["D"], "%m/%d/%Y").date().isoformat()
                if not "2021-01-01" <= ex <= "2023-12-31":
                    continue
                if values["C"] != "78462F103" or any(Decimal(values.get(c) or "0") != 0 for c in ["H", "I"]):
                    raise ValueError("Unexpected security identifier or capital-gain distribution")
                amount = Decimal(values["G"])
                if not amount.is_finite() or amount <= 0:
                    raise ValueError("Invalid issuer dividend amount")
                record = datetime.strptime(values["E"], "%m/%d/%Y").date().isoformat()
                pay = datetime.strptime(values["F"], "%m/%d/%Y").date().isoformat()
                if not ex <= record <= pay:
                    raise ValueError("Invalid issuer dividend chronology")
                result.append({"ex_date": ex, "record_date": record, "payable_date": pay,
                               "usd_per_share": float(amount), "worksheet_row": int(row.attrib["r"])})
        result.sort(key=lambda row: row["ex_date"])
        if len(result) != 12 or len({row["ex_date"] for row in result}) != 12:
            raise ValueError("Expected twelve distinct quarterly ex-dates in the reference horizon")
        return {"schema_version": 1, "symbol": "SPY", "cusip": "78462F103",
                "source_url": self.source_url, "source_file_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "retrieved_utc": retrieved_utc, "worksheet": "dividend",
                "ex_date_first": "2021-01-01", "ex_date_last": "2023-12-31", "cash_distributions": result}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--retrieved-utc", required=True)
    args = parser.parse_args()
    reference = IssuerDistributionExtractor().extract(args.source, args.retrieved_utc)
    with args.output.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(reference, indent=2) + "\n")


if __name__ == "__main__":
    main()
