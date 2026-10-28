#include "process_broker_report.hpp"
#include "artifact_io.hpp"
#include "content_digest.hpp"
#include <algorithm>
#include <set>
#include <sstream>

namespace trading {
void ProcessBrokerReport::write(const execution::ProcessBrokerDiagnosticResult &result) const {
    using Json = nlohmann::json;
    Json views = Json::array();
    std::set<std::string> seen;
    for (const auto &c : result.cases) {
        for (const auto *leg : {"broker_journals", "client_journals"}) {
            for (const auto &path : c.at(leg)) {
                const auto name = path.get<std::string>();
                if (!seen.insert(name).second) {
                    continue;
                }
                Json events = Json::array();
                const auto bytes = read_text_file(result.directory / name, execution::maximum_journal_bytes);
                std::size_t offset = 0;
                while (offset < bytes.size()) {
                    const auto end = bytes.find('\n', offset);
                    if (end == std::string::npos) {
                        throw execution::JournalError("accepted process view has incomplete frame");
                    }
                    auto [value, digest] =
                        execution::JournalFrame::decode(std::string_view{bytes}.substr(offset, end - offset));
                    static_cast<void>(digest);
                    if (value.at("kind").get<std::string>() != "header") {
                        events.push_back(std::move(value));
                    }
                    offset = end + 1;
                }
                views.push_back({{"case", c.at("name")},
                                 {"path", name},
                                 {"leg", std::string{leg} == "broker_journals" ? "Broker" : "Client"},
                                 {"events", events}});
            }
        }
    }
    write_new_text_file(result.directory / "calls.json", result.calls.dump(2) + "\n");
    std::ostringstream page;
    page
        << R"html(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Independent broker process and recovery</title>
<style>body{margin:0;background:#f1f5f8;color:#143349;font:16px/1.5 system-ui}main{max-width:1200px;margin:30px auto;padding:0 20px}section{background:white;border:1px solid #d3dfe5;border-radius:8px;margin:18px 0;padding:22px}h1,h2{line-height:1.2}select,button,input{font:inherit;padding:7px}select{max-width:100%}input{width:60%}.cards{display:flex;gap:28px;flex-wrap:wrap}.cards strong{display:block;font-size:22px}svg{width:100%;background:#f6f9fb}table{border-collapse:collapse;width:100%}th,td{padding:8px;text-align:right;border-bottom:1px solid #dde5eb}th:first-child,td:first-child{text-align:left}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:13px}.scroll{overflow-x:auto}</style>
<main><h1>Independent broker process and recovery</h1><p><strong>Constructed offline evidence.</strong> A native C++ peer owns its account and durable activity history independently of the shared client. No network service, observed broker event or historical performance result is represented.</p>
<p>One FIFO matcher serves the process and in-process peers. DAY/IOC/GTC limits, fees, finite quote liquidity, cancellation races and restart boundaries are explicit. Money is stored in USD 1e-9; filled sale proceeds are immediately available in this synthetic account.</p>
<section><label for="view">Scenario and ledger </label><select id="view"></select><p><button id="previous">Previous</button> <input id="event" type="range" min="0" value="0" aria-label="Saved event"><button id="next">Next</button></p><p id="caption" aria-live="polite"></p><div class="cards"><div>Cash, USD<strong id="cash"></strong></div><div>Reserved, USD<strong id="reserved"></strong></div><div>Open orders<strong id="orders-count"></strong></div><div>Activity / routing<strong id="health"></strong></div></div></section>
<section><h2>Cash across committed events</h2><p>The horizontal axis is saved command/input index. The vertical axis uses the displayed local USD range. Virtual clocks are recorded separately; these are not measured network latencies.</p><svg id="chart" viewBox="0 0 1000 280" role="img" aria-label="Cash by saved event index"></svg></section>
<section><h2>Open orders and symbol inventory</h2><div class="scroll"><table><thead><tr><th>Client order / symbol</th><th>Side</th><th>Filled / requested</th><th>Status</th><th>Policy / owner claims</th></tr></thead><tbody id="orders"></tbody></table></div><pre id="positions"></pre></section>
<section><h2>Saved commit and outcome</h2><pre id="detail"></pre><p><a href="summary.json">Manifest and scenario checks</a> | <a href="calls.json">Recorded logical peer calls</a></p></section>
<script>const views=)html"
        << views.dump() << R"html(;
const choose=document.getElementById('view'),slider=document.getElementById('event');
function money(value){const n=BigInt(String(value));return (n/1000000000n).toString()+'.'+(n%1000000000n).toString().padStart(9,'0')}
views.forEach((v,i)=>{const o=document.createElement('option');o.value=i;o.textContent=v.case.replaceAll('_',' ')+' | '+v.leg+' | '+v.path;choose.append(o)});
function draw(){const v=views[Number(choose.value)],i=Number(slider.value),e=v.events[i];if(!e){document.getElementById('caption').textContent='Valid peer header retained; no command was committed.';for(const id of ['cash','reserved','orders-count','health','chart','orders','positions','detail']){document.getElementById(id).textContent='';document.getElementById(id).innerHTML=''}return}
const s=e.checkpoint,input=e.command||e.input,result=e.reply||e.result;
document.getElementById('caption').textContent=v.leg+' | event '+(i+1)+'/'+v.events.length+' | '+input.kind+' | virtual receipt '+input.at_seconds+' | '+(result.status||result.outcome);
document.getElementById('cash').textContent=money(s.cash_units);document.getElementById('reserved').textContent=money(s.reserved_cash_units);document.getElementById('orders-count').textContent=s.open_orders.length;document.getElementById('health').textContent=v.leg==='Broker'?'Cursor '+s.activity_cursor:s.connected&&!s.reconciliation_required?'Open':'Closed';
const values=v.events.map(x=>Number(x.checkpoint.cash_units)/1e9),low=Math.min(...values),high=Math.max(...values),span=Math.max(1,high-low),x=j=>80+j*880/Math.max(1,values.length-1),y=n=>230-(n-low)*190/span;
const line=values.map((n,j)=>(j?'L':'M')+x(j)+','+y(n)).join(' ');document.getElementById('chart').innerHTML=`<text x="10" y="25" font-size="14">${(low+span).toFixed(6)} USD</text><text x="10" y="250" font-size="14">${low.toFixed(6)} USD</text><path d="${line}" fill="none" stroke="#18799f" stroke-width="3"/><line x1="${x(i)}" y1="30" x2="${x(i)}" y2="240" stroke="#aa6026" stroke-dasharray="4"/><circle cx="${x(i)}" cy="${y(values[i])}" r="5" fill="#aa6026"/><text x="80" y="275" font-size="14">1</text><text x="960" y="275" text-anchor="end" font-size="14">${values.length}</text>`;
document.getElementById('orders').innerHTML=s.open_orders.length?s.open_orders.map(o=>`<tr><td>${o.order.id} / ${o.order.symbol}</td><td>${o.order.side}</td><td>${o.filled_shares} / ${o.order.shares}</td><td>${o.status}</td><td>${o.time_in_force||o.claims.map(c=>c.family+': '+c.remaining).join('; ')}</td></tr>`).join(''):'<tr><td colspan="5">None</td></tr>';document.getElementById('positions').textContent=JSON.stringify(s.positions,null,2);document.getElementById('detail').textContent=JSON.stringify(e,null,2)}
function select(){slider.max=Math.max(0,views[Number(choose.value)].events.length-1);slider.value='0';draw()}
choose.addEventListener('change',select);slider.addEventListener('input',draw);document.getElementById('previous').addEventListener('click',()=>{slider.value=Math.max(0,Number(slider.value)-1);draw()});document.getElementById('next').addEventListener('click',()=>{slider.value=Math.min(Number(slider.max),Number(slider.value)+1);draw()});select();</script></main></html>)html";
    write_new_text_file(result.directory / "report.html", page.str());
    Json artifacts = Json::array();
    std::vector<std::filesystem::path> files;
    for (const auto &f : std::filesystem::recursive_directory_iterator(result.directory)) {
        if (f.is_regular_file()) {
            files.push_back(f.path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const auto &f : files) {
        artifacts.push_back({{"path", std::filesystem::relative(f, result.directory).generic_string()},
                             {"bytes", std::filesystem::file_size(f)},
                             {"sha256", sha256_file(f, execution::maximum_journal_bytes)}});
    }
    const Json summary{{"schema_version", 1},
                       {"kind", "process_broker_diagnostic"},
                       {"evidence_origin", "synthetic"},
                       {"network_requests", 0},
                       {"external_broker_events", 0},
                       {"reserved_strategy_inputs_opened", false},
                       {"historical_strategy_scored", false},
                       {"policy_sha256", TRADING_BROKER_PROTOCOL_SHA256},
                       {"source_sha256", TRADING_SOURCE_SHA256},
                       {"compiler", TRADING_COMPILER_ID},
                       {"build_configuration", TRADING_BUILD_CONFIGURATION},
                       {"checks", result.checks},
                       {"cases", result.cases},
                       {"process_ids", result.process_ids},
                       {"rejected_files", result.rejected_files},
                       {"artifacts", artifacts}};
    write_new_text_file(result.directory / "summary.json", summary.dump(2) + '\n');
}
} // namespace trading
