// VN30FRiskManager — implementation.

#include "services/vn30f/VN30FRiskManager.h"

#include "core/logging/Logger.h"
#include "trading/vn30f/VN30FSettlement.h"

#include <algorithm>
#include <cmath>

namespace fincept::services::vn30f {

VN30FRiskManager::VN30FRiskManager(const VN30FRiskConfig& config, QObject* parent)
    : QObject(parent), config_(config) {}

// ── Session management ────────────────────────────────────────────────────────

void VN30FRiskManager::begin_session(double starting_equity_vnd) {
    kill_switch_active_  = false;
    session_start_equity_ = starting_equity_vnd;
    session_peak_equity_  = starting_equity_vnd;
    realised_pnl_today_   = 0.0;
    unrealised_pnl_       = 0.0;
    LOG_INFO("VN30FRiskManager",
             QString("Session started. Equity: %1 VND").arg(starting_equity_vnd));
}

void VN30FRiskManager::end_session() {
    LOG_INFO("VN30FRiskManager",
             QString("Session ended. Realised PnL: %1 VND").arg(realised_pnl_today_));
    realised_pnl_today_ = 0.0;
    unrealised_pnl_     = 0.0;
}

bool VN30FRiskManager::is_kill_switch_active() const {
    return kill_switch_active_;
}

// ── PnL tracking ──────────────────────────────────────────────────────────────

void VN30FRiskManager::on_trade_filled(double pnl_vnd) {
    realised_pnl_today_ += pnl_vnd;
    LOG_INFO("VN30FRiskManager",
             QString("Trade filled. PnL: %1 VND. Daily total: %2 VND")
                 .arg(pnl_vnd).arg(realised_pnl_today_));
    check_and_trigger_kill_switch();
}

void VN30FRiskManager::on_tick(const fincept::trading::VN30FTick& /*tick*/) {
    // Unrealised PnL is updated externally via set_unrealised_pnl().
    // Tick hook is provided for future extensions (e.g. trailing stop logic).
    check_and_trigger_kill_switch();
}

void VN30FRiskManager::set_unrealised_pnl(double pnl_vnd) {
    unrealised_pnl_ = pnl_vnd;

    // Track session peak equity for drawdown calculation
    const double current_equity = session_start_equity_ + realised_pnl_today_ + unrealised_pnl_;
    if (current_equity > session_peak_equity_)
        session_peak_equity_ = current_equity;

    check_and_trigger_kill_switch();
}

double VN30FRiskManager::realised_pnl_today() const {
    return realised_pnl_today_;
}

double VN30FRiskManager::unrealised_pnl() const {
    return unrealised_pnl_;
}

double VN30FRiskManager::total_pnl_today() const {
    return realised_pnl_today_ + unrealised_pnl_;
}

// ── Order pre-check ───────────────────────────────────────────────────────────

bool VN30FRiskManager::can_place_order(int lots, int current_open_lots) const {
    if (kill_switch_active_) {
        LOG_WARN("VN30FRiskManager", "Order rejected — kill switch is active");
        return false;
    }
    if (lots > config_.max_order_lots) {
        LOG_WARN("VN30FRiskManager",
                 QString("Order rejected — lots %1 exceeds max_order_lots %2")
                     .arg(lots).arg(config_.max_order_lots));
        return false;
    }
    if (current_open_lots + lots > config_.max_open_lots) {
        LOG_WARN("VN30FRiskManager",
                 QString("Order rejected — would exceed max_open_lots %1")
                     .arg(config_.max_open_lots));
        return false;
    }
    return true;
}

// ── Position sizing ───────────────────────────────────────────────────────────

int VN30FRiskManager::recommended_lots(double equity_vnd, double stop_pts,
                                        double win_rate, double avg_win_pts,
                                        double avg_loss_pts) const {
    if (stop_pts <= 0.0)
        return config_.fixed_lots;

    int lots = 1;
    using SizingMode = VN30FRiskConfig::SizingMode;

    switch (config_.sizing_mode) {
        case SizingMode::FixedLots:
            lots = config_.fixed_lots;
            break;

        case SizingMode::PercentRisk: {
            // Risk = risk_pct * equity
            // Risk per lot = stop_pts * point_value
            const double risk_budget = equity_vnd * config_.risk_pct_per_trade;
            const double risk_per_lot =
                stop_pts * fincept::trading::vn30f::kPointValue;
            lots = static_cast<int>(std::floor(risk_budget / risk_per_lot));
            break;
        }

        case SizingMode::KellyFraction: {
            // Kelly f* = win_rate/avg_loss - (1-win_rate)/avg_win
            if (avg_loss_pts <= 0.0 || avg_win_pts <= 0.0)
                break;
            const double raw_kelly =
                win_rate / avg_loss_pts - (1.0 - win_rate) / avg_win_pts;
            const double kelly = std::max(0.0, raw_kelly * config_.kelly_fraction);
            const double risk_budget = equity_vnd * kelly;
            const double risk_per_lot =
                stop_pts * fincept::trading::vn30f::kPointValue;
            lots = static_cast<int>(std::floor(risk_budget / risk_per_lot));
            break;
        }
    }

    // Clamp to configured limits
    lots = std::max(1, std::min(lots, config_.max_order_lots));
    return lots;
}

// ── Kill switch logic ─────────────────────────────────────────────────────────

void VN30FRiskManager::check_and_trigger_kill_switch() {
    if (kill_switch_active_)
        return;

    const double total_pnl  = total_pnl_today();
    const double limit      = -std::abs(config_.max_daily_loss_vnd);
    const double warn_level = limit * 0.8; // 80% of limit

    // Warning
    if (total_pnl < warn_level) {
        emit daily_loss_warning(total_pnl, config_.max_daily_loss_vnd);
    }

    // Hard stop: daily loss limit
    if (total_pnl <= limit) {
        kill_switch_active_ = true;
        const QString reason =
            QString("Daily loss limit breached: %1 VND (limit: %2 VND)")
                .arg(total_pnl).arg(limit);
        LOG_WARN("VN30FRiskManager", reason);
        emit kill_switch_triggered(reason);
        return;
    }

    // Hard stop: session drawdown from peak
    if (session_peak_equity_ > 0.0) {
        const double current_equity =
            session_start_equity_ + total_pnl;
        const double drawdown =
            (session_peak_equity_ - current_equity) / session_peak_equity_;
        if (drawdown >= config_.max_session_drawdown) {
            kill_switch_active_ = true;
            const QString reason =
                QString("Session drawdown limit breached: %.2f%% (limit: %.2f%%)")
                    .arg(drawdown * 100.0)
                    .arg(config_.max_session_drawdown * 100.0);
            LOG_WARN("VN30FRiskManager", reason);
            emit kill_switch_triggered(reason);
        }
    }
}

} // namespace fincept::services::vn30f
