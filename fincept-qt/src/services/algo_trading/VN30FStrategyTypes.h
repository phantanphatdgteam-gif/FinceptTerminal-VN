#pragma once
// VN30FStrategyTypes — pre-built strategy templates for VN30F futures.
//
// These templates use the same AlgoStrategy / ConditionDef infrastructure as
// the rest of the algo-trading system (AlgoTradingTypes.h) so they can be
// saved, backtested, and deployed through the existing AlgoTradingService
// without any changes.
//
// Available strategy templates:
//   1. Breakout     — enters on price/volume breakout of previous session high/low
//   2. MeanReversion — fades extreme RSI moves back toward VWAP
//   3. Momentum     — follows MACD + EMA trend confirmation
//   4. VWAP         — buys dips to VWAP in an uptrend, sells rallies in downtrend

#include "services/algo_trading/AlgoTradingTypes.h"

namespace fincept::services::algo::vn30f {

// ── Helper: build a ConditionDef JSON object ──────────────────────────────────

inline QJsonObject make_condition(const QString& indicator,
                                   const QJsonObject& params,
                                   const QString& field,
                                   const QString& op,
                                   double value) {
    QJsonObject c;
    c["indicator"] = indicator;
    c["params"]    = params;
    c["field"]     = field;
    c["operator"]  = op;
    c["value"]     = value;
    return c;
}

// ── 1. Breakout Strategy ──────────────────────────────────────────────────────
//
// Logic:
//   Entry Long  : Close > previous-session high  AND  Volume > 20-session avg volume
//   Entry Short : Close < previous-session low   AND  Volume > 20-session avg volume
//   Exit Long   : RSI(14) > 70  OR  trailing stop
//   Exit Short  : RSI(14) < 30  OR  trailing stop
//
// Best timeframe: 5m, 15m intraday session

inline AlgoStrategy breakout_strategy() {
    AlgoStrategy s;
    s.name        = "VN30F Breakout";
    s.description = "Enters on high-volume price breakout of the previous session range. "
                    "Suitable for trending intraday sessions on VN30F.";
    s.timeframe   = "5m";
    s.entry_logic = "AND";
    s.exit_logic  = "OR";
    s.stop_loss     = 5.0;   // 5 index points stop loss
    s.take_profit   = 15.0;  // 15 index points target (1:3 risk/reward)
    s.trailing_stop = 3.0;   // trail by 3 points after breakout

    // Entry conditions
    s.entry_conditions.append(
        make_condition("CLOSE", {}, "value", "crosses_above",
                       0 /* placeholder; strategy engine fills in prev-high */));
    s.entry_conditions.append(
        make_condition("VOLUME", {}, "value", ">", 0 /* engine fills 20-period avg */));

    // Exit conditions
    s.exit_conditions.append(make_condition("RSI", {{"period", 14}}, "value", ">", 70));

    return s;
}

// ── 2. Mean Reversion Strategy ────────────────────────────────────────────────
//
// Logic:
//   Entry Long  : RSI(14) < 30  AND  Close < Lower Bollinger(20, 2)  AND  Close > VWAP
//   Entry Short : RSI(14) > 70  AND  Close > Upper Bollinger(20, 2)  AND  Close < VWAP
//   Exit        : Close crosses_above VWAP (for long)  OR  RSI returns to 50
//
// Best timeframe: 15m, 30m — captures mean-reversion after sharp intraday moves

inline AlgoStrategy mean_reversion_strategy() {
    AlgoStrategy s;
    s.name        = "VN30F Mean Reversion";
    s.description = "Fades oversold/overbought extremes back toward VWAP. "
                    "Works well in range-bound VN30F sessions.";
    s.timeframe   = "15m";
    s.entry_logic = "AND";
    s.exit_logic  = "OR";
    s.stop_loss     = 8.0;
    s.take_profit   = 12.0;
    s.trailing_stop = 0.0;

    // Long entry: oversold extremes
    s.entry_conditions.append(make_condition("RSI", {{"period", 14}}, "value", "<", 30));
    s.entry_conditions.append(
        make_condition("BOLLINGER", {{"period", 20}, {"std_dev", 2}}, "lower", ">",
                       0 /* close < lower band — engine inverts comparison */));
    s.entry_conditions.append(make_condition("VWAP", {}, "value", "<", 0 /* close > VWAP */));

    // Exit: price returns to VWAP
    s.exit_conditions.append(make_condition("RSI", {{"period", 14}}, "value", ">", 50));
    s.exit_conditions.append(make_condition("VWAP", {}, "value", "crosses_above", 0));

    return s;
}

// ── 3. Momentum Strategy (MACD + EMA Trend) ──────────────────────────────────
//
// Logic:
//   Entry Long  : MACD histogram > 0  AND  Close > EMA(50)  AND  ADX(14) > 25
//   Entry Short : MACD histogram < 0  AND  Close < EMA(50)  AND  ADX(14) > 25
//   Exit Long   : MACD line crosses_below signal_line
//   Exit Short  : MACD line crosses_above signal_line
//
// Best timeframe: 15m, 1h — trend-following on medium-term moves

inline AlgoStrategy momentum_strategy() {
    AlgoStrategy s;
    s.name        = "VN30F Momentum";
    s.description = "Trend-following strategy combining MACD momentum with EMA trend "
                    "filter and ADX strength confirmation.";
    s.timeframe   = "15m";
    s.entry_logic = "AND";
    s.exit_logic  = "OR";
    s.stop_loss     = 10.0;
    s.take_profit   = 25.0;
    s.trailing_stop = 5.0;

    // Entry conditions
    s.entry_conditions.append(
        make_condition("MACD", {{"fast", 12}, {"slow", 26}, {"signal", 9}},
                       "histogram", ">", 0));
    s.entry_conditions.append(make_condition("EMA", {{"period", 50}}, "value", "<", 0));
    s.entry_conditions.append(make_condition("ADX", {{"period", 14}}, "value", ">", 25));

    // Exit: MACD cross
    s.exit_conditions.append(
        make_condition("MACD", {{"fast", 12}, {"slow", 26}, {"signal", 9}},
                       "line", "crosses_below", 0));

    return s;
}

// ── 4. VWAP Strategy ──────────────────────────────────────────────────────────
//
// Logic:
//   Entry Long  : Close crosses_above VWAP  AND  RSI(14) > 45  AND  ADX(14) > 20
//   Entry Short : Close crosses_below VWAP  AND  RSI(14) < 55  AND  ADX(14) > 20
//   Exit        : Close crosses_below VWAP (long)  /  crosses_above VWAP (short)
//              OR  End of session (handled by deployment layer)
//
// Best timeframe: 1m, 3m, 5m — scalping around VWAP

inline AlgoStrategy vwap_strategy() {
    AlgoStrategy s;
    s.name        = "VN30F VWAP";
    s.description = "Intraday scalping around VWAP. Enters when price crosses back "
                    "through VWAP with RSI and ADX confirmation. Close all positions "
                    "before end of session.";
    s.timeframe   = "3m";
    s.entry_logic = "AND";
    s.exit_logic  = "OR";
    s.stop_loss     = 3.0;
    s.take_profit   = 6.0;
    s.trailing_stop = 2.0;

    // Entry conditions
    s.entry_conditions.append(make_condition("VWAP", {}, "value", "crosses_above", 0));
    s.entry_conditions.append(make_condition("RSI", {{"period", 14}}, "value", ">", 45));
    s.entry_conditions.append(make_condition("ADX", {{"period", 14}}, "value", ">", 20));

    // Exit: VWAP cross back
    s.exit_conditions.append(make_condition("VWAP", {}, "value", "crosses_below", 0));

    return s;
}

// ── Registry: all VN30F preset strategies ────────────────────────────────────

inline QVector<AlgoStrategy> vn30f_preset_strategies() {
    return {
        breakout_strategy(),
        mean_reversion_strategy(),
        momentum_strategy(),
        vwap_strategy(),
    };
}

// ── VN30F watchlist / default symbols ────────────────────────────────────────

/// Near-month and next-month VN30F contract codes (updated each roll).
inline QStringList vn30f_symbols() {
    return {"VN30F1!", "VN30F2!", "VN30F2406", "VN30F2407", "VN30F2408"};
}

/// Canonical continuous contract identifier (front-month roll).
inline QString vn30f_continuous() {
    return "VN30F1!";
}

} // namespace fincept::services::algo::vn30f
