#include "strategy_execution_diagnostic.hpp"
#include "momentum_order_adapter.hpp"

#include "artifact_io.hpp"
#include "content_digest.hpp"
#include "long_cash_account.hpp"
#include "market_calendar.hpp"
#include "reversion_study.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace trading::execution {
namespace {
using Clock = std::chrono::sys_seconds;

void check(std::vector<PipelineCheck> &checks, std::string label, bool passed) {
    if (!passed) {
        throw std::logic_error("strategy execution check failed: " + label);
    }
    checks.push_back({std::move(label), true});
}

// Only selects the public adapter; signal equations remain in research engines.
class SignalBridge {
  public:
    SignalBridge(ExecutionDesign design, const StrategyExecutionInputs &inputs, Cents fee, bool cash = false)
        : design_{design}, inputs_{inputs}, fee_{fee}, cash_{cash} {
        for (const auto &s : inputs.history) {
            continuation_.append(s.request, s.bars);
            static_cast<void>(reversion_.complete(s.request, s.bars, s.request.end_exclusive()));
        }
    }

    Clock entry() const {
        if (design_ == ExecutionDesign::momentum) {
            return IntradayMomentumStrategy{inputs_.current}.entry_decision_time();
        }
        if (design_ == ExecutionDesign::reversion) {
            return ReversionOrderAdapter{inputs_.current, 10, fee_}.entry_time();
        }
        return IntradayContinuationStrategy{inputs_.current, variant()}.decision_times().front();
    }

    Clock exit() const {
        if (design_ == ExecutionDesign::momentum) {
            return IntradayMomentumStrategy{inputs_.current}.exit_decision_time();
        }
        if (design_ == ExecutionDesign::reversion) {
            return ReversionOrderAdapter{inputs_.current, 10, fee_}.liquidation_time();
        }
        return IntradayContinuationStrategy{inputs_.current, variant()}.liquidation_time();
    }

    std::vector<Clock> clocks() const {
        if (design_ == ExecutionDesign::momentum || design_ == ExecutionDesign::reversion) {
            return {entry(), exit()};
        }
        const auto times = IntradayContinuationStrategy{inputs_.current, variant()}.decision_times();
        std::vector<Clock> result{times.begin(), times.end()};
        result.push_back(exit());
        return result;
    }

    const std::vector<MinuteBar> &bars() const {
        if (design_ == ExecutionDesign::momentum) {
            return cash_ ? inputs_.momentum_cash : inputs_.momentum_long;
        }
        if (design_ == ExecutionDesign::reversion) {
            return cash_ ? inputs_.reversion_cash : inputs_.reversion_long;
        }
        return cash_ ? inputs_.continuation_cash : inputs_.continuation_long;
    }

    TargetIntent target(Clock at, Shares position, const TopOfBook &quote) {
        const auto count =
            std::chrono::duration_cast<std::chrono::minutes>(at - inputs_.current.start()).count();
        const auto prefix = std::span{bars()}.first(static_cast<std::size_t>(count));
        if (design_ == ExecutionDesign::momentum) {
            const MomentumOrderAdapter adapter{IntradayMomentumStrategy{inputs_.current}, 10, fee_};
            if (at == exit()) {
                reason_ = "scheduled_liquidation";
                return adapter.liquidation(quote);
            }
            auto intent = adapter.entry(prefix, quote);
            reason_ = intent.target_shares > 0 ? "positive_momentum" : "nonpositive_momentum";
            return intent;
        }
        if (design_ == ExecutionDesign::reversion) {
            const ReversionOrderAdapter adapter{inputs_.current, 10, fee_};
            if (at == exit()) {
                reason_ = "scheduled_liquidation";
                return adapter.liquidation(quote);
            }
            auto output = adapter.entry(reversion_, prefix, quote);
            reason_ = output.signal.actions[0].reason;
            return output.target;
        }
        const ContinuationOrderAdapter adapter{IntradayContinuationStrategy{inputs_.current, variant()}, 10,
                                               fee_};
        if (at == exit()) {
            reason_ = "scheduled_liquidation";
            return adapter.liquidation(quote);
        }
        auto output = adapter.decide(continuation_, prefix, at, position, quote);
        reason_ = std::string{continuation_action_name(output.signal.action)};
        return output.target;
    }

    const std::string &reason() const noexcept { return reason_; }

  private:
    ContinuationVariant variant() const {
        return design_ == ExecutionDesign::continuation_volume ? ContinuationVariant::volume_conditioned
                                                               : ContinuationVariant::price_only;
    }

    ExecutionDesign design_;
    const StrategyExecutionInputs &inputs_;
    Cents fee_;
    bool cash_;
    IntradayContinuationHistory continuation_{};
    ReversionHistory reversion_{};
    std::string reason_;
};

class ScenarioRunner {
  public:
    ScenarioRunner(std::filesystem::path directory, ExecutionDesign design, std::string mode, Cents cash,
                   Cents fee, std::vector<PipelineCheck> &checks, bool historical = false)
        : config_{"SPY", cash, {20, 4}}, broker_{"SPY", cash, fee}, directory_{std::move(directory)},
          checks_{checks}, result_{design,
                                   std::string{name(design)} + " / " + mode,
                                   std::string{name(design)} + "-" + mode + ".journal",
                                   "",
                                   historical,
                                   {},
                                   {},
                                   {},
                                   {},
                                   {}} {
        runtime_ = std::make_unique<JournaledRuntime>(
            JournaledRuntime::create(create_journal_file(directory_ / result_.journal_file), config_, 2));
        pipeline_ = std::make_unique<OfflinePipeline>(*runtime_, broker_);
    }

    void quote(TopOfBook value) {
        check(checks_, result_.label + ": fresh quote", broker_.on_quote(value));
        result_.quotes.push_back(value);
        capture("Quote received", value.at_seconds);
    }

    void synchronize(std::int64_t at) {
        check(checks_, result_.label + ": exact reconciliation", pipeline_->synchronize());
        capture("Broker reports replayed and exact snapshot reconciled", at);
    }

    RouteResult accept(const TargetIntent &target, std::string label) {
        const auto r = pipeline_->accept_target(target, target.decision_at_seconds);
        capture(std::move(label) + " | " + std::string{name(r.status)} + ": " + r.reason,
                target.decision_at_seconds);
        return r;
    }

    void deliver(std::int64_t at) {
        pipeline_->deliver_reports();
        capture("Broker reports delivered", at);
    }

    void restart(std::int64_t at) {
        pipeline_.reset();
        runtime_.reset();
        runtime_ = std::make_unique<JournaledRuntime>(
            JournaledRuntime::recover(open_journal_file(directory_ / result_.journal_file), config_));
        pipeline_ = std::make_unique<OfflinePipeline>(*runtime_, broker_);
        ++restarts_;
        check(checks_, result_.label + ": recovered routing gate",
              pipeline_->route_latest(at).status == RouteStatus::blocked);
        capture("Client restart: verified journal replay; routing closed", at);
        synchronize(at);
    }

    void capture(std::string label, std::int64_t at) {
        const auto &trace = runtime_->trace();
        result_.frames.push_back({std::move(label), at, runtime_->account(), broker_.snapshot(),
                                  runtime_->orders(), runtime_->latest_target(), broker_.quote(),
                                  trace.empty() ? std::nullopt : std::optional{trace.back()},
                                  broker_.fills().size(), broker_.reports().size(),
                                  runtime_->inspection().input_records, restarts_});
    }

    StrategyExecutionCase finish() {
        result_.fills = broker_.fills();
        result_.reports = broker_.reports();
        return std::move(result_);
    }

    StrategyExecutionCase simple(SignalBridge &signals, bool cash, Cents fee, bool rounding) {
        const auto e = signals.entry().time_since_epoch().count(),
                   x = signals.exit().time_since_epoch().count();
        quote({1, e, 10000, 10000, 20, 20});
        synchronize(e);
        const auto target = signals.target(signals.entry(), 0, *broker_.quote());
        result_.signal_reason = signals.reason();
        check(checks_, result_.label + ": authoritative entry", target.target_shares == (cash ? 0 : 10));
        static_cast<void>(accept(target, "Completed-prefix decision: " + signals.reason()));
        deliver(e);
        const auto idle = runtime_->account().cash_units;
        check(checks_, result_.label + ": complete fill or cash",
              runtime_->account().position_shares == (cash ? 0 : 10));
        const Cents exit_price = fee ? 10000 : 10100;
        quote({2, x, exit_price, exit_price, 20, 20});
        static_cast<void>(
            accept(signals.target(signals.exit(), runtime_->account().position_shares, *broker_.quote()),
                   "Scheduled liquidation"));
        deliver(x);
        synchronize(x);
        check(checks_, result_.label + ": flat final account",
              runtime_->account().position_shares == 0 && runtime_->account().open_orders == 0);
        if (!cash) {
            LongCashAccount fractional{static_cast<double>(config_.initial_cash_units) / 100};
            fractional.rebalance(signals.entry(), 100, 1, fee ? 1 : 0);
            const auto quantity = fractional.value(100).shares;
            fractional.rebalance(signals.exit(), static_cast<double>(exit_price) / 100, 0, fee ? 1 : 0);
            const double f = fractional.value(static_cast<double>(exit_price) / 100).equity_usd;
            const double w = static_cast<double>(runtime_->account().cash_units) / 100;
            result_.parity = ExecutionParity{rounding ? "Whole-share idle cash; fractional residual exposure"
                                                      : "Matched prices, sizing and fees",
                                             quantity,
                                             f,
                                             w,
                                             static_cast<double>(idle) / 100,
                                             w - f,
                                             !rounding};
            check(checks_, result_.label + ": explicit accounting comparison",
                  rounding ? std::abs((w - f) + .005) < 1e-9 : std::abs(w - f) < 1e-9);
        }
        return finish();
    }

    StrategyExecutionCase partial(SignalBridge &signals, bool interrupted) {
        const auto e = signals.entry().time_since_epoch().count(),
                   x = signals.exit().time_since_epoch().count();
        quote({1, e, 9999, 10000, 20, 4});
        synchronize(e);
        const auto target = signals.target(signals.entry(), 0, *broker_.quote());
        result_.signal_reason = signals.reason();
        check(checks_, result_.label + ": long signal", target.target_shares == 10);
        static_cast<void>(accept(target, "Entry target persisted and dispatched"));
        deliver(e);
        check(checks_, result_.label + ": partial buy accounted",
              runtime_->account().position_shares == 4 && runtime_->account().reserved_cash_units == 60006);
        check(checks_, result_.label + ": duplicate decision waits",
              pipeline_->accept_target(target, e).status == RouteStatus::waiting &&
                  broker_.order_count() == 1);
        quote({2, e + 1, 9999, 10000, 20, 2});
        if (interrupted) {
            restart(e + 1);
        } else {
            deliver(e + 1);
        }
        check(checks_, result_.label + ": missing report recovered once",
              runtime_->account().position_shares == 6 && runtime_->account().reserved_cash_units == 40004);
        quote({3, x, 9999, 10000, 3, 0});
        const auto flat =
            signals.target(signals.exit(), runtime_->account().position_shares, *broker_.quote());
        check(checks_, result_.label + ": cancellation before new sale",
              accept(flat, "Liquidation requests cancel of outstanding buy").status == RouteStatus::waiting);
        quote({4, x + 1, 9999, 10000, 3, 1});
        deliver(x + 1);
        check(checks_, result_.label + ": fill races pending cancellation",
              runtime_->account().position_shares == 7);
        broker_.complete_cancels(x + 1);
        deliver(x + 1);
        check(checks_, result_.label + ": route actual inventory",
              pipeline_->route_latest(x + 1).status == RouteStatus::submitted);
        capture("Liquidation dispatched against actual seven-share inventory", x + 1);
        if (interrupted) {
            restart(x + 1);
        } else {
            deliver(x + 1);
        }
        check(checks_, result_.label + ": partial sale retains reservation",
              runtime_->account().position_shares == 4 && runtime_->account().reserved_sell_shares == 4);
        quote({5, x + 2, 10010, 10011, 4, 0});
        deliver(x + 2);
        synchronize(x + 2);
        check(checks_, result_.label + ": exact independent integer ledger",
              runtime_->account().cash_units == 1000023 && runtime_->account().position_shares == 0 &&
                  runtime_->account().open_orders == 0);
        return finish();
    }

    StrategyExecutionCase historical(SignalBridge &signals) {
        std::uint64_t sequence = 0;
        const auto cents = [](double p) {
            if (!std::isfinite(p) || p < .01 || p > 1e9) {
                throw std::invalid_argument("reference price outside diagnostic integer range");
            }
            return static_cast<Cents>(std::llround(p * 100));
        };
        for (const auto at : signals.clocks()) {
            const auto m = static_cast<std::size_t>(
                std::chrono::duration_cast<std::chrono::minutes>(at - signals.bars().front().start())
                    .count());
            const auto now = at.time_since_epoch().count();
            const auto last = cents(signals.bars().at(m - 1).prices().close);
            // No future open is exposed to the decision or its limit prices.
            quote({++sequence, now, last, last, 0, 0});
            if (sequence == 1) {
                synchronize(now);
            }
            const auto target = signals.target(at, runtime_->account().position_shares, *broker_.quote());
            if (result_.signal_reason.empty()) {
                result_.signal_reason = signals.reason();
            }
            static_cast<void>(accept(target, "Bar-proxy decision: " + signals.reason()));
            broker_.complete_cancels(now);
            deliver(now);
            static_cast<void>(pipeline_->route_latest(now));
            capture("Pending old orders resolved before routing", now);
            const auto delayed = cents(signals.bars().at(m + 1).prices().open);
            quote({++sequence, now + 60, delayed, delayed, 10, 10});
            deliver(now + 60);
        }
        const auto close = signals.bars().back().start().time_since_epoch().count() + 60;
        for (const auto &o : runtime_->orders()) {
            if (o.status == OrderStatus::filled || o.status == OrderStatus::canceled ||
                o.status == OrderStatus::rejected) {
                continue;
            }
            const auto cancel = "session-end-" + o.request.id;
            if (runtime_->execute(CancelInput{o.request.id, cancel}) == Outcome::applied) {
                static_cast<void>(broker_.cancel(o.request.id, cancel, close));
            }
        }
        broker_.complete_cancels(close);
        deliver(close);
        synchronize(close);
        check(checks_, result_.label + ": open-order cleanup", runtime_->account().open_orders == 0);
        // Unfilled liquidation is retained as inventory, not replaced by a fill.
        return finish();
    }

  private:
    RuntimeConfiguration config_;
    SimulatedBroker broker_;
    std::filesystem::path directory_;
    std::vector<PipelineCheck> &checks_;
    StrategyExecutionCase result_;
    std::unique_ptr<JournaledRuntime> runtime_;
    std::unique_ptr<OfflinePipeline> pipeline_;
    std::size_t restarts_{0};
};

std::vector<MinuteBar> bars(const HistoricalBarRequest &session, std::size_t day, int shape) {
    std::array<double, 25> residuals{};
    for (std::size_t k = 1; k < 25; ++k) {
        residuals[k] = .0002 + .8 * residuals[k - 1] + .00005 * std::sin(1.73 * static_cast<double>(k + day));
    }
    std::vector<MinuteBar> result;
    for (std::size_t m = 0; m < 390; ++m) {
        double close = 100;
        if (shape == 0 && m >= 29 && m <= 149 && (m - 29) % 5 == 0) {
            close = 100 * std::exp(residuals[(m - 29) / 5]);
        }
        if (shape == 1) {
            close = 102;
        }
        if (shape == 2) {
            close = 99;
        }
        if ((shape == 3 || shape == 4) && m == 149) {
            close = 100 * std::exp(shape == 3 ? -.004 : .004);
        }
        const double open = (shape == 5 ? 99 : shape == 6 ? 101 : 100);
        result.emplace_back("SPY", session.start() + std::chrono::minutes{m},
                            Ohlc{open, std::max(open, close), std::min(open, close), close},
                            shape == 1 ? 200 : 100);
    }
    return result;
}
} // namespace

std::string_view name(ExecutionDesign design) {
    switch (design) {
    case ExecutionDesign::momentum:
        return "momentum";
    case ExecutionDesign::continuation_price:
        return "continuation_price";
    case ExecutionDesign::continuation_volume:
        return "continuation_volume";
    case ExecutionDesign::reversion:
        return "reversion";
    }
    throw std::invalid_argument("unknown execution design");
}

StrategyExecutionInputs StrategyExecutionDiagnostic::constructed_inputs() {
    const auto calendar = development_calendar({"2022-01-01", "2022-01-31"});
    const auto current = CalendarSession{"2022-02-01", "09:30", "16:00"}.bar_request();
    StrategyExecutionInputs result{{},
                                   bars(current, 20, 1),
                                   bars(current, 20, 2),
                                   bars(current, 20, 3),
                                   bars(current, 20, 4),
                                   bars(current, 20, 5),
                                   bars(current, 20, 6),
                                   current};
    for (std::size_t i = 0; i < calendar.size(); ++i) {
        const auto request = calendar[i].bar_request();
        auto values = bars(request, i, 0);
        const auto audit = audit_session(request, values);
        result.history.push_back(
            {request, std::move(values), {}, audit, request.start(), request.end_exclusive()});
    }
    return result;
}

StrategyExecutionResult StrategyExecutionDiagnostic::run() const {
    StrategyExecutionResult result{{}, {}, constructed_inputs(), {}, {}};
    for (const auto design : {ExecutionDesign::momentum, ExecutionDesign::continuation_price,
                              ExecutionDesign::continuation_volume, ExecutionDesign::reversion}) {
        SignalBridge full{design, result.inputs, 0};
        result.cases.push_back(ScenarioRunner{directory_, design, "full", 100000, 0, result.checks}.simple(
            full, false, 0, false));
        SignalBridge cash{design, result.inputs, 0, true};
        result.cases.push_back(ScenarioRunner{directory_, design, "cash", 100000, 0, result.checks}.simple(
            cash, true, 0, false));
        for (const bool restart : {false, true}) {
            SignalBridge partial{design, result.inputs, 1};
            result.cases.push_back(
                ScenarioRunner{directory_, design, restart ? "restart" : "partial", 1000000, 1, result.checks}
                    .partial(partial, restart));
        }
        const auto &a = result.cases[result.cases.size() - 2];
        const auto &b = result.cases.back();
        check(result.checks, std::string{name(design)} + ": interrupted/uninterrupted parity",
              a.fills == b.fills && a.reports == b.reports &&
                  a.frames.back().account == b.frames.back().account);
    }
    SignalBridge fees{ExecutionDesign::reversion, result.inputs, 1};
    result.cases.push_back(
        ScenarioRunner{directory_, ExecutionDesign::reversion, "aligned-fee", 100010, 1, result.checks}
            .simple(fees, false, 1, false));
    SignalBridge rounding{ExecutionDesign::reversion, result.inputs, 0};
    result.cases.push_back(
        ScenarioRunner{directory_, ExecutionDesign::reversion, "rounding", 100050, 0, result.checks}.simple(
            rounding, false, 0, true));
    return result;
}

void StrategyExecutionDiagnostic::historical(StrategyExecutionResult &result,
                                             const FrozenDevelopmentDataset &dataset) const {
    if (dataset.manifest_sha256 != reversion_manifest_digest || dataset.sessions.size() != 496 ||
        dataset.protocol_revision != 2 || dataset.manifest_file != "study_manifest.json" ||
        sha256_file(contained_path(dataset.directory, dataset.manifest_file)) != reversion_manifest_digest) {
        throw std::invalid_argument(
            "historical reference replay requires the verified frozen development snapshot");
    }
    const auto &current = dataset.sessions.at(20);
    StrategyExecutionInputs inputs{{},           current.bars, current.bars, current.bars,
                                   current.bars, current.bars, current.bars, current.request};
    inputs.history.assign(dataset.sessions.begin(), dataset.sessions.begin() + 20);
    for (const auto design : {ExecutionDesign::momentum, ExecutionDesign::continuation_price,
                              ExecutionDesign::continuation_volume, ExecutionDesign::reversion}) {
        SignalBridge signal{design, inputs, 0};
        result.cases.push_back(
            ScenarioRunner{directory_, design, "historical-proxy", 1000000, 0, result.checks, true}
                .historical(signal));
    }
    result.historical_manifest = dataset.manifest_sha256;
    result.historical_inputs = std::move(inputs);
}
} // namespace trading::execution
