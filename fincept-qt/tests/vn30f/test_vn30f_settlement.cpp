// Fincept Terminal — VN30FSettlement unit tests
//
// Tests the header-only helpers in trading/vn30f/VN30FSettlement.h:
//   - expiry_date()         third-Thursday calculation
//   - contract_month_code() CME month-letter encoding
//   - position_pnl()        long/short P&L in VND
//   - initial_margin()      margin requirement in VND
//   - upcoming_expiries()   ordered list of future expiry dates

#include "trading/vn30f/VN30FSettlement.h"

#include <QObject>
#include <QTest>

using namespace fincept::trading::vn30f;

class TestVN30FSettlement : public QObject {
    Q_OBJECT

  private slots:

    // ── expiry_date() ─────────────────────────────────────────────────────────

    // January 2025: 1st = Wednesday → first Thursday = 2nd → third = 16th.
    void expiry_date_jan_2025() {
        const QDate exp = expiry_date(2025, 1);
        QCOMPARE(exp, QDate(2025, 1, 16));
        QCOMPARE(exp.dayOfWeek(), static_cast<int>(Qt::Thursday));
    }

    // March 2025: 1st = Saturday → first Thursday = 6th → third = 20th.
    void expiry_date_mar_2025() {
        const QDate exp = expiry_date(2025, 3);
        QCOMPARE(exp, QDate(2025, 3, 20));
        QCOMPARE(exp.dayOfWeek(), static_cast<int>(Qt::Thursday));
    }

    // May 2025: 1st = Thursday → first Thursday = 1st → third = 15th.
    void expiry_date_month_starts_on_thursday() {
        const QDate exp = expiry_date(2025, 5);
        QCOMPARE(exp, QDate(2025, 5, 15));
        QCOMPARE(exp.dayOfWeek(), static_cast<int>(Qt::Thursday));
    }

    // December 2025: 1st = Monday → first Thursday = 4th → third = 18th.
    void expiry_date_dec_2025() {
        const QDate exp = expiry_date(2025, 12);
        QCOMPARE(exp, QDate(2025, 12, 18));
        QCOMPARE(exp.dayOfWeek(), static_cast<int>(Qt::Thursday));
    }

    // Every expiry must fall on a Thursday — spot-check a full calendar year.
    void expiry_date_always_thursday_all_months_2025() {
        for (int m = 1; m <= 12; ++m) {
            const QDate exp = expiry_date(2025, m);
            QVERIFY2(exp.dayOfWeek() == Qt::Thursday,
                     qPrintable(QString("Month %1: %2 is not a Thursday").arg(m).arg(exp.toString())));
        }
    }

    // ── contract_month_code() ─────────────────────────────────────────────────

    void contract_month_code_january() {
        QCOMPARE(contract_month_code(2025, 1), QString("F25"));
    }

    void contract_month_code_february() {
        QCOMPARE(contract_month_code(2025, 2), QString("G25"));
    }

    void contract_month_code_march() {
        QCOMPARE(contract_month_code(2025, 3), QString("H25"));
    }

    void contract_month_code_june() {
        QCOMPARE(contract_month_code(2025, 6), QString("M25"));
    }

    void contract_month_code_december() {
        QCOMPARE(contract_month_code(2025, 12), QString("Z25"));
    }

    // All 12 month codes must be a single uppercase letter followed by 2 digits.
    void contract_month_code_format_all_months() {
        static constexpr QChar expected[] = {
            'F','G','H','J','K','M','N','Q','U','V','X','Z'
        };
        for (int m = 1; m <= 12; ++m) {
            const QString code = contract_month_code(2025, m);
            QCOMPARE(code.length(), 3);
            QCOMPARE(code[0], expected[m - 1]);
            QCOMPARE(code.mid(1), QString("25"));
        }
    }

    // Year rollover: 2030 → "30".
    void contract_month_code_year_rollover() {
        const QString code = contract_month_code(2030, 1);
        QCOMPARE(code, QString("F30"));
    }

    // ── position_pnl() ────────────────────────────────────────────────────────

    // Long position gains when price rises.
    void position_pnl_long_profit() {
        // Entry 1200, exit 1210, 2 lots → (10 pts * 100 000 VND/pt * 2 lots) = 2 000 000 VND
        const double pnl = position_pnl(1200.0, 1210.0, 2, /*is_long=*/true);
        QCOMPARE(pnl, 2'000'000.0);
    }

    // Long position loses when price falls.
    void position_pnl_long_loss() {
        // Entry 1200, exit 1195, 1 lot → (-5 * 100 000) = -500 000 VND
        const double pnl = position_pnl(1200.0, 1195.0, 1, /*is_long=*/true);
        QCOMPARE(pnl, -500'000.0);
    }

    // Short position gains when price falls.
    void position_pnl_short_profit() {
        // Entry 1200, exit 1190, 1 lot → (10 * 100 000) = 1 000 000 VND
        const double pnl = position_pnl(1200.0, 1190.0, 1, /*is_long=*/false);
        QCOMPARE(pnl, 1'000'000.0);
    }

    // Short position loses when price rises.
    void position_pnl_short_loss() {
        // Entry 1200, exit 1205, 2 lots → (-5 * 100 000 * 2) = -1 000 000 VND
        const double pnl = position_pnl(1200.0, 1205.0, 2, /*is_long=*/false);
        QCOMPARE(pnl, -1'000'000.0);
    }

    // Zero move gives zero P&L.
    void position_pnl_flat() {
        QCOMPARE(position_pnl(1200.0, 1200.0, 3, true),  0.0);
        QCOMPARE(position_pnl(1200.0, 1200.0, 3, false), 0.0);
    }

    // ── initial_margin() ─────────────────────────────────────────────────────

    void initial_margin_two_lots() {
        // 1200 pts * 100 000 VND/pt * 2 lots * 12% = 28 800 000 VND
        const double margin = initial_margin(1200.0, 2);
        QCOMPARE(margin, 28'800'000.0);
    }

    void initial_margin_single_lot() {
        // 1000 * 100 000 * 1 * 0.12 = 12 000 000 VND
        const double margin = initial_margin(1000.0, 1);
        QCOMPARE(margin, 12'000'000.0);
    }

    // ── Contract constants ────────────────────────────────────────────────────

    void constants_point_value() {
        QCOMPARE(kPointValue, 100'000.0);
    }

    void constants_tick_size() {
        QCOMPARE(kTickSize, 0.1);
    }

    void constants_tick_value() {
        // 0.1 pt * 100 000 VND/pt = 10 000 VND per minimum tick move
        QCOMPARE(kTickValue, 10'000.0);
    }

    void constants_initial_margin_rate() {
        QCOMPARE(kInitialMarginRate, 0.12);
    }

    void constants_maintenance_margin_less_than_initial() {
        QVERIFY(kMaintenanceMarginRate < kInitialMarginRate);
    }

    // ── upcoming_expiries() ───────────────────────────────────────────────────

    // Returns exactly the requested count.
    void upcoming_expiries_count() {
        const auto list = upcoming_expiries(3);
        QCOMPARE(list.size(), 3);
    }

    void upcoming_expiries_all_thursdays() {
        const auto list = upcoming_expiries(6);
        for (const QDate& d : list) {
            QVERIFY2(d.dayOfWeek() == Qt::Thursday,
                     qPrintable(QString("%1 is not a Thursday").arg(d.toString())));
        }
    }

    void upcoming_expiries_all_on_or_after_today() {
        const QDate today = QDate::currentDate();
        const auto list = upcoming_expiries(4);
        for (const QDate& d : list) {
            QVERIFY2(d >= today,
                     qPrintable(QString("%1 is in the past").arg(d.toString())));
        }
    }

    void upcoming_expiries_sorted_ascending() {
        const auto list = upcoming_expiries(4);
        for (int i = 1; i < list.size(); ++i) {
            QVERIFY2(list[i] > list[i - 1],
                     qPrintable(QString("Expiry list not sorted at index %1").arg(i)));
        }
    }
};

QTEST_MAIN(TestVN30FSettlement)
#include "test_vn30f_settlement.moc"
