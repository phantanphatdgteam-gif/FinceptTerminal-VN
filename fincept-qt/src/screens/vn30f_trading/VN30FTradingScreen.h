// src/screens/vn30f_trading/VN30FTradingScreen.h
#pragma once
#include "screens/IStatefulScreen.h"
#include "services/vn30f/VN30FTradingService.h"

#include <QHideEvent>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>

namespace fincept::screens {

/// VN30F Trading screen — 7-tab VN30F futures trading terminal.
/// Tabs: Overview, Strategy, ML Models, Agent, Backtest, Live, Risk
class VN30FTradingScreen : public QWidget, public IStatefulScreen {
    Q_OBJECT
  public:
    explicit VN30FTradingScreen(QWidget* parent = nullptr);

    void restore_state(const QVariantMap& state) override;
    QVariantMap save_state() const override;
    QString state_key() const override { return "vn30f_trading"; }
    int state_version() const override { return 1; }

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private slots:
    void on_tab_changed(int index);

  private:
    void build_ui();
    QWidget* build_top_bar();
    QWidget* build_status_bar();

    // Tab panels — built lazily
    QWidget* build_overview_panel();
    QWidget* build_strategy_panel();
    QWidget* build_ml_panel();
    QWidget* build_agent_panel();
    QWidget* build_backtest_panel();
    QWidget* build_live_panel();
    QWidget* build_risk_panel();

    void connect_service();
    void refresh_current_tab();
    void update_tab_buttons();

    // Components
    QStackedWidget* content_stack_ = nullptr;
    QVector<QPushButton*> tab_buttons_;
    int active_tab_ = 0;

    // Status bar
    QLabel* session_label_ = nullptr;
    QLabel* price_label_ = nullptr;
    QLabel* pnl_label_ = nullptr;
    QLabel* deploy_count_label_ = nullptr;

    // Overview panel widgets
    QLabel* vn30f_price_ = nullptr;
    QLabel* vn30_index_ = nullptr;
    QLabel* basis_label_ = nullptr;
    QLabel* oi_label_ = nullptr;
    QTableWidget* overview_table_ = nullptr;

    // Strategy panel
    QTableWidget* strategy_table_ = nullptr;
    QLabel* signal_label_ = nullptr;

    // ML panel
    QLabel* ml_status_ = nullptr;
    QTableWidget* ml_table_ = nullptr;

    // Agent panel
    QTextEdit* agent_output_ = nullptr;

    // Backtest panel
    QTableWidget* backtest_metrics_ = nullptr;
    QTextEdit* backtest_log_ = nullptr;

    // Live panel
    QTableWidget* deploy_table_ = nullptr;
    QLabel* live_status_ = nullptr;

    // Risk panel
    QTableWidget* risk_table_ = nullptr;
    QLabel* var_label_ = nullptr;

    QTimer* poll_timer_ = nullptr;
    bool first_show_ = true;
};

} // namespace fincept::screens
