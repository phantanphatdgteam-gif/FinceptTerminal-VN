#pragma once
// VN30FPaperTrading — convenience wrappers around the generic paper trading
// engine that pre-configure VN30F-specific parameters.
//
// VN30F specifics applied here:
//   • Currency  : VND
//   • Fee rate  : 0.235 % per side = 0.47 % round-trip (typical broker rate)
//   • Leverage  : 1 / kInitialMarginRate ≈ 8.33×
//   • Margin mode: "cross" (single daily settlement, not per-position isolated)
//   • Exchange  : "HNX" (VN30F is listed on Hanoi Stock Exchange)
//
// These wrappers call the plain pt_* functions from PaperTrading.h with
// the correct defaults so callers do not need to remember every constant.

#include "trading/PaperTrading.h"
#include "trading/vn30f/VN30FSettlement.h"

namespace fincept::trading::vn30f {

// ── Default portfolio parameters ──────────────────────────────────────────────

/// Standard fee rate per side (0.235 % of notional).
inline constexpr double kFeeRatePerSide = 0.00235;

/// Round-trip (open + close) fee rate.
inline constexpr double kFeeRateRoundTrip = kFeeRatePerSide * 2.0;

/// Default starting capital for a VN30F paper account (10 million VND).
inline constexpr double kDefaultCapital = 10'000'000.0;

/// Implied leverage from initial margin rate.
inline constexpr double kLeverage = 1.0 / kInitialMarginRate; // ≈ 8.33

// ── Portfolio factory ─────────────────────────────────────────────────────────

/// Create a paper-trading portfolio pre-configured for VN30F.
/// @param name     Human-readable name for the portfolio.
/// @param capital  Starting cash balance in VND.
inline PtPortfolio create_vn30f_portfolio(const QString& name,
                                          double capital = kDefaultCapital) {
    return pt_create_portfolio(
        name,
        capital,
        "VND",
        kLeverage,
        "cross",
        kFeeRatePerSide,
        "HNX"
    );
}

// ── Order helpers ─────────────────────────────────────────────────────────────

/// Place a VN30F market order (long entry or short entry).
/// @param portfolio_id  ID of the paper portfolio.
/// @param symbol        Contract code, e.g. "VN30F2406".
/// @param side          "buy" (long) or "sell" (short).
/// @param lots          Number of contracts.
inline PtOrder place_market_order(const QString& portfolio_id,
                                  const QString& symbol,
                                  const QString& side,
                                  int lots) {
    return pt_place_order(portfolio_id, symbol, side, "market",
                          static_cast<double>(lots));
}

/// Place a VN30F limit order.
inline PtOrder place_limit_order(const QString& portfolio_id,
                                 const QString& symbol,
                                 const QString& side,
                                 int lots,
                                 double limit_price) {
    return pt_place_order(portfolio_id, symbol, side, "limit",
                          static_cast<double>(lots), limit_price);
}

/// Place a VN30F stop-loss order (reduce-only).
inline PtOrder place_stop_order(const QString& portfolio_id,
                                const QString& symbol,
                                const QString& side,
                                int lots,
                                double stop_price) {
    return pt_place_order(portfolio_id, symbol, side, "stop",
                          static_cast<double>(lots),
                          std::nullopt, stop_price,
                          /*reduce_only=*/true);
}

// ── Settlement ────────────────────────────────────────────────────────────────

/// Force-settle all open VN30F positions at the given settlement price.
/// Call this on expiry Thursday after market close.
///
/// For each open position in @p portfolio_id whose symbol starts with
/// "VN30F", a synthetic fill is applied at @p settlement_price and the
/// position is closed.
///
/// @param portfolio_id       Paper portfolio to settle.
/// @param settlement_price   Official HNX final settlement price (index pts).
inline void settle_expiry(const QString& portfolio_id, double settlement_price) {
    const auto positions = pt_get_positions(portfolio_id);
    for (const auto& pos : positions) {
        if (!pos.symbol.startsWith("VN30F", Qt::CaseInsensitive))
            continue;
        // Find the open order or create a synthetic close order
        const QString close_side = (pos.side == "long") ? "sell" : "buy";
        auto close_order = pt_place_order(
            portfolio_id, pos.symbol, close_side, "market",
            pos.quantity, std::nullopt, std::nullopt, /*reduce_only=*/true);
        pt_fill_order(close_order.id, settlement_price);
    }
}

} // namespace fincept::trading::vn30f
