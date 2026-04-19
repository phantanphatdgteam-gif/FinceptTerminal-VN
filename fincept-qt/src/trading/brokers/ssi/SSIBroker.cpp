// SSI Broker — Vietnam derivatives broker implementation
// Implements IBroker for VN30F futures trading via SSI Securities API

#include "trading/brokers/BrokerHttp.h"
#include "trading/brokers/SSIBroker.h"

#include "core/logging/Logger.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace fincept::trading {

static int64_t now_ts() {
    return QDateTime::currentSecsSinceEpoch();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Auth
// ═══════════════════════════════════════════════════════════════════════════════

QMap<QString, QString> SSIBroker::auth_headers(const BrokerCredentials& creds) const {
    return {
        {"Authorization", "Bearer " + creds.access_token},
        {"Content-Type", "application/json"},
        {"X-Consumer-Id", creds.api_key},
    };
}

TokenExchangeResponse SSIBroker::exchange_token(const QString& api_key, const QString& api_secret,
                                                 const QString& auth_code) {
    Q_UNUSED(auth_code);
    // SSI uses consumer_id + consumer_secret for token exchange
    QJsonObject body;
    body["consumerID"] = api_key;
    body["consumerSecret"] = api_secret;

    auto resp = BrokerHttp::instance().post_json(
        QString("%1/api/v2/Trading/AccessToken").arg(base_url()), body, {{"Content-Type", "application/json"}});

    int64_t ts = now_ts();
    if (!resp.success)
        return {false, "", "", resp.error, ts};

    auto data = resp.json.value("data").toObject();
    QString token = data.value("accessToken").toString();
    if (token.isEmpty())
        return {false, "", "", "No access token in response", ts};

    return {true, token, "", "", ts};
}

// ═══════════════════════════════════════════════════════════════════════════════
// Orders
// ═══════════════════════════════════════════════════════════════════════════════

QString SSIBroker::ssi_order_type(OrderType t) {
    switch (t) {
        case OrderType::Market:
            return "MP"; // Market Price
        case OrderType::Limit:
            return "LO"; // Limit Order
        case OrderType::StopLoss:
            return "SL"; // Stop Loss
        case OrderType::StopLossLimit:
            return "SL"; // Stop Loss Limit
    }
    return "LO";
}

QString SSIBroker::ssi_side(OrderSide s) {
    return s == OrderSide::Buy ? "B" : "S";
}

QString SSIBroker::ssi_product(ProductType p) {
    switch (p) {
        case ProductType::Intraday:
            return "intraday";
        case ProductType::Margin:
            return "normal";
        default:
            return "normal";
    }
}

OrderPlaceResponse SSIBroker::place_order(const BrokerCredentials& creds, const UnifiedOrder& order) {
    QJsonObject body;
    body["instrumentID"] = order.symbol;
    body["market"] = order.exchange.isEmpty() ? "DER" : order.exchange;
    body["buySell"] = ssi_side(order.side);
    body["orderType"] = ssi_order_type(order.order_type);
    body["quantity"] = static_cast<int>(order.quantity);
    if (order.price > 0)
        body["price"] = order.price;
    if (order.stop_price > 0)
        body["stopPrice"] = order.stop_price;

    auto resp = BrokerHttp::instance().post_json(
        QString("%1/api/v2/Trading/NewOrder").arg(base_url()), body, auth_headers(creds));

    int64_t ts = now_ts();
    if (!resp.success)
        return {false, "", resp.error, ts};

    auto data = resp.json.value("data").toObject();
    QString order_id = data.value("orderID").toString();
    if (order_id.isEmpty())
        order_id = data.value("requestID").toString();

    return {true, order_id, "", ts};
}

ApiResponse<QJsonObject> SSIBroker::modify_order(const BrokerCredentials& creds, const QString& order_id,
                                                  const QJsonObject& modifications) {
    QJsonObject body = modifications;
    body["orderID"] = order_id;

    auto resp = BrokerHttp::instance().post_json(
        QString("%1/api/v2/Trading/ModifyOrder").arg(base_url()), body, auth_headers(creds));

    int64_t ts = now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

ApiResponse<QJsonObject> SSIBroker::cancel_order(const BrokerCredentials& creds, const QString& order_id) {
    QJsonObject body;
    body["orderID"] = order_id;

    auto resp = BrokerHttp::instance().post_json(
        QString("%1/api/v2/Trading/CancelOrder").arg(base_url()), body, auth_headers(creds));

    int64_t ts = now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

ApiResponse<QVector<BrokerOrderInfo>> SSIBroker::get_orders(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/v2/Trading/OrderList").arg(base_url()), auth_headers(creds));

    int64_t ts = now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerOrderInfo> orders;
    auto arr = resp.json.value("data").toArray();
    for (const auto& item : arr) {
        auto obj = item.toObject();
        BrokerOrderInfo info;
        info.order_id = obj.value("orderID").toString();
        info.symbol = obj.value("instrumentID").toString();
        info.exchange = obj.value("market").toString();
        info.side = obj.value("buySell").toString() == "B" ? "buy" : "sell";
        info.order_type = obj.value("orderType").toString();
        info.quantity = obj.value("quantity").toDouble();
        info.price = obj.value("price").toDouble();
        info.filled_qty = obj.value("filledQuantity").toDouble();
        info.status = obj.value("orderStatus").toString();
        orders.append(info);
    }
    return {true, orders, "", ts};
}

ApiResponse<QJsonObject> SSIBroker::get_trade_book(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/v2/Trading/OrderHistory").arg(base_url()), auth_headers(creds));

    int64_t ts = now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

// ═══════════════════════════════════════════════════════════════════════════════
// Portfolio
// ═══════════════════════════════════════════════════════════════════════════════

ApiResponse<QVector<BrokerPosition>> SSIBroker::get_positions(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/v2/Trading/DerivativePosition").arg(base_url()), auth_headers(creds));

    int64_t ts = now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerPosition> positions;
    auto arr = resp.json.value("data").toArray();
    for (const auto& item : arr) {
        auto obj = item.toObject();
        BrokerPosition pos;
        pos.symbol = obj.value("instrumentID").toString();
        pos.exchange = obj.value("market").toString("DER");
        pos.quantity = obj.value("longQty").toDouble() - obj.value("shortQty").toDouble();
        pos.average_price = obj.value("avgPrice").toDouble();
        pos.last_price = obj.value("marketPrice").toDouble();
        pos.pnl = obj.value("floatingPL").toDouble();
        positions.append(pos);
    }
    return {true, positions, "", ts};
}

ApiResponse<QVector<BrokerHolding>> SSIBroker::get_holdings(const BrokerCredentials& creds) {
    Q_UNUSED(creds);
    // Derivatives don't have holdings in the equity sense
    return {true, QVector<BrokerHolding>{}, "", now_ts()};
}

ApiResponse<BrokerFunds> SSIBroker::get_funds(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/v2/Trading/AccountBalance").arg(base_url()), auth_headers(creds));

    int64_t ts = now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    auto data = resp.json.value("data").toObject();
    BrokerFunds funds;
    funds.available_margin = data.value("accountBalance").toDouble();
    funds.used_margin = data.value("usedMargin").toDouble(0.0);
    return {true, funds, "", ts};
}

// ═══════════════════════════════════════════════════════════════════════════════
// Market Data
// ═══════════════════════════════════════════════════════════════════════════════

ApiResponse<QVector<BrokerQuote>> SSIBroker::get_quotes(const BrokerCredentials& creds,
                                                         const QVector<QString>& symbols) {
    QStringList sym_list;
    for (const auto& s : symbols)
        sym_list.append(s);

    auto resp = BrokerHttp::instance().get(
        QString("%1/api/v2/Market/SecuritiesDetails?market=DER&symbols=%2")
            .arg(base_url(), sym_list.join(",")),
        auth_headers(creds));

    int64_t ts = now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerQuote> quotes;
    auto arr = resp.json.value("data").toArray();
    for (const auto& item : arr) {
        auto obj = item.toObject();
        BrokerQuote q;
        q.symbol = obj.value("symbol").toString();
        q.last_price = obj.value("lastPrice").toDouble();
        q.bid_price = obj.value("bidPrice1").toDouble();
        q.ask_price = obj.value("askPrice1").toDouble();
        q.open = obj.value("openPrice").toDouble();
        q.high = obj.value("highPrice").toDouble();
        q.low = obj.value("lowPrice").toDouble();
        q.close = obj.value("closePrice").toDouble();
        q.volume = obj.value("totalVolume").toDouble();
        q.change = obj.value("change").toDouble();
        q.change_percent = obj.value("changePercent").toDouble();
        quotes.append(q);
    }
    return {true, quotes, "", ts};
}

ApiResponse<QVector<BrokerCandle>> SSIBroker::get_history(const BrokerCredentials& creds, const QString& symbol,
                                                           const QString& resolution, const QString& from_date,
                                                           const QString& to_date) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/v2/Market/IntradayOHLC?symbol=%2&resolution=%3&fromDate=%4&toDate=%5")
            .arg(base_url(), symbol, resolution, from_date, to_date),
        auth_headers(creds));

    int64_t ts = now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QVector<BrokerCandle> candles;
    auto arr = resp.json.value("data").toArray();
    for (const auto& item : arr) {
        auto obj = item.toObject();
        BrokerCandle c;
        c.timestamp = obj.value("tradingDate").toString();
        c.open = obj.value("open").toDouble();
        c.high = obj.value("high").toDouble();
        c.low = obj.value("low").toDouble();
        c.close = obj.value("close").toDouble();
        c.volume = obj.value("volume").toDouble();
        candles.append(c);
    }
    return {true, candles, "", ts};
}

// ═══════════════════════════════════════════════════════════════════════════════
// Margin Calculator (VN30F specific)
// ═══════════════════════════════════════════════════════════════════════════════

double SSIBroker::compute_vn30f_margin(double price, int lots) {
    // VN30F margin = price * multiplier * lots * initial_margin_pct
    return price * vn30f_multiplier * lots * vn30f_initial_margin_pct;
}

ApiResponse<OrderMargin> SSIBroker::get_order_margins(const BrokerCredentials& creds, const UnifiedOrder& order) {
    Q_UNUSED(creds);
    OrderMargin margin;
    margin.total = compute_vn30f_margin(order.price > 0 ? order.price : 1200.0, // default ~1200
                                        static_cast<int>(order.quantity));
    margin.var_margin = margin.total;
    margin.leverage = 1.0 / vn30f_initial_margin_pct; // ~7.7x
    return {true, margin, "", now_ts()};
}

ApiResponse<BasketMargin> SSIBroker::get_basket_margins(const BrokerCredentials& creds,
                                                         const QVector<UnifiedOrder>& orders) {
    Q_UNUSED(creds);
    double total = 0.0;
    for (const auto& order : orders)
        total += compute_vn30f_margin(order.price > 0 ? order.price : 1200.0, static_cast<int>(order.quantity));

    BasketMargin basket;
    basket.initial_margin = total;
    basket.final_margin = total;
    return {true, basket, "", now_ts()};
}

} // namespace fincept::trading
