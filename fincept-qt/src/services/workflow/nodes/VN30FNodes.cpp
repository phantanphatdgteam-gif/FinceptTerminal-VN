#include "services/workflow/nodes/VN30FNodes.h"

#include "python/PythonRunner.h"
#include "services/workflow/NodeRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace fincept::workflow {

using fincept::python::extract_json;
using fincept::python::PythonResult;
using fincept::python::PythonRunner;

namespace {

// Helper: run a Python script and parse the JSON result.
void vn30f_run_python_json(const QString& script, const QStringList& args,
                           std::function<void(bool, QJsonValue, QString)> cb) {
    PythonRunner::instance().run(script, args, [cb](const PythonResult& res) {
        if (!res.success) {
            cb(false, {}, res.error);
            return;
        }
        QString json_str = extract_json(res.output).trimmed();
        auto doc = QJsonDocument::fromJson(json_str.toUtf8());
        if (doc.isNull()) {
            cb(false, {}, "Invalid JSON: " + res.output.left(200));
            return;
        }
        if (doc.isObject()) {
            auto obj = doc.object();
            if (obj.contains("success") && !obj.value("success").toBool(true)) {
                cb(false, {}, obj.value("error").toString("Python script returned failure"));
                return;
            }
        }
        cb(true, doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()), {});
    });
}

} // anonymous namespace

void register_vn30f_nodes(NodeRegistry& registry) {

    // ── VN30F Data Node ───────────────────────────────────────────
    // Fetches VN30F market data (OHLCV, real-time quotes, contract info)
    registry.register_type({
        .type_id = "vn30f.data",
        .display_name = "VN30F Data",
        .category = "VN30F",
        .description = "Fetch VN30F futures market data (OHLCV, real-time, contract info)",
        .icon_text = "VN",
        .accent_color = "#ef4444",
        .version = 1,
        .inputs = {{"input_0", "Trigger", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Market Data", PortDirection::Output, ConnectionType::MarketData}},
        .parameters =
            {
                {"action",
                 "Action",
                 "select",
                 "historical",
                 {"historical", "realtime", "contract_info", "margin_requirements", "open_interest"},
                 "",
                 true},
                {"symbol", "Symbol", "string", "VN30F1M", {}, "VN30F contract symbol"},
                {"period", "Period (days)", "number", 90, {}, "Historical data lookback days"},
                {"interval", "Interval", "select", "1d", {"1m", "5m", "15m", "30m", "1h", "1d"}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString action = params.value("action").toString("historical");
                QString symbol = params.value("symbol").toString("VN30F1M");
                int period = params.value("period").toInt(90);
                QString interval = params.value("interval").toString("1d");
                vn30f_run_python_json("vn30f_data.py",
                                      {action, symbol, QString::number(period), interval}, cb);
            },
    });

    // ── VN30F Indicator Node ──────────────────────────────────────
    // Runs VN30F-tuned technical indicators on price data
    registry.register_type({
        .type_id = "vn30f.indicators",
        .display_name = "VN30F Indicators",
        .category = "VN30F",
        .description = "Calculate VN30F-tuned technical indicators (RSI, MACD, ATR, BB, etc.)",
        .icon_text = "TA",
        .accent_color = "#ef4444",
        .version = 1,
        .inputs = {{"input_0", "Price Data", PortDirection::Input, ConnectionType::MarketData}},
        .outputs = {{"output_main", "Technical Data", PortDirection::Output, ConnectionType::TechnicalData}},
        .parameters =
            {
                {"category",
                 "Category",
                 "select",
                 "all",
                 {"all", "momentum", "trend", "volatility", "volume", "scanner"},
                 "",
                 true},
                {"symbol", "Symbol", "string", "VN30F1M", {}, "VN30F contract symbol"},
                {"period", "Period (days)", "number", 90, {}, "Historical lookback days"},
                {"scanner_preset",
                 "Scanner Preset",
                 "select",
                 "rsi_oversold",
                 {"rsi_oversold", "bollinger_squeeze", "high_volume", "atr_breakout", "macd_crossover"},
                 "Only used when category=scanner"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString category = params.value("category").toString("all");
                QString symbol = params.value("symbol").toString("VN30F1M");
                int period = params.value("period").toInt(90);
                QStringList args = {"indicators", category, symbol, QString::number(period)};
                if (category == "scanner")
                    args << params.value("scanner_preset").toString("rsi_oversold");
                vn30f_run_python_json("vn30f_technicals.py", args, cb);
            },
    });

    // ── VN30F ML Signal Node ──────────────────────────────────────
    // Generates ML-based trading signals for VN30F
    registry.register_type({
        .type_id = "vn30f.ml_signal",
        .display_name = "VN30F ML Signal",
        .category = "VN30F",
        .description = "Generate ML model trading signals for VN30F (LightGBM, XGBoost, LSTM, etc.)",
        .icon_text = "ML",
        .accent_color = "#ef4444",
        .version = 1,
        .inputs = {{"input_0", "Technical Data", PortDirection::Input, ConnectionType::TechnicalData}},
        .outputs = {{"output_main", "Signal", PortDirection::Output, ConnectionType::SignalData}},
        .parameters =
            {
                {"action",
                 "Action",
                 "select",
                 "predict",
                 {"predict", "train", "evaluate", "retrain"},
                 "",
                 true},
                {"model",
                 "Model",
                 "select",
                 "lightgbm",
                 {"lightgbm", "xgboost", "catboost", "lstm", "gru", "transformer"},
                 ""},
                {"symbol", "Symbol", "string", "VN30F1M", {}, "VN30F contract symbol"},
                {"lookback_days", "Training Lookback (days)", "number", 120, {}, "Rolling window for training"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString action = params.value("action").toString("predict");
                QString model = params.value("model").toString("lightgbm");
                QString symbol = params.value("symbol").toString("VN30F1M");
                int lookback = params.value("lookback_days").toInt(120);
                vn30f_run_python_json("vn30f_ml_training.py",
                                      {action, model, symbol, QString::number(lookback)}, cb);
            },
    });

    // ── VN30F LLM Agent Node ──────────────────────────────────────
    // Runs VN30F specialist LLM agent for market analysis
    registry.register_type({
        .type_id = "vn30f.llm_agent",
        .display_name = "VN30F LLM Agent",
        .category = "VN30F",
        .description = "Run VN30F specialist LLM agent for market analysis and trading decisions",
        .icon_text = "AI",
        .accent_color = "#ef4444",
        .version = 1,
        .inputs = {{"input_0", "Context", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Analysis", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"action",
                 "Action",
                 "select",
                 "analyze",
                 {"analyze", "decide", "fusion"},
                 "",
                 true},
                {"agent_role",
                 "Agent Role",
                 "select",
                 "market_analyst",
                 {"market_analyst", "quant_strategist", "risk_manager", "execution_specialist",
                  "portfolio_manager"},
                 ""},
                {"symbol", "Symbol", "string", "VN30F1M", {}, "VN30F contract symbol"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString action = params.value("action").toString("analyze");
                QString role = params.value("agent_role").toString("market_analyst");
                QString symbol = params.value("symbol").toString("VN30F1M");
                // Pass input context as JSON argument
                QString context_str = "{}";
                if (!inputs.isEmpty() && inputs[0].isObject())
                    context_str = QString::fromUtf8(
                        QJsonDocument(inputs[0].toObject()).toJson(QJsonDocument::Compact));
                vn30f_run_python_json("vn30f_agent.py",
                                      {action, role, symbol, context_str}, cb);
            },
    });

    // ── VN30F Order Node ──────────────────────────────────────────
    // Places VN30F futures orders (Market/Limit/ATO/ATC)
    registry.register_type({
        .type_id = "vn30f.place_order",
        .display_name = "VN30F Order",
        .category = "VN30F",
        .description = "Place VN30F futures order (Market, Limit, ATO, ATC) via SSI/VPS/VNDirect",
        .icon_text = "TX",
        .accent_color = "#ef4444",
        .version = 1,
        .inputs = {{"input_0", "Signal In", PortDirection::Input, ConnectionType::SignalData}},
        .outputs = {{"output_main", "Order Result", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "VN30F1M", {}, "VN30F contract", true},
                {"side", "Side", "select", "buy", {"buy", "sell"}, "", true},
                {"order_type",
                 "Order Type",
                 "select",
                 "limit",
                 {"market", "limit", "ato", "atc"},
                 "VN30F order types"},
                {"quantity", "Quantity (lots)", "number", 1, {}, "Number of lots", true},
                {"price", "Price", "number", 0, {}, "Limit price (0 for market/ATO/ATC)"},
                {"stop_loss", "Stop Loss", "number", 0, {}, "Stop loss price (0 = none)"},
                {"take_profit", "Take Profit", "number", 0, {}, "Take profit price (0 = none)"},
                {"broker",
                 "Broker",
                 "select",
                 "paper",
                 {"paper", "ssi", "vps", "vndirect"},
                 "VN broker"},
            },
        .execute = nullptr, // Wired via VN30FTradingService at runtime
    });

    // ── VN30F Risk Check Node ─────────────────────────────────────
    // VN30F-specific risk validation (margin, price limits, session check)
    registry.register_type({
        .type_id = "vn30f.risk_check",
        .display_name = "VN30F Risk Check",
        .category = "VN30F",
        .description = "VN30F risk validation: margin check, ±7% price limit, session hours, drawdown",
        .icon_text = "!!",
        .accent_color = "#ef4444",
        .version = 1,
        .inputs = {{"input_0", "Trade In", PortDirection::Input, ConnectionType::Main}},
        .outputs =
            {
                {"output_pass", "Pass", PortDirection::Output, ConnectionType::Main},
                {"output_fail", "Fail", PortDirection::Output, ConnectionType::Main},
            },
        .parameters =
            {
                {"max_drawdown_pct", "Max Drawdown %", "number", 10.0, {}, "Max drawdown % of NAV"},
                {"daily_loss_limit_pct", "Daily Loss Limit %", "number", 3.0, {}, "Max daily loss % of NAV"},
                {"max_margin_utilization", "Max Margin Util %", "number", 70.0, {},
                 "Max margin utilization percentage"},
                {"max_lots", "Max Position (lots)", "number", 50, {}, "Maximum position size in lots"},
                {"check_session", "Check Trading Session", "boolean", true, {},
                 "Block orders outside VN trading hours"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
               std::function<void(bool, QJsonValue, QString)> cb) {
                auto data = inputs.isEmpty() ? QJsonValue{} : inputs[0];
                QJsonObject trade = data.isObject() ? data.toObject() : QJsonObject{};

                double max_dd = params.value("max_drawdown_pct").toDouble(10.0);
                double daily_limit = params.value("daily_loss_limit_pct").toDouble(3.0);
                double max_margin = params.value("max_margin_utilization").toDouble(70.0);
                int max_lots = params.value("max_lots").toInt(50);
                bool check_session = params.value("check_session").toBool(true);

                QStringList failures;

                // Check position size
                int quantity = trade.value("quantity").toInt(0);
                if (quantity > max_lots)
                    failures << QString("Position %1 lots exceeds max %2").arg(quantity).arg(max_lots);

                // Check drawdown limit
                double current_dd = trade.value("current_drawdown_pct").toDouble(0);
                if (current_dd > max_dd)
                    failures << QString("Drawdown %1% exceeds limit %2%")
                                    .arg(current_dd, 0, 'f', 2)
                                    .arg(max_dd, 0, 'f', 2);

                // Check daily loss
                double daily_loss = trade.value("daily_loss_pct").toDouble(0);
                if (daily_loss > daily_limit)
                    failures << QString("Daily loss %1% exceeds limit %2%")
                                    .arg(daily_loss, 0, 'f', 2)
                                    .arg(daily_limit, 0, 'f', 2);

                // Check margin utilization
                double margin_util = trade.value("margin_utilization_pct").toDouble(0);
                if (margin_util > max_margin)
                    failures << QString("Margin utilization %1% exceeds limit %2%")
                                    .arg(margin_util, 0, 'f', 2)
                                    .arg(max_margin, 0, 'f', 2);

                // Check VN30F price limits (±7%)
                double price = trade.value("price").toDouble(0);
                double ref_price = trade.value("reference_price").toDouble(0);
                if (ref_price > 0 && price > 0) {
                    double pct_change = ((price - ref_price) / ref_price) * 100.0;
                    if (pct_change > 7.0 || pct_change < -7.0)
                        failures << QString("Price %1 outside ±7%% limit (ref: %2)")
                                        .arg(price, 0, 'f', 1)
                                        .arg(ref_price, 0, 'f', 1);
                }

                // Check trading session (VN: 8:45-11:30, 13:00-14:30 ICT = UTC+7)
                if (check_session) {
                    auto now = QDateTime::currentDateTimeUtc().addSecs(7 * 3600); // ICT
                    int minutes = now.time().hour() * 60 + now.time().minute();
                    bool in_morning = (minutes >= 525 && minutes <= 690);   // 8:45-11:30
                    bool in_afternoon = (minutes >= 780 && minutes <= 870); // 13:00-14:30
                    if (!in_morning && !in_afternoon)
                        failures << "Outside VN30F trading hours (8:45-11:30, 13:00-14:30 ICT)";
                }

                QJsonObject out = trade;
                bool passed = failures.isEmpty();
                out["risk_check_passed"] = passed;
                out["_branch"] = passed ? "true" : "false";
                if (!passed) {
                    QJsonArray fa;
                    for (const QString& f : failures)
                        fa.append(f);
                    out["risk_failures"] = fa;
                }
                cb(true, out, {});
            },
    });

    // ── VN30F Backtest Node ───────────────────────────────────────
    // Runs VN30F strategy backtesting
    registry.register_type({
        .type_id = "vn30f.backtest",
        .display_name = "VN30F Backtest",
        .category = "VN30F",
        .description = "Backtest VN30F strategies with walk-forward analysis and risk metrics",
        .icon_text = "BT",
        .accent_color = "#ef4444",
        .version = 1,
        .inputs = {{"input_0", "Strategy In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Results", PortDirection::Output, ConnectionType::BacktestData}},
        .parameters =
            {
                {"action",
                 "Action",
                 "select",
                 "backtest",
                 {"backtest", "walk_forward", "risk_report", "derivatives_analytics"},
                 "",
                 true},
                {"strategy",
                 "Strategy",
                 "select",
                 "dual_thrust",
                 {"dual_thrust", "mean_reversion_ibs", "intraday_reversal", "futures_momentum"},
                 "VN30F strategy"},
                {"capital", "Capital (VND)", "number", 500000000, {}, "Initial capital in VND"},
                {"start_date", "Start Date", "string", "", {}, "YYYY-MM-DD (empty = 1yr ago)"},
                {"end_date", "End Date", "string", "", {}, "YYYY-MM-DD (empty = today)"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString action = params.value("action").toString("backtest");
                QString strategy = params.value("strategy").toString("dual_thrust");
                double capital = params.value("capital").toDouble(500000000);
                QString start = params.value("start_date").toString();
                QString end = params.value("end_date").toString();
                vn30f_run_python_json("vn30f_backtesting.py",
                                      {action, strategy, QString::number(capital, 'f', 0),
                                       start, end},
                                      cb);
            },
    });
}

} // namespace fincept::workflow
