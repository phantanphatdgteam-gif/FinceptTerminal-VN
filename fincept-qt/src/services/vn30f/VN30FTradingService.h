// src/services/vn30f/VN30FTradingService.h
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

namespace fincept::services::vn30f {

// ── VN30F Market Constants ─────────────────────────────────────────────────
struct VN30FConstants {
    static constexpr double TICK_SIZE = 0.1;
    static constexpr double MULTIPLIER = 100000.0; // 100,000 VND per point
    static constexpr double INITIAL_MARGIN_PCT = 0.13;
    static constexpr double MAINT_MARGIN_PCT = 0.10;
    static constexpr double FEE_PER_SIDE = 0.0005; // 0.05%
    static constexpr double PRICE_LIMIT_PCT = 0.07; // ±7%
    static constexpr int MORNING_OPEN_MIN = 525;    // 8:45
    static constexpr int MORNING_CLOSE_MIN = 690;   // 11:30
    static constexpr int AFTERNOON_OPEN_MIN = 780;  // 13:00
    static constexpr int AFTERNOON_CLOSE_MIN = 870;  // 14:30
};

// ── Deployment info ────────────────────────────────────────────────────────
struct VN30FDeployment {
    QString deploy_id;
    QString strategy_id;
    QString strategy_name;
    QString symbol;
    QString mode;   // "paper" or "live"
    QString status; // "running", "stopped", "error"
    QString created_at;
    double total_pnl = 0;
    double daily_pnl = 0;
    int total_trades = 0;
    double win_rate = 0;
    double max_drawdown = 0;
    double sharpe_ratio = 0;
    double margin_used = 0;
};

// ── Tab identifiers for the screen ─────────────────────────────────────────
enum class VN30FTab {
    Overview = 0,
    Strategy,
    MLModels,
    Agent,
    Backtest,
    Live,
    Risk,
};

/// Singleton service for VN30F Trading — data fetching, strategy management,
/// deployment, ML model control, agent dispatch, backtesting, risk analysis.
class VN30FTradingService : public QObject {
    Q_OBJECT
  public:
    static VN30FTradingService& instance();

    // ── Market Data ─────────────────────────────────────────────────────────
    void fetch_overview(const QString& symbol = "VN30F1M");
    void fetch_realtime(const QString& symbol = "VN30F1M");
    void fetch_history(const QString& symbol, int days = 90, const QString& interval = "1d");

    // ── Strategy Management ─────────────────────────────────────────────────
    void list_strategies();
    void run_strategy_signal(const QString& strategy_id, const QString& symbol = "VN30F1M");

    // ── ML Models ───────────────────────────────────────────────────────────
    void train_model(const QString& model_type, const QString& symbol = "VN30F1M", int lookback = 120);
    void predict(const QString& model_type, const QString& symbol = "VN30F1M");
    void evaluate_model(const QString& model_type, const QString& symbol = "VN30F1M");

    // ── Agent ───────────────────────────────────────────────────────────────
    void run_agent(const QString& action, const QString& role, const QString& symbol = "VN30F1M",
                   const QJsonObject& context = {});

    // ── Backtesting ─────────────────────────────────────────────────────────
    void run_backtest(const QString& strategy, double capital = 500000000,
                      const QString& start = "", const QString& end = "");
    void run_walk_forward(const QString& strategy, double capital = 500000000);
    void run_risk_report(const QString& symbol = "VN30F1M");

    // ── Live Deployment ─────────────────────────────────────────────────────
    void deploy(const QString& deploy_id, const QString& strategy_id, const QJsonObject& params);
    void stop_deployment(const QString& deploy_id);
    void stop_all();
    void list_deployments();
    void get_deployment_status(const QString& deploy_id);
    void rollover(const QString& deploy_id);

    // ── Risk ────────────────────────────────────────────────────────────────
    void fetch_risk_metrics(const QString& symbol = "VN30F1M");
    void calculate_var(const QString& symbol = "VN30F1M", double confidence = 0.95);

  signals:
    void overview_loaded(QJsonObject data);
    void realtime_updated(QJsonObject data);
    void history_loaded(QJsonObject data);
    void strategies_loaded(QJsonArray strategies);
    void signal_generated(QJsonObject signal);
    void model_trained(QJsonObject result);
    void prediction_ready(QJsonObject prediction);
    void model_evaluated(QJsonObject evaluation);
    void agent_result(QJsonObject result);
    void backtest_result(QJsonObject data);
    void walk_forward_result(QJsonObject data);
    void risk_report_ready(QJsonObject data);
    void deployment_started(QString deploy_id);
    void deployment_stopped(QString deploy_id);
    void deployments_loaded(QVector<fincept::services::vn30f::VN30FDeployment> deployments);
    void deployment_status(QJsonObject status);
    void risk_metrics_loaded(QJsonObject data);
    void var_calculated(QJsonObject data);
    void error_occurred(QString context, QString message);

  private:
    explicit VN30FTradingService(QObject* parent = nullptr);
    Q_DISABLE_COPY(VN30FTradingService)

    void run_python(const QString& script, const QStringList& args, const QString& context,
                    std::function<void(bool, const QString&)> cb);

    QString db_path_;
};

} // namespace fincept::services::vn30f
