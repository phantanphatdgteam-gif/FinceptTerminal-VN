#pragma once
// VN30FProducer — DataHub Producer for VN30F realtime tick data.
//
// Subscribes to the SSI FastConnect WebSocket and publishes normalised
// VN30FTick objects to the DataHub under the topic scheme:
//
//   "vn30f:tick:<SYMBOL>"    — latest tick for a contract (e.g. "vn30f:tick:VN30F2406")
//   "vn30f:tick:*"           — wildcard to receive all VN30F ticks
//
// Other producers (algo strategies, risk manager, charting) subscribe to
// these topics via DataHub::subscribe() and are notified on every new tick.

#include "datahub/Producer.h"
#include "trading/websocket/SSIWebSocket.h"
#include "trading/websocket/VN30FTickTypes.h"

#include <QObject>

namespace fincept::datahub {

class VN30FProducer : public QObject, public Producer {
    Q_OBJECT
  public:
    /// @param consumer_id     SSI FastConnect consumer ID.
    /// @param consumer_secret SSI FastConnect consumer secret.
    explicit VN30FProducer(const QString& consumer_id, const QString& consumer_secret,
                           QObject* parent = nullptr);

    // ── Producer interface ────────────────────────────────────────────────────

    QStringList topic_patterns() const override;

    /// Called by DataHub when ≥1 subscriber is waiting for a vn30f:tick:* topic.
    /// Opens the SSI WebSocket for each requested symbol if not already streaming.
    void refresh(const QStringList& topics) override;

    void on_topic_idle(const QString& topic) override;

    int max_requests_per_sec() const override { return 0; } // streaming — unlimited

    // ── Direct subscription helpers ───────────────────────────────────────────

    void subscribe_symbols(const QStringList& symbols);
    void unsubscribe_symbols(const QStringList& symbols);

    bool is_connected() const;

  signals:
    void tick_received(const fincept::trading::VN30FTick& tick);

  private slots:
    void on_tick(const fincept::trading::VN30FTick& tick);

  private:
    static QString topic_for(const QString& symbol);
    static QString symbol_from_topic(const QString& topic);

    fincept::trading::SSIWebSocket* ws_;
};

} // namespace fincept::datahub
