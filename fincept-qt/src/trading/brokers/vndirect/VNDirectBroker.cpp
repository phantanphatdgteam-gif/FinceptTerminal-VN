// VNDirectBroker — IBroker implementation for VNDirect Securities.

#include "trading/brokers/vndirect/VNDirectBroker.h"

#include "core/logging/Logger.h"
#include "trading/brokers/BrokerHttp.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

namespace fincept::trading {

static int64_t vnd_now_ts() {
    return QDateTime::currentSecsSinceEpoch();
}

// ── Static helpers ────────────────────────────────────────────────────────────

QString VNDirectBroker::vnd_order_type(OrderType t) {
    switch (t) {
        case OrderType::Market:        return "MP";   // Market Price
        case OrderType::Limit:         return "LO";   // Limit Order
        case OrderType::StopLoss:      return "SL";
        case OrderType::StopLossLimit: return "SL";
    }
    return "MP";
}

QString VNDirectBroker::vnd_side(OrderSide s) {
    return s == OrderSide::Buy ? "B" : "S";
}

// Map canonical exchange codes to VNDirect exchange identifiers
QString VNDirectBroker::vnd_exchange(const QString& exchange) {
    static const QMap<QString, QString> map = {
        {"HOSE", "10"}, {"HNX", "02"}, {"UPCOM", "03"},
    };
    return map.value(exchange.toUpper(), "10");
}

// Map resolution string to VNDirect chart interval code
QString VNDirectBroker::vnd_resolution(const QString& resolution) {
    static const QMap<QString, QString> map = {
        {"1",  "1"},   {"1m",  "1"},
        {"3",  "3"},   {"3m",  "3"},
        {"5",  "5"},   {"5m",  "5"},
        {"15", "15"},  {"15m", "15"},
        {"30", "30"},  {"30m", "30"},
        {"60", "60"},  {"1h",  "60"},
        {"1d", "D"},   {"D",   "D"},
        {"1w", "W"},   {"W",   "W"},
    };
    return map.value(resolution, "D");
}

// ── Auth headers ──────────────────────────────────────────────────────────────

QMap<QString, QString> VNDirectBroker::auth_headers(const BrokerCredentials& creds) const {
    return {
        {"Authorization", "Bearer " + creds.access_token},
        {"Content-Type", "application/json"},
    };
}

// ── Authentication ────────────────────────────────────────────────────────────

TokenExchangeResponse VNDirectBroker::exchange_token(const QString& api_key,
                                                      const QString& api_secret,
                                                      const QString& auth_code) {
    // VNDirect uses username/password + optional OTP.
    // api_key   = username
    // api_secret = password (stored encrypted)
    // auth_code  = OTP (may be empty for trusted devices)
    QJsonObject body;
    body["username"] = api_key;
    body["password"] = api_secret;
    if (!auth_code.isEmpty())
        body["twoFactorCode"] = auth_code;

    const QString url = QString("%1/iboard/login").arg(base_url());
    auto resp = BrokerHttp::instance().post_json(url, body);

    TokenExchangeResponse result;
    if (!resp.success) {
        result.error = resp.error;
        return result;
    }

    const auto data = resp.json.value("data").toObject();
    const QString token = data.value("token").toString();
    if (token.isEmpty()) {
        result.error = resp.json.value("message").toString("Login failed");
        return result;
    }

    result.success      = true;
    result.access_token = token;
    result.user_id      = data.value("accountNo").toString();
    return result;
}

// ── Orders ────────────────────────────────────────────────────────────────────

OrderPlaceResponse VNDirectBroker::place_order(const BrokerCredentials& creds,
                                                const UnifiedOrder& order) {
    QJsonObject body;
    body["accountNo"]   = creds.user_id;
    body["symbol"]      = order.symbol;
    body["exchange"]    = vnd_exchange(order.exchange);
    body["side"]        = vnd_side(order.side);
    body["orderType"]   = vnd_order_type(order.order_type);
    body["price"]       = order.price;
    body["quantity"]    = order.quantity;

    const QString url = QString("%1/iboard/orders").arg(base_url());
    auto resp = BrokerHttp::instance().post_json(url, body, auth_headers(creds));

    OrderPlaceResponse result;
    if (!resp.success) {
        result.error = resp.error;
        return result;
    }

    const auto data = resp.json.value("data").toObject();
    result.success  = resp.json.value("result").toBool(false);
    result.order_id = data.value("orderId").toString();
    if (!result.success)
        result.error = resp.json.value("message").toString("Order placement failed");

    LOG_INFO("VNDirectBroker", QString("place_order %1 %2 x%3 → %4")
             .arg(order.symbol, vnd_side(order.side))
             .arg(order.quantity).arg(result.order_id));
    return result;
}

ApiResponse<QJsonObject> VNDirectBroker::modify_order(const BrokerCredentials& creds,
                                                        const QString& order_id,
                                                        const QJsonObject& mods) {
    const QString url = QString("%1/iboard/orders/%2").arg(base_url(), order_id);
    auto resp = BrokerHttp::instance().put_json(url, mods, auth_headers(creds));
    int64_t ts = vnd_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

ApiResponse<QJsonObject> VNDirectBroker::cancel_order(const BrokerCredentials& creds,
                                                        const QString& order_id) {
    const QString url = QString("%1/iboard/orders/%2").arg(base_url(), order_id);
    auto resp = BrokerHttp::instance().del(url, auth_headers(creds));
    int64_t ts = vnd_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

ApiResponse<QVector<BrokerOrderInfo>> VNDirectBroker::get_orders(const BrokerCredentials& creds) {
    const QString url = QString("%1/iboard/orders?accountNo=%2").arg(base_url(), creds.user_id);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = vnd_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerOrderInfo> orders;
    for (const auto& v : resp.json.value("data").toArray())
        orders.append(parse_order(v.toObject()));
    return {true, orders, "", ts};
}

ApiResponse<QJsonObject> VNDirectBroker::get_trade_book(const BrokerCredentials& creds) {
    const QString url = QString("%1/iboard/trades?accountNo=%2").arg(base_url(), creds.user_id);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = vnd_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

// ── Portfolio ─────────────────────────────────────────────────────────────────

ApiResponse<QVector<BrokerPosition>> VNDirectBroker::get_positions(const BrokerCredentials& creds) {
    // VNDirect: futures open positions
    const QString url = QString("%1/iboard/positions?accountNo=%2").arg(base_url(), creds.user_id);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = vnd_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerPosition> positions;
    for (const auto& v : resp.json.value("data").toArray())
        positions.append(parse_position(v.toObject()));
    return {true, positions, "", ts};
}

ApiResponse<QVector<BrokerHolding>> VNDirectBroker::get_holdings(const BrokerCredentials& creds) {
    const QString url = QString("%1/iboard/portfolio?accountNo=%2").arg(base_url(), creds.user_id);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = vnd_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerHolding> holdings;
    for (const auto& v : resp.json.value("data").toArray())
        holdings.append(parse_holding(v.toObject()));
    return {true, holdings, "", ts};
}

ApiResponse<BrokerFunds> VNDirectBroker::get_funds(const BrokerCredentials& creds) {
    const QString url = QString("%1/iboard/accounts/%2/balance").arg(base_url(), creds.user_id);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = vnd_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    const auto data = resp.json.value("data").toObject();
    BrokerFunds funds;
    funds.available_balance = data.value("cashAvailable").toDouble();
    funds.used_margin       = data.value("usedMargin").toDouble();
    funds.total_balance     = data.value("totalAssets").toDouble();
    funds.collateral        = data.value("collateral").toDouble();
    funds.raw_data          = data;
    return {true, funds, "", ts};
}

// ── Market data ───────────────────────────────────────────────────────────────

ApiResponse<QVector<BrokerQuote>> VNDirectBroker::get_quotes(const BrokerCredentials& creds,
                                                              const QVector<QString>& symbols) {
    const QString sym_list = symbols.join(",");
    const QString url = QString("%1/market/quote?symbols=%2").arg(base_url(), sym_list);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = vnd_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerQuote> quotes;
    const auto arr = resp.json.value("data").isArray()
                         ? resp.json.value("data").toArray()
                         : QJsonArray{resp.json.value("data").toObject()};
    for (const auto& v : arr) {
        const auto o = v.toObject();
        BrokerQuote q;
        q.symbol     = o.value("symbol").toString();
        q.ltp        = o.value("lastPrice").toDouble();
        q.open       = o.value("open").toDouble();
        q.high       = o.value("high").toDouble();
        q.low        = o.value("low").toDouble();
        q.close      = o.value("refPrice").toDouble();
        q.volume     = o.value("totalVol").toDouble();
        q.change     = o.value("change").toDouble();
        q.change_pct = o.value("ratioChange").toDouble();
        q.bid        = o.value("best1Bid").toDouble();
        q.ask        = o.value("best1Offer").toDouble();
        q.timestamp  = ts;
        quotes.append(q);
    }
    return {true, quotes, "", ts};
}

ApiResponse<QVector<BrokerCandle>> VNDirectBroker::get_history(const BrokerCredentials& creds,
                                                                const QString& symbol,
                                                                const QString& resolution,
                                                                const QString& from_date,
                                                                const QString& to_date) {
    const QString url = QString("%1/market/chart?symbol=%2&resolution=%3&from=%4&to=%5")
                            .arg(base_url(), symbol.toUpper(),
                                 vnd_resolution(resolution), from_date, to_date);
    auto resp = BrokerHttp::instance().get(url, auth_headers(creds));
    int64_t ts = vnd_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerCandle> candles;
    for (const auto& v : resp.json.value("data").toArray())
        candles.append(parse_candle(v.toObject()));
    return {true, candles, "", ts};
}

// ── Parsers ───────────────────────────────────────────────────────────────────

BrokerOrderInfo VNDirectBroker::parse_order(const QJsonObject& o) {
    BrokerOrderInfo info;
    info.order_id    = o.value("orderId").toString();
    info.symbol      = o.value("symbol").toString();
    info.exchange    = o.value("exchange").toString();
    info.side        = o.value("side").toString() == "B" ? "buy" : "sell";
    info.order_type  = o.value("orderType").toString();
    info.quantity    = o.value("qty").toDouble();
    info.price       = o.value("price").toDouble();
    info.filled_qty  = o.value("filledQty").toDouble();
    info.avg_price   = o.value("avgPrice").toDouble();
    info.status      = o.value("orderStatus").toString();
    info.timestamp   = o.value("time").toString();
    return info;
}

BrokerPosition VNDirectBroker::parse_position(const QJsonObject& o) {
    BrokerPosition pos;
    pos.symbol       = o.value("symbol").toString();
    pos.exchange     = o.value("market").toString();
    pos.product_type = "futures";
    pos.quantity     = o.value("long").toDouble() - o.value("short").toDouble();
    pos.side         = pos.quantity >= 0 ? "long" : "short";
    pos.avg_price    = o.value("avgPrice").toDouble();
    pos.ltp          = o.value("marketPrice").toDouble();
    pos.pnl          = o.value("pnL").toDouble();
    return pos;
}

BrokerHolding VNDirectBroker::parse_holding(const QJsonObject& o) {
    BrokerHolding h;
    h.symbol          = o.value("symbol").toString();
    h.exchange        = o.value("exchange").toString();
    h.quantity        = o.value("qty").toDouble();
    h.avg_price       = o.value("avgPrice").toDouble();
    h.ltp             = o.value("marketPrice").toDouble();
    h.pnl             = o.value("unrealisedPnL").toDouble();
    h.invested_value  = h.quantity * h.avg_price;
    h.current_value   = h.quantity * h.ltp;
    return h;
}

BrokerCandle VNDirectBroker::parse_candle(const QJsonObject& o) {
    BrokerCandle c;
    c.timestamp = static_cast<int64_t>(o.value("time").toDouble());
    c.open      = o.value("open").toDouble();
    c.high      = o.value("high").toDouble();
    c.low       = o.value("low").toDouble();
    c.close     = o.value("close").toDouble();
    c.volume    = o.value("volume").toDouble();
    return c;
}

} // namespace fincept::trading
