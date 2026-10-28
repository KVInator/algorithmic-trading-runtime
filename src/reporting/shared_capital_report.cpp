#include "shared_capital_report.hpp"
#include "shared_capital_json.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "journal_frame.hpp"
#include <algorithm>
#include <sstream>

namespace trading {
void SharedCapitalReport::write(const std::filesystem::path& directory, const execution::SharedDiagnosticResult& result) const {
    using Json = nlohmann::json;
    Json cases = Json::array(), views = Json::array();
    for (const auto& item : result.cases) {
        Json segments = Json::array(), events = Json::array();
        for (const auto& segment : item.segments) {
            segments.push_back({{"path", segment.path}, {"generation", segment.identity.generation}, {"previous_segment_sha256", segment.identity.previous_segment_sha256}});
            const auto bytes = read_text_file(directory / segment.path, execution::maximum_journal_bytes);
            std::size_t start = 0;
            while (start < bytes.size()) {
                const auto end = bytes.find('\n', start);
                if (end == std::string::npos) { throw execution::JournalError("report cannot display an incomplete accepted segment"); }
                const auto [value, digest] = execution::JournalFrame::decode(std::string_view{bytes}.substr(start, end - start));
                static_cast<void>(digest);
                if (value.at("kind").get<std::string>() == "input") { events.push_back(value); }
                start = end + 1;
            }
        }
        const auto config = execution::shared_configuration_json(item.configuration);
        cases.push_back({{"name", item.name}, {"configuration", config}, {"segments", segments}, {"final_state", execution::shared_snapshot_json(item.final_state)}});
        views.push_back({{"name", item.name}, {"configuration", config}, {"events", events}});
    }
    const auto data = views.dump();
    std::ostringstream page;
    page << R"html(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Shared capital, ownership and recovery</title><style>
body{font:16px/1.5 system-ui;background:#f2f5f8;color:#173347;margin:0}main{max-width:1180px;margin:32px auto;padding:0 20px}
section{background:white;padding:22px;margin:18px 0;border:1px solid #d3dce3;border-radius:8px}h1,h2{line-height:1.2}
select,input,button{font:inherit;padding:8px}input[type=range]{width:65%}table{border-collapse:collapse;width:100%}th,td{padding:7px;border-bottom:1px solid #d8e1e7;text-align:right}th:first-child,td:first-child{text-align:left}
.cards{display:flex;gap:25px;flex-wrap:wrap}.cards strong{display:block;font-size:22px}svg{width:100%;background:#f6f8fa}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:13px}.alert{color:#a13e22}
</style><main><h1>One account, fifteen strategy owners</h1>
<p><strong>Constructed offline evidence.</strong> Prices, quotes, broker reports, receipts and faults are synthetic.
No network connection, historical performance claim or external broker observation.</p>
<p>The registered $100,000 account allocates one third per family and one fifth of each family to each instrument.
Global cash and broker inventory reconcile to owner attribution after every event. Pending cancellations retain resources.</p>
<section><label for="case">Scenario </label><select id="case"></select>
<p><button id="previous" type="button">Previous</button> <input id="event" type="range" min="0" value="0" aria-label="Saved event">
<button id="next" type="button">Next</button></p><p id="caption" aria-live="polite"></p>
<div class="cards"><div>Global cash<strong id="cash"></strong></div><div>Reserved cash<strong id="reserved"></strong></div><div>Marked equity<strong id="equity"></strong></div><div>Routing gate<strong id="gate"></strong></div></div>
<p id="breaches" class="alert"></p></section>
<section><h2>Instrument exposure and ownership</h2><p>Stacked bars show marked owner positions against the 20% instrument target cap.
Blue: momentum. Purple: reversion. Orange: continuation. The outer line marks the cap; market movements may create a visible breach.</p>
<svg id="positions" viewBox="0 0 1000 300" role="img" aria-label="Saved marked positions by strategy owner"></svg></section>
<section><h2>Owner ledger</h2><p>Attribution cash flow records trades, fees and marked internal transfers.
Owner marked P&amp;L is that flow plus the value of owned shares; initial capital remains in the global account.</p>
<table><thead><tr><th>Family / instrument</th><th>Shares</th><th>Desired</th><th>Cash flow, USD</th><th>Marked P&amp;L, USD</th></tr></thead><tbody id="owners"></tbody></table></section>
<section><h2>Open broker orders</h2><table><thead><tr><th>Order / instrument</th><th>Side</th><th>Filled / requested</th><th>Status</th><th>Remaining owner claims</th></tr></thead><tbody id="orders"></tbody></table></section>
<section><h2>Saved input and dispatch evidence</h2><pre id="detail"></pre><p><a href="summary.json">Full manifest and verification checks</a></p></section>
<script>const cases=)html" << data << R"html(;
const choose=document.getElementById('case'), slider=document.getElementById('event');
const families=['momentum','reversion','volume_conditioned_continuation'];
const colors=['#1979a5','#7142a2','#c77525'];
function money(value){let n=typeof value==='bigint'?value:BigInt(String(value));const sign=n<0n?'-':'';if(n<0n)n=-n;return sign+(n/1000000000n).toString()+'.'+(n%1000000000n).toString().padStart(9,'0')}
function label(f){return f==='volume_conditioned_continuation'?'Continuation':f==='momentum'?'Momentum':'Reversion'}
cases.forEach((c,i)=>{const option=document.createElement('option');option.value=i;option.textContent=c.name.replaceAll('_',' ');choose.append(option)});
function draw(){
 const c=cases[Number(choose.value)], e=c.events[Number(slider.value)];if(!e)return;const s=e.checkpoint;
 document.getElementById('caption').textContent=`Event ${Number(slider.value)+1}/${c.events.length} | receipt clock ${e.input.at_seconds} | ${e.input.kind} | ${e.result.outcome}: ${e.result.reason}`;
 ['cash','reserved','equity'].forEach((id,i)=>{document.getElementById(id).textContent=money(s[['cash_units','reserved_cash_units','equity_units'][i]])});
 document.getElementById('gate').textContent=s.sealed?'Sealed':s.connected&&!s.reconciliation_required?'Open':'Closed';
 document.getElementById('breaches').textContent=s.budget_breaches.length?'Current budget breaches: '+s.budget_breaches.join(', '):'Current budget breaches: none';
 const marks={};s.quotes.forEach(q=>{marks[q.symbol]=BigInt(String(q.bid_units))+(BigInt(String(q.ask_units))-BigInt(String(q.bid_units)))/2n});
 let chart='';const cap=Number(s.equity_units)/5;const peak=Math.max(1,...s.positions.map(p=>cap>0?Number(BigInt(String(p.shares))*(marks[p.symbol]||0n))/cap:0));
 s.positions.forEach((p,i)=>{const y=30+i*52;chart+=`<text x="10" y="${y+19}" font-size="16">${p.symbol}</text><rect x="90" y="${y}" width="${650/peak}" height="26" fill="none" stroke="#7b8b97"/>`;let x=90;
  families.forEach((f,j)=>{const o=s.owners.find(v=>v.family===f&&v.symbol===p.symbol);const value=Number(BigInt(String(o.shares))*(marks[p.symbol]||0n));const width=cap>0?value/cap/peak*650:0;
   chart+=`<rect x="${x}" y="${y+2}" width="${width}" height="22" fill="${colors[j]}"/>`;x+=width});
  chart+=`<text x="985" y="${y+19}" text-anchor="end" font-size="14">${p.shares} shares</text>`});
 document.getElementById('positions').innerHTML=chart;
 document.getElementById('owners').innerHTML=s.owners.map(o=>{const pnl=BigInt(String(o.cash_flow_units))+BigInt(String(o.shares))*(marks[o.symbol]||0n);return `<tr><td>${label(o.family)} / ${o.symbol}</td><td>${o.shares}</td><td>${o.desired_shares}</td><td>${money(o.cash_flow_units)}</td><td>${money(pnl)}</td></tr>`}).join('');
 document.getElementById('orders').innerHTML=s.open_orders.length?s.open_orders.map(o=>`<tr><td>${o.order.id} / ${o.order.symbol}</td><td>${o.order.side}</td><td>${o.filled_shares} / ${o.order.shares}</td><td>${o.status}</td><td>${o.claims.map(q=>label(q.family)+': '+q.remaining).join('; ')}</td></tr>`).join(''):'<tr><td colspan="5">None</td></tr>';
 document.getElementById('detail').textContent=JSON.stringify({generation:s.generation,input:e.input,result:e.result},null,2);
}
function select(){slider.max=cases[Number(choose.value)].events.length-1;slider.value='0';draw()}
choose.addEventListener('change',select);slider.addEventListener('input',draw);
document.getElementById('previous').addEventListener('click',()=>{slider.value=Math.max(0,Number(slider.value)-1);draw()});
document.getElementById('next').addEventListener('click',()=>{slider.value=Math.min(Number(slider.max),Number(slider.value)+1);draw()});
select();</script></main></html>)html";
    write_new_text_file(directory / "report.html", page.str());
    Json artifacts = Json::array();
    std::vector<std::filesystem::path> files;
    for (const auto& file : std::filesystem::recursive_directory_iterator(directory)) { if (file.is_regular_file()) { files.push_back(file.path()); } }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) { artifacts.push_back({{"path", std::filesystem::relative(file, directory).generic_string()}, {"bytes", std::filesystem::file_size(file)},
        {"sha256", sha256_file(file, execution::maximum_journal_bytes)}}); }
    const Json summary{{"schema_version", 1}, {"kind", "shared_capital_diagnostic"}, {"evidence_origin", "synthetic"},
        {"network_requests", 0}, {"external_broker_events", 0}, {"historical_strategy_scored", false}, {"reserved_strategy_inputs_opened", false},
        {"source_sha256", TRADING_SOURCE_SHA256}, {"compiler", TRADING_COMPILER_ID}, {"build_configuration", TRADING_BUILD_CONFIGURATION},
        {"registered_protocol_sha256", TRADING_EXTENSION_PROTOCOL_SHA256}, {"checks", result.checks}, {"cases", cases}, {"evidence", result.evidence}, {"artifacts", artifacts}};
    write_new_text_file(directory / "summary.json", summary.dump(2) + '\n');
}
} // namespace trading
