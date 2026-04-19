#pragma once
// VN30FSettlement — expiry date helpers for VN30F futures.
//
// VN30F (VN30 Index Futures listed on HNX) expires on the third Thursday
// of each calendar month.  The last trading day is the expiry Thursday
// itself (unlike many other futures where it is the business day before).
//
// If the third Thursday falls on a Vietnamese public holiday the exchange
// will announce a substitute date; this module cannot account for ad-hoc
// closures but covers the standard rule used for scheduling.

#include <QDate>
#include <QString>
#include <QStringList>

namespace fincept::trading::vn30f {

/// Return the expiry date (third Thursday) for a given month/year.
inline QDate expiry_date(int year, int month) {
    // Start from the first day of the month, advance to first Thursday,
    // then add 14 days to reach the third Thursday.
    QDate first(year, month, 1);
    // Qt: Qt::Thursday == 4; dayOfWeek() returns 1 (Mon) … 7 (Sun)
    int dow = first.dayOfWeek(); // 1=Mon … 7=Sun
    int days_to_thu = (Qt::Thursday - dow + 7) % 7;
    QDate first_thu = first.addDays(days_to_thu);
    return first_thu.addDays(14); // + 2 weeks = third Thursday
}

/// Return the expiry date for the current near-month contract.
/// If today is past the current month's expiry, returns next month's expiry.
inline QDate nearest_expiry() {
    QDate today = QDate::currentDate();
    QDate candidate = expiry_date(today.year(), today.month());
    if (today > candidate) {
        // Roll to next month
        QDate next_month = today.addMonths(1);
        candidate = expiry_date(next_month.year(), next_month.month());
    }
    return candidate;
}

/// Return the number of calendar days to the nearest expiry (can be 0).
inline int days_to_expiry() {
    return QDate::currentDate().daysTo(nearest_expiry());
}

/// Check whether today is an expiry day.
inline bool is_expiry_today() {
    return QDate::currentDate() == nearest_expiry();
}

/// Standard VN30F contract code suffix derived from month and year.
/// Convention: <Month-letter><2-digit-year>  e.g. "F25" for Jan 2025,
/// "G25" for Feb 2025, etc. (CME/Bloomberg month codes).
inline QString contract_month_code(int year, int month) {
    static constexpr QChar kMonthCodes[] = {'F','G','H','J','K','M','N','Q','U','V','X','Z'};
    Q_ASSERT(month >= 1 && month <= 12);
    return QString(kMonthCodes[month - 1]) + QString::number(year).right(2);
}

/// Return the canonical near-month VN30F symbol, e.g. "VN30F2H25".
inline QString near_month_symbol() {
    QDate exp = nearest_expiry();
    return "VN30F" + contract_month_code(exp.year(), exp.month());
}

/// List the next N monthly expiry dates starting from today.
inline QList<QDate> upcoming_expiries(int count = 3) {
    QList<QDate> result;
    QDate base = QDate::currentDate();
    for (int month_offset = 0; result.size() < count; ++month_offset) {
        QDate m = base.addMonths(month_offset);
        QDate exp = expiry_date(m.year(), m.month());
        if (exp >= base)
            result.append(exp);
    }
    return result;
}

// ── VN30F contract specification constants ────────────────────────────────────

/// Contract multiplier: 1 point = 100 000 VND.
inline constexpr double kPointValue = 100'000.0;

/// Initial margin requirement (approximate, may vary by broker).
/// HNX publishes the official rate; ~12% is a common reference.
inline constexpr double kInitialMarginRate = 0.12;

/// Maintenance margin rate.
inline constexpr double kMaintenanceMarginRate = 0.09;

/// Minimum tick size: 0.1 index points.
inline constexpr double kTickSize = 0.1;

/// Value of one minimum tick move.
inline constexpr double kTickValue = kTickSize * kPointValue;

/// Calculate required initial margin for a position.
/// @param entry_price  Entry price in index points.
/// @param lots         Number of contracts.
inline double initial_margin(double entry_price, int lots) {
    return entry_price * kPointValue * lots * kInitialMarginRate;
}

/// Calculate P&L (in VND) for a position.
/// @param entry_price  Price when position was opened (index points).
/// @param current_price Current market price (index points).
/// @param lots         Number of contracts.
/// @param is_long      true = long, false = short.
inline double position_pnl(double entry_price, double current_price, int lots, bool is_long) {
    const double pts = is_long ? (current_price - entry_price) : (entry_price - current_price);
    return pts * kPointValue * lots;
}

} // namespace fincept::trading::vn30f
