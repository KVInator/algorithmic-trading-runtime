"""Independently replay synthetic portfolio broker and shared-client journals."""

import argparse
from collections import Counter
from copy import deepcopy
from datetime import date
import hashlib
import json
from pathlib import Path
import re
import zipfile

from verify_historical_capability import utc_clock
from verify_shared_capital import AccountAudit, frames, integer_fields, require

SYMBOLS = ["GLD", "IWM", "QQQ", "SPY", "TLT"]
FEE = 10000
INITIAL = 10**14
POLICY = "1f9a6198eb444932b943aee788bdbfc0957e2a335efe016d23126401c20c46ea"
FAMILIES = {"protocol", "fifo", "day", "ioc", "gtc", "replacement", "rejection", "delivery", "ambiguous", "client_restart", "broker_restart", "storage"}
READS = {"hello", "snapshot", "activities", "lookup"}
COMMAND_FIELDS = {
    "session": {"kind", "command_id", "at_seconds", "date"},
    "quote": {"kind", "command_id", "at_seconds", "quote"},
    "submit": {"kind", "command_id", "at_seconds", "order", "time_in_force"},
    "cancel": {"kind", "command_id", "at_seconds", "order_id", "cancel_id"},
    "advance": {"kind", "command_id", "at_seconds"},
    "complete_cancels": {"kind", "command_id", "at_seconds"},
}


class BrokerAudit:
    """Independent FIFO, finite liquidity, lifecycle and global cash model."""

    def __init__(self, header):
        require(header == dict(kind="header", schema_version=5, model_contract="synthetic_portfolio_broker_v1", policy_sha256=POLICY,
                               account_id=header["account_id"], sequence=0, previous_sha256="0" * 64), "Peer header differs")
        self.account = header["account_id"]
        self.cash, self.at, self.opening, self.close = INITIAL, 0, 0, 0
        self.day = ""
        self.positions = dict.fromkeys(SYMBOLS, 0)
        self.orders, self.quotes, self.quote_history, self.commands, self.cancels = {}, {}, {}, {}, {}
        self.pending, self.activities, self.fills = [], [], []

    def reserved(self):
        return sum((o["order"]["shares"] - o["filled_shares"]) * (o["order"]["limit_units"] + o["order"]["fee_per_share_ceiling_units"])
                   for o in self.orders.values() if o["status"] == "open" and o["order"]["side"] == "buy")

    def emit(self, identity, kind, execution="", q=0, price=0, fee=0, cumulative=0, cancel=""):
        self.activities.append(dict(cursor=len(self.activities) + 1, at_seconds=self.at, broker_order_id=self.orders[identity]["broker_order_id"],
                                    report=dict(event_id="SIM-E" + str(len(self.activities) + 1), order_id=identity, report_kind=kind, execution_id=execution,
                                                fill_shares=q, fill_price_units=price, fee_units=fee, cumulative_filled_shares=cumulative, cancel_id=cancel)))

    def advance(self, at):
        require(at > 0 and at >= self.at, "Peer clock regressed")
        self.at = at
        for identity, o in self.orders.items():
            if o["status"] == "open" and o["time_in_force"] == "day" and self.at >= o["expires_at_seconds"]:
                o["status"] = "canceled"
                self.emit(identity, "canceled", cumulative=o["filled_shares"])

    def match(self, symbol):
        if symbol not in self.quotes or self.at - self.quotes[symbol]["source_at_seconds"] > 2:
            return
        quote = self.quotes[symbol]
        for identity, o in self.orders.items():
            r = o["order"]
            if o["status"] != "open" or r["symbol"] != symbol:
                continue
            buy = r["side"] == "buy"
            side = "ask" if buy else "bid"
            price = quote[side + "_units"]
            if (buy and price > r["limit_units"]) or (not buy and price < r["limit_units"]):
                continue
            q = min(r["shares"] - o["filled_shares"], quote[side + "_remaining"])
            if not q:
                continue
            fee = q * FEE
            self.cash += (-1 if buy else 1) * q * price - fee
            self.positions[symbol] += q if buy else -q
            o["filled_shares"] += q
            quote[side + "_remaining"] -= q
            if o["filled_shares"] == r["shares"]:
                o["status"] = "filled"
            execution = "SIM-X" + str(len(self.fills) + 1)
            self.fills.append(dict(order_id=identity, execution_id=execution, symbol=symbol, shares=q, price_units=price, fee_units=fee, quote_sequence=quote["sequence"]))
            self.emit(identity, "fill", execution=execution, q=q, price=price, fee=fee)
            self.invariants()

    def invariants(self):
        require(self.cash >= self.reserved() >= 0 and all(q >= 0 for q in self.positions.values()), "Independent broker borrowing or negative inventory")
        for symbol in SYMBOLS:
            reserved = sum(o["order"]["shares"] - o["filled_shares"] for o in self.orders.values()
                           if o["status"] == "open" and o["order"]["side"] == "sell" and o["order"]["symbol"] == symbol)
            require(reserved <= self.positions[symbol], "Independent sell reservation exceeds inventory")

    def apply(self, c):
        kind, at = c["kind"], c["at_seconds"]
        require(set(c) == COMMAND_FIELDS.get(kind), "Noncanonical broker command fields")
        require(re.fullmatch(r"[A-Za-z0-9_-]{1,96}", c["command_id"]), "Invalid command identity")
        if kind == "session":
            require(c["date"] in {"2023-01-03", "2023-01-04"} and c["date"] > self.day, "Unexpected constructed session")
            d = date.fromisoformat(c["date"])
            require(at == utc_clock(d, 9, 30) // 10**9, "Session opening differs")
            self.day, self.opening, self.close = c["date"], at, utc_clock(d, 16, 0) // 10**9
            self.advance(at)
        elif kind == "quote":
            require(self.opening <= at < self.close, "Quote outside open session")
            q = c["quote"]
            symbol, seq = q["symbol"], q["sequence"]
            require(symbol in SYMBOLS and seq > 0 and 0 < q["source_at_seconds"] <= at and q["bid_units"] > 0
                    and q["ask_units"] >= q["bid_units"] and min(q["bid_shares"], q["ask_shares"]) >= 0, "Invalid independent quote")
            if (symbol, seq) in self.quote_history:
                require(self.quote_history[(symbol, seq)] == q, "Conflicting quote identity")
                self.advance(at)
                return
            require(symbol not in self.quotes or seq > self.quotes[symbol]["sequence"], "Quote sequence regressed")
            require(len(self.quote_history) < 4096, "Quote bound")
            self.advance(at)
            self.quote_history[(symbol, seq)] = deepcopy(q)
            self.quotes[symbol] = dict(**q, bid_remaining=q["bid_shares"], ask_remaining=q["ask_shares"])
            self.match(symbol)
        elif kind == "submit":
            require(self.opening <= at < self.close, "Submit outside open session")
            r, tif = c["order"], c["time_in_force"]
            require(r["symbol"] in SYMBOLS and r["shares"] > 0 and r["limit_units"] > 0 and r["fee_per_share_ceiling_units"] >= 0
                    and r["side"] in {"buy", "sell"} and tif in {"day", "ioc", "gtc"}, "Invalid independent order")
            expiry = self.close if tif == "day" else 0
            identity = r["id"]
            if identity in self.orders:
                o = self.orders[identity]
                require(o["order"] == r and o["time_in_force"] == tif and o["expires_at_seconds"] == expiry, "Conflicting client order identity")
                return
            require(len(self.orders) < 1024 and r["shares"] * (r["limit_units"] + r["fee_per_share_ceiling_units"]) < 2**63, "Order/product bound")
            self.advance(at)
            accept = (r["fee_per_share_ceiling_units"] >= FEE and r["limit_units"] > FEE
                      and sum(o["status"] == "open" for o in self.orders.values()) < 32)
            if r["side"] == "buy":
                accept = accept and r["shares"] * (r["limit_units"] + r["fee_per_share_ceiling_units"]) <= self.cash - self.reserved()
            else:
                held = self.positions[r["symbol"]]
                reserved = sum(o["order"]["shares"] - o["filled_shares"] for o in self.orders.values()
                               if o["status"] == "open" and o["order"]["symbol"] == r["symbol"] and o["order"]["side"] == "sell")
                accept = accept and r["shares"] <= held - reserved
            self.orders[identity] = dict(order=r, broker_order_id="B5-O" + str(len(self.orders) + 1), filled_shares=0,
                                         status="open" if accept else "rejected", time_in_force=tif, expires_at_seconds=expiry)
            self.emit(identity, "accepted" if accept else "rejected")
            if accept:
                self.match(r["symbol"])
                o = self.orders[identity]
                if tif == "ioc" and o["status"] == "open":
                    o["status"] = "canceled"
                    self.emit(identity, "canceled", cumulative=o["filled_shares"])
        elif kind == "cancel":
            identity, cancel = c["order_id"], c["cancel_id"]
            require(identity in self.orders and cancel, "Unknown cancel order")
            if cancel in self.cancels:
                require(self.cancels[cancel] == identity, "Conflicting cancel identity")
                return
            self.advance(at)
            self.cancels[cancel] = identity
            self.pending.append((cancel, identity))
        elif kind in {"advance", "complete_cancels"}:
            self.advance(at)
            if kind == "complete_cancels":
                for cancel, identity in self.pending:
                    o = self.orders[identity]
                    if o["status"] == "open":
                        o["status"] = "canceled"
                        self.emit(identity, "canceled", cumulative=o["filled_shares"], cancel=cancel)
                    else:
                        self.emit(identity, "cancel_rejected", cancel=cancel)
                self.pending.clear()
        else:
            raise ValueError("Unknown independent mutation")
        self.invariants()

    def step(self, c, reply):
        identity = c["command_id"]
        if identity in self.commands:
            expected = self.commands[identity][1] if self.commands[identity][0] == c else dict(status="denied")
        else:
            old = deepcopy(self.__dict__)
            try:
                integer_fields(c)
                self.apply(c)
                expected = dict(status="ok", activity_cursor=len(self.activities))
            except (ValueError, KeyError):
                self.__dict__ = old
                expected = dict(status="denied")
            self.commands[identity] = deepcopy(c), deepcopy(reply)
        require(reply["status"] == expected["status"], "Broker admission outcome differs")
        if expected["status"] == "ok":
            require(reply == expected, "Broker reply cursor differs")
        else:
            require(isinstance(reply.get("detail"), str) and reply["detail"], "Denial lacks reason")

    def snapshot(self):
        return dict(account_id=self.account, cash_units=self.cash, available_cash_units=self.cash, reserved_cash_units=self.reserved(),
                    positions=self.positions, open_orders=[o for o in self.orders.values() if o["status"] == "open"],
                    quotes=[q for _, q in sorted(self.quotes.items())], activity_cursor=len(self.activities), order_count=len(self.orders), fill_count=len(self.fills),
                    at_seconds=self.at, session_date=self.day, session_open_seconds=self.opening, session_close_seconds=self.close)

    def read(self, c):
        kind = c["kind"]
        if kind == "hello":
            return dict(protocol=1, account_id=self.account, policy_sha256=POLICY, usd_scale=10**9, fee_per_share_units=FEE, initial_cash_units=INITIAL, symbols=SYMBOLS)
        if kind == "snapshot":
            return self.snapshot()
        if kind == "lookup":
            return dict(found=True, value=self.orders[c["order_id"]]) if c["order_id"] in self.orders else dict(found=False)
        require(kind == "activities", "Unknown independent read")
        require(0 <= c["after"] <= len(self.activities) and 0 < c["limit"] <= 16, "Activity page bound")
        end = min(c["after"] + c["limit"], len(self.activities))
        return dict(activities=self.activities[c["after"]:end], next=end, end=len(self.activities))


class ProcessBrokerVerifier:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()

    def path(self, name):
        p = (self.directory / name).resolve()
        require(p.is_relative_to(self.directory) and not p.is_symlink(), "Artifact escaped run boundary")
        return p

    def verify(self, bundle=True):
        s = json.loads(self.path("summary.json").read_text(encoding="utf-8"))
        require(s["kind"] == "process_broker_diagnostic" and s["evidence_origin"] == "synthetic" and s["policy_sha256"] == POLICY, "Wrong process evidence contract")
        require(s["network_requests"] == s["external_broker_events"] == 0 and not s["historical_strategy_scored"] and not s["reserved_strategy_inputs_opened"], "Evidence scope differs")
        require({c["name"] for c in s["cases"]} == FAMILIES and len(s["cases"]) == 12, "Acceptance scenario matrix differs")
        require(len(s["process_ids"]) >= 12 and all(type(pid) is int and pid > 0 for pid in s["process_ids"]), "Missing native-process observations")
        paths = [a["path"] for a in s["artifacts"]]
        require(len(paths) == len(set(paths)), "Duplicate artifact")
        for a in s["artifacts"]:
            data = self.path(a["path"]).read_bytes()
            require(len(data) == a["bytes"] <= 32 * 1024**2 and hashlib.sha256(data).hexdigest() == a["sha256"], "Artifact identity differs: " + a["path"])
        audits, records, views, client_count, broker_count = {}, {}, [], 0, 0
        for case in s["cases"]:
            finals, reports = [], set()
            for leg in ("broker_journals", "client_journals"):
                for path in case[leg]:
                    require(path in paths and path not in records, "Missing or repeated accepted journal")
                    rows = frames(self.path(path))
                    header, previous = rows[0]
                    integer_fields(header)
                    audit = BrokerAudit(header) if leg == "broker_journals" else AccountAudit(header)
                    if leg == "client_journals":
                        require(header["configuration"]["fee_per_share_ceiling_units"] == FEE, "Client fee convention differs")
                    records[path] = rows
                    for seq, (r, digest) in enumerate(rows[1:], 1):
                        integer_fields(r)
                        require(r["sequence"] == seq and r["previous_sha256"] == previous, "Journal chain differs")
                        try:
                            if leg == "broker_journals":
                                require(r["kind"] == "command", "Wrong peer record kind")
                                audit.step(r["command"], r["reply"])
                                require(audit.snapshot() == r["checkpoint"], "Peer checkpoint differs")
                                broker_count += 1
                            else:
                                audit.apply(r)
                                client_count += 1
                        except (ValueError, KeyError) as error:
                            raise ValueError(f"{path}, sequence {seq}: {error}") from error
                        previous = digest
                    if leg == "broker_journals":
                        audits[path] = audit
                        finals.append(audit.snapshot())
                        reports.update(json.dumps(a["report"], sort_keys=True) for a in audit.activities)
                    views.append(dict(case=case["name"], path=path, leg="Broker" if leg == "broker_journals" else "Client", events=[r for r, _ in rows[1:]]))
            observed = Counter(json.dumps(v, sort_keys=True) for v in finals)
            for value in case["broker_states"]:
                encoded = json.dumps(value, sort_keys=True)
                require(observed[encoded] > 0, "Declared final broker state differs")
                observed[encoded] -= 1
            for path in case["client_journals"]:
                for r, _ in records[path][1:]:
                    if r["input"]["kind"] == "report":
                        report = r["input"]["report"]
                        require(json.dumps(report, sort_keys=True) in reports, "Client booked report absent from independent broker history")
        # Rebuild logical read responses at their actual position in each command
        # stream. A lost response may still have a committed mutation.
        cursors, current = {}, {}
        calls = json.loads(self.path("calls.json").read_text(encoding="utf-8"))
        for call in calls:
            path = call["peer"] + ".journal"
            require(path in audits, "Logical call references unknown peer journal")
            if path not in current:
                current[path] = BrokerAudit(records[path][0][0])
                cursors[path] = 1
            audit, c = current[path], call["command"]
            if c["kind"] not in READS:
                index = cursors[path]
                require(index < len(records[path]) and records[path][index][0]["command"] == c, "Logical mutation differs from committed peer command")
                r = records[path][index][0]
                audit.step(c, r["reply"])
                cursors[path] += 1
                if "response" in call:
                    require(call["response"] == r["reply"], "Logical mutating response differs")
            else:
                try:
                    expected = audit.read(c)
                except ValueError:
                    require("error" in call, "Invalid read did not fail")
                else:
                    require(call.get("response") == expected, "Lookup, activity or snapshot response differs from independent state")
        for rejected in s["rejected_files"]:
            p, kind = self.path(rejected["path"]), rejected["failure"]
            if kind == "incomplete_tail":
                frames(p, incomplete=True)
            else:
                require(kind == "checksum", "Unknown rejected artifact")
                failed = False
                try:
                    frames(p)
                except ValueError:
                    failed = True
                require(failed, "Claimed corrupt peer journal is valid")
        page = self.path("report.html").read_text(encoding="utf-8")
        match = re.search(r"const views=(\[.*?\]);\s*const choose=", page, re.S)
        require(match is not None and json.loads(match[1]) == views, "Displayed ledger events differ")
        if bundle:
            with zipfile.ZipFile(self.path("review.zip"), "w", zipfile.ZIP_DEFLATED) as archive:
                for path in ["summary.json", *paths]:
                    archive.write(self.path(path), path)
        return len(s["cases"]), broker_count, client_count, len(calls)


def self_check(directory):
    def replay(rows):
        audit = BrokerAudit(rows[0][0])
        for r, _ in rows[1:]:
            audit.step(r["command"], r["reply"])
            require(audit.snapshot() == r["checkpoint"], "Changed semantic checkpoint")
    checks = 0
    for field in ("cash_units", "reserved_cash_units", "activity_cursor"):
        altered = frames(directory / "fifo/broker.journal")
        altered[-1][0]["checkpoint"][field] += 1
        failed = False
        try:
            replay(altered)
        except ValueError:
            failed = True
        require(failed, "Semantic corruption accepted: " + field)
        checks += 1
    for case, kind, field, value in [("fifo", "quote", "ask_shares", 99), ("fifo", "submit", "fee_per_share_ceiling_units", 0),
                                     ("fifo", "sell", "side", "buy"), ("ioc", "tif", "time_in_force", "gtc"),
                                     ("gtc", "tif", "time_in_force", "day")]:
        altered = frames(directory / case / "broker.journal")
        changed = False
        for r, _ in altered[1:]:
            c = r["command"]
            if kind == "quote" and c["kind"] == "quote":
                c["quote"][field] = value
            elif kind == "submit" and c["kind"] == "submit":
                c["order"][field] = value
            elif kind == "sell" and c["kind"] == "submit" and c["order"]["side"] == "sell":
                c["order"][field] = value
            elif kind == "tif" and c["kind"] == "submit":
                c[field] = value
            else:
                continue
            changed = True
            break
        require(changed, "Semantic corruption found no applicable record")
        failed = False
        try:
            replay(altered)
        except ValueError:
            failed = True
        require(failed, "Changed quote, fee, side or lifecycle was accepted")
        checks += 1
    altered = frames(directory / "fifo/broker.journal")
    altered[0][0]["schema_version"] = 3
    failed = False
    try:
        replay(altered)
    except ValueError:
        failed = True
    require(failed, "Unsupported journal schema accepted")
    checks += 1
    rows = frames(directory / "replacement/client.journal")
    rows[-1][0]["checkpoint"]["owners"][0]["cash_flow_units"] += 1
    audit, failed = AccountAudit(rows[0][0]), False
    try:
        for r, _ in rows[1:]:
            audit.apply(r)
    except ValueError:
        failed = True
    require(failed, "Altered client attribution accepted")
    checks += 1
    print(f"Independent semantic self-checks passed: {checks} corruptions rejected, including cash, fees, liquidity, lifecycle, schema and client ownership.")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    p.add_argument("--self-check", action="store_true")
    args = p.parse_args()
    try:
        families, broker, client, calls = ProcessBrokerVerifier(args.directory).verify()
        if args.self_check:
            self_check(args.directory)
    except (ValueError, OSError, KeyError, TypeError, IndexError) as error:
        raise SystemExit(f"Process broker verification failed: {error}") from error
    print(f"Verified {families} scenario families, {broker} broker commits, {client} client inputs and {calls} logical peer calls.")
    print(f"Review bundle: {args.directory.resolve() / 'review.zip'}")


if __name__ == "__main__":
    main()
