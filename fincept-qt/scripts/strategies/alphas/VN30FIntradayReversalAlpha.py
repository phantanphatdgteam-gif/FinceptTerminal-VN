#!/usr/bin/env python3
"""
VN30F Intraday Reversal Alpha — Adapted from IntradayReversalCurrencyMarketsAlpha.py
Uses Price/SMA crossover for intraday reversal signals on VN30F.

Strategy ID: FCT-VN30F-IR01
Reference: IntradayReversalCurrencyMarketsAlpha.py

VN30F Adaptations:
- Applied to VN30F futures instead of EURUSD forex
- Trading window: 09:00-11:00 (morning) and 13:30-14:15 (afternoon)
- SMA period: 5-bar on 15m chart (75 minutes lookback)
- Avoids ATO/ATC and lunch break
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


class VN30FIntradayReversalAlpha(QCAlgorithm):
    """VN30F Intraday Reversal Strategy.

    Logic:
    - When price drops below SMA → expect mean reversion UP → Long
    - When price rises above SMA → expect mean reversion DOWN → Short
    - Close all positions at 14:15 before ATC
    """

    strategy_id = "FCT-VN30F-IR01"
    strategy_name = "VN30F Intraday Reversal"
    strategy_category = "Alpha Model"

    def initialize(self):
        self.set_start_date(2023, 1, 2)
        self.set_end_date(2024, 12, 31)
        self.set_cash(500_000_000)  # 500M VND

        # Parameters
        self.sma_period = 5       # 5-bar SMA on 15m = 75 min lookback
        self.position_size = 0.85

        # Add VN30F
        self.vn30f = self.add_future("VN30F", Resolution.MINUTE, Market.HOSE)

        # Create SMA indicator
        self._sma = self.sma(self.vn30f.symbol, self.sma_period, Resolution.MINUTE)

        # Warm up
        self.set_warm_up(20)

        # Schedule session management
        self.schedule.on(
            self.date_rules.every_day(),
            self.time_rules.at(9, 0),
            self._enable_trading,
        )
        self.schedule.on(
            self.date_rules.every_day(),
            self.time_rules.at(11, 15),
            self._disable_for_lunch,
        )
        self.schedule.on(
            self.date_rules.every_day(),
            self.time_rules.at(13, 30),
            self._enable_trading,
        )
        self.schedule.on(
            self.date_rules.every_day(),
            self.time_rules.at(14, 15),
            self._close_all,
        )

        self._trading_enabled = False

    def _enable_trading(self):
        self._trading_enabled = True

    def _disable_for_lunch(self):
        """Close before lunch break."""
        self._trading_enabled = False
        if self.portfolio.invested:
            self.liquidate()
            self.log("Closed positions for lunch break")

    def _close_all(self):
        """Close all before ATC."""
        self._trading_enabled = False
        if self.portfolio.invested:
            self.liquidate()
            self.log("Closed positions before ATC")

    def on_data(self, data):
        if self.is_warming_up:
            return
        if not self._trading_enabled:
            return
        if not self._sma.is_ready:
            return
        if not data.contains_key(self.vn30f.symbol):
            return

        price = data[self.vn30f.symbol].close
        sma_val = self._sma.current.value

        if sma_val <= 0:
            return

        current_pos = self.portfolio[self.vn30f.symbol]

        # Intraday reversal logic (contrarian)
        if price < sma_val and not current_pos.is_long:
            # Price below SMA → expect bounce → Long
            self.set_holdings(self.vn30f.symbol, self.position_size)
            self.log(f"LONG: price={price:.1f} < SMA={sma_val:.1f}")
        elif price > sma_val and not current_pos.is_short:
            # Price above SMA → expect pullback → Short
            self.set_holdings(self.vn30f.symbol, -self.position_size)
            self.log(f"SHORT: price={price:.1f} > SMA={sma_val:.1f}")

    def on_end_of_algorithm(self):
        self.log(f"[{self.strategy_name}] Final NAV: {self.portfolio.total_portfolio_value:,.0f} VND")


class VN30FIntradayReversalAlphaModel(AlphaModel):
    """VN30F Intraday Reversal as a pluggable AlphaModel."""

    def __init__(self, sma_period=5):
        super().__init__()
        self.sma_period = sma_period
        self._data = {}

    def update(self, algorithm, data):
        insights = []
        for symbol, sd in self._data.items():
            if not data.contains_key(symbol):
                continue
            price = data[symbol].close
            sd["prices"].append(price)
            if len(sd["prices"]) > self.sma_period:
                sd["prices"] = sd["prices"][-self.sma_period:]
            if len(sd["prices"]) < self.sma_period:
                continue

            sma_val = sum(sd["prices"]) / len(sd["prices"])
            if price < sma_val:
                insights.append(
                    Insight.price(symbol, timedelta(hours=1), InsightDirection.UP,
                                  magnitude=0.003, confidence=0.5,
                                  source_model="VN30FReverse")
                )
            elif price > sma_val:
                insights.append(
                    Insight.price(symbol, timedelta(hours=1), InsightDirection.DOWN,
                                  magnitude=0.003, confidence=0.5,
                                  source_model="VN30FReverse")
                )
        return insights

    def on_securities_changed(self, algorithm, changes):
        for security in changes.added_securities:
            self._data[security.symbol] = {"prices": []}
        for security in changes.removed_securities:
            self._data.pop(security.symbol, None)
