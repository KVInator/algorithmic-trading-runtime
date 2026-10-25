#include "strategy_execution_report.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "report_text.hpp"
#include "runtime_json.hpp"
#include "trading_source_inventory.hpp"
#include "utc_timestamp.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>
#include <iomanip>

namespace trading::execution {
namespace {
using Json = nlohmann::json;

Json quote_json(const TopOfBook& q) {
    return {{"sequence", q.sequence},   {"at_seconds", q.at_seconds}, {"bid_cents", q.bid_cents},
            {"ask_cents", q.ask_cents}, {"bid_shares", q.bid_shares}, {"ask_shares", q.ask_shares}};
}

Json orders_json(const std::vector<OrderView>& orders) {
    Json out = Json::array();
    for (const auto& o : orders) {
        out.push_back({{"request", input_json(SubmitInput{o.request})},
                       {"filled_shares", o.filled_shares},
                       {"status", name(o.status)},
                       {"acknowledged", o.acknowledged},
                       {"pending_cancel_id", o.pending_cancel_id}});
    }
    return out;
}

Json frame_json(const ExecutionFrame& f) {
    Json open = Json::array();
    for (const auto& o : f.broker.open_orders) {
        open.push_back({{"request", input_json(SubmitInput{o.request})}, {"filled_shares", o.filled_shares}});
    }
    return {{"label", f.label},
            {"at_seconds", f.at_seconds},
            {"client", account_json(f.account)},
            {"broker",
             {{"cash_cents", f.broker.cash_units},
              {"position_shares", f.broker.position_shares},
              {"open_orders", open}}},
            {"orders", orders_json(f.orders)},
            {"target", f.target ? input_json(*f.target) : Json(nullptr)},
            {"quote", f.quote ? quote_json(*f.quote) : Json(nullptr)},
            {"latest_transition", f.last_transition ? trace_json(*f.last_transition) : Json(nullptr)},
            {"broker_fill_count", f.broker_fill_count},
            {"broker_report_count", f.broker_report_count},
            {"journal_records", f.journal_records},
            {"client_restarts", f.client_restarts}};
}

Json bars_json(std::span<const MinuteBar> bars) {
    Json result = Json::array();
    for (const auto& b : bars) {
        const auto& p = b.prices();
        result.push_back({b.start().time_since_epoch().count(), p.open, p.high, p.low, p.close, b.volume()});
    }
    return result;
}

Json inputs_json(const StrategyExecutionInputs& inputs) {
    Json history = Json::array();
    for (const auto& s : inputs.history) {
        history.push_back({{"start", s.request.start().time_since_epoch().count()},
                           {"end", s.request.end_exclusive().time_since_epoch().count()},
                           {"bars", bars_json(s.bars)}});
    }
    return {{"bar_columns",
             {"start_seconds_UTC", "open_USD", "high_USD", "low_USD", "close_USD", "volume_shares"}},
            {"history", history},
            {"current_start", inputs.current.start().time_since_epoch().count()},
            {"current_end", inputs.current.end_exclusive().time_since_epoch().count()},
            {"continuation_long", bars_json(inputs.continuation_long)},
            {"continuation_cash", bars_json(inputs.continuation_cash)},
            {"reversion_long", bars_json(inputs.reversion_long)},
            {"reversion_cash", bars_json(inputs.reversion_cash)},
            {"momentum_long", bars_json(inputs.momentum_long)},
            {"momentum_cash", bars_json(inputs.momentum_cash)}};
}

std::string clock(std::int64_t seconds) {
    return format_utc_seconds(std::chrono::sys_seconds{std::chrono::seconds{seconds}});
}

// Geometry and formatting only. Every balance/order/fill comes from the core.
void plot(std::ostream& out, const StrategyExecutionCase& item, const ExecutionFrame& frame) {
    if (!frame.quote) {
        out << "<p>No quote received at this event.</p>";
        return;
    }
    Cents low = frame.quote->bid_cents, high = frame.quote->ask_cents;
    for (const auto& q : item.quotes) {
        if (q.sequence <= frame.quote->sequence) {
            low = std::min(low, q.bid_cents);
            high = std::max(high, q.ask_cents);
        }
    }
    const double price_span = static_cast<double>(std::max<Cents>(1, high - low));
    const auto start = item.frames.front().at_seconds, end = frame.at_seconds;
    const double time_span = static_cast<double>(std::max<std::int64_t>(1, end - start));
    const auto x = [&](std::int64_t t) { return 90 + 820 * static_cast<double>(t - start) / time_span; };
    const auto y = [&](Cents p) { return 190 - 140 * static_cast<double>(p - low) / price_span; };
    out << "<svg viewBox=\"0 0 1000 240\" role=\"img\" aria-label=\"Available quote prices and broker fills "
           "at the selected event\">"
           "<rect x=\"90\" y=\"30\" width=\"820\" height=\"170\" fill=\"#f4f7fc\"/>"
           "<text x=\"2\" y=\"34\">USD cents</text><text x=\"2\" y=\"54\">"
        << high << "</text><text x=\"2\" y=\"194\">" << low << "</text>";
    for (const bool bid : {true, false}) {
        out << "<polyline fill=\"none\" stroke=\"" << (bid ? "#167869" : "#7c51b0")
            << "\" stroke-width=\"2\" points=\"";
        for (const auto& q : item.quotes) {
            if (frame.quote && q.sequence <= frame.quote->sequence) {
                out << x(q.at_seconds) << ',' << y(bid ? q.bid_cents : q.ask_cents) << ' ';
            }
        }
        out << "\"/>";
    }
    for (std::size_t i = 0; i < frame.broker_fill_count; ++i) {
        const auto& f = item.fills.at(i);
        const auto q = std::find_if(item.quotes.begin(), item.quotes.end(),
                                    [&](const auto& value) { return value.sequence == f.quote_sequence; });
        if (q != item.quotes.end()) {
            out << "<circle cx=\"" << x(q->at_seconds) << "\" cy=\"" << y(f.price_cents)
                << "\" r=\"5\" fill=\"" << (f.side == Side::buy ? "#137a48" : "#c04a35") << "\"><title>"
                << name(f.side) << ' ' << f.shares << " shares at " << f.price_cents
                << " cents</title></circle>";
        }
    }
    out << "<line x1=\"" << x(frame.at_seconds) << "\" x2=\"" << x(frame.at_seconds)
        << "\" y1=\"25\" y2=\"205\" stroke=\"#244878\" stroke-dasharray=\"4 4\"/>"
           "<text x=\"90\" y=\"225\">"
        << clock(start) << "</text><text x=\"725\" y=\"225\">" << clock(end) << "</text></svg>";
}
} // namespace

void StrategyExecutionReport::write(const std::filesystem::path& directory) const {
    const auto& result = result_;
    Json cases = Json::array(), checks = Json::array(), artifacts = Json::array();
    std::ostringstream html, css;
    html << std::setprecision(12);
    const auto save = [&](std::string_view filename, const std::string& data) {
        write_new_text_file(directory / filename, data);
        artifacts.push_back({{"path", filename}, {"sha256", sha256_file(directory / filename)}});
    };
    html << "<main><header><p class=\"eyebrow\">STRATEGY TO EXECUTION · EVENT REPLAY</p><h1>Decisions, "
            "orders and recovery</h1>"
            "<p>Three strategy designs, four variants, one journaled single-symbol execution path. Each case "
            "runs separately. "
            "Select a case and event to inspect the client and broker independently.</p></header>"
            "<aside><strong>Evidence scope</strong><p>Synthetic fixtures use constructed prices, sizes and "
            "fees. Optional historical cases use "
            "the first eligible development date after twenty warm-up sessions. They construct zero-spread "
            "quotes from completed closes and "
            "subsequent minute opens, rounded to cents, with assumed ten-share liquidity and zero fees. "
            "These are reference-price proxies, "
            "not observed quotes, broker fills, measured latency or revised historical strategy results. "
            "Final bar revision times are unavailable.</p></aside>"
            "<details><summary>Account convention comparisons</summary><div "
            "class=\"scroll\"><table><tr><th>Case</th><th>Fractional entry shares</th>"
            "<th>Fractional final USD</th><th>Integer final USD</th><th>Idle cash after entry "
            "USD</th><th>Integer minus fractional USD</th></tr>";
    for (const auto& c : result.cases) {
        if (c.parity) {
            const auto& p = *c.parity;
            html << "<tr><td>" << ReportText{c.label}.html() << "<br><small>"
                 << ReportText{p.convention}.html() << "</small></td><td>" << p.fractional_entry_shares
                 << "</td><td>" << p.fractional_final_equity_usd << "</td><td>" << p.integer_final_equity_usd
                 << "</td><td>" << p.integer_idle_cash_after_entry_usd << "</td><td>" << p.difference_usd
                 << "</td></tr>";
        }
    }
    html << "</table></div></details><h2>Replay cases</h2><p>The event buttons form the playhead. "
            "Previous/next also "
            "work with keyboard-focused radio controls. "
            "Quote lines: green bid, purple ask. Dots: green buys, red sells. Synthetic time is not measured "
            "network latency. "
            "Broker executions may precede client report delivery. Client/broker differences at those frames "
            "are intentional.</p>";
    for (std::size_t ci = 0; ci < result.cases.size(); ++ci) {
        const auto& c = result.cases[ci];
        const auto prefix = "c" + std::to_string(ci) + "e";
        Json frames = Json::array(), quotes = Json::array(), fills = Json::array(), reports = Json::array();
        for (const auto& q : c.quotes) {
            quotes.push_back(quote_json(q));
        }
        for (const auto& f : c.fills) {
            fills.push_back({{"execution_id", f.execution_id},
                             {"order_id", f.order_id},
                             {"side", name(f.side)},
                             {"shares", f.shares},
                             {"price_cents", f.price_cents},
                             {"fee_cents", f.fee_units},
                             {"quote_sequence", f.quote_sequence}});
        }
        for (const auto& r : c.reports) {
            reports.push_back(input_json(r));
        }
        html << "<details class=\"case\" id=\"case" << ci << "\"" << (ci == 0 ? " open" : "") << "><summary>"
             << ReportText{c.label}.html() << " <small>"
             << (c.historical_proxy ? "historical bar proxy" : "constructed scenario")
             << "</small></summary>"
                "<p>Entry reason: <strong>"
             << ReportText{c.signal_reason}.html() << "</strong>. Broker fills: " << c.fills.size()
             << ".</p>";
        for (std::size_t i = 0; i < c.frames.size(); ++i) {
            const auto id = prefix + std::to_string(i);
            html << "<input class=\"step\" type=\"radio\" name=\"case" << ci << "\" id=\"" << id
                 << "\" aria-label=\"Event " << i << "\"" << (i == 0 ? " checked" : "") << ">";
            css << '#' << id << ":checked~.frames>.f" << i << "{display:block}#" << id
                << ":checked~.events>label[for=\"" << id << "\"]{background:#234e7f;color:white}";
        }
        html << "<div class=\"events\" aria-label=\"Event playhead\">";
        for (std::size_t i = 0; i < c.frames.size(); ++i) {
            html << "<label for=\"" << prefix << i << "\" title=\"" << ReportText{c.frames[i].label}.html()
                 << "\">" << i << "</label>";
        }
        html << "</div><div class=\"frames\">";
        for (std::size_t i = 0; i < c.frames.size(); ++i) {
            const auto& f = c.frames[i];
            auto data = frame_json(f);
            data["last_generated_broker_report"] =
                f.broker_report_count ? input_json(c.reports.at(f.broker_report_count - 1)) : Json(nullptr);
            frames.push_back(data);
            const bool gate = f.account.connected && !f.account.reconciliation_required;
            html << "<article class=\"frame f" << i << "\"><nav>";
            if (i > 0) {
                html << "<label for=\"" << prefix << i - 1 << "\">Previous</label>";
            }
            if (i + 1 < c.frames.size()) {
                html << "<label for=\"" << prefix << i + 1 << "\">Next</label>";
            }
            html << "</nav><h3>" << i << " · " << ReportText{f.label}.html() << "</h3><p><time>"
                 << clock(f.at_seconds) << "</time> · Routing gate: <strong class=\""
                 << (gate ? "open" : "closed") << "\">" << (gate ? "OPEN" : "CLOSED")
                 << "</strong> · Client restarts: " << f.client_restarts
                 << " · Durable records: " << f.journal_records << "</p>";
            plot(html, c, f);
            html << "<div class=\"accounts\"><section><h4>Client ledger</h4><dl><dt>Cash cents</dt><dd>"
                 << f.account.cash_units << "</dd><dt>Position shares</dt><dd>" << f.account.position_shares
                 << "</dd><dt>Reserved cash cents</dt><dd>" << f.account.reserved_cash_units
                 << "</dd><dt>Reserved sell shares</dt><dd>" << f.account.reserved_sell_shares
                 << "</dd><dt>Open orders</dt><dd>" << f.account.open_orders
                 << "</dd></dl></section>"
                    "<section><h4>Independent broker ledger</h4><dl><dt>Cash cents</dt><dd>"
                 << f.broker.cash_units << "</dd><dt>Position shares</dt><dd>" << f.broker.position_shares
                 << "</dd><dt>Open orders</dt><dd>" << f.broker.open_orders.size()
                 << "</dd><dt>Executed fills</dt><dd>" << f.broker_fill_count
                 << "</dd></dl></section></div>"
                    "<div "
                    "class=\"scroll\"><table><tr><th>Order</th><th>Side</th><th>Quantity</th><th>Filled</"
                    "th><th>State</th><th>Pending cancel</th></tr>";
            for (const auto& o : f.orders) {
                html << "<tr><td>" << ReportText{o.request.id}.html() << "</td><td>" << name(o.request.side)
                     << "</td><td>" << o.request.shares << "</td><td>" << o.filled_shares << "</td><td>"
                     << name(o.status) << "</td><td>" << ReportText{o.pending_cancel_id}.html()
                     << "</td></tr>";
            }
            html << "</table></div><details><summary>Inspect saved event, target and account "
                    "evidence</summary><pre>"
                 << ReportText{data.dump(2)}.html() << "</pre></details></article>";
        }
        html << "</div></details>";
        Json parity = nullptr;
        if (c.parity) {
            const auto& p = *c.parity;
            parity = {{"convention", p.convention},
                      {"fractional_entry_shares", p.fractional_entry_shares},
                      {"fractional_final_equity_usd", p.fractional_final_equity_usd},
                      {"integer_final_equity_usd", p.integer_final_equity_usd},
                      {"integer_idle_cash_after_entry_usd", p.integer_idle_cash_after_entry_usd},
                      {"integer_minus_fractional_usd", p.difference_usd},
                      {"exact_alignment", p.exact_alignment}};
        }
        cases.push_back({{"design", name(c.design)},
                         {"label", c.label},
                         {"journal_file", c.journal_file},
                         {"historical_proxy", c.historical_proxy},
                         {"signal_reason", c.signal_reason},
                         {"quotes", quotes},
                         {"fills", fills},
                         {"reports", reports},
                         {"frames", frames},
                         {"parity", parity}});
        artifacts.push_back({{"path", c.journal_file}, {"sha256", sha256_file(directory / c.journal_file)}});
    }
    for (const auto& c : result.checks) {
        checks.push_back({{"label", c.name}, {"passed", c.passed}});
    }
    html << "<h2>Reproduction</h2><p>" << result.checks.size()
         << " checks passed. <a href=\"execution_evidence.json\">Saved states, quotes, fills, reports and "
            "parity</a> · "
            "<a href=\"inputs.json\">Exact signal inputs</a> · <a href=\"source_inventory.txt\">Source "
            "inventory</a> · <a href=\"summary.json\">Manifest</a></p>"
            "<p>Full market depth, queue position, market impact and live execution quality are not measured "
            "by this replay.</p></main>";
    const std::string document =
        "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
        "content=\"width=device-width,initial-scale=1\"><title>Strategy execution replay</title><style>"
        "body{font:16px system-ui;max-width:1250px;margin:2rem auto;padding:0 "
        "1rem;background:#f3f6fa;color:#1c3047}h1{font-size:2.4rem;margin:.5rem "
        "0}.eyebrow{letter-spacing:.15em;font-size:.8rem;color:#456180}aside{background:#fff4dc;border-left:"
        "4px solid #c98b1b;padding:1rem;margin:1rem 0}aside p{margin:.5rem "
        "0}.case{background:white;border:1px solid #cbd7e4;border-radius:8px;margin:1rem "
        "0;padding:1rem}summary{cursor:pointer;font-weight:650}small{font-weight:400;color:#526479}.events{"
        "display:flex;flex-wrap:wrap;gap:5px;margin:1rem 0}.events label,nav "
        "label{cursor:pointer;padding:6px "
        "10px;background:#e9eff6;border-radius:4px}nav{display:flex;gap:10px;justify-content:flex-end}.step{"
        "position:absolute;opacity:0;width:1px;height:1px}.step:focus-visible~.events{outline:2px solid "
        "#327ac0}.frame{display:none}.accounts{display:grid;grid-template-columns:1fr 1fr;gap:1rem}.accounts "
        "section{background:#edf3f9;padding:1rem;border-radius:5px}dl{display:grid;grid-template-columns:1fr "
        "auto;gap:8px}dd{font-variant-numeric:tabular-nums;font-weight:650}h4{margin:0 0 "
        "1rem}table{border-collapse:collapse;width:100%;font-size:14px}th,td{text-align:left;padding:9px;"
        "border-bottom:1px solid "
        "#d5dfe9}.scroll{overflow:auto}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#f4f7fb;"
        "padding:1rem;font-size:12px}.open{color:#147548}.closed{color:#b23c37}svg{width:100%}@media(max-"
        "width:700px){.accounts{grid-template-columns:1fr}h1{font-size:1.8rem}}" +
        css.str() + "</style>" + html.str() + "</html>\n";
    save("execution_evidence.json", Json{{"checks", checks}, {"cases", cases}}.dump(2) + '\n');
    const Json inputs{
        {"synthetic", inputs_json(result.inputs)},
        {"historical", result.historical_inputs ? inputs_json(*result.historical_inputs) : Json(nullptr)}};
    save("inputs.json", inputs.dump() + '\n');
    save("source_inventory.txt", std::string{build::source_records});
    save("report.html", document);
    const Json summary{{"schema_version", 1},
                       {"status", "offline_execution_checks_passed"},
                       {"case_count", cases.size()},
                       {"checked_assertions", checks.size()},
                       {"historical_manifest_sha256",
                        result.historical_manifest ? Json(*result.historical_manifest) : Json(nullptr)},
                       {"source_sha256", build::source_sha256},
                       {"compiler", TRADING_COMPILER_ID},
                       {"build_configuration", TRADING_BUILD_CONFIGURATION},
                       {"artifacts", artifacts}};
    write_new_text_file(directory / "summary.json", summary.dump(2) + '\n');
}
} // namespace trading::execution
