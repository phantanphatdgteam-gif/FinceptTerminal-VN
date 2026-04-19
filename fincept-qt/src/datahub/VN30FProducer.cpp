// VN30FProducer — DataHub Producer for VN30F realtime tick data.

#include "datahub/VN30FProducer.h"

#include "core/logging/Logger.h"
#include "datahub/DataHub.h"

#include <QMetaType>

// Register VN30FTick so it can travel over QVariant / queued connections.
Q_DECLARE_METATYPE(fincept::trading::VN30FTick)

namespace fincept::datahub {

// ── Constructor ───────────────────────────────────────────────────────────────

VN30FProducer::VN30FProducer(const QString& consumer_id, const QString& consumer_secret, QObject* parent)
    : QObject(parent) {
    ws_ = new fincept::trading::SSIWebSocket(consumer_id, consumer_secret, this);
    connect(ws_, &fincept::trading::SSIWebSocket::tick_received, this, &VN30FProducer::on_tick);
    connect(ws_, &fincept::trading::SSIWebSocket::connected, this, [this] {
        LOG_INFO("VN30FProducer", "SSI WebSocket connected — streaming live");
    });
    connect(ws_, &fincept::trading::SSIWebSocket::disconnected, this, [this] {
        LOG_WARN("VN30FProducer", "SSI WebSocket disconnected — will reconnect");
    });
    connect(ws_, &fincept::trading::SSIWebSocket::error_occurred, this, [this](const QString& err) {
        LOG_WARN("VN30FProducer", QString("SSI WebSocket error: %1").arg(err));
    });
}

// ── Producer interface ─────────────────────────────────────────────────────────

QStringList VN30FProducer::topic_patterns() const {
    return {"vn30f:tick:*"};
}

void VN30FProducer::refresh(const QStringList& topics) {
    // Convert topic names back to symbols and subscribe
    QStringList symbols;
    for (const auto& topic : topics) {
        const QString sym = symbol_from_topic(topic);
        if (!sym.isEmpty())
            symbols.append(sym);
    }
    if (!symbols.isEmpty())
        subscribe_symbols(symbols);
}

void VN30FProducer::on_topic_idle(const QString& topic) {
    const QString sym = symbol_from_topic(topic);
    if (!sym.isEmpty())
        ws_->unsubscribe({sym});
}

// ── Direct subscription helpers ───────────────────────────────────────────────

void VN30FProducer::subscribe_symbols(const QStringList& symbols) {
    if (!ws_->is_connected())
        ws_->open();
    ws_->subscribe(symbols);
    LOG_INFO("VN30FProducer", QString("Subscribed: %1").arg(symbols.join(", ")));
}

void VN30FProducer::unsubscribe_symbols(const QStringList& symbols) {
    ws_->unsubscribe(symbols);
}

bool VN30FProducer::is_connected() const {
    return ws_->is_connected();
}

// ── Private slots ─────────────────────────────────────────────────────────────

void VN30FProducer::on_tick(const fincept::trading::VN30FTick& tick) {
    // Publish to DataHub under the topic for this symbol
    const QString topic = topic_for(tick.symbol);
    QVariant v;
    v.setValue(tick);
    DataHub::instance().publish(topic, v);

    // Forward via signal for direct consumers (e.g. VN30FRiskManager)
    emit tick_received(tick);
}

// ── Static helpers ────────────────────────────────────────────────────────────

/* static */
QString VN30FProducer::topic_for(const QString& symbol) {
    return "vn30f:tick:" + symbol.toLower();
}

/* static */
QString VN30FProducer::symbol_from_topic(const QString& topic) {
    // topic format: "vn30f:tick:<SYMBOL>"
    const QString prefix = "vn30f:tick:";
    if (!topic.startsWith(prefix))
        return {};
    return topic.mid(prefix.size()).toUpper();
}

} // namespace fincept::datahub
