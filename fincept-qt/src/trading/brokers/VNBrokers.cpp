// VN Brokers — stub implementations for VPS and VNDirect brokers.
// SSI has a full implementation in its own directory.
// VPS and VNDirect get auth_headers + placeholder trading-API stubs.

#include "core/logging/Logger.h"
#include "trading/brokers/BrokerHttp.h"
#include "trading/brokers/VNDirectBroker.h"
#include "trading/brokers/VPSBroker.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace fincept::trading {

static int64_t vn_now_ts() {
    return QDateTime::currentSecsSinceEpoch();
}

// ═══════════════════════════════════════════════════════════════════════════════
// VPS Securities
// ═══════════════════════════════════════════════════════════════════════════════

QMap<QString, QString> VPSBroker::auth_headers(const BrokerCredentials& creds) const {
    return {
        {"Authorization", "Bearer " + creds.access_token},
        {"Content-Type", "application/json"},
    };
}

TokenExchangeResponse VPSBroker::exchange_token(const QString& api_key, const QString& api_secret,
                                                 const QString& auth_code) {
    Q_UNUSED(auth_code);
    QJsonObject body;
    body["username"] = api_key;
    body["password"] = api_secret;

    auto resp = BrokerHttp::instance().post_json(
        QString("%1/api/auth/login").arg(base_url()), body, {{"Content-Type", "application/json"}});

    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, "", "", resp.error, ts};

    auto data = resp.json.value("data").toObject();
    QString token = data.value("token").toString();
    return {!token.isEmpty(), token, "", token.isEmpty() ? "No token" : "", ts};
}

OrderPlaceResponse VPSBroker::place_order(const BrokerCredentials& creds, const UnifiedOrder& order) {
    QJsonObject body;
    body["symbol"] = order.symbol;
    body["side"] = order.side == OrderSide::Buy ? "B" : "S";
    body["quantity"] = static_cast<int>(order.quantity);
    body["price"] = order.price;
    body["market"] = order.exchange.isEmpty() ? "DER" : order.exchange;

    auto resp = BrokerHttp::instance().post_json(
        QString("%1/api/trading/order").arg(base_url()), body, auth_headers(creds));

    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, "", resp.error, ts};
    return {true, resp.json.value("data").toObject().value("orderId").toString(), "", ts};
}

ApiResponse<QJsonObject> VPSBroker::modify_order(const BrokerCredentials& creds, const QString& order_id,
                                                  const QJsonObject& modifications) {
    QJsonObject body = modifications;
    body["orderId"] = order_id;
    auto resp = BrokerHttp::instance().post_json(
        QString("%1/api/trading/order/modify").arg(base_url()), body, auth_headers(creds));
    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

ApiResponse<QJsonObject> VPSBroker::cancel_order(const BrokerCredentials& creds, const QString& order_id) {
    QJsonObject body;
    body["orderId"] = order_id;
    auto resp = BrokerHttp::instance().post_json(
        QString("%1/api/trading/order/cancel").arg(base_url()), body, auth_headers(creds));
    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

ApiResponse<QVector<BrokerOrderInfo>> VPSBroker::get_orders(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/trading/orders").arg(base_url()), auth_headers(creds));
    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, QVector<BrokerOrderInfo>{}, "", ts};
}

ApiResponse<QJsonObject> VPSBroker::get_trade_book(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/trading/trades").arg(base_url()), auth_headers(creds));
    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

ApiResponse<QVector<BrokerPosition>> VPSBroker::get_positions(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/trading/positions").arg(base_url()), auth_headers(creds));
    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, QVector<BrokerPosition>{}, "", ts};
}

ApiResponse<QVector<BrokerHolding>> VPSBroker::get_holdings(const BrokerCredentials& creds) {
    Q_UNUSED(creds);
    return {true, QVector<BrokerHolding>{}, "", vn_now_ts()};
}

ApiResponse<BrokerFunds> VPSBroker::get_funds(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/trading/balance").arg(base_url()), auth_headers(creds));
    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    BrokerFunds funds;
    funds.available_margin = resp.json.value("data").toObject().value("balance").toDouble();
    return {true, funds, "", ts};
}

ApiResponse<QVector<BrokerQuote>> VPSBroker::get_quotes(const BrokerCredentials& creds,
                                                         const QVector<QString>& symbols) {
    Q_UNUSED(creds);
    Q_UNUSED(symbols);
    return {false, std::nullopt, "Use SSI market data API for quotes", vn_now_ts()};
}

ApiResponse<QVector<BrokerCandle>> VPSBroker::get_history(const BrokerCredentials& creds, const QString& symbol,
                                                           const QString& resolution, const QString& from_date,
                                                           const QString& to_date) {
    Q_UNUSED(creds);
    Q_UNUSED(symbol);
    Q_UNUSED(resolution);
    Q_UNUSED(from_date);
    Q_UNUSED(to_date);
    return {false, std::nullopt, "Use SSI market data API for history", vn_now_ts()};
}

// ═══════════════════════════════════════════════════════════════════════════════
// VNDirect Securities
// ═══════════════════════════════════════════════════════════════════════════════

QMap<QString, QString> VNDirectBroker::auth_headers(const BrokerCredentials& creds) const {
    return {
        {"Authorization", "Bearer " + creds.access_token},
        {"Content-Type", "application/json"},
    };
}

TokenExchangeResponse VNDirectBroker::exchange_token(const QString& api_key, const QString& api_secret,
                                                      const QString& auth_code) {
    Q_UNUSED(auth_code);
    QJsonObject body;
    body["username"] = api_key;
    body["password"] = api_secret;

    auto resp = BrokerHttp::instance().post_json(
        QString("%1/api/auth/login").arg(base_url()), body, {{"Content-Type", "application/json"}});

    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, "", "", resp.error, ts};

    auto data = resp.json.value("data").toObject();
    QString token = data.value("token").toString();
    return {!token.isEmpty(), token, "", token.isEmpty() ? "No token" : "", ts};
}

OrderPlaceResponse VNDirectBroker::place_order(const BrokerCredentials& creds, const UnifiedOrder& order) {
    QJsonObject body;
    body["symbol"] = order.symbol;
    body["side"] = order.side == OrderSide::Buy ? "NB" : "NS";
    body["quantity"] = static_cast<int>(order.quantity);
    body["price"] = order.price;
    body["market"] = order.exchange.isEmpty() ? "DER" : order.exchange;

    auto resp = BrokerHttp::instance().post_json(
        QString("%1/api/trading/order").arg(base_url()), body, auth_headers(creds));

    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, "", resp.error, ts};
    return {true, resp.json.value("data").toObject().value("orderId").toString(), "", ts};
}

ApiResponse<QJsonObject> VNDirectBroker::modify_order(const BrokerCredentials& creds, const QString& order_id,
                                                       const QJsonObject& modifications) {
    QJsonObject body = modifications;
    body["orderId"] = order_id;
    auto resp = BrokerHttp::instance().post_json(
        QString("%1/api/trading/order/modify").arg(base_url()), body, auth_headers(creds));
    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

ApiResponse<QJsonObject> VNDirectBroker::cancel_order(const BrokerCredentials& creds, const QString& order_id) {
    QJsonObject body;
    body["orderId"] = order_id;
    auto resp = BrokerHttp::instance().post_json(
        QString("%1/api/trading/order/cancel").arg(base_url()), body, auth_headers(creds));
    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

ApiResponse<QVector<BrokerOrderInfo>> VNDirectBroker::get_orders(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/trading/orders").arg(base_url()), auth_headers(creds));
    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, QVector<BrokerOrderInfo>{}, "", ts};
}

ApiResponse<QJsonObject> VNDirectBroker::get_trade_book(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/trading/trades").arg(base_url()), auth_headers(creds));
    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, resp.json, "", ts};
}

ApiResponse<QVector<BrokerPosition>> VNDirectBroker::get_positions(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/trading/positions").arg(base_url()), auth_headers(creds));
    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    return {true, QVector<BrokerPosition>{}, "", ts};
}

ApiResponse<QVector<BrokerHolding>> VNDirectBroker::get_holdings(const BrokerCredentials& creds) {
    Q_UNUSED(creds);
    return {true, QVector<BrokerHolding>{}, "", vn_now_ts()};
}

ApiResponse<BrokerFunds> VNDirectBroker::get_funds(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(
        QString("%1/api/trading/balance").arg(base_url()), auth_headers(creds));
    int64_t ts = vn_now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    BrokerFunds funds;
    funds.available_margin = resp.json.value("data").toObject().value("balance").toDouble();
    return {true, funds, "", ts};
}

ApiResponse<QVector<BrokerQuote>> VNDirectBroker::get_quotes(const BrokerCredentials& creds,
                                                              const QVector<QString>& symbols) {
    Q_UNUSED(creds);
    Q_UNUSED(symbols);
    return {false, std::nullopt, "Use SSI market data API for quotes", vn_now_ts()};
}

ApiResponse<QVector<BrokerCandle>> VNDirectBroker::get_history(const BrokerCredentials& creds, const QString& symbol,
                                                                const QString& resolution, const QString& from_date,
                                                                const QString& to_date) {
    Q_UNUSED(creds);
    Q_UNUSED(symbol);
    Q_UNUSED(resolution);
    Q_UNUSED(from_date);
    Q_UNUSED(to_date);
    return {false, std::nullopt, "Use SSI market data API for history", vn_now_ts()};
}

} // namespace fincept::trading
