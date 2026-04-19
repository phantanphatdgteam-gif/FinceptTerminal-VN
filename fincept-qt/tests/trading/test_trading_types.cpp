// Fincept Terminal — TradingTypes unit tests
//
// Tests the helper functions declared in trading/TradingTypes.h:
//   - broker_id_str()     enum → string for every BrokerId value
//   - parse_broker_id()   string → enum round-trip, including Vietnamese brokers
//   - order_side_str()    buy/sell strings
//   - order_type_str()    market/limit/stop_loss/stop_loss_limit strings
//   - product_type_str()  all five product type strings

#include "trading/TradingTypes.h"

#include <QObject>
#include <QTest>

using namespace fincept::trading;

class TestTradingTypes : public QObject {
    Q_OBJECT

  private slots:

    // ── broker_id_str() ───────────────────────────────────────────────────────

    void broker_id_str_indian_brokers() {
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::Fyers)),    QLatin1String("fyers"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::Zerodha)),  QLatin1String("zerodha"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::Upstox)),   QLatin1String("upstox"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::Dhan)),     QLatin1String("dhan"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::Kotak)),    QLatin1String("kotak"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::Groww)),    QLatin1String("groww"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::AliceBlue)),QLatin1String("aliceblue"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::AngelOne)), QLatin1String("angelone"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::FivePaisa)),QLatin1String("fivepaisa"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::IIFL)),     QLatin1String("iifl"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::Motilal)),  QLatin1String("motilal"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::Shoonya)),  QLatin1String("shoonya"));
    }

    void broker_id_str_global_brokers() {
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::Alpaca)),   QLatin1String("alpaca"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::IBKR)),     QLatin1String("ibkr"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::Tradier)),  QLatin1String("tradier"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::SaxoBank)), QLatin1String("saxobank"));
    }

    void broker_id_str_vietnamese_brokers() {
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::VNDirect)), QLatin1String("vndirect"));
        QCOMPARE(QLatin1String(broker_id_str(BrokerId::SSI)),      QLatin1String("ssi"));
    }

    // ── parse_broker_id() — round-trip ────────────────────────────────────────

    void parse_broker_id_round_trip_all_brokers() {
        const QVector<BrokerId> ids = {
            BrokerId::Fyers,  BrokerId::Zerodha, BrokerId::Upstox,
            BrokerId::Dhan,   BrokerId::Kotak,   BrokerId::Groww,
            BrokerId::AliceBlue, BrokerId::AngelOne, BrokerId::FivePaisa,
            BrokerId::IIFL,   BrokerId::Motilal, BrokerId::Shoonya,
            BrokerId::Alpaca, BrokerId::IBKR,    BrokerId::Tradier,
            BrokerId::SaxoBank,
            BrokerId::VNDirect, BrokerId::SSI,
        };

        for (BrokerId id : ids) {
            const QString name = QString::fromLatin1(broker_id_str(id));
            const auto parsed = parse_broker_id(name);
            QVERIFY2(parsed.has_value(),
                     qPrintable(QString("parse_broker_id failed for: '%1'").arg(name)));
            QCOMPARE(*parsed, id);
        }
    }

    void parse_broker_id_vndirect() {
        const auto result = parse_broker_id("vndirect");
        QVERIFY(result.has_value());
        QCOMPARE(*result, BrokerId::VNDirect);
    }

    void parse_broker_id_ssi() {
        const auto result = parse_broker_id("ssi");
        QVERIFY(result.has_value());
        QCOMPARE(*result, BrokerId::SSI);
    }

    void parse_broker_id_unknown_returns_nullopt() {
        QVERIFY(!parse_broker_id("nonexistent_broker").has_value());
        QVERIFY(!parse_broker_id("").has_value());
        QVERIFY(!parse_broker_id("VNDirect").has_value()); // case-sensitive
    }

    // ── order_side_str() ──────────────────────────────────────────────────────

    void order_side_str_buy() {
        QCOMPARE(QLatin1String(order_side_str(OrderSide::Buy)),  QLatin1String("buy"));
    }

    void order_side_str_sell() {
        QCOMPARE(QLatin1String(order_side_str(OrderSide::Sell)), QLatin1String("sell"));
    }

    // ── order_type_str() ──────────────────────────────────────────────────────

    void order_type_str_market() {
        QCOMPARE(QLatin1String(order_type_str(OrderType::Market)),        QLatin1String("market"));
    }

    void order_type_str_limit() {
        QCOMPARE(QLatin1String(order_type_str(OrderType::Limit)),         QLatin1String("limit"));
    }

    void order_type_str_stop_loss() {
        QCOMPARE(QLatin1String(order_type_str(OrderType::StopLoss)),      QLatin1String("stop_loss"));
    }

    void order_type_str_stop_loss_limit() {
        QCOMPARE(QLatin1String(order_type_str(OrderType::StopLossLimit)), QLatin1String("stop_loss_limit"));
    }

    // ── product_type_str() ────────────────────────────────────────────────────

    void product_type_str_intraday() {
        QCOMPARE(QLatin1String(product_type_str(ProductType::Intraday)),    QLatin1String("intraday"));
    }

    void product_type_str_delivery() {
        QCOMPARE(QLatin1String(product_type_str(ProductType::Delivery)),    QLatin1String("delivery"));
    }

    void product_type_str_margin() {
        QCOMPARE(QLatin1String(product_type_str(ProductType::Margin)),      QLatin1String("margin"));
    }

    void product_type_str_cover_order() {
        QCOMPARE(QLatin1String(product_type_str(ProductType::CoverOrder)),  QLatin1String("cover_order"));
    }

    void product_type_str_bracket_order() {
        QCOMPARE(QLatin1String(product_type_str(ProductType::BracketOrder)),QLatin1String("bracket_order"));
    }
};

QTEST_MAIN(TestTradingTypes)
#include "test_trading_types.moc"
