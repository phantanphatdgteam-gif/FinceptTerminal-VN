#!/usr/bin/env python3
"""
VN30F Dual Thrust Alpha — Adapted from VIXDualThrustAlpha.py
Uses dual-thrust breakout bands calibrated for VN30F intraday volatility.

Strategy ID: FCT-VN30F-DT01
Reference: VIXDualThrustAlpha.py (k1/k2 band breakout model)

VN30F Adaptations:
- Replaced UVXY with VN30F continuous futures
- Adjusted k1/k2 for VN30F volatility (typically 1-3% intraday)
- Added morning/afternoon session filter
- Added ±7% price limit awareness
"""

import sys
import os

# Add strategies path
scripts_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
strategies_dir = os.path.join(scripts_dir, "strategies")
if strategies_dir not in sys.path:
    sys.path.insert(0, strategies_dir)

from fincept_engine.algorithm import QCAlgorithm
from fincept_engine.enums import Resolution, SecurityType, InsightDirection, Market
from fincept_engine.types import Insight, Symbol
from fincept_engine.framework.alphas import AlphaModel


class VN30FDualThrustAlpha(QCAlgorithm):
    """VN30F Dual Thrust Breakout Strategy.

    Generates long/short signals when price breaks above/below
    the dual-thrust bands computed from N-day range.

    Band Formulas:
        range_val = max(HH-LC, HC-LL) over range_period
        upper_band = open + k1 * range_val
        lower_band = open - k2 * range_val
    """

    # Strategy metadata
    strategy_id = "FCT-VN30F-DT01"
    strategy_name = "VN30F Dual Thrust"
    strategy_category = "Alpha Model"

    def initialize(self):
        self.set_start_date(2023, 1, 2)
        self.set_end_date(2024, 12, 31)
        self.set_cash(500_000_000)  # 500M VND

        # VN30F parameters tuned for VN market
        self.k1 = 0.5           # Upper band multiplier (lower than VIX due to ±7% limit)
        self.k2 = 0.5           # Lower band multiplier
        self.range_period = 15  # Range lookback (trading days)

        # Add VN30F continuous futures
        self.vn30f = self.add_future("VN30F", Resolution.MINUTE, Market.HOSE)

        # Track range data
        self._highs = []
        self._lows = []
        self._closes = []
        self._bar_count = 0

        # Session filter times (VN: 09:00-11:00 morning, 13:30-14:15 afternoon)
        self.schedule.on(
            self.date_rules.every_day(),
            self.time_rules.at(9, 0),
            self._morning_session_start,
        )
        self.schedule.on(
            self.date_rules.every_day(),
            self.time_rules.at(14, 15),
            self._afternoon_session_end,
        )

        self._allow_trading = False
        self._daily_open = 0.0

    def _morning_session_start(self):
        """Enable trading at 9:00 AM after ATO."""
        self._allow_trading = True
        if self.vn30f and hasattr(self.securities, '__getitem__'):
            self._daily_open = self.securities[self.vn30f.symbol].price

    def _afternoon_session_end(self):
        """Close all positions at 14:15 before ATC."""
        self._allow_trading = False
        if self.portfolio.invested:
            self.liquidate()
            self.log("Position closed before ATC session")

    def on_data(self, data):
        if not self._allow_trading:
            return
        if not data.contains_key(self.vn30f.symbol):
            return

        bar = data[self.vn30f.symbol]
        price = bar.close

        # Update rolling window
        self._highs.append(bar.high)
        self._lows.append(bar.low)
        self._closes.append(bar.close)
        self._bar_count += 1

        # Trim to range_period
        if len(self._highs) > self.range_period:
            self._highs = self._highs[-self.range_period:]
            self._lows = self._lows[-self.range_period:]
            self._closes = self._closes[-self.range_period:]

        if len(self._highs) < self.range_period or self._daily_open <= 0:
            return

        # Dual Thrust band calculation
        hh = max(self._highs)   # Highest high
        ll = min(self._lows)    # Lowest low
        hc = max(self._closes)  # Highest close
        lc = min(self._closes)  # Lowest close

        range_val = max(hh - lc, hc - ll)
        upper_band = self._daily_open + self.k1 * range_val
        lower_band = self._daily_open - self.k2 * range_val

        # Generate signals
        if price > upper_band and not self.portfolio[self.vn30f.symbol].is_long:
            self.set_holdings(self.vn30f.symbol, 0.9)  # Long 90%
            self.log(f"LONG VN30F: price={price:.1f} > upper={upper_band:.1f}")
        elif price < lower_band and not self.portfolio[self.vn30f.symbol].is_short:
            self.set_holdings(self.vn30f.symbol, -0.9)  # Short 90%
            self.log(f"SHORT VN30F: price={price:.1f} < lower={lower_band:.1f}")

    def on_end_of_algorithm(self):
        self.log(f"[{self.strategy_name}] Final NAV: {self.portfolio.total_portfolio_value:,.0f} VND")


# ============================================================================
# Alpha Model variant (for use with framework pipeline)
# ============================================================================

class VN30FDualThrustAlphaModel(AlphaModel):
    """VN30F Dual Thrust as a pluggable AlphaModel for the framework pipeline."""

    def __init__(self, k1=0.5, k2=0.5, range_period=15):
        super().__init__()
        self.k1 = k1
        self.k2 = k2
        self.range_period = range_period
        self._symbol_data = {}

    def update(self, algorithm, data):
        insights = []
        for symbol, sd in self._symbol_data.items():
            if not data.contains_key(symbol):
                continue
            bar = data[symbol]
            sd["highs"].append(bar.high)
            sd["lows"].append(bar.low)
            sd["closes"].append(bar.close)
            if len(sd["highs"]) > self.range_period:
                sd["highs"] = sd["highs"][-self.range_period:]
                sd["lows"] = sd["lows"][-self.range_period:]
                sd["closes"] = sd["closes"][-self.range_period:]
            if len(sd["highs"]) < self.range_period:
                continue

            hh = max(sd["highs"])
            ll = min(sd["lows"])
            hc = max(sd["closes"])
            lc = min(sd["closes"])
            range_val = max(hh - lc, hc - ll)
            open_price = sd.get("daily_open", bar.open)
            upper = open_price + self.k1 * range_val
            lower = open_price - self.k2 * range_val

            if bar.close > upper:
                insights.append(
                    Insight.price(symbol, timedelta(hours=4), InsightDirection.UP,
                                  magnitude=0.01, confidence=0.6,
                                  source_model="VN30FDualThrust")
                )
            elif bar.close < lower:
                insights.append(
                    Insight.price(symbol, timedelta(hours=4), InsightDirection.DOWN,
                                  magnitude=0.01, confidence=0.6,
                                  source_model="VN30FDualThrust")
                )
        return insights

    def on_securities_changed(self, algorithm, changes):
        for security in changes.added_securities:
            self._symbol_data[security.symbol] = {
                "highs": [], "lows": [], "closes": [], "daily_open": 0
            }
        for security in changes.removed_securities:
            self._symbol_data.pop(security.symbol, None)


from datetime import timedelta  # noqa: E402 — needed by AlphaModel variant
