#pragma once
// SSIBroker — IBroker implementation for SSI Securities (Vietnam).
//
// SSI Securities provides two separate APIs:
//   1. SSI iBoard REST API — order management, account info, market data.
//      Base URL: https://iboard.ssi.com.vn
//   2. SSI FastConnect WebSocket — realtime streaming (handled by SSIWebSocket).
//
// Authentication:
//   POST /iboard/login with {clientId, clientSecret}  (or username/password)
//   → returns {token, expiry}
//   Header for all requests: "Authorization: Bearer <token>"
//
// SSI is the primary VN30F market maker and provides the best
// data quality for futures. The SSI FastConnect WebSocket stream is
// also integrated via SSIWebSocket / VN30FProducer.

#include "trading/BrokerInterface.h"

namespace fincept::trading {

class SSIBroker : public IBroker {
  public:
    BrokerId id() const override { return BrokerId::SSI; }
    const char* name() const override { return "SSI Securities"; }
    const char* base_url() const override { return "https://iboard.ssi.com.vn"; }
    const char* ws_adapter_name() const override { return "ssi"; }

    BrokerProfile profile() const override {
        return BrokerProfile{
            .id           = "ssi",
            .display_name = "SSI Securities",
            .region       = "VN",
            .currency     = "VND",
            .credential_fields =
                {
                    {CredentialField::ApiKey,    "Client ID",     "Nhập Client ID...",     false},
                    {CredentialField::ApiSecret, "Client Secret", "Nhập Client Secret...", true},
                },
            .exchanges = {"HOSE", "HNX", "UPCOM"},
            .product_types =
                {
                    {"Giao dịch trong ngày (T0)", ProductType::Intraday},
                    {"Giao dịch thông thường (T+2)", ProductType::Delivery},
                    {"Ký quỹ (Margin)", ProductType::Margin},
                },
            .supports_intraday      = true,
            .supports_bracket_order = false,
            .supports_cover_order   = false,
            .has_native_paper       = false,
            .default_paper_balance  = 100'000'000.0, // 100 million VND
            .default_watchlist = {"VN30F1!", "VN30F2!", "HPG", "VHM", "VCB",
                                  "BID", "CTG", "TCB", "MBB", "SSI"},
            .default_symbol   = "VN30F1!",
            .default_exchange = "HNX",
            .brokerage_info   = "0.15% cổ phiếu / 10 VNĐ/hợp đồng VN30F",
        };
    }

    // ── IBroker interface ─────────────────────────────────────────────────────

    TokenExchangeResponse exchange_token(const QString& api_key, const QString& api_secret,
                                         const QString& auth_code) override;

    OrderPlaceResponse place_order(const BrokerCredentials& creds,
                                   const UnifiedOrder& order) override;

    ApiResponse<QJsonObject> modify_order(const BrokerCredentials& creds,
                                          const QString& order_id,
                                          const QJsonObject& mods) override;

    ApiResponse<QJsonObject> cancel_order(const BrokerCredentials& creds,
                                          const QString& order_id) override;

    ApiResponse<QVector<BrokerOrderInfo>> get_orders(const BrokerCredentials& creds) override;

    ApiResponse<QJsonObject> get_trade_book(const BrokerCredentials& creds) override;

    ApiResponse<QVector<BrokerPosition>> get_positions(const BrokerCredentials& creds) override;

    ApiResponse<QVector<BrokerHolding>> get_holdings(const BrokerCredentials& creds) override;

    ApiResponse<BrokerFunds> get_funds(const BrokerCredentials& creds) override;

    ApiResponse<QVector<BrokerQuote>> get_quotes(const BrokerCredentials& creds,
                                                 const QVector<QString>& symbols) override;

    ApiResponse<QVector<BrokerCandle>> get_history(const BrokerCredentials& creds,
                                                   const QString& symbol,
                                                   const QString& resolution,
                                                   const QString& from_date,
                                                   const QString& to_date) override;

  protected:
    QMap<QString, QString> auth_headers(const BrokerCredentials& creds) const override;

  private:
    static QString ssi_order_type(OrderType t);
    static QString ssi_side(OrderSide s);
    static QString ssi_resolution(const QString& resolution);

    static BrokerOrderInfo parse_order(const QJsonObject& o);
    static BrokerPosition  parse_position(const QJsonObject& o);
    static BrokerHolding   parse_holding(const QJsonObject& o);
    static BrokerCandle    parse_candle(const QJsonObject& o);
};

} // namespace fincept::trading
