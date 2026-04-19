// SSIBroker — IBroker implementation for SSI Securities.

#include "trading/brokers/ssi/SSIBroker.h"

#include "core/logging/Logger.h"
#include "trading/brokers/BrokerHttp.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

namespace fincept::trading {

static int64_t ssi_now_ts() {
    return QDateTime::currentSecsSinceEpoch();
}

// ── Static helpers ────────────────────────────────────────────────────────────

QString SSIBroker::ssi_order_type(OrderType t) {
    switch (t) {
        case OrderType::Market:        return "MP";
        case OrderType::Limit:         return "LO";
        case OrderType::StopLoss:      return "SL";
        case OrderType::StopLossLimit: return "SL";
    }
    return "LO";
}

QString SSIBroker::ssi_side(OrderSide s) {
    return s == OrderSide::Buy ? "B" : "S";
}

QString SSIBroker::ssi_resolution(const QString& resolution) {
    static const QMap<QString, QString> map = {
        {"1",  "1"},   {"1m",  "1"},
        {"3",  "3"},   {"3m",  "3"},
        {"5",  "5"},   {"5m",  "5"},
        {"15", "15"},  {"15m", "15"},
        {"30", "30"},  {"30m", "30"},
        {"60", "60"},  {"1h",  "60"},
        {"1d", "1D"},  {"D",   "1D"},
        {"1w", "1W"},  {"W",   "1W"},
    };
    return map.value(resolution, "1D");
}

// ── Auth headers ──────────────────────────────────────────────────────────────

QMap<QString, QString> SSIBroker::auth_headers(const BrokerCredentials& creds) const {
    return {
        {"Authorization", "Bearer " + creds.access_token},
        {"Content-Type",  "application/json"},
    };
}

// ── Authentication ────────────────────────────────────────────────────────────
// SSI iBoard uses client_id / client_secret OAuth2 client-credentials flow.

TokenExchangeResponse SSIBroker::exchange_token(const QString& api_key,
                                                 const QString& api_secret,
                                                 const QString& /*auth_code*/) {
    QJsonObject body;
    body["clientId"]     = api_key;
    body["clientSecret"] = api_secret;

    const QString url = QString("%1/api/v2/Market/AccessToken").arg(base_url());
    auto resp = BrokerHttp::instance().post_json(url, body);

    TokenExchangeResponse result;
    if (!resp.success) {
        result.error = resp.error;
        return result;
    }

    // SSI returns: {"status": 200, "message": "...", "data": {"accessToken": "..."}}
    const auto data  = resp.json.value("data").toObject();
    const QString tok = data.value("accessToken").toString();
    if (tok.isEmpty()) {
        result.error = resp.json.value("message").toString("Auth failed");
        return result;
    }

    result.success      = true;
    result.access_token = tok;
    result.user_id      = data.value("accountNo").toString();
    return result;
}

// ── Orders ────────────────────────────────────────────────────────────────────

OrderPlaceResponse SSIBroker::place_order(const BrokerCredentials& creds,
                                           const UnifiedOrder& order) {
    QJsonObject body;
    body["accountNo"]  = creds.user_id;
    body["symbol"]     = order.symbol.toUpper();
    body["side"]       = ssi_side(order.side);
    body["orderType"]  = ssi_order_type(order.order_type);
    body["price"]      = order.price;
    body["quantity"]   = order.quantity;

    const QString url = QString("%1/iboard/api/order").arg(base_url());
    auto resp = BrokerHttp::instance().post_json(url, body, auth_headers(creds));

    OrderPlaceResponse result;
    if (!resp.success) {
        result.error = resp.error;
        return result;
    }

    result.success  = resp.json.value("result").toBool(false);
    result.order_id = resp.json.value("data").toObject().value("orderId").toString();
    if (!result.success)
        result.error = resp.json.value("message").toString("Order failed");

    LOG_INFO("SSIBroker", QString("place_order %1 %2 x%3 → %4")
             .arg(order.symbol, ssi_side(order.side))
             .arg(order.quantity).arg(result.order_id));
    return result;
}

ApiResponse<QJsonObject> SSIBroker::modify_order(const BrokerCredentials& creds,
                                                   const QString& order_id,
                                                   const QJsonObject& mods) {
    const QString url = QString("%1/iboard/api/order/%2").arg(base_url(), order_id);
    auto resp = BrokerHttp::instance().put_json(url, mods, auth_headers(creds));
    int64_t ts = ssi_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

ApiResponse<QJsonObject> SSIBroker::cancel_order(const BrokerCredentials& creds,
                                                   const QString& order_id) {
    const QString url = QString("%1/iboard/api/order/%2").arg(base_url(), order_id);
    auto resp = BrokerHttp::instance().del(url, auth_headers(creds));
    int64_t ts = ssi_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

ApiResponse<QVector<BrokerOrderInfo>> SSIBroker::get_orders(const BrokerCredentials& creds) {
    const QString url = QString("%1/iboard/api/order?accountNo=%2").arg(base_url(), creds.user_id);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = ssi_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerOrderInfo> orders;
    for (const auto& v : resp.json.value("data").toArray())
        orders.append(parse_order(v.toObject()));
    return {true, orders, "", ts};
}

ApiResponse<QJsonObject> SSIBroker::get_trade_book(const BrokerCredentials& creds) {
    const QString url = QString("%1/iboard/api/trade?accountNo=%2").arg(base_url(), creds.user_id);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = ssi_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

// ── Portfolio ─────────────────────────────────────────────────────────────────

ApiResponse<QVector<BrokerPosition>> SSIBroker::get_positions(const BrokerCredentials& creds) {
    const QString url =
        QString("%1/iboard/api/position?accountNo=%2").arg(base_url(), creds.user_id);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = ssi_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerPosition> positions;
    for (const auto& v : resp.json.value("data").toArray())
        positions.append(parse_position(v.toObject()));
    return {true, positions, "", ts};
}

ApiResponse<QVector<BrokerHolding>> SSIBroker::get_holdings(const BrokerCredentials& creds) {
    const QString url =
        QString("%1/iboard/api/portfolio?accountNo=%2").arg(base_url(), creds.user_id);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = ssi_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerHolding> holdings;
    for (const auto& v : resp.json.value("data").toArray())
        holdings.append(parse_holding(v.toObject()));
    return {true, holdings, "", ts};
}

ApiResponse<BrokerFunds> SSIBroker::get_funds(const BrokerCredentials& creds) {
    const QString url =
        QString("%1/iboard/api/account/%2/balance").arg(base_url(), creds.user_id);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = ssi_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    const auto data = resp.json.value("data").toObject();
    BrokerFunds funds;
    funds.available_balance = data.value("cashAvailable").toDouble();
    funds.used_margin       = data.value("marginUsed").toDouble();
    funds.total_balance     = data.value("totalAssets").toDouble();
    funds.collateral        = data.value("collateral").toDouble();
    funds.raw_data          = data;
    return {true, funds, "", ts};
}

// ── Market data ───────────────────────────────────────────────────────────────

ApiResponse<QVector<BrokerQuote>> SSIBroker::get_quotes(const BrokerCredentials& creds,
                                                         const QVector<QString>& symbols) {
    const QString sym_list = QStringList(symbols.begin(), symbols.end()).join(",");
    const QString url = QString("%1/api/v2/Market/Snapshot?symbols=%2").arg(base_url(), sym_list);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = ssi_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerQuote> quotes;
    const auto arr = resp.json.value("data").toArray();
    for (const auto& v : arr) {
        const auto o = v.toObject();
        BrokerQuote q;
        q.symbol     = o.value("Symbol").toString();
        q.ltp        = o.value("LastPrice").toDouble();
        q.open       = o.value("Open").toDouble();
        q.high       = o.value("High").toDouble();
        q.low        = o.value("Low").toDouble();
        q.close      = o.value("RefPrice").toDouble();
        q.volume     = o.value("AccumulatedVol").toDouble();
        q.change     = o.value("Change").toDouble();
        q.change_pct = o.value("RatioChange").toDouble();
        q.bid        = o.value("Best1Bid").toDouble();
        q.ask        = o.value("Best1Offer").toDouble();
        q.timestamp  = ts;
        quotes.append(q);
    }
    return {true, quotes, "", ts};
}

ApiResponse<QVector<BrokerCandle>> SSIBroker::get_history(const BrokerCredentials& creds,
                                                            const QString& symbol,
                                                            const QString& resolution,
                                                            const QString& from_date,
                                                            const QString& to_date) {
    const QString url =
        QString("%1/api/v2/Market/HistoryBar?symbol=%2&resolution=%3&from=%4&to=%5")
            .arg(base_url(), symbol.toUpper(), ssi_resolution(resolution),
                 from_date, to_date);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = ssi_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerCandle> candles;
    for (const auto& v : resp.json.value("data").toArray())
        candles.append(parse_candle(v.toObject()));
    return {true, candles, "", ts};
}

// ── Parsers ───────────────────────────────────────────────────────────────────

BrokerOrderInfo SSIBroker::parse_order(const QJsonObject& o) {
    BrokerOrderInfo info;
    info.order_id   = o.value("OrderId").toString();
    info.symbol     = o.value("Symbol").toString();
    info.side       = o.value("Side").toString() == "B" ? "buy" : "sell";
    info.order_type = o.value("OrderType").toString();
    info.quantity   = o.value("Qty").toDouble();
    info.price      = o.value("Price").toDouble();
    info.filled_qty = o.value("FilledQty").toDouble();
    info.avg_price  = o.value("AvgPrice").toDouble();
    info.status     = o.value("OrderStatus").toString();
    info.timestamp  = o.value("Time").toString();
    return info;
}

BrokerPosition SSIBroker::parse_position(const QJsonObject& o) {
    BrokerPosition pos;
    pos.symbol       = o.value("Symbol").toString();
    pos.exchange     = o.value("Market").toString();
    pos.product_type = "futures";
    const double net = o.value("Long").toDouble() - o.value("Short").toDouble();
    pos.quantity     = net;
    pos.side         = net >= 0 ? "long" : "short";
    pos.avg_price    = o.value("AvgPrice").toDouble();
    pos.ltp          = o.value("MarketPrice").toDouble();
    pos.pnl          = o.value("PnL").toDouble();
    return pos;
}

BrokerHolding SSIBroker::parse_holding(const QJsonObject& o) {
    BrokerHolding h;
    h.symbol         = o.value("Symbol").toString();
    h.exchange       = o.value("Exchange").toString();
    h.quantity       = o.value("Qty").toDouble();
    h.avg_price      = o.value("AvgPrice").toDouble();
    h.ltp            = o.value("MarketPrice").toDouble();
    h.pnl            = o.value("UnrealisedPnL").toDouble();
    h.invested_value = h.quantity * h.avg_price;
    h.current_value  = h.quantity * h.ltp;
    return h;
}

BrokerCandle SSIBroker::parse_candle(const QJsonObject& o) {
    BrokerCandle c;
    c.timestamp = static_cast<int64_t>(o.value("Time").toDouble());
    c.open      = o.value("Open").toDouble();
    c.high      = o.value("High").toDouble();
    c.low       = o.value("Low").toDouble();
    c.close     = o.value("Close").toDouble();
    c.volume    = o.value("Volume").toDouble();
    return c;
}

} // namespace fincept::trading
