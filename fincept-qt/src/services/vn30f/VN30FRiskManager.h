#pragma once
// VN30FRiskManager — position sizing, daily loss limit, and kill switch
// for VN30F auto-trading deployments.
//
// This service is meant to run alongside VN30FProducer and AlgoTradingService.
// It listens to filled-trade events and live PnL updates, and automatically:
//   1. Rejects or reduces new orders that would exceed position limits.
//   2. Triggers a kill switch when daily loss exceeds the configured threshold.
//   3. Computes recommended lot sizes based on fixed-risk or percent-of-capital models.
//
// All thresholds are denominated in VND.

#include "trading/websocket/VN30FTickTypes.h"
#include "trading/vn30f/VN30FSettlement.h"

#include <QObject>
#include <QString>

namespace fincept::services::vn30f {

// ── Risk configuration ────────────────────────────────────────────────────────

struct VN30FRiskConfig {
    // Daily loss limit: auto-stop if realised + unrealised PnL < -threshold (VND)
    double max_daily_loss_vnd = 1'000'000.0; // 1 million VND default

    // Maximum open position (number of contracts)
    int max_open_lots = 3;

    // Maximum single-order size
    int max_order_lots = 2;

    // Kill switch: if portfolio drawdown from session peak exceeds this fraction, halt
    double max_session_drawdown = 0.05; // 5% of session starting equity

    // Trailing stop in index points (0 = disabled)
    double trailing_stop_pts = 5.0;

    // Take profit in index points (0 = disabled at risk layer; strategy may set its own)
    double take_profit_pts = 15.0;

    // Position sizing mode
    enum class SizingMode {
        FixedLots,      // always trade fixed_lots
        PercentRisk,    // risk risk_pct_per_trade of capital per trade
        KellyFraction,  // use Kelly criterion (requires win_rate + avg_win/avg_loss)
    };
    SizingMode sizing_mode = SizingMode::FixedLots;
    int    fixed_lots           = 1;
    double risk_pct_per_trade   = 0.01; // 1% of capital per trade
    double kelly_fraction       = 0.25; // fractional Kelly (conservative)
};

// ── VN30FRiskManager ──────────────────────────────────────────────────────────

class VN30FRiskManager : public QObject {
    Q_OBJECT
  public:
    explicit VN30FRiskManager(const VN30FRiskConfig& config, QObject* parent = nullptr);

    // ── Session management ────────────────────────────────────────────────────

    /// Call once at session start (09:00 ICT) with current portfolio equity.
    void begin_session(double starting_equity_vnd);

    /// Call at session end (14:45 ICT) to reset daily accumulators.
    void end_session();

    bool is_kill_switch_active() const;

    // ── PnL tracking ─────────────────────────────────────────────────────────

    /// Called when a trade fills — updates realised PnL.
    /// @param pnl_vnd  Signed PnL in VND (positive = profit, negative = loss).
    void on_trade_filled(double pnl_vnd);

    /// Called on each tick for open positions — updates unrealised PnL.
    void on_tick(const fincept::trading::VN30FTick& tick);

    /// Set current unrealised PnL directly (e.g. from broker API poll).
    void set_unrealised_pnl(double pnl_vnd);

    double realised_pnl_today() const;
    double unrealised_pnl() const;
    double total_pnl_today() const;

    // ── Order pre-check ───────────────────────────────────────────────────────

    /// Returns true if a new order with @p lots contracts is allowed.
    /// Checks open position limit, order size limit, and kill switch.
    /// @param current_open_lots  Current net open position (abs value).
    bool can_place_order(int lots, int current_open_lots) const;

    // ── Position sizing ───────────────────────────────────────────────────────

    /// Compute the recommended number of lots given current equity and
    /// the distance to the stop-loss level.
    ///
    /// @param equity_vnd   Current portfolio equity in VND.
    /// @param stop_pts     Distance from entry to stop in index points.
    /// @param win_rate     Historical win rate [0,1] (used for Kelly mode).
    /// @param avg_win_pts  Average winning trade size in index points (Kelly).
    /// @param avg_loss_pts Average losing trade size in index points (Kelly).
    int recommended_lots(double equity_vnd, double stop_pts,
                         double win_rate = 0.5,
                         double avg_win_pts = 10.0,
                         double avg_loss_pts = 5.0) const;

  signals:
    /// Emitted when the kill switch fires — callers must stop all new orders
    /// and optionally close all open positions.
    void kill_switch_triggered(const QString& reason);

    /// Emitted when daily loss crosses the warning threshold (80% of max).
    void daily_loss_warning(double current_loss_vnd, double limit_vnd);

  private:
    void check_and_trigger_kill_switch();

    VN30FRiskConfig config_;
    bool   kill_switch_active_ = false;
    double session_start_equity_ = 0.0;
    double session_peak_equity_  = 0.0;
    double realised_pnl_today_   = 0.0;
    double unrealised_pnl_       = 0.0;
};

} // namespace fincept::services::vn30f
