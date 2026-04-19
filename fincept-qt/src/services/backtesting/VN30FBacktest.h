#pragma once
// VN30FBacktest — helpers for configuring and running backtests on VN30F data.
//
// Plugs into the existing BacktestingService infrastructure.  All heavy
// lifting (strategy simulation, walk-forward, optimisation) is done by the
// Python backtesting providers (vectorbt, backtesting.py, fincept).
// This file provides:
//   - Standard backtest configuration for VN30F (costs, slippage, data range)
//   - Suggested parameter grids for the four preset VN30F strategies
//   - Helper to build the QJsonObject that BacktestingService::execute() expects

#include "services/backtesting/BacktestingTypes.h"
#include "trading/vn30f/VN30FSettlement.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace fincept::services::backtest::vn30f {

// ── Cost model ────────────────────────────────────────────────────────────────

/// One-way commission rate (as fraction, not percent).
inline constexpr double kCommissionRate = 0.00235;

/// Slippage per trade in index points (accounts for bid-ask spread + impact).
inline constexpr double kSlippagePoints = 0.2;

/// Value of one index point in VND.
inline constexpr double kPointValue = fincept::trading::vn30f::kPointValue;

// ── Data range ────────────────────────────────────────────────────────────────

/// Earliest available VN30F tick/OHLCV data (HNX launched VN30F in 2017-08-10).
inline const QString kDataStartDate = "2017-08-10";

/// Recommended minimum in-sample window for statistical reliability.
inline const QString kRecommendedFromDate = "2019-01-01";

// ── Configuration builder ─────────────────────────────────────────────────────

/// Build the JSON config object passed to BacktestingService::execute().
///
/// @param provider     One of "vectorbt", "backtestingpy", "fincept".
/// @param strategy_id  Strategy identifier (matches Python strategy class name).
/// @param symbol       Contract to backtest, e.g. "VN30F1!" for continuous.
/// @param from_date    Start date "YYYY-MM-DD".
/// @param to_date      End date   "YYYY-MM-DD".
/// @param capital      Starting capital in VND.
/// @param params       Strategy-specific parameter overrides (optional).
inline QJsonObject build_backtest_config(const QString& provider,
                                          const QString& strategy_id,
                                          const QString& symbol      = "VN30F1!",
                                          const QString& from_date   = kRecommendedFromDate,
                                          const QString& to_date     = "",
                                          double         capital     = 100'000'000.0,
                                          const QJsonObject& params  = {}) {
    QJsonObject cfg;
    cfg["provider"]   = provider;
    cfg["strategy"]   = strategy_id;
    cfg["symbol"]     = symbol;
    cfg["exchange"]   = "HNX";
    cfg["from_date"]  = from_date;
    cfg["to_date"]    = to_date.isEmpty()
                            ? QDate::currentDate().toString("yyyy-MM-dd")
                            : to_date;
    cfg["capital"]    = capital;
    cfg["currency"]   = "VND";
    cfg["commission"] = kCommissionRate;
    cfg["slippage"]   = kSlippagePoints;
    cfg["point_value"]= kPointValue;

    if (!params.isEmpty())
        cfg["params"] = params;

    return cfg;
}

// ── Parameter grids for each preset strategy ─────────────────────────────────

/// Optimisation grid for the Breakout strategy.
inline QJsonObject breakout_param_grid() {
    QJsonObject g;
    g["lookback"]    = QJsonArray{5, 10, 20};    // session-high/low lookback periods
    g["volume_mult"] = QJsonArray{1.2, 1.5, 2.0};// volume threshold multiplier
    g["stop_loss"]   = QJsonArray{3.0, 5.0, 8.0};
    g["take_profit"] = QJsonArray{9.0, 15.0, 24.0};
    return g;
}

/// Optimisation grid for the Mean Reversion strategy.
inline QJsonObject mean_reversion_param_grid() {
    QJsonObject g;
    g["rsi_period"]  = QJsonArray{9, 14, 21};
    g["rsi_oversold"]= QJsonArray{25, 30, 35};
    g["bb_period"]   = QJsonArray{15, 20, 25};
    g["bb_std"]      = QJsonArray{1.5, 2.0, 2.5};
    g["stop_loss"]   = QJsonArray{5.0, 8.0, 10.0};
    return g;
}

/// Optimisation grid for the Momentum strategy.
inline QJsonObject momentum_param_grid() {
    QJsonObject g;
    g["macd_fast"]   = QJsonArray{8, 12, 16};
    g["macd_slow"]   = QJsonArray{21, 26, 30};
    g["ema_period"]  = QJsonArray{30, 50, 100};
    g["adx_min"]     = QJsonArray{20, 25, 30};
    g["trailing"]    = QJsonArray{3.0, 5.0, 8.0};
    return g;
}

/// Optimisation grid for the VWAP strategy.
inline QJsonObject vwap_param_grid() {
    QJsonObject g;
    g["rsi_confirm"] = QJsonArray{40, 45, 50};
    g["adx_min"]     = QJsonArray{15, 20, 25};
    g["stop_loss"]   = QJsonArray{2.0, 3.0, 4.0};
    g["take_profit"] = QJsonArray{4.0, 6.0, 8.0};
    return g;
}

// ── Walk-forward configuration ────────────────────────────────────────────────

/// Standard walk-forward window settings for VN30F.
/// In-sample: 12 months, out-of-sample: 3 months, step: 1 month.
inline QJsonObject walk_forward_config() {
    QJsonObject wf;
    wf["in_sample_months"]   = 12;
    wf["out_sample_months"]  = 3;
    wf["step_months"]        = 1;
    wf["anchored"]           = false; // rolling window (not anchored/expanding)
    return wf;
}

// ── Performance metric thresholds ─────────────────────────────────────────────

/// Minimum acceptable Sharpe ratio for live deployment consideration.
inline constexpr double kMinSharpe = 1.0;

/// Maximum acceptable drawdown (fraction, e.g. 0.15 = 15%).
inline constexpr double kMaxDrawdown = 0.15;

/// Minimum win rate (fraction, e.g. 0.45 = 45%).
inline constexpr double kMinWinRate = 0.45;

/// Minimum number of trades for a statistically meaningful backtest.
inline constexpr int kMinTrades = 30;

/// Check whether a backtest result meets deployment thresholds.
/// @param result  The QJsonObject from BacktestingService::result_ready signal.
inline bool meets_deployment_thresholds(const QJsonObject& result) {
    const double sharpe   = result.value("sharpe_ratio").toDouble();
    const double drawdown = qAbs(result.value("max_drawdown").toDouble());
    const double win_rate = result.value("win_rate").toDouble();
    const int    trades   = result.value("total_trades").toInt();
    return sharpe   >= kMinSharpe
        && drawdown <= kMaxDrawdown
        && win_rate >= kMinWinRate
        && trades   >= kMinTrades;
}

} // namespace fincept::services::backtest::vn30f
