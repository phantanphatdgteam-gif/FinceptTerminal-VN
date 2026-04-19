#pragma once
// VN30F tick types — used by SSIWebSocket and VN30FProducer.
//
// SSI FastConnect streams JSON frames over WebSocket.
// This file defines the normalised tick and depth structures that the
// rest of the application consumes.

#include <QDateTime>
#include <QString>

namespace fincept::trading {

/// One level of the order-book depth (bid or ask).
struct VN30FDepthLevel {
    double price = 0.0;
    int    volume = 0;
    int    orders = 0;
};

/// Normalised realtime tick received from SSI FastConnect.
///
/// SSI sends one JSON object per market-data event.  The WebSocket adapter
/// parses the raw JSON and populates this struct so the rest of the system
/// does not depend on the raw SSI wire format.
///
/// Fields that are not present in a given event are left at their zero
/// defaults (e.g. depth levels when receiving an LTP-only event).
struct VN30FTick {
    // ── Identity ────────────────────────────────────────────────────────────
    QString symbol;    // e.g. "VN30F2406" or canonical "VN30F1!"
    QString exchange;  // "HOSE" | "HNX" | "UPCOM"
    QString market_type; // "Futures" | "Stock" | "Index"

    // ── Price ────────────────────────────────────────────────────────────────
    double ltp = 0.0;           // last traded price
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;         // previous close / reference price
    double avg_price = 0.0;     // volume-weighted average price

    // ── Volume & OI ──────────────────────────────────────────────────────────
    long long volume = 0;       // total traded volume (lots)
    long long open_interest = 0; // open interest (contracts)
    long long oi_change = 0;    // OI change vs previous session

    // ── Derived ──────────────────────────────────────────────────────────────
    double change = 0.0;        // ltp - close
    double change_pct = 0.0;    // change / close * 100

    // ── Order book (best 3 levels each side for VN30F) ───────────────────────
    VN30FDepthLevel bids[3];
    VN30FDepthLevel asks[3];

    // ── Timing ───────────────────────────────────────────────────────────────
    QDateTime exchange_timestamp; // exchange-reported time
    QDateTime local_timestamp;    // time the tick was received locally

    // ── Status flags ─────────────────────────────────────────────────────────
    bool tradable = true;       // false during ATO/ATC matching or halt
    QString trading_status;     // "Open" | "ATO" | "ATC" | "Halt" | "Close"
};

} // namespace fincept::trading
