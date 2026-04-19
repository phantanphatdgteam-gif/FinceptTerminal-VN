#pragma once
// SSI Broker — Vietnam derivatives broker (SSI Securities)
// API: https://fc-tradeapi.ssi.com.vn
// Supports VN30F futures on HOSE derivatives exchange

#include "trading/BrokerInterface.h"

namespace fincept::trading {

class SSIBroker : public IBroker {
  public:
    BrokerId id() const override { return BrokerId::SSI; }
    const char* name() const override { return "SSI"; }
    const char* base_url() const override { return "https://fc-tradeapi.ssi.com.vn"; }
    const char* ws_adapter_name() const override { return "ssi"; }

    BrokerProfile profile() const override {
        return BrokerProfile{
            .id = "ssi",
            .display_name = "SSI Securities",
            .region = "VN",
            .currency = "VND",
            .credential_fields =
                {
                    {CredentialField::ApiKey, "Consumer ID", "Enter SSI Consumer ID...", false},
                    {CredentialField::ApiSecret, "Consumer Secret", "Enter SSI Consumer Secret...", true},
                    {CredentialField::AuthCode, "PIN", "Enter trading PIN...", true},
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
            .default_watchlist = {"VN30F1M", "VN30F2M", "VN30", "VN100", "VNINDEX",
                                  "VCB", "VHM", "VIC", "HPG", "FPT"},
            .default_symbol = "VN30F1M",
            .default_exchange = "DER",
            .brokerage_info = "0.027%/side futures",
        };
    }

    // --- VN30F Market Constants ---
    static constexpr double vn30f_tick_size = 0.1;
    static constexpr double vn30f_multiplier = 100000.0; // 100,000 VND per point
    static constexpr double vn30f_initial_margin_pct = 0.13; // ~13% contract value
    static constexpr double vn30f_maintenance_margin_pct = 0.10;
    static constexpr double vn30f_fee_per_side = 0.00027; // 0.027%
    static constexpr double vn30f_price_limit_pct = 0.07; // ±7% daily limit

    // --- VN Trading Session Times (ICT / UTC+7) ---
    // Morning: 08:45 - 11:30
    // Lunch break: 11:30 - 13:00
    // Afternoon: 13:00 - 14:30
    // ATO: 08:45 - 09:00
    // ATC: 14:15 - 14:30

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

    // --- Margin Calculator (VN30F specific) ---
    ApiResponse<OrderMargin> get_order_margins(const BrokerCredentials& creds, const UnifiedOrder& order) override;
    ApiResponse<BasketMargin> get_basket_margins(const BrokerCredentials& creds,
                                                 const QVector<UnifiedOrder>& orders) override;

  protected:
    QMap<QString, QString> auth_headers(const BrokerCredentials& creds) const override;

  private:
    static QString ssi_order_type(OrderType t);
    static QString ssi_side(OrderSide s);
    static QString ssi_product(ProductType p);

    // Compute margin for a VN30F contract
    static double compute_vn30f_margin(double price, int lots);
};

} // namespace fincept::trading
