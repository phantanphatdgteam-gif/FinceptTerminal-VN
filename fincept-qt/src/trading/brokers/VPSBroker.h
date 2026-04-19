#pragma once
// VPS Broker — Vietnam derivatives broker (VPS Securities)
// API: https://trading-api.vps.com.vn

#include "trading/BrokerInterface.h"

namespace fincept::trading {

class VPSBroker : public IBroker {
  public:
    BrokerId id() const override { return BrokerId::VPS; }
    const char* name() const override { return "VPS"; }
    const char* base_url() const override { return "https://trading-api.vps.com.vn"; }

    BrokerProfile profile() const override {
        return BrokerProfile{
            .id = "vps",
            .display_name = "VPS Securities",
            .region = "VN",
            .currency = "VND",
            .credential_fields =
                {
                    {CredentialField::ApiKey, "Account ID", "Enter VPS Account ID...", false},
                    {CredentialField::ApiSecret, "Password", "Enter Password...", true},
                    {CredentialField::AuthCode, "OTP", "Enter OTP code...", false},
                },
            .exchanges = {"HOSE", "HNX", "UPCOM", "DER"},
            .product_types =
                {
                    {"Intraday (T+0)", ProductType::Intraday},
                    {"Normal (NRML)", ProductType::Margin},
                },
            .supports_intraday = true,
            .supports_bracket_order = false,
            .supports_cover_order = false,
            .has_native_paper = false,
            .default_paper_balance = 500000000.0, // 500M VND
            .default_watchlist = {"VN30F1M", "VN30F2M", "VN30", "VNINDEX",
                                  "VCB", "VHM", "VIC", "HPG", "FPT", "MBB"},
            .default_symbol = "VN30F1M",
            .default_exchange = "DER",
            .brokerage_info = "0.025%/side futures",
        };
    }

    TokenExchangeResponse exchange_token(const QString& api_key, const QString& api_secret,
                                         const QString& auth_code) override;
    OrderPlaceResponse place_order(const BrokerCredentials& creds, const UnifiedOrder& order) override;
    ApiResponse<QJsonObject> modify_order(const BrokerCredentials& creds, const QString& order_id,
                                          const QJsonObject& modifications) override;
    ApiResponse<QJsonObject> cancel_order(const BrokerCredentials& creds, const QString& order_id) override;
    ApiResponse<QVector<BrokerOrderInfo>> get_orders(const BrokerCredentials& creds) override;
    ApiResponse<QJsonObject> get_trade_book(const BrokerCredentials& creds) override;
    ApiResponse<QVector<BrokerPosition>> get_positions(const BrokerCredentials& creds) override;
    ApiResponse<QVector<BrokerHolding>> get_holdings(const BrokerCredentials& creds) override;
    ApiResponse<BrokerFunds> get_funds(const BrokerCredentials& creds) override;
    ApiResponse<QVector<BrokerQuote>> get_quotes(const BrokerCredentials& creds,
                                                 const QVector<QString>& symbols) override;
    ApiResponse<QVector<BrokerCandle>> get_history(const BrokerCredentials& creds, const QString& symbol,
                                                   const QString& resolution, const QString& from_date,
                                                   const QString& to_date) override;

  protected:
    QMap<QString, QString> auth_headers(const BrokerCredentials& creds) const override;
};

} // namespace fincept::trading
