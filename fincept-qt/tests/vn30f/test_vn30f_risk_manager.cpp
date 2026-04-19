// Fincept Terminal — VN30FRiskManager unit tests
//
// Tests the VN30FRiskManager QObject service:
//   - Session lifecycle  (begin_session / end_session)
//   - can_place_order()  kill-switch guard, lot-size limits, position limits
//   - Kill switch        daily-loss limit and session-drawdown limit
//   - daily_loss_warning signal (fires once per session at 80% of limit)
//   - recommended_lots() FixedLots / PercentRisk / KellyFraction modes
//   - PnL accessors      realised_pnl_today / unrealised_pnl / total_pnl_today

#include "services/vn30f/VN30FRiskManager.h"

#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

using namespace fincept::services::vn30f;

class TestVN30FRiskManager : public QObject {
    Q_OBJECT

  private:
    // Convenience: build a risk manager with common defaults.
    static VN30FRiskManager* make_manager(double max_daily_loss = 1'000'000.0,
                                          int    max_open_lots  = 3,
                                          int    max_order_lots = 2,
                                          double max_drawdown   = 0.05) {
        VN30FRiskConfig cfg;
        cfg.max_daily_loss_vnd  = max_daily_loss;
        cfg.max_open_lots       = max_open_lots;
        cfg.max_order_lots      = max_order_lots;
        cfg.max_session_drawdown = max_drawdown;
        return new VN30FRiskManager(cfg);
    }

  private slots:

    // ── Session lifecycle ─────────────────────────────────────────────────────

    void begin_session_resets_pnl() {
        auto* mgr = make_manager();
        mgr->begin_session(5'000'000.0);
        QCOMPARE(mgr->realised_pnl_today(), 0.0);
        QCOMPARE(mgr->unrealised_pnl(), 0.0);
        QCOMPARE(mgr->total_pnl_today(), 0.0);
        QVERIFY(!mgr->is_kill_switch_active());
        delete mgr;
    }

    void begin_session_clears_kill_switch() {
        auto* mgr = make_manager(/*max_daily_loss=*/100'000.0);
        mgr->begin_session(1'000'000.0);
        // Trigger kill switch.
        mgr->on_trade_filled(-200'000.0);
        QVERIFY(mgr->is_kill_switch_active());

        // A new session must clear it.
        mgr->begin_session(1'000'000.0);
        QVERIFY(!mgr->is_kill_switch_active());
        delete mgr;
    }

    void end_session_resets_realised_pnl() {
        auto* mgr = make_manager();
        mgr->begin_session(2'000'000.0);
        mgr->on_trade_filled(300'000.0);
        QCOMPARE(mgr->realised_pnl_today(), 300'000.0);

        mgr->end_session();
        QCOMPARE(mgr->realised_pnl_today(), 0.0);
        QCOMPARE(mgr->unrealised_pnl(), 0.0);
        delete mgr;
    }

    // ── can_place_order() ─────────────────────────────────────────────────────

    void can_place_order_normal_allowed() {
        auto* mgr = make_manager();
        mgr->begin_session(5'000'000.0);
        QVERIFY(mgr->can_place_order(/*lots=*/1, /*open=*/0));
        delete mgr;
    }

    void can_place_order_blocked_by_kill_switch() {
        auto* mgr = make_manager(/*max_daily_loss=*/100'000.0);
        mgr->begin_session(1'000'000.0);
        mgr->on_trade_filled(-200'000.0); // triggers kill switch
        QVERIFY(mgr->is_kill_switch_active());
        QVERIFY(!mgr->can_place_order(1, 0));
        delete mgr;
    }

    void can_place_order_blocked_by_order_size_limit() {
        // max_order_lots = 2 → a 3-lot order must be rejected.
        auto* mgr = make_manager(/*max_daily_loss=*/1'000'000.0,
                                 /*max_open_lots=*/10,
                                 /*max_order_lots=*/2);
        mgr->begin_session(5'000'000.0);
        QVERIFY(!mgr->can_place_order(3, 0));
        QVERIFY( mgr->can_place_order(2, 0));
        delete mgr;
    }

    void can_place_order_blocked_when_position_limit_would_be_exceeded() {
        // max_open_lots = 3, currently open = 2, new order = 2 → total = 4 → reject.
        auto* mgr = make_manager(/*max_daily_loss=*/1'000'000.0,
                                 /*max_open_lots=*/3,
                                 /*max_order_lots=*/2);
        mgr->begin_session(5'000'000.0);
        QVERIFY(!mgr->can_place_order(2, 2));
        // But adding 1 lot to existing 2 = 3 total → allowed.
        QVERIFY( mgr->can_place_order(1, 2));
        delete mgr;
    }

    // ── Kill switch — daily loss limit ────────────────────────────────────────

    void kill_switch_fires_on_daily_loss_limit() {
        auto* mgr = make_manager(/*max_daily_loss=*/1'000'000.0);
        mgr->begin_session(10'000'000.0);

        QSignalSpy spy(mgr, &VN30FRiskManager::kill_switch_triggered);

        mgr->on_trade_filled(-1'000'001.0); // exceeds limit
        QVERIFY(mgr->is_kill_switch_active());
        QCOMPARE(spy.count(), 1);
        delete mgr;
    }

    void kill_switch_does_not_fire_below_limit() {
        auto* mgr = make_manager(/*max_daily_loss=*/1'000'000.0);
        mgr->begin_session(10'000'000.0);

        QSignalSpy spy(mgr, &VN30FRiskManager::kill_switch_triggered);

        mgr->on_trade_filled(-999'999.0); // just under the limit
        QVERIFY(!mgr->is_kill_switch_active());
        QCOMPARE(spy.count(), 0);
        delete mgr;
    }

    // Kill switch fires only once even when further losses arrive.
    void kill_switch_fires_exactly_once() {
        auto* mgr = make_manager(/*max_daily_loss=*/500'000.0);
        mgr->begin_session(5'000'000.0);

        QSignalSpy spy(mgr, &VN30FRiskManager::kill_switch_triggered);

        mgr->on_trade_filled(-600'000.0);
        mgr->on_trade_filled(-200'000.0); // already active — no second signal
        QCOMPARE(spy.count(), 1);
        delete mgr;
    }

    // ── Kill switch — session drawdown ────────────────────────────────────────

    void kill_switch_fires_on_session_drawdown() {
        // max_session_drawdown = 5%.
        // Begin with 1 M VND equity; let it rise to 1.1 M (new peak) then fall hard.
        auto* mgr = make_manager(/*max_daily_loss=*/10'000'000.0, // keep daily limit out of the way
                                 /*max_open_lots=*/10,
                                 /*max_order_lots=*/10,
                                 /*max_drawdown=*/0.05);
        mgr->begin_session(1'000'000.0);

        QSignalSpy spy(mgr, &VN30FRiskManager::kill_switch_triggered);

        // Unrealised profit pushes peak to 1 100 000.
        mgr->set_unrealised_pnl(100'000.0);
        QVERIFY(!mgr->is_kill_switch_active());

        // Now drop well below 5% drawdown from peak (1 100 000 * 95% = 1 045 000).
        // set_unrealised_pnl(-60 000) → current equity = 1 000 000 + (-60 000) = 940 000
        // drawdown = (1 100 000 - 940 000) / 1 100 000 ≈ 14.5% > 5%
        mgr->set_unrealised_pnl(-60'000.0);
        QVERIFY(mgr->is_kill_switch_active());
        QCOMPARE(spy.count(), 1);
        delete mgr;
    }

    // ── daily_loss_warning signal ─────────────────────────────────────────────

    void daily_loss_warning_fires_at_80_percent() {
        // Limit = 1 000 000 VND; warning level = 800 000 VND.
        auto* mgr = make_manager(/*max_daily_loss=*/1'000'000.0);
        mgr->begin_session(5'000'000.0);

        QSignalSpy warn_spy(mgr,  &VN30FRiskManager::daily_loss_warning);
        QSignalSpy kill_spy(mgr,  &VN30FRiskManager::kill_switch_triggered);

        mgr->on_trade_filled(-800'001.0); // crosses 80% threshold
        QCOMPARE(warn_spy.count(), 1);
        QCOMPARE(kill_spy.count(), 0); // not yet at 100%
        delete mgr;
    }

    // Warning must not fire a second time once already sent.
    void daily_loss_warning_fires_only_once_per_session() {
        auto* mgr = make_manager(/*max_daily_loss=*/1'000'000.0);
        mgr->begin_session(5'000'000.0);

        QSignalSpy warn_spy(mgr, &VN30FRiskManager::daily_loss_warning);

        mgr->on_trade_filled(-800'001.0); // first cross
        mgr->on_trade_filled(-10'000.0);  // deeper — no second warning
        QCOMPARE(warn_spy.count(), 1);
        delete mgr;
    }

    // After end_session() + begin_session() the warning can fire again.
    void daily_loss_warning_resets_after_new_session() {
        auto* mgr = make_manager(/*max_daily_loss=*/1'000'000.0);
        mgr->begin_session(5'000'000.0);
        mgr->on_trade_filled(-800'001.0);

        mgr->end_session();
        mgr->begin_session(5'000'000.0);

        QSignalSpy warn_spy(mgr, &VN30FRiskManager::daily_loss_warning);
        mgr->on_trade_filled(-800'001.0);
        QCOMPARE(warn_spy.count(), 1);
        delete mgr;
    }

    // ── PnL accessors ─────────────────────────────────────────────────────────

    void pnl_accessors_accumulate_correctly() {
        auto* mgr = make_manager();
        mgr->begin_session(10'000'000.0);

        mgr->on_trade_filled(200'000.0);
        mgr->on_trade_filled(-50'000.0);
        mgr->set_unrealised_pnl(30'000.0);

        QCOMPARE(mgr->realised_pnl_today(), 150'000.0);
        QCOMPARE(mgr->unrealised_pnl(),      30'000.0);
        QCOMPARE(mgr->total_pnl_today(),    180'000.0);
        delete mgr;
    }

    // ── recommended_lots() — FixedLots mode ──────────────────────────────────

    void recommended_lots_fixed_mode_returns_fixed_value() {
        VN30FRiskConfig cfg;
        cfg.sizing_mode = VN30FRiskConfig::SizingMode::FixedLots;
        cfg.fixed_lots  = 2;
        cfg.max_order_lots = 5;
        VN30FRiskManager mgr(cfg);
        mgr.begin_session(10'000'000.0);

        QCOMPARE(mgr.recommended_lots(10'000'000.0, 5.0), 2);
    }

    // ── recommended_lots() — PercentRisk mode ────────────────────────────────

    void recommended_lots_percent_risk_mode() {
        // equity = 100 M VND, risk_pct = 1%, stop = 5 pts
        // risk_budget = 1 000 000 VND
        // risk_per_lot = 5 * 100 000 = 500 000 VND
        // lots = floor(1 000 000 / 500 000) = 2 → clamped to min(2, max_order_lots=2) = 2
        VN30FRiskConfig cfg;
        cfg.sizing_mode       = VN30FRiskConfig::SizingMode::PercentRisk;
        cfg.risk_pct_per_trade = 0.01;
        cfg.max_order_lots    = 2;
        VN30FRiskManager mgr(cfg);
        mgr.begin_session(100'000'000.0);

        QCOMPARE(mgr.recommended_lots(100'000'000.0, 5.0), 2);
    }

    // ── recommended_lots() — KellyFraction mode ──────────────────────────────

    void recommended_lots_kelly_mode_clamped_to_max_order_lots() {
        // win_rate = 0.6, avg_win = 10, avg_loss = 5, kelly_fraction = 0.25
        // raw_kelly = 0.6/5 - 0.4/10 = 0.12 - 0.04 = 0.08
        // kelly = 0.08 * 0.25 = 0.02
        // equity = 100 M → risk_budget = 2 000 000
        // risk_per_lot = 5 * 100 000 = 500 000 → lots = 4 → clamped to max_order_lots = 2
        VN30FRiskConfig cfg;
        cfg.sizing_mode     = VN30FRiskConfig::SizingMode::KellyFraction;
        cfg.kelly_fraction  = 0.25;
        cfg.max_order_lots  = 2;
        VN30FRiskManager mgr(cfg);
        mgr.begin_session(100'000'000.0);

        const int lots = mgr.recommended_lots(
            100'000'000.0, /*stop_pts=*/5.0,
            /*win_rate=*/0.6, /*avg_win=*/10.0, /*avg_loss=*/5.0);
        QCOMPARE(lots, 2);
    }

    // Negative Kelly edge (win_rate too low) must not produce a negative lot count.
    void recommended_lots_kelly_negative_edge_clamps_to_one() {
        VN30FRiskConfig cfg;
        cfg.sizing_mode    = VN30FRiskConfig::SizingMode::KellyFraction;
        cfg.kelly_fraction = 0.25;
        cfg.max_order_lots = 5;
        VN30FRiskManager mgr(cfg);
        mgr.begin_session(10'000'000.0);

        // Terrible stats: win_rate=0.1, avg_win=1, avg_loss=10
        // raw_kelly = 0.1/10 - 0.9/1 = 0.01 - 0.9 = negative → clamped to 0 → lots clamped to 1
        const int lots = mgr.recommended_lots(
            10'000'000.0, 5.0, 0.1, 1.0, 10.0);
        QCOMPARE(lots, 1);
    }

    // zero stop_pts falls back to fixed_lots.
    void recommended_lots_zero_stop_returns_fixed_lots() {
        VN30FRiskConfig cfg;
        cfg.sizing_mode    = VN30FRiskConfig::SizingMode::PercentRisk;
        cfg.fixed_lots     = 3;
        cfg.max_order_lots = 5;
        VN30FRiskManager mgr(cfg);
        mgr.begin_session(10'000'000.0);

        QCOMPARE(mgr.recommended_lots(10'000'000.0, /*stop_pts=*/0.0), 3);
    }
};

QTEST_MAIN(TestVN30FRiskManager)
#include "test_vn30f_risk_manager.moc"
