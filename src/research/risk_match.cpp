#include "risk_match.hpp"
#include "utc_timestamp.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace trading {
namespace {
using Json = nlohmann::json;
Json value(std::optional<double> v) { return v ? Json(*v) : Json(nullptr); }
void require(bool yes, const char *reason) {
    if (!yes)
        throw std::invalid_argument(reason);
}
} // namespace
Json RiskMatchSizing::json() const {
    return {{"first", first},
            {"last", last},
            {"slots", slots},
            {"valid", valid},
            {"candidate_volatility", value(candidate_volatility)},
            {"control_volatility", value(control_volatility)},
            {"target", value(target)},
            {"candidate_scale", value(candidate_scale)},
            {"control_scale", value(control_scale)}};
}
RiskMatchSizing RiskMatchWindow::prior(const std::string &date) const {
    RiskMatchSizing out;
    if (history_.empty())
        return out;
    require(history_.back().first < date, "risk matching cannot consume the current or future return");
    out.first = history_.front().first;
    out.last = history_.back().first;
    out.slots = history_.size();
    long double a = 0, b = 0;
    for (const auto &[d, p] : history_) {
        static_cast<void>(d);
        if (p) {
            ++out.valid;
            a += p->candidate;
            b += p->control;
        }
    }
    if (out.slots != 252 || out.valid != 252)
        return out;
    a /= 252;
    b /= 252;
    long double va = 0, vb = 0;
    for (const auto &[d, p] : history_) {
        static_cast<void>(d);
        va += (p->candidate - a) * (p->candidate - a);
        vb += (p->control - b) * (p->control - b);
    }
    out.candidate_volatility = static_cast<double>(std::sqrt(252 * va / 251));
    out.control_volatility = static_cast<double>(std::sqrt(252 * vb / 251));
    if (std::all_of(history_.begin(), history_.end(), [&](const auto &v) {
            return v.second->candidate == history_.front().second->candidate;
        }))
        out.candidate_volatility = 0;
    if (std::all_of(history_.begin(), history_.end(), [&](const auto &v) {
            return v.second->control == history_.front().second->control;
        }))
        out.control_volatility = 0;
    require(std::isfinite(*out.candidate_volatility) && std::isfinite(*out.control_volatility),
            "unit volatility exceeds finite range");
    if (*out.candidate_volatility <= 0 || *out.control_volatility <= 0)
        return out;
    out.target = std::min({.10, *out.candidate_volatility, *out.control_volatility});
    out.candidate_scale = *out.target / *out.candidate_volatility;
    out.control_scale = *out.target / *out.control_volatility;
    return out;
}
void RiskMatchWindow::commit(const std::string &date, std::optional<StabilityPair> p) {
    require(history_.empty() || date > history_.back().first,
            "unit-account slots must be ordered and unique");
    if (p)
        require(std::isfinite(p->candidate) && std::isfinite(p->control) && p->candidate > -1 &&
                    p->control > -1,
                "invalid unit-account return");
    history_.emplace_back(date, p);
    if (history_.size() > 252)
        history_.pop_front();
}
nlohmann::json IntradayAccountLedger::append(const PanelReplayDay &day, const SessionDownload &market,
                                             const PanelPolicyReplay *policy, bool ready,
                                             std::optional<double> scale) {
    require(day.date < "2024-01-01" && market.request.symbol() == day.symbol &&
                (previous_date_.empty() || day.date > previous_date_),
            "intraday ledger calendar/identity differs");
    if (scale)
        require(std::isfinite(*scale) && *scale >= 0 && *scale <= 1,
                "risk matching cannot borrow or lever");
    previous_date_ = day.date;
    const auto begin = account_.ledger().size();
    std::string status = "awaiting_history";
    double minutes = 0;
    std::size_t trades = 0;
    const bool excluded = day.availability.diagnostic_excluded;
    if (!stopped_.empty())
        status = "stopped";
    else if (excluded) {
        require(!held_, "excluded date reached with an unresolved position");
        status = "excluded";
    } else if (!day.availability.regular_session) {
        require(!held_, "shortened session reached with an open position");
        status = started_ ? "observed" : "awaiting_history";
    } else if ((!ready || !scale) && started_) {
        stopped_ = "required_prior_overlay_unavailable";
        status = "stopped";
    } else if (ready && scale) {
        require(policy != nullptr, "regular intraday ledger lacks its frozen policy");
        started_ = true;
        status = "observed";
        const ResearchSessionView view{market.request, market.bars};
        std::optional<std::chrono::sys_seconds> entry;
        for (const auto &decision : policy->decisions) {
            if (!decision.target_long) {
                stopped_ = "unresolved_frozen_target";
                break;
            }
            const bool desired = *decision.target_long;
            if (desired == held_)
                continue;
            const auto at = decision.at + std::chrono::minutes{1};
            const auto slot =
                std::chrono::duration_cast<std::chrono::minutes>(at - market.request.start()).count();
            const auto *bar = view.at(static_cast<std::size_t>(slot));
            if (!bar) {
                stopped_ = "required_execution_open_unavailable";
                break;
            }
            account_.rebalance(at, bar->prices().open, desired ? *scale : 0, 1);
            last_price_ = bar->prices().open;
            if (desired)
                entry = decision.at;
            else {
                require(entry.has_value(), "intraday account exit lacks an entry");
                minutes += static_cast<double>(
                    std::chrono::duration_cast<std::chrono::minutes>(decision.at - *entry).count());
                ++trades;
                entry.reset();
            }
            held_ = desired;
        }
        if (stopped_.empty())
            require(!held_, "intraday account lacks terminal liquidation");
        else
            status = "stopped";
    }
    Json events = Json::array();
    double cost = 0, turnover = 0;
    for (auto i = begin; i < account_.ledger().size(); ++i) {
        const auto &e = account_.ledger()[i];
        cost += e.cost_usd;
        turnover += e.traded_notional_usd;
        events.push_back({{"at", format_utc_seconds(e.at)},
                          {"kind", e.kind},
                          {"price", e.reference_price_usd},
                          {"notional", e.traded_notional_usd},
                          {"cost", e.cost_usd},
                          {"cash", e.value.cash_usd},
                          {"shares", e.value.shares}});
    }
    const auto v = account_.value(last_price_);
    Json r = nullptr;
    if (status == "observed") {
        r = v.equity_usd / previous_equity_ - 1;
        previous_equity_ = v.equity_usd;
    }
    return {{"date", day.date},
            {"symbol", day.symbol},
            {"status", status},
            {"stop_reason", stopped_},
            {"net_return", r},
            {"scale", value(scale)},
            {"events", events},
            {"cost", cost},
            {"turnover", turnover},
            {"cash", v.cash_usd},
            {"shares", v.shares},
            {"equity", v.equity_usd},
            {"minutes_held", status == "observed" ? Json(minutes) : Json(nullptr)},
            {"exposure", status == "observed" ? Json(scale.value_or(0) * minutes / 390) : Json(nullptr)},
            {"trades", status == "observed" ? Json(trades) : Json(nullptr)}};
}
StabilityObservation ledger_observation(const Json &r, bool risk) {
    StabilityObservation o;
    if (r.at("status").get<std::string>() == "observed" && !r.at("net_return").is_null()) {
        o.net = r.at("net_return").get<double>();
        if (!risk) {
            o.holding_minutes = r.at("minutes_held").get<double>();
            o.exposure = r.at("exposure").get<double>();
            o.trades = r.at("trades").get<double>();
        } else {
            const auto &v = r.at("value");
            o.exposure = v.at("shares").get<double>() * r.at("last_price").get<double>() /
                         v.at("liquid").get<double>();
        }
    }
    if (!risk) {
        o.cost = r.at("cost").get<double>() / 100000;
        o.turnover = r.at("turnover").get<double>() / 100000;
    } else {
        o.cost = 0;
        o.turnover = 0;
        for (const auto &e : r.at("events")) {
            *o.cost += e.at("cost").get<double>() / 100000;
            *o.turnover += std::abs(e.at("notional").get<double>()) / 100000;
        }
    }
    return o;
}
} // namespace trading
