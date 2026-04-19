// src/services/vn30f/VN30FTradingService.cpp
#include "services/vn30f/VN30FTradingService.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"

#include <QDir>
#include <QJsonDocument>
#include <QStandardPaths>

namespace fincept::services::vn30f {

static constexpr auto TAG = "VN30FService";

using fincept::python::extract_json;
using fincept::python::PythonResult;
using fincept::python::PythonRunner;

VN30FTradingService& VN30FTradingService::instance() {
    static VN30FTradingService inst;
    return inst;
}

VN30FTradingService::VN30FTradingService(QObject* parent) : QObject(parent) {
    QString data_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(data_dir);
    db_path_ = data_dir + "/vn30f_live.db";
    LOG_INFO(TAG, "Service initialized, db: " + db_path_);
}

void VN30FTradingService::run_python(const QString& script, const QStringList& args, const QString& context,
                                     std::function<void(bool, const QString&)> cb) {
    PythonRunner::instance().run(script, args, [this, context, cb](const PythonResult& res) {
        if (!res.success) {
            LOG_ERROR(TAG, context + " failed: " + res.error);
            emit error_occurred(context, res.error);
            if (cb)
                cb(false, res.error);
            return;
        }
        if (cb)
            cb(true, res.output);
    });
}

// ── Market Data ─────────────────────────────────────────────────────────────

void VN30FTradingService::fetch_overview(const QString& symbol) {
    run_python("vn30f_data.py", {"overview", symbol}, "fetch_overview", [this](bool ok, const QString& out) {
        if (!ok)
            return;
        auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
        if (doc.isObject())
            emit overview_loaded(doc.object());
    });
}

void VN30FTradingService::fetch_realtime(const QString& symbol) {
    run_python("vn30f_data.py", {"realtime", symbol}, "fetch_realtime", [this](bool ok, const QString& out) {
        if (!ok)
            return;
        auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
        if (doc.isObject())
            emit realtime_updated(doc.object());
    });
}

void VN30FTradingService::fetch_history(const QString& symbol, int days, const QString& interval) {
    run_python("vn30f_data.py", {"historical", symbol, QString::number(days), interval}, "fetch_history",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (doc.isObject())
                       emit history_loaded(doc.object());
               });
}

// ── Strategy Management ─────────────────────────────────────────────────────

void VN30FTradingService::list_strategies() {
    run_python("vn30f_technicals.py", {"list_strategies"}, "list_strategies", [this](bool ok, const QString& out) {
        if (!ok)
            return;
        auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
        if (doc.isObject() && doc.object().contains("strategies"))
            emit strategies_loaded(doc.object().value("strategies").toArray());
    });
}

void VN30FTradingService::run_strategy_signal(const QString& strategy_id, const QString& symbol) {
    run_python("vn30f_technicals.py", {"signal", strategy_id, symbol}, "strategy_signal",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (doc.isObject())
                       emit signal_generated(doc.object());
               });
}

// ── ML Models ───────────────────────────────────────────────────────────────

void VN30FTradingService::train_model(const QString& model_type, const QString& symbol, int lookback) {
    run_python("vn30f_ml_training.py", {"train", model_type, symbol, QString::number(lookback)}, "train_model",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (doc.isObject())
                       emit model_trained(doc.object());
               });
}

void VN30FTradingService::predict(const QString& model_type, const QString& symbol) {
    run_python("vn30f_ml_training.py", {"predict", model_type, symbol}, "predict",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (doc.isObject())
                       emit prediction_ready(doc.object());
               });
}

void VN30FTradingService::evaluate_model(const QString& model_type, const QString& symbol) {
    run_python("vn30f_ml_training.py", {"evaluate", model_type, symbol}, "evaluate_model",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (doc.isObject())
                       emit model_evaluated(doc.object());
               });
}

// ── Agent ───────────────────────────────────────────────────────────────────

void VN30FTradingService::run_agent(const QString& action, const QString& role, const QString& symbol,
                                    const QJsonObject& context) {
    QString ctx_str = QString::fromUtf8(QJsonDocument(context).toJson(QJsonDocument::Compact));
    run_python("vn30f_agent.py", {action, role, symbol, ctx_str}, "run_agent",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (doc.isObject())
                       emit agent_result(doc.object());
               });
}

// ── Backtesting ─────────────────────────────────────────────────────────────

void VN30FTradingService::run_backtest(const QString& strategy, double capital, const QString& start,
                                       const QString& end) {
    run_python("vn30f_backtesting.py",
               {"backtest", strategy, QString::number(capital, 'f', 0), start, end}, "run_backtest",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (doc.isObject())
                       emit backtest_result(doc.object());
               });
}

void VN30FTradingService::run_walk_forward(const QString& strategy, double capital) {
    run_python("vn30f_backtesting.py",
               {"walk_forward", strategy, QString::number(capital, 'f', 0)}, "walk_forward",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (doc.isObject())
                       emit walk_forward_result(doc.object());
               });
}

void VN30FTradingService::run_risk_report(const QString& symbol) {
    run_python("vn30f_backtesting.py", {"risk_report", "dual_thrust", "500000000", "", symbol}, "risk_report",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (doc.isObject())
                       emit risk_report_ready(doc.object());
               });
}

// ── Live Deployment ─────────────────────────────────────────────────────────

void VN30FTradingService::deploy(const QString& deploy_id, const QString& strategy_id, const QJsonObject& params) {
    QString params_str = QString::fromUtf8(QJsonDocument(params).toJson(QJsonDocument::Compact));
    run_python("vn30f_live_runner.py",
               {"deploy", deploy_id, strategy_id, params_str, "--db", db_path_}, "deploy",
               [this, deploy_id](bool ok, const QString&) {
                   if (ok)
                       emit deployment_started(deploy_id);
               });
}

void VN30FTradingService::stop_deployment(const QString& deploy_id) {
    run_python("vn30f_live_runner.py", {"stop", deploy_id, "--db", db_path_}, "stop_deployment",
               [this, deploy_id](bool ok, const QString&) {
                   if (ok)
                       emit deployment_stopped(deploy_id);
               });
}

void VN30FTradingService::stop_all() {
    run_python("vn30f_live_runner.py", {"stop_all", "--db", db_path_}, "stop_all", nullptr);
}

void VN30FTradingService::list_deployments() {
    run_python("vn30f_live_runner.py", {"list", "--db", db_path_}, "list_deployments",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (!doc.isObject())
                       return;
                   auto strategies = doc.object().value("data").toObject().value("strategies").toArray();
                   QVector<VN30FDeployment> result;
                   result.reserve(strategies.size());
                   for (const auto& s : strategies) {
                       auto obj = s.toObject();
                       VN30FDeployment d;
                       d.deploy_id = obj.value("deploy_id").toString();
                       d.strategy_id = obj.value("strategy_id").toString();
                       d.strategy_name = obj.value("strategy_name").toString();
                       d.symbol = obj.value("symbol").toString();
                       d.mode = obj.value("mode").toString();
                       d.status = obj.value("status").toString();
                       d.created_at = obj.value("created_at").toString();
                       result.append(d);
                   }
                   emit deployments_loaded(result);
               });
}

void VN30FTradingService::get_deployment_status(const QString& deploy_id) {
    run_python("vn30f_live_runner.py", {"status", deploy_id, "--db", db_path_}, "deployment_status",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (doc.isObject())
                       emit deployment_status(doc.object());
               });
}

void VN30FTradingService::rollover(const QString& deploy_id) {
    run_python("vn30f_live_runner.py", {"rollover", deploy_id, "--db", db_path_}, "rollover", nullptr);
}

// ── Risk ────────────────────────────────────────────────────────────────────

void VN30FTradingService::fetch_risk_metrics(const QString& symbol) {
    run_python("vn30f_backtesting.py", {"risk_report", "dual_thrust", "500000000", "", symbol}, "risk_metrics",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (doc.isObject())
                       emit risk_metrics_loaded(doc.object());
               });
}

void VN30FTradingService::calculate_var(const QString& symbol, double confidence) {
    run_python("vn30f_backtesting.py",
               {"derivatives_analytics", "dual_thrust", "500000000", "", symbol}, "calculate_var",
               [this](bool ok, const QString& out) {
                   if (!ok)
                       return;
                   auto doc = QJsonDocument::fromJson(extract_json(out).toUtf8());
                   if (doc.isObject())
                       emit var_calculated(doc.object());
               });
}

} // namespace fincept::services::vn30f
