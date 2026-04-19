// SSIWebSocket — realtime market data client for SSI FastConnect.
//
// Authentication flow:
//   1. POST /api/v2/Market/AccessToken with consumerID + consumerSecret
//   2. SSI returns a short-lived JWT; include it as "Authorization: Bearer <token>"
//      in the WebSocket upgrade request.
//   3. After connecting, send subscription frames as JSON text messages.
//
// Frame format (subscribe):
//   {"action":"sub","params":{"regtopic":"ssi_fcdata_<SYMBOL>_<MARKET>",...}}
//
// Incoming market-data frames have a "DataType" field that discriminates
// between quote snapshots, index values, board announcements, etc.

#include "trading/websocket/SSIWebSocket.h"

#include "core/logging/Logger.h"
#include "trading/brokers/BrokerHttp.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>

namespace fincept::trading {

// ── Constructor ───────────────────────────────────────────────────────────────

SSIWebSocket::SSIWebSocket(const QString& consumer_id, const QString& consumer_secret, QObject* parent)
    : QObject(parent), consumer_id_(consumer_id), consumer_secret_(consumer_secret) {
    ws_ = new fincept::WebSocketClient(this);
    connect(ws_, &fincept::WebSocketClient::connected, this, &SSIWebSocket::on_ws_connected);
    connect(ws_, &fincept::WebSocketClient::disconnected, this, &SSIWebSocket::on_ws_disconnected);
    connect(ws_, &fincept::WebSocketClient::message_received, this, &SSIWebSocket::on_text_message);
    connect(ws_, &fincept::WebSocketClient::error_occurred, this, &SSIWebSocket::error_occurred);

    token_refresh_timer_ = new QTimer(this);
    token_refresh_timer_->setSingleShot(false);
    token_refresh_timer_->setInterval(kTokenRefreshIntervalMs);
    connect(token_refresh_timer_, &QTimer::timeout, this, &SSIWebSocket::on_token_refresh_timer);
}

// ── Public API ────────────────────────────────────────────────────────────────

void SSIWebSocket::open() {
    LOG_INFO("SSIWebSocket", "Requesting SSI access token");
    if (!request_access_token()) {
        emit error_occurred("SSI: failed to obtain access token");
        return;
    }

    const QString url = QString("%1&token=%2").arg(kWsUrl, access_token_);
    LOG_INFO("SSIWebSocket", "Connecting to SSI FastConnect");
    ws_->connect_to(url);
    token_refresh_timer_->start();
}

void SSIWebSocket::close() {
    token_refresh_timer_->stop();
    ws_->disconnect();
}

bool SSIWebSocket::is_connected() const {
    return ws_->is_connected();
}

void SSIWebSocket::subscribe(const QStringList& symbols) {
    for (const auto& s : symbols)
        subscribed_symbols_.insert(s.toUpper());
    if (ws_->is_connected())
        send_subscribe(symbols);
}

void SSIWebSocket::unsubscribe(const QStringList& symbols) {
    for (const auto& s : symbols)
        subscribed_symbols_.remove(s.toUpper());
    if (ws_->is_connected())
        send_unsubscribe(symbols);
}

void SSIWebSocket::set_subscriptions(const QStringList& symbols) {
    const QStringList current = subscribed_symbols_.values();
    unsubscribe(current);
    subscribed_symbols_.clear();
    subscribe(symbols);
}

void SSIWebSocket::clear_subscriptions() {
    const QStringList current = subscribed_symbols_.values();
    unsubscribe(current);
    subscribed_symbols_.clear();
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void SSIWebSocket::on_ws_connected() {
    LOG_INFO("SSIWebSocket", "Connected to SSI FastConnect");
    resubscribe_all();
    emit connected();
}

void SSIWebSocket::on_ws_disconnected() {
    LOG_INFO("SSIWebSocket", "Disconnected from SSI FastConnect");
    emit disconnected();
}

void SSIWebSocket::on_token_refresh_timer() {
    LOG_INFO("SSIWebSocket", "Refreshing SSI access token");
    if (!request_access_token())
        LOG_WARN("SSIWebSocket", "Token refresh failed — will retry at next interval");
}

void SSIWebSocket::on_text_message(const QString& message) {
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(message.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_WARN("SSIWebSocket", QString("Malformed JSON frame: %1").arg(err.errorString()));
        return;
    }

    const auto root = doc.object();
    const QString data_type = root.value("DataType").toString();

    // SSI sends market data under "DataType" == "Quote" for derivatives
    if (data_type == "Quote" || data_type == "Derivatives" || data_type == "Index") {
        const auto data = root.value("Data").toObject();
        if (data.isEmpty())
            return;
        const VN30FTick tick = parse_market_data(data);
        if (!tick.symbol.isEmpty())
            emit tick_received(tick);
    }
    // Ignore other data types (board announcements, corporate actions, etc.)
}

// ── Private helpers ───────────────────────────────────────────────────────────

bool SSIWebSocket::request_access_token() {
    QJsonObject body;
    body["consumerID"]     = consumer_id_;
    body["consumerSecret"] = consumer_secret_;

    auto resp = BrokerHttp::instance().post_json(kAuthUrl, body);
    if (!resp.success) {
        LOG_WARN("SSIWebSocket", QString("Auth request failed: %1").arg(resp.error));
        return false;
    }

    // SSI returns: {"status": 200, "message": "...", "data": {"accessToken": "..."}}
    const auto data = resp.json.value("data").toObject();
    const QString token = data.value("accessToken").toString();
    if (token.isEmpty()) {
        LOG_WARN("SSIWebSocket", "SSI auth response missing accessToken");
        return false;
    }

    access_token_ = token;
    LOG_INFO("SSIWebSocket", "SSI access token obtained");
    return true;
}

void SSIWebSocket::send_subscribe(const QStringList& symbols) {
    // SSI FastConnect subscription frame:
    // {"action":"sub","params":{"regtopic":"X|<SYMBOL>|<MARKET>"}}
    // VN30F futures are listed on HNX; equities are on HOSE or UPCOM.
    // Use HNX for VN30F symbols, HOSE as default for everything else.
    for (const auto& sym : symbols) {
        const QString market = sym.toUpper().startsWith("VN30F") ? "HNX" : "HOSE";
        QJsonObject params;
        params["regtopic"] = QString("X|%1|%2").arg(sym.toUpper(), market);
        QJsonObject frame;
        frame["action"] = "sub";
        frame["params"] = params;
        ws_->send(QJsonDocument(frame).toJson(QJsonDocument::Compact));
    }
}

void SSIWebSocket::send_unsubscribe(const QStringList& symbols) {
    for (const auto& sym : symbols) {
        const QString market = sym.toUpper().startsWith("VN30F") ? "HNX" : "HOSE";
        QJsonObject params;
        params["regtopic"] = QString("X|%1|%2").arg(sym.toUpper(), market);
        QJsonObject frame;
        frame["action"] = "unsub";
        frame["params"] = params;
        ws_->send(QJsonDocument(frame).toJson(QJsonDocument::Compact));
    }
}

void SSIWebSocket::resubscribe_all() {
    if (subscribed_symbols_.isEmpty())
        return;
    const QStringList syms = subscribed_symbols_.values();
    LOG_INFO("SSIWebSocket", QString("Resubscribing to %1 symbols").arg(syms.size()));
    send_subscribe(syms);
}

// ── Tick parsing ──────────────────────────────────────────────────────────────

/* static */
VN30FDepthLevel SSIWebSocket::parse_depth_level(const QJsonObject& obj) {
    VN30FDepthLevel lvl;
    lvl.price   = obj.value("Price").toDouble();
    lvl.volume  = obj.value("Qty").toInt();
    lvl.orders  = obj.value("NumOrders").toInt();
    return lvl;
}

VN30FTick SSIWebSocket::parse_market_data(const QJsonObject& obj) const {
    VN30FTick tick;

    tick.symbol       = obj.value("Symbol").toString().toUpper();
    tick.exchange     = obj.value("Market").toString("HOSE").toUpper();
    tick.market_type  = obj.value("MarketType").toString("Futures");

    tick.ltp          = obj.value("LastPrice").toDouble();
    tick.open         = obj.value("Open").toDouble();
    tick.high         = obj.value("High").toDouble();
    tick.low          = obj.value("Low").toDouble();
    tick.close        = obj.value("RefPrice").toDouble(); // reference/previous close
    tick.avg_price    = obj.value("AvgPrice").toDouble();

    tick.volume         = static_cast<long long>(obj.value("AccumulatedVol").toDouble());
    tick.open_interest  = static_cast<long long>(obj.value("OpenInterest").toDouble());
    tick.oi_change      = static_cast<long long>(obj.value("OIChange").toDouble());

    tick.change     = obj.value("Change").toDouble();
    tick.change_pct = obj.value("RatioChange").toDouble();

    tick.trading_status = obj.value("TradingStatus").toString("Open");
    tick.tradable       = (tick.trading_status == "Open");

    // Parse depth (SSI sends up to 3 levels for futures)
    const auto bids = obj.value("BidPrices").toArray();
    for (int i = 0; i < qMin(3, bids.size()); ++i)
        tick.bids[i] = parse_depth_level(bids[i].toObject());

    const auto asks = obj.value("AskPrices").toArray();
    for (int i = 0; i < qMin(3, asks.size()); ++i)
        tick.asks[i] = parse_depth_level(asks[i].toObject());

    const QString ts_str = obj.value("TradingDate").toString();
    if (!ts_str.isEmpty())
        tick.exchange_timestamp = QDateTime::fromString(ts_str, Qt::ISODate);
    tick.local_timestamp = QDateTime::currentDateTimeUtc();

    return tick;
}

} // namespace fincept::trading
