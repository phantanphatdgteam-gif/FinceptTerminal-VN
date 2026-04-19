// Fincept Terminal — VN30FBacktest unit tests
//
// Tests the header-only helpers in services/backtesting/VN30FBacktest.h:
//   - build_backtest_config()         JSON structure and field values
//   - meets_deployment_thresholds()   pass/fail logic against metric thresholds
//   - walk_forward_config()           walk-forward window fields
//   - *_param_grid() helpers          key presence and array types

#include "services/backtesting/VN30FBacktest.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTest>

using namespace fincept::services::backtest::vn30f;

class TestVN30FBacktest : public QObject {
    Q_OBJECT

  private slots:

    // ── build_backtest_config() ───────────────────────────────────────────────

    void build_config_mandatory_fields_present() {
        const QJsonObject cfg = build_backtest_config("vectorbt", "Breakout");
        QVERIFY(cfg.contains("provider"));
        QVERIFY(cfg.contains("strategy"));
        QVERIFY(cfg.contains("symbol"));
        QVERIFY(cfg.contains("exchange"));
        QVERIFY(cfg.contains("from_date"));
        QVERIFY(cfg.contains("to_date"));
        QVERIFY(cfg.contains("capital"));
        QVERIFY(cfg.contains("currency"));
        QVERIFY(cfg.contains("commission"));
        QVERIFY(cfg.contains("slippage"));
        QVERIFY(cfg.contains("point_value"));
    }

    void build_config_provider_and_strategy_match_args() {
        const QJsonObject cfg = build_backtest_config("backtestingpy", "MeanReversion");
        QCOMPARE(cfg["provider"].toString(), QString("backtestingpy"));
        QCOMPARE(cfg["strategy"].toString(), QString("MeanReversion"));
    }

    void build_config_exchange_always_hnx() {
        const QJsonObject cfg = build_backtest_config("vectorbt", "Breakout");
        QCOMPARE(cfg["exchange"].toString(), QString("HNX"));
    }

    void build_config_currency_is_vnd() {
        const QJsonObject cfg = build_backtest_config("fincept", "VWAP");
        QCOMPARE(cfg["currency"].toString(), QString("VND"));
    }

    void build_config_commission_matches_constant() {
        const QJsonObject cfg = build_backtest_config("vectorbt", "Breakout");
        QCOMPARE(cfg["commission"].toDouble(), kCommissionRate);
    }

    void build_config_slippage_matches_constant() {
        const QJsonObject cfg = build_backtest_config("vectorbt", "Breakout");
        QCOMPARE(cfg["slippage"].toDouble(), kSlippagePoints);
    }

    void build_config_point_value_matches_constant() {
        const QJsonObject cfg = build_backtest_config("vectorbt", "Breakout");
        QCOMPARE(cfg["point_value"].toDouble(), kPointValue);
    }

    void build_config_symbol_defaults_to_continuous() {
        const QJsonObject cfg = build_backtest_config("vectorbt", "Breakout");
        QCOMPARE(cfg["symbol"].toString(), QString("VN30F1!"));
    }

    void build_config_custom_symbol() {
        const QJsonObject cfg = build_backtest_config("vectorbt", "Breakout", "VN30F2406");
        QCOMPARE(cfg["symbol"].toString(), QString("VN30F2406"));
    }

    void build_config_empty_to_date_uses_today() {
        const QJsonObject cfg = build_backtest_config("vectorbt", "Breakout");
        const QString to_date = cfg["to_date"].toString();
        QVERIFY(!to_date.isEmpty());
        // Must be parseable as a date.
        const QDate parsed = QDate::fromString(to_date, "yyyy-MM-dd");
        QVERIFY(parsed.isValid());
    }

    void build_config_explicit_to_date_is_preserved() {
        const QJsonObject cfg = build_backtest_config(
            "vectorbt", "Breakout", "VN30F1!",
            "2020-01-01", "2023-12-31");
        QCOMPARE(cfg["to_date"].toString(), QString("2023-12-31"));
    }

    void build_config_capital_is_set() {
        const QJsonObject cfg = build_backtest_config(
            "vectorbt", "Breakout", "VN30F1!", "2020-01-01", "",
            50'000'000.0);
        QCOMPARE(cfg["capital"].toDouble(), 50'000'000.0);
    }

    void build_config_params_included_when_non_empty() {
        QJsonObject params;
        params["lookback"] = 10;
        const QJsonObject cfg = build_backtest_config(
            "vectorbt", "Breakout", "VN30F1!", "2020-01-01", "", 100'000'000.0,
            params);
        QVERIFY(cfg.contains("params"));
        QCOMPARE(cfg["params"].toObject()["lookback"].toInt(), 10);
    }

    void build_config_params_absent_when_empty() {
        const QJsonObject cfg = build_backtest_config("vectorbt", "Breakout");
        QVERIFY(!cfg.contains("params"));
    }

    // ── meets_deployment_thresholds() ────────────────────────────────────────

    // A result meeting all four criteria must pass.
    void thresholds_all_criteria_met_returns_true() {
        QJsonObject r;
        r["sharpe_ratio"]  = 1.5;
        r["max_drawdown"]  = -0.10;   // qAbs = 0.10 ≤ 0.15
        r["win_rate"]      = 0.55;
        r["total_trades"]  = 50;
        QVERIFY(meets_deployment_thresholds(r));
    }

    void thresholds_fails_on_low_sharpe() {
        QJsonObject r;
        r["sharpe_ratio"]  = 0.8;     // < 1.0
        r["max_drawdown"]  = -0.10;
        r["win_rate"]      = 0.55;
        r["total_trades"]  = 50;
        QVERIFY(!meets_deployment_thresholds(r));
    }

    void thresholds_fails_on_excessive_drawdown() {
        QJsonObject r;
        r["sharpe_ratio"]  = 1.5;
        r["max_drawdown"]  = -0.20;   // qAbs = 0.20 > 0.15
        r["win_rate"]      = 0.55;
        r["total_trades"]  = 50;
        QVERIFY(!meets_deployment_thresholds(r));
    }

    void thresholds_fails_on_low_win_rate() {
        QJsonObject r;
        r["sharpe_ratio"]  = 1.5;
        r["max_drawdown"]  = -0.10;
        r["win_rate"]      = 0.40;    // < 0.45
        r["total_trades"]  = 50;
        QVERIFY(!meets_deployment_thresholds(r));
    }

    void thresholds_fails_on_too_few_trades() {
        QJsonObject r;
        r["sharpe_ratio"]  = 1.5;
        r["max_drawdown"]  = -0.10;
        r["win_rate"]      = 0.55;
        r["total_trades"]  = 25;     // < 30
        QVERIFY(!meets_deployment_thresholds(r));
    }

    // Exactly at the boundary must pass.
    void thresholds_pass_at_exact_boundary_values() {
        QJsonObject r;
        r["sharpe_ratio"]  = kMinSharpe;
        r["max_drawdown"]  = -kMaxDrawdown;  // qAbs = kMaxDrawdown
        r["win_rate"]      = kMinWinRate;
        r["total_trades"]  = kMinTrades;
        QVERIFY(meets_deployment_thresholds(r));
    }

    // Positive max_drawdown value (some providers report it without the sign).
    void thresholds_positive_drawdown_value_handled_by_qabs() {
        QJsonObject r;
        r["sharpe_ratio"]  = 1.5;
        r["max_drawdown"]  = 0.10;   // positive — qAbs still gives 0.10 ≤ 0.15
        r["win_rate"]      = 0.55;
        r["total_trades"]  = 50;
        QVERIFY(meets_deployment_thresholds(r));
    }

    // ── walk_forward_config() ─────────────────────────────────────────────────

    void walk_forward_config_fields_present() {
        const QJsonObject wf = walk_forward_config();
        QVERIFY(wf.contains("in_sample_months"));
        QVERIFY(wf.contains("out_sample_months"));
        QVERIFY(wf.contains("step_months"));
        QVERIFY(wf.contains("anchored"));
    }

    void walk_forward_config_standard_window() {
        const QJsonObject wf = walk_forward_config();
        QCOMPARE(wf["in_sample_months"].toInt(),  12);
        QCOMPARE(wf["out_sample_months"].toInt(),  3);
        QCOMPARE(wf["step_months"].toInt(),         1);
        QCOMPARE(wf["anchored"].toBool(),        false);
    }

    // ── Parameter grids ───────────────────────────────────────────────────────

    void breakout_param_grid_has_required_keys() {
        const QJsonObject g = breakout_param_grid();
        QVERIFY(g.contains("lookback"));
        QVERIFY(g.contains("volume_mult"));
        QVERIFY(g.contains("stop_loss"));
        QVERIFY(g.contains("take_profit"));
    }

    void breakout_param_grid_values_are_arrays() {
        const QJsonObject g = breakout_param_grid();
        QVERIFY(g["lookback"].isArray());
        QVERIFY(g["stop_loss"].isArray());
        QVERIFY(!g["lookback"].toArray().isEmpty());
    }

    void mean_reversion_param_grid_has_required_keys() {
        const QJsonObject g = mean_reversion_param_grid();
        QVERIFY(g.contains("rsi_period"));
        QVERIFY(g.contains("rsi_oversold"));
        QVERIFY(g.contains("bb_period"));
        QVERIFY(g.contains("bb_std"));
        QVERIFY(g.contains("stop_loss"));
    }

    void momentum_param_grid_has_required_keys() {
        const QJsonObject g = momentum_param_grid();
        QVERIFY(g.contains("macd_fast"));
        QVERIFY(g.contains("macd_slow"));
        QVERIFY(g.contains("ema_period"));
        QVERIFY(g.contains("adx_min"));
        QVERIFY(g.contains("trailing"));
    }

    void vwap_param_grid_has_required_keys() {
        const QJsonObject g = vwap_param_grid();
        QVERIFY(g.contains("rsi_confirm"));
        QVERIFY(g.contains("adx_min"));
        QVERIFY(g.contains("stop_loss"));
        QVERIFY(g.contains("take_profit"));
    }

    // ── Threshold constants ───────────────────────────────────────────────────

    void threshold_constants_have_sensible_values() {
        QVERIFY(kMinSharpe   >  0.0);
        QVERIFY(kMaxDrawdown >  0.0 && kMaxDrawdown <= 1.0);
        QVERIFY(kMinWinRate  >  0.0 && kMinWinRate  <  1.0);
        QVERIFY(kMinTrades   >  0);
    }
};

QTEST_MAIN(TestVN30FBacktest)
#include "test_vn30f_backtest.moc"
