// src/screens/vn30f_trading/VN30FTradingScreen.cpp
#include "screens/vn30f_trading/VN30FTradingScreen.h"

#include "core/logging/Logger.h"
#include "core/session/ScreenStateManager.h"
#include "services/vn30f/VN30FTradingService.h"
#include "ui/theme/Theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>

namespace fincept::screens {

using namespace fincept::services::vn30f;

VN30FTradingScreen::VN30FTradingScreen(QWidget* parent) : QWidget(parent) {
    build_ui();
    connect_service();

    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(5000);
    connect(poll_timer_, &QTimer::timeout, this, &VN30FTradingScreen::refresh_current_tab);

    LOG_INFO("VN30FTrading", "Screen constructed");
}

void VN30FTradingScreen::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    poll_timer_->start();
    if (first_show_) {
        first_show_ = false;
        on_tab_changed(0);
    }
}

void VN30FTradingScreen::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    poll_timer_->stop();
}

void VN30FTradingScreen::restore_state(const QVariantMap& state) {
    active_tab_ = state.value("active_tab", 0).toInt();
    on_tab_changed(active_tab_);
}

QVariantMap VN30FTradingScreen::save_state() const {
    return {{"active_tab", active_tab_}};
}

// ── Build UI ────────────────────────────────────────────────────────────────

void VN30FTradingScreen::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(build_top_bar());

    content_stack_ = new QStackedWidget(this);
    content_stack_->addWidget(build_overview_panel());
    content_stack_->addWidget(build_strategy_panel());
    content_stack_->addWidget(build_ml_panel());
    content_stack_->addWidget(build_agent_panel());
    content_stack_->addWidget(build_backtest_panel());
    content_stack_->addWidget(build_live_panel());
    content_stack_->addWidget(build_risk_panel());
    root->addWidget(content_stack_, 1);

    root->addWidget(build_status_bar());
    setStyleSheet(QString("background:%1;").arg(ui::colors::BG_BASE()));
}

QWidget* VN30FTradingScreen::build_top_bar() {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(40);
    bar->setStyleSheet(
        QString("background:%1; border-bottom:1px solid %2;").arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));

    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(12, 0, 12, 0);
    hl->setSpacing(8);

    auto* title = new QLabel("VN30F TRADING", bar);
    title->setStyleSheet(QString("color:%1; font-size:12px; font-weight:700;"
                                 "letter-spacing:1.5px; background:transparent;")
                             .arg(ui::colors::TEXT_PRIMARY()));
    hl->addWidget(title);

    auto* subtitle = new QLabel("futures · strategies · ML · agents · risk", bar);
    subtitle->setStyleSheet(
        QString("color:%1; font-size:10px; background:transparent;").arg(ui::colors::TEXT_TERTIARY()));
    hl->addWidget(subtitle);

    auto* div = new QWidget(bar);
    div->setFixedSize(1, 20);
    div->setStyleSheet(QString("background:%1;").arg(ui::colors::BORDER_DIM()));
    hl->addWidget(div);

    QStringList tabs = {"OVERVIEW", "STRATEGY", "ML MODELS", "AGENT", "BACKTEST", "LIVE", "RISK"};
    QStringList colors = {"#ef4444", "#f97316", "#eab308", "#22c55e", "#3b82f6", "#8b5cf6", "#ec4899"};

    for (int i = 0; i < tabs.size(); ++i) {
        auto* btn = new QPushButton(tabs[i], bar);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QString("QPushButton { color:%1; font-size:10px; font-family:%2;"
                                   "padding:4px 10px; border:none;"
                                   "background:transparent; font-weight:400; }"
                                   "QPushButton:hover { color:%3; }")
                               .arg(ui::colors::TEXT_TERTIARY())
                               .arg(ui::fonts::DATA_FAMILY())
                               .arg(colors[i]));
        connect(btn, &QPushButton::clicked, this, [this, i]() { on_tab_changed(i); });
        hl->addWidget(btn);
        tab_buttons_.append(btn);
    }

    hl->addStretch();

    deploy_count_label_ = new QLabel("0 LIVE", bar);
    deploy_count_label_->setStyleSheet(
        QString("color:%1; font-size:10px; font-weight:600; background:transparent;").arg("#22c55e"));
    hl->addWidget(deploy_count_label_);

    return bar;
}

QWidget* VN30FTradingScreen::build_status_bar() {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(24);
    bar->setStyleSheet(
        QString("background:%1; border-top:1px solid %2;").arg(ui::colors::BG_BASE(), ui::colors::BORDER_DIM()));

    auto* hl = new QHBoxLayout(bar);
    hl->setContentsMargins(12, 0, 12, 0);
    hl->setSpacing(16);

    auto mklbl = [&](const QString& text) {
        auto* lbl = new QLabel(text, bar);
        lbl->setStyleSheet(
            QString("color:%1; font-size:9px; font-family:%2; background:transparent;")
                .arg(ui::colors::TEXT_TERTIARY(), ui::fonts::DATA_FAMILY()));
        return lbl;
    };

    session_label_ = mklbl("SESSION: --");
    price_label_ = mklbl("VN30F: --");
    pnl_label_ = mklbl("P&L: --");

    hl->addWidget(session_label_);
    hl->addWidget(price_label_);
    hl->addWidget(pnl_label_);
    hl->addStretch();
    hl->addWidget(mklbl("TICK:0.1 · MULT:100K · MARGIN:13% · FEE:0.05%"));

    return bar;
}

// ── Tab panels ──────────────────────────────────────────────────────────────

QWidget* VN30FTradingScreen::build_overview_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(8);

    // Top row: price cards
    auto* cards = new QHBoxLayout;
    auto mkcard = [&](const QString& label, QLabel*& val_label) {
        auto* card = new QWidget(w);
        card->setStyleSheet(
            QString("background:%1; border:1px solid %2; border-radius:4px;")
                .arg(ui::colors::BG_ELEVATED(), ui::colors::BORDER_DIM()));
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(12, 8, 12, 8);
        auto* lbl = new QLabel(label, card);
        lbl->setStyleSheet(
            QString("color:%1; font-size:9px; background:transparent;").arg(ui::colors::TEXT_TERTIARY()));
        val_label = new QLabel("--", card);
        val_label->setStyleSheet(
            QString("color:%1; font-size:16px; font-weight:700; font-family:%2; background:transparent;")
                .arg(ui::colors::TEXT_PRIMARY(), ui::fonts::DATA_FAMILY()));
        cl->addWidget(lbl);
        cl->addWidget(val_label);
        return card;
    };

    cards->addWidget(mkcard("VN30F PRICE", vn30f_price_));
    cards->addWidget(mkcard("VN30 INDEX", vn30_index_));
    cards->addWidget(mkcard("BASIS", basis_label_));
    cards->addWidget(mkcard("OPEN INTEREST", oi_label_));
    vl->addLayout(cards);

    // Contract info table
    overview_table_ = new QTableWidget(0, 2, w);
    overview_table_->setHorizontalHeaderLabels({"Property", "Value"});
    overview_table_->horizontalHeader()->setStretchLastSection(true);
    overview_table_->verticalHeader()->setVisible(false);
    overview_table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; gridline-color:%3; border:1px solid %3; }"
                "QHeaderView::section { background:%4; color:%2; border:none; padding:4px; font-size:10px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                 ui::colors::BG_ELEVATED()));
    vl->addWidget(overview_table_, 1);

    return w;
}

QWidget* VN30FTradingScreen::build_strategy_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(8);

    auto* hdr = new QLabel("VN30F STRATEGIES — Dual Thrust · IBS · Intraday Reversal · Momentum", w);
    hdr->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:600; background:transparent;").arg(ui::colors::TEXT_PRIMARY()));
    vl->addWidget(hdr);

    strategy_table_ = new QTableWidget(0, 5, w);
    strategy_table_->setHorizontalHeaderLabels({"Strategy", "ID", "Signal", "Confidence", "Status"});
    strategy_table_->horizontalHeader()->setStretchLastSection(true);
    strategy_table_->verticalHeader()->setVisible(false);
    strategy_table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; gridline-color:%3; border:1px solid %3; }"
                "QHeaderView::section { background:%4; color:%2; border:none; padding:4px; font-size:10px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                 ui::colors::BG_ELEVATED()));
    vl->addWidget(strategy_table_, 1);

    signal_label_ = new QLabel("Latest signal: --", w);
    signal_label_->setStyleSheet(
        QString("color:%1; font-size:10px; background:transparent;").arg(ui::colors::TEXT_SECONDARY()));
    vl->addWidget(signal_label_);

    return w;
}

QWidget* VN30FTradingScreen::build_ml_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(8);

    auto* hdr = new QLabel("ML MODELS — LightGBM · XGBoost · LSTM · GRU · Transformer", w);
    hdr->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:600; background:transparent;").arg(ui::colors::TEXT_PRIMARY()));
    vl->addWidget(hdr);

    ml_table_ = new QTableWidget(0, 6, w);
    ml_table_->setHorizontalHeaderLabels({"Model", "IC", "ICIR", "Sharpe", "Win Rate", "Status"});
    ml_table_->horizontalHeader()->setStretchLastSection(true);
    ml_table_->verticalHeader()->setVisible(false);
    ml_table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; gridline-color:%3; border:1px solid %3; }"
                "QHeaderView::section { background:%4; color:%2; border:none; padding:4px; font-size:10px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                 ui::colors::BG_ELEVATED()));
    vl->addWidget(ml_table_, 1);

    // Action buttons
    auto* btns = new QHBoxLayout;
    for (const auto& label : {"TRAIN", "PREDICT", "EVALUATE", "RETRAIN"}) {
        auto* btn = new QPushButton(label, w);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            QString("QPushButton { color:%1; font-size:10px; padding:6px 16px; border:1px solid %2;"
                    "background:transparent; border-radius:3px; }"
                    "QPushButton:hover { background:%2; }")
                .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
        btns->addWidget(btn);
    }
    btns->addStretch();
    vl->addLayout(btns);

    ml_status_ = new QLabel("Ready", w);
    ml_status_->setStyleSheet(
        QString("color:%1; font-size:10px; background:transparent;").arg(ui::colors::TEXT_TERTIARY()));
    vl->addWidget(ml_status_);

    return w;
}

QWidget* VN30FTradingScreen::build_agent_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(8);

    auto* hdr = new QLabel("VN30F LLM AGENT TEAM — Analyst · Quant · Risk · Execution · Portfolio", w);
    hdr->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:600; background:transparent;").arg(ui::colors::TEXT_PRIMARY()));
    vl->addWidget(hdr);

    agent_output_ = new QTextEdit(w);
    agent_output_->setReadOnly(true);
    agent_output_->setStyleSheet(
        QString("QTextEdit { background:%1; color:%2; border:1px solid %3; font-family:%4; font-size:11px; }")
            .arg(ui::colors::BG_ELEVATED(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                 ui::fonts::DATA_FAMILY()));
    agent_output_->setPlaceholderText("Agent analysis will appear here...");
    vl->addWidget(agent_output_, 1);

    // Agent action buttons
    auto* btns = new QHBoxLayout;
    QStringList roles = {"ANALYZE", "DECIDE", "FUSION"};
    for (const auto& label : roles) {
        auto* btn = new QPushButton(label, w);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            QString("QPushButton { color:%1; font-size:10px; padding:6px 16px; border:1px solid %2;"
                    "background:transparent; border-radius:3px; }"
                    "QPushButton:hover { background:%2; }")
                .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
        btns->addWidget(btn);
    }
    btns->addStretch();
    vl->addLayout(btns);

    return w;
}

QWidget* VN30FTradingScreen::build_backtest_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(8);

    auto* hdr = new QLabel("BACKTESTING — VectorBT · Walk-Forward · Parameter Optimization", w);
    hdr->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:600; background:transparent;").arg(ui::colors::TEXT_PRIMARY()));
    vl->addWidget(hdr);

    backtest_metrics_ = new QTableWidget(0, 2, w);
    backtest_metrics_->setHorizontalHeaderLabels({"Metric", "Value"});
    backtest_metrics_->horizontalHeader()->setStretchLastSection(true);
    backtest_metrics_->verticalHeader()->setVisible(false);
    backtest_metrics_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; gridline-color:%3; border:1px solid %3; }"
                "QHeaderView::section { background:%4; color:%2; border:none; padding:4px; font-size:10px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                 ui::colors::BG_ELEVATED()));
    vl->addWidget(backtest_metrics_, 1);

    // Action buttons
    auto* btns = new QHBoxLayout;
    for (const auto& label : {"RUN BACKTEST", "WALK FORWARD", "OPTIMIZE", "RISK REPORT"}) {
        auto* btn = new QPushButton(label, w);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            QString("QPushButton { color:%1; font-size:10px; padding:6px 16px; border:1px solid %2;"
                    "background:transparent; border-radius:3px; }"
                    "QPushButton:hover { background:%2; }")
                .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
        btns->addWidget(btn);
    }
    btns->addStretch();
    vl->addLayout(btns);

    backtest_log_ = new QTextEdit(w);
    backtest_log_->setReadOnly(true);
    backtest_log_->setMaximumHeight(120);
    backtest_log_->setStyleSheet(
        QString("QTextEdit { background:%1; color:%2; border:1px solid %3; font-family:%4; font-size:10px; }")
            .arg(ui::colors::BG_ELEVATED(), ui::colors::TEXT_SECONDARY(), ui::colors::BORDER_DIM(),
                 ui::fonts::DATA_FAMILY()));
    vl->addWidget(backtest_log_);

    return w;
}

QWidget* VN30FTradingScreen::build_live_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(8);

    auto* hdr = new QLabel("LIVE DEPLOYMENT — Paper → Live · SQLite State · P&L Tracking", w);
    hdr->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:600; background:transparent;").arg(ui::colors::TEXT_PRIMARY()));
    vl->addWidget(hdr);

    deploy_table_ = new QTableWidget(0, 7, w);
    deploy_table_->setHorizontalHeaderLabels({"Deploy ID", "Strategy", "Symbol", "Mode", "Status", "P&L", "Trades"});
    deploy_table_->horizontalHeader()->setStretchLastSection(true);
    deploy_table_->verticalHeader()->setVisible(false);
    deploy_table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; gridline-color:%3; border:1px solid %3; }"
                "QHeaderView::section { background:%4; color:%2; border:none; padding:4px; font-size:10px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                 ui::colors::BG_ELEVATED()));
    vl->addWidget(deploy_table_, 1);

    // Action buttons
    auto* btns = new QHBoxLayout;
    for (const auto& label : {"DEPLOY", "STOP", "STOP ALL", "ROLLOVER", "REFRESH"}) {
        auto* btn = new QPushButton(label, w);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            QString("QPushButton { color:%1; font-size:10px; padding:6px 16px; border:1px solid %2;"
                    "background:transparent; border-radius:3px; }"
                    "QPushButton:hover { background:%2; }")
                .arg(ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM()));
        btns->addWidget(btn);
    }
    btns->addStretch();
    vl->addLayout(btns);

    live_status_ = new QLabel("No active deployments", w);
    live_status_->setStyleSheet(
        QString("color:%1; font-size:10px; background:transparent;").arg(ui::colors::TEXT_TERTIARY()));
    vl->addWidget(live_status_);

    return w;
}

QWidget* VN30FTradingScreen::build_risk_panel() {
    auto* w = new QWidget(this);
    auto* vl = new QVBoxLayout(w);
    vl->setContentsMargins(12, 12, 12, 12);
    vl->setSpacing(8);

    auto* hdr = new QLabel("RISK MANAGEMENT — VaR · CVaR · Drawdown · Margin · Stress Test", w);
    hdr->setStyleSheet(
        QString("color:%1; font-size:11px; font-weight:600; background:transparent;").arg(ui::colors::TEXT_PRIMARY()));
    vl->addWidget(hdr);

    // VaR display
    var_label_ = new QLabel("VaR (95%): -- | CVaR: -- | Max Drawdown: --", w);
    var_label_->setStyleSheet(
        QString("color:%1; font-size:13px; font-weight:600; font-family:%2; background:transparent;")
            .arg(ui::colors::TEXT_PRIMARY(), ui::fonts::DATA_FAMILY()));
    vl->addWidget(var_label_);

    risk_table_ = new QTableWidget(0, 2, w);
    risk_table_->setHorizontalHeaderLabels({"Risk Metric", "Value"});
    risk_table_->horizontalHeader()->setStretchLastSection(true);
    risk_table_->verticalHeader()->setVisible(false);
    risk_table_->setStyleSheet(
        QString("QTableWidget { background:%1; color:%2; gridline-color:%3; border:1px solid %3; }"
                "QHeaderView::section { background:%4; color:%2; border:none; padding:4px; font-size:10px; }")
            .arg(ui::colors::BG_BASE(), ui::colors::TEXT_PRIMARY(), ui::colors::BORDER_DIM(),
                 ui::colors::BG_ELEVATED()));
    vl->addWidget(risk_table_, 1);

    // Risk limits info
    auto* limits = new QLabel(
        "LIMITS: Max DD -10% NAV · Daily Loss -3% NAV · Margin Util ≤70% · Position ≤50 lots · Price ±7%", w);
    limits->setStyleSheet(
        QString("color:%1; font-size:9px; background:transparent;").arg(ui::colors::TEXT_TERTIARY()));
    vl->addWidget(limits);

    return w;
}

// ── Tab navigation ──────────────────────────────────────────────────────────

void VN30FTradingScreen::on_tab_changed(int index) {
    if (index < 0 || index >= content_stack_->count())
        return;
    active_tab_ = index;
    content_stack_->setCurrentIndex(index);
    update_tab_buttons();
    refresh_current_tab();
}

void VN30FTradingScreen::update_tab_buttons() {
    QStringList colors = {"#ef4444", "#f97316", "#eab308", "#22c55e", "#3b82f6", "#8b5cf6", "#ec4899"};
    for (int i = 0; i < tab_buttons_.size(); ++i) {
        bool active = (i == active_tab_);
        tab_buttons_[i]->setStyleSheet(
            QString("QPushButton { color:%1; font-size:10px; font-family:%2;"
                    "padding:4px 10px; border:none; border-bottom:%3;"
                    "background:transparent; font-weight:%4; }"
                    "QPushButton:hover { color:%5; }")
                .arg(active ? colors[i] : ui::colors::TEXT_TERTIARY())
                .arg(ui::fonts::DATA_FAMILY())
                .arg(active ? QString("2px solid %1").arg(colors[i]) : "none")
                .arg(active ? "700" : "400")
                .arg(colors[i]));
    }
}

void VN30FTradingScreen::refresh_current_tab() {
    auto& svc = VN30FTradingService::instance();
    switch (active_tab_) {
    case 0:
        svc.fetch_overview();
        break;
    case 1:
        svc.list_strategies();
        break;
    case 5:
        svc.list_deployments();
        break;
    case 6:
        svc.fetch_risk_metrics();
        break;
    default:
        break;
    }
}

// ── Service signals ─────────────────────────────────────────────────────────

void VN30FTradingScreen::connect_service() {
    auto& svc = VN30FTradingService::instance();

    connect(&svc, &VN30FTradingService::overview_loaded, this, [this](const QJsonObject& data) {
        auto d = data.value("data").toObject();
        if (vn30f_price_)
            vn30f_price_->setText(QString::number(d.value("last_price").toDouble(), 'f', 1));
        if (vn30_index_)
            vn30_index_->setText(QString::number(d.value("vn30_index").toDouble(), 'f', 2));
        if (basis_label_)
            basis_label_->setText(QString::number(d.value("basis").toDouble(), 'f', 2));
        if (oi_label_)
            oi_label_->setText(QString::number(d.value("open_interest").toInt()));
        if (price_label_)
            price_label_->setText(QString("VN30F: %1").arg(d.value("last_price").toDouble(), 0, 'f', 1));
    });

    connect(&svc, &VN30FTradingService::deployments_loaded, this,
            [this](const QVector<VN30FDeployment>& deps) {
                int active = 0;
                for (const auto& d : deps) {
                    if (d.status == "running")
                        ++active;
                }
                if (deploy_count_label_)
                    deploy_count_label_->setText(QString("%1 LIVE").arg(active));

                if (deploy_table_) {
                    deploy_table_->setRowCount(deps.size());
                    for (int i = 0; i < deps.size(); ++i) {
                        deploy_table_->setItem(i, 0, new QTableWidgetItem(deps[i].deploy_id));
                        deploy_table_->setItem(i, 1, new QTableWidgetItem(deps[i].strategy_name));
                        deploy_table_->setItem(i, 2, new QTableWidgetItem(deps[i].symbol));
                        deploy_table_->setItem(i, 3, new QTableWidgetItem(deps[i].mode));
                        deploy_table_->setItem(i, 4, new QTableWidgetItem(deps[i].status));
                        deploy_table_->setItem(
                            i, 5, new QTableWidgetItem(QString::number(deps[i].total_pnl, 'f', 0)));
                        deploy_table_->setItem(
                            i, 6, new QTableWidgetItem(QString::number(deps[i].total_trades)));
                    }
                }
            });

    connect(&svc, &VN30FTradingService::agent_result, this, [this](const QJsonObject& data) {
        if (agent_output_) {
            auto d = data.value("data").toObject();
            agent_output_->append(QString("[%1] %2: %3\n")
                                      .arg(d.value("role").toString())
                                      .arg(d.value("action").toString())
                                      .arg(d.value("analysis").toString()));
        }
    });

    connect(&svc, &VN30FTradingService::backtest_result, this, [this](const QJsonObject& data) {
        auto d = data.value("data").toObject();
        auto metrics = d.value("metrics").toObject();
        if (backtest_metrics_) {
            backtest_metrics_->setRowCount(0);
            for (auto it = metrics.begin(); it != metrics.end(); ++it) {
                int row = backtest_metrics_->rowCount();
                backtest_metrics_->insertRow(row);
                backtest_metrics_->setItem(row, 0, new QTableWidgetItem(it.key()));
                backtest_metrics_->setItem(
                    row, 1, new QTableWidgetItem(QString::number(it.value().toDouble(), 'f', 4)));
            }
        }
    });

    connect(&svc, &VN30FTradingService::risk_metrics_loaded, this, [this](const QJsonObject& data) {
        auto d = data.value("data").toObject();
        auto risk = d.value("risk_metrics").toObject();
        if (var_label_) {
            var_label_->setText(
                QString("VaR (95%%): %1 | CVaR: %2 | Max DD: %3%%")
                    .arg(risk.value("var_95").toDouble(), 0, 'f', 2)
                    .arg(risk.value("cvar_95").toDouble(), 0, 'f', 2)
                    .arg(risk.value("max_drawdown_pct").toDouble() * 100, 0, 'f', 2));
        }
        if (risk_table_) {
            risk_table_->setRowCount(0);
            for (auto it = risk.begin(); it != risk.end(); ++it) {
                int row = risk_table_->rowCount();
                risk_table_->insertRow(row);
                risk_table_->setItem(row, 0, new QTableWidgetItem(it.key()));
                risk_table_->setItem(
                    row, 1, new QTableWidgetItem(QString::number(it.value().toDouble(), 'f', 4)));
            }
        }
    });

    connect(&svc, &VN30FTradingService::error_occurred, this, [this](const QString& ctx, const QString& msg) {
        LOG_ERROR("VN30FTrading", ctx + ": " + msg);
    });
}

} // namespace fincept::screens
