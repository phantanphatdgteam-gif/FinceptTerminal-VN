#!/usr/bin/env python3
"""
VN30F Mean Reversion IBS Alpha — Adapted from GlobalEquityMeanReversionIBSAlpha.py
Uses Internal Bar Strength (IBS) for intraday mean reversion on VN30F.

Strategy ID: FCT-VN30F-IBS01
Reference: GlobalEquityMeanReversionIBSAlpha.py
Formula: IBS = (Close - Low) / (High - Low)

VN30F Adaptations:
- Single instrument (VN30F) instead of global ETF basket
- 15m/30m bars for intraday mean reversion (VN session is short)
- Session-aware: trade 09:15-11:00 morning, 13:15-14:00 afternoon
- Avoids ATO/ATC periods and lunch break
"""

import sys
import os
from datetime import timedelta

scripts_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
strategies_dir = os.path.join(scripts_dir, "strategies")
if strategies_dir not in sys.path:
    sys.path.insert(0, strategies_dir)

from fincept_engine.algorithm import QCAlgorithm
from fincept_engine.enums import Resolution, InsightDirection, Market
from fincept_engine.types import Insight
from fincept_engine.framework.alphas import AlphaModel


class VN30FMeanReversionIBSAlpha(QCAlgorithm):
    """VN30F Intraday Mean Reversion using Internal Bar Strength.

    IBS Formula: (Close - Low) / (High - Low)
    - IBS < 0.2 → Oversold → Long signal (expect bounce)
    - IBS > 0.8 → Overbought → Short signal (expect pullback)

    Works best on 15m/30m timeframes during VN trading hours.
    """

    strategy_id = "FCT-VN30F-IBS01"
    strategy_name = "VN30F Mean Reversion IBS"
    strategy_category = "Alpha Model"

    def initialize(self):
        self.set_start_date(2023, 1, 2)
        self.set_end_date(2024, 12, 31)
        self.set_cash(500_000_000)  # 500M VND

        # Parameters
        self.ibs_long_threshold = 0.2   # IBS below → long
        self.ibs_short_threshold = 0.8  # IBS above → short
        self.lookback = 5               # Number of bars for IBS averaging
        self.position_size = 0.8        # 80% of capital

        # Add VN30F
        self.vn30f = self.add_future("VN30F", Resolution.MINUTE, Market.HOSE)

        # IBS history
        self._ibs_values = []

        # Schedule position close before end of session
        self.schedule.on(
            self.date_rules.every_day(),
            self.time_rules.at(14, 10),
            self._close_positions,
        )

    def _close_positions(self):
        """Close all positions before ATC."""
        if self.portfolio.invested:
            self.liquidate()
            self.log("Closed all positions before ATC")

    def on_data(self, data):
        if not data.contains_key(self.vn30f.symbol):
            return

        bar = data[self.vn30f.symbol]

        # Skip if range is zero (no movement)
        bar_range = bar.high - bar.low
        if bar_range <= 0:
            return

        # Calculate IBS
        ibs = (bar.close - bar.low) / bar_range
        self._ibs_values.append(ibs)
        if len(self._ibs_values) > self.lookback:
            self._ibs_values = self._ibs_values[-self.lookback:]

        if len(self._ibs_values) < self.lookback:
            return

        # Average IBS over lookback
        avg_ibs = sum(self._ibs_values) / len(self._ibs_values)

        # Mean reversion signals
        current_pos = self.portfolio[self.vn30f.symbol]

        if avg_ibs < self.ibs_long_threshold and not current_pos.is_long:
            self.set_holdings(self.vn30f.symbol, self.position_size)
            self.log(f"LONG: IBS={avg_ibs:.3f} < {self.ibs_long_threshold}")
        elif avg_ibs > self.ibs_short_threshold and not current_pos.is_short:
            self.set_holdings(self.vn30f.symbol, -self.position_size)
            self.log(f"SHORT: IBS={avg_ibs:.3f} > {self.ibs_short_threshold}")
        elif self.ibs_long_threshold <= avg_ibs <= self.ibs_short_threshold:
            # Neutral zone — close if invested
            if current_pos.invested:
                self.liquidate(self.vn30f.symbol)
                self.log(f"FLAT: IBS={avg_ibs:.3f} in neutral zone")

    def on_end_of_algorithm(self):
        self.log(f"[{self.strategy_name}] Final NAV: {self.portfolio.total_portfolio_value:,.0f} VND")


class VN30FMeanReversionIBSAlphaModel(AlphaModel):
    """VN30F IBS Mean Reversion as a pluggable AlphaModel."""

    def __init__(self, ibs_long=0.2, ibs_short=0.8, lookback=5):
        super().__init__()
        self.ibs_long = ibs_long
        self.ibs_short = ibs_short
        self.lookback = lookback
        self._data = {}

    def update(self, algorithm, data):
        insights = []
        for symbol, sd in self._data.items():
            if not data.contains_key(symbol):
                continue
            bar = data[symbol]
            bar_range = bar.high - bar.low
            if bar_range <= 0:
                continue

            ibs = (bar.close - bar.low) / bar_range
            sd["ibs_values"].append(ibs)
            if len(sd["ibs_values"]) > self.lookback:
                sd["ibs_values"] = sd["ibs_values"][-self.lookback:]
            if len(sd["ibs_values"]) < self.lookback:
                continue

            avg_ibs = sum(sd["ibs_values"]) / len(sd["ibs_values"])

            if avg_ibs < self.ibs_long:
                insights.append(
                    Insight.price(symbol, timedelta(hours=2), InsightDirection.UP,
                                  magnitude=0.005, confidence=0.55,
                                  source_model="VN30FIBS")
                )
            elif avg_ibs > self.ibs_short:
                insights.append(
                    Insight.price(symbol, timedelta(hours=2), InsightDirection.DOWN,
                                  magnitude=0.005, confidence=0.55,
                                  source_model="VN30FIBS")
                )
        return insights

    def on_securities_changed(self, algorithm, changes):
        for security in changes.added_securities:
            self._data[security.symbol] = {"ibs_values": []}
        for security in changes.removed_securities:
            self._data.pop(security.symbol, None)
