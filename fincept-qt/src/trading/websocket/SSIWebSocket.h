#pragma once
// SSIWebSocket — realtime market data client for SSI FastConnect.
//
// SSI Securities provides a WebSocket-based streaming API called
// "FastConnect" (https://fc-data.ssi.com.vn/). Authentication is done via
// a JWT access-token obtained through the SSI REST API.
//
// Wire format: JSON frames with a "DataType" discriminator field.
//
// Usage:
//   auto* ws = new SSIWebSocket(consumerID, consumerSecret, this);
//   ws->subscribe({"VN30F2406", "VN30F2407"});
//   connect(ws, &SSIWebSocket::tick_received, this, &MyWidget::on_tick);
//   ws->open();
//
// Reconnect: exponential back-off via WebSocketClient; subscriptions are
// re-sent automatically after each reconnect.

#include "network/websocket/WebSocketClient.h"
#include "trading/websocket/VN30FTickTypes.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

namespace fincept::trading {

class SSIWebSocket : public QObject {
    Q_OBJECT
  public:
    explicit SSIWebSocket(const QString& consumer_id, const QString& consumer_secret,
                          QObject* parent = nullptr);

    /// Connect to the SSI FastConnect WebSocket endpoint.
    void open();

    /// Disconnect cleanly (subscriptions are retained for next open()).
    void close();

    bool is_connected() const;

    /// Add symbols to the subscription list and (re)subscribe if connected.
    /// @param symbols  Canonical contract codes, e.g. "VN30F2406".
    void subscribe(const QStringList& symbols);

    /// Remove symbols from the subscription list.
    void unsubscribe(const QStringList& symbols);

    /// Replace the full subscription set.
    void set_subscriptions(const QStringList& symbols);

    /// Remove all subscriptions.
    void clear_subscriptions();

  signals:
    void tick_received(const fincept::trading::VN30FTick& tick);
    void connected();
    void disconnected();
    void error_occurred(const QString& error);

  private slots:
    void on_ws_connected();
    void on_text_message(const QString& message);
    void on_ws_disconnected();
    void on_token_refresh_timer();

  private:
    bool request_access_token();
    void send_subscribe(const QStringList& symbols);
    void send_unsubscribe(const QStringList& symbols);
    void resubscribe_all();

    VN30FTick parse_market_data(const QJsonObject& obj) const;
    static VN30FDepthLevel parse_depth_level(const QJsonObject& obj);

    QString consumer_id_;
    QString consumer_secret_;
    QString access_token_;

    WebSocketClient*  ws_;
    QSet<QString>     subscribed_symbols_;
    QTimer*           token_refresh_timer_;

    // SSI FastConnect endpoints
    static constexpr const char* kAuthUrl = "https://fc-data.ssi.com.vn/api/v2/Market/AccessToken";
    static constexpr const char* kWsUrl   = "wss://fc-data.ssi.com.vn/socket.io/?transport=websocket";
    // Token refresh interval: SSI tokens expire after 24h; refresh every 23h.
    static constexpr int kTokenRefreshIntervalMs = 23 * 60 * 60 * 1000;
};

} // namespace fincept::trading
