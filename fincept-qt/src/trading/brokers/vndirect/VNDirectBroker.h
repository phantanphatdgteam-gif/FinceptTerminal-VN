#pragma once
// VNDirectBroker — IBroker implementation for VNDirect Securities (Vietnam).
//
// VNDirect API documentation: https://trading.vndirect.com.vn/api-docs
//
// Authentication:
//   POST /iboard/login  with {username, password, twoFactorCode}
//   → returns {token, tokenExpiry, …}
//   All subsequent requests include "Authorization: Bearer <token>".
//
// Notes:
//   • VNDirect is one of the largest retail brokers in Vietnam and provides
//     access to VN30F futures via its IBOARD API.
//   • Trading hours: 09:00 – 14:45 ICT (UTC+7), Mon–Fri.
//   • Exchange codes: "HOSE" (equities), "HNX" (bonds/SMEs/futures),
//     "UPCOM" (unlisted public companies).

#include "trading/BrokerInterface.h"

namespace fincept::trading {

class VNDirectBroker : public IBroker {
  public:
    BrokerId id() const override { return BrokerId::VNDirect; }
    const char* name() const override { return "VNDirect"; }
    const char* base_url() const override { return "https://trading.vndirect.com.vn"; }

    BrokerProfile profile() const override {
        return BrokerProfile{
            .id           = "vndirect",
            .display_name = "VNDirect",
            .region       = "VN",
            .currency     = "VND",
            .credential_fields =
                {
                    {CredentialField::UserId,    "Tên đăng nhập", "Nhập tài khoản VNDirect...", false},
                    {CredentialField::Password,  "Mật khẩu",      "Nhập mật khẩu...",           true},
                    {CredentialField::AuthCode,  "Mã OTP",        "Nhập mã OTP (nếu có)...",    false},
                },
            .exchanges = {"HOSE", "HNX", "UPCOM"},
            .product_types =
                {
                    {"Giao dịch trong ngày (T0)", ProductType::Intraday},
                    {"Giao dịch thông thường (T+2)", ProductType::Delivery},
                    {"Ký quỹ (Margin)", ProductType::Margin},
                },
            .supports_intraday    = true,
            .supports_bracket_order = false,
            .supports_cover_order   = false,
            .has_native_paper       = false,
            .default_paper_balance  = 100'000'000.0, // 100 million VND
            .default_watchlist = {"VN30F1!", "VN30F2!", "HPG", "VHM", "VCB", "BID",
                                  "CTG", "MBB", "TCB", "SSI"},
            .default_symbol   = "VN30F1!",
            .default_exchange = "HNX",
            .brokerage_info   = "0.15% cổ phiếu / 0.5 VNĐ/hợp đồng VN30F",
        };
    }

    // ── IBroker interface ─────────────────────────────────────────────────────

    TokenExchangeResponse exchange_token(const QString& api_key, const QString& api_secret,
                                         const QString& auth_code) override;

    OrderPlaceResponse place_order(const BrokerCredentials& creds,
                                   const UnifiedOrder& order) override;

    ApiResponse<QJsonObject> modify_order(const BrokerCredentials& creds, const QString& order_id,
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
    static QString vnd_order_type(OrderType t);
    static QString vnd_side(OrderSide s);
    static QString vnd_exchange(const QString& exchange);
    static QString vnd_resolution(const QString& resolution);

    static BrokerOrderInfo parse_order(const QJsonObject& o);
    static BrokerPosition  parse_position(const QJsonObject& o);
    static BrokerHolding   parse_holding(const QJsonObject& o);
    static BrokerCandle    parse_candle(const QJsonObject& o);
};

} // namespace fincept::trading
