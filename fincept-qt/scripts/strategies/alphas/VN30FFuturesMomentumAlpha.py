#!/usr/bin/env python3
"""
VN30F Futures Momentum Alpha — Adapted from FuturesMomentumAlgorithm.py
Uses EMA crossover for trend-following on VN30F continuous futures.

Strategy ID: FCT-VN30F-MOM01
Reference: FuturesMomentumAlgorithm.py + BasicTemplateContinuousFutureAlgorithm.py

VN30F Adaptations:
- EMA cross: Fast(20) vs Slow(60) on VN30F
- Front-month contract selection with auto-rollover
- Session-aware: avoid ATO/ATC and lunch break
- Position sizing based on ATR for VN30F volatility
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


class VN30FFuturesMomentumAlpha(QCAlgorithm):
    """VN30F Futures Momentum Strategy.

    Uses dual EMA crossover for trend detection:
    - Fast EMA(20) > Slow EMA(60) * 1.001 → Uptrend → Long
    - Fast EMA(20) < Slow EMA(60) * 0.999 → Downtrend → Short
    - Dead zone (1.001 > ratio > 0.999) → Hold current position

    Auto-rolls to next front-month contract before expiry.
    """

    strategy_id = "FCT-VN30F-MOM01"
    strategy_name = "VN30F Futures Momentum"
    strategy_category = "Futures"

    def initialize(self):
        self.set_start_date(2023, 1, 2)
        self.set_end_date(2024, 12, 31)
        self.set_cash(500_000_000)  # 500M VND

        # Parameters
        self.fast_period = 20
        self.slow_period = 60
        self.threshold = 0.001  # 0.1% trend confirmation threshold
        self.position_size = 0.9

        # Add VN30F continuous futures
        self.vn30f = self.add_future("VN30F", Resolution.MINUTE, Market.HOSE)

        # Create EMA indicators
        self._fast_ema = self.ema(self.vn30f.symbol, self.fast_period, Resolution.MINUTE)
        self._slow_ema = self.ema(self.vn30f.symbol, self.slow_period, Resolution.MINUTE)

        # ATR for volatility-based sizing
        self._atr = self.atr(self.vn30f.symbol, 14, Resolution.MINUTE)

        # Warm up
        self.set_warm_up(self.slow_period + 10)

        # Track current contract for rollover
        self._current_contract = None

        # Schedule daily rebalance
        self.schedule.on(
            self.date_rules.every_day(),
            self.time_rules.at(9, 15),
            self._check_signal,
        )
        self.schedule.on(
            self.date_rules.every_day(),
            self.time_rules.at(13, 15),
            self._check_signal,
        )

    def _check_signal(self):
        """Check EMA crossover signal and adjust position."""
        if self.is_warming_up:
            return
        if not self._fast_ema.is_ready or not self._slow_ema.is_ready:
            return

        fast = self._fast_ema.current.value
        slow = self._slow_ema.current.value

        if slow <= 0:
            return

        ratio = fast / slow
        current_pos = self.portfolio[self.vn30f.symbol]

        if ratio > (1 + self.threshold):
            # Uptrend — go long
            if not current_pos.is_long:
                self.set_holdings(self.vn30f.symbol, self.position_size)
                self.log(f"LONG: Fast EMA={fast:.1f}, Slow EMA={slow:.1f}, Ratio={ratio:.4f}")
        elif ratio < (1 - self.threshold):
            # Downtrend — go short
            if not current_pos.is_short:
                self.set_holdings(self.vn30f.symbol, -self.position_size)
                self.log(f"SHORT: Fast EMA={fast:.1f}, Slow EMA={slow:.1f}, Ratio={ratio:.4f}")

    def on_data(self, data):
        # Main logic handled by scheduled events
        pass

    def on_securities_changed(self, changes):
        """Handle contract rollover (from BasicTemplateFutureRolloverAlgorithm pattern)."""
        for security in changes.added_securities:
            if self._current_contract is not None:
                # Roll: liquidate old, buy new
                old_qty = self.portfolio[self._current_contract].quantity
                if old_qty != 0:
                    self.liquidate(self._current_contract)
                    self.log(f"Rolled from {self._current_contract} to {security.symbol}")
                    # Re-enter with same direction
                    if old_qty > 0:
                        self.set_holdings(security.symbol, self.position_size)
                    else:
                        self.set_holdings(security.symbol, -self.position_size)
            self._current_contract = security.symbol

    def on_end_of_algorithm(self):
        self.log(f"[{self.strategy_name}] Final NAV: {self.portfolio.total_portfolio_value:,.0f} VND")


class VN30FFuturesMomentumAlphaModel(AlphaModel):
    """VN30F EMA Momentum as a pluggable AlphaModel."""

    def __init__(self, fast_period=20, slow_period=60, threshold=0.001):
        super().__init__()
        self.fast_period = fast_period
        self.slow_period = slow_period
        self.threshold = threshold
        self._data = {}

    def update(self, algorithm, data):
        insights = []
        for symbol, sd in self._data.items():
            if not data.contains_key(symbol):
                continue
            price = data[symbol].close
            sd["prices"].append(price)
            max_len = self.slow_period + 10
            if len(sd["prices"]) > max_len:
                sd["prices"] = sd["prices"][-max_len:]
            if len(sd["prices"]) < self.slow_period:
                continue

            # Compute EMAs manually
            fast_ema = self._compute_ema(sd["prices"], self.fast_period)
            slow_ema = self._compute_ema(sd["prices"], self.slow_period)

            if slow_ema <= 0:
                continue

            ratio = fast_ema / slow_ema
            if ratio > (1 + self.threshold):
                insights.append(
                    Insight.price(symbol, timedelta(days=1), InsightDirection.UP,
                                  magnitude=0.01, confidence=0.6,
                                  source_model="VN30FMomentum")
                )
            elif ratio < (1 - self.threshold):
                insights.append(
                    Insight.price(symbol, timedelta(days=1), InsightDirection.DOWN,
                                  magnitude=0.01, confidence=0.6,
                                  source_model="VN30FMomentum")
                )
        return insights

    @staticmethod
    def _compute_ema(prices, period):
        """Simple EMA computation."""
        if len(prices) < period:
            return 0
        k = 2.0 / (period + 1)
        ema = sum(prices[:period]) / period
        for p in prices[period:]:
            ema = p * k + ema * (1 - k)
        return ema

    def on_securities_changed(self, algorithm, changes):
        for security in changes.added_securities:
            self._data[security.symbol] = {"prices": []}
        for security in changes.removed_securities:
            self._data.pop(security.symbol, None)
