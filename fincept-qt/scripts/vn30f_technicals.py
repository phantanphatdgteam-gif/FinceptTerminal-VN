#!/usr/bin/env python3
"""
VN30F Technical Analysis Adapter — Wraps existing codebase technicals for VN30F.
Uses momentum, trend, volatility, and volume indicators from scripts/technicals/.
Adjusts parameters for VN30F market characteristics.
"""

import sys
import os
import json
import math

# Add parent scripts directory to path for imports
scripts_dir = os.path.dirname(os.path.abspath(__file__))
if scripts_dir not in sys.path:
    sys.path.insert(0, scripts_dir)

# ============================================================================
# VN30F Market Parameters
# ============================================================================

VN30F_CONFIG = {
    "tick_size": 0.1,
    "multiplier": 100_000,
    "price_limit_pct": 0.07,
    "sessions": {
        "morning": {"open": "08:45", "close": "11:30"},
        "afternoon": {"open": "13:00", "close": "14:30"},
    },
    # Recommended indicator parameters for VN30F
    "indicators": {
        "rsi_period": 14,
        "stoch_k": 14, "stoch_d": 3, "stoch_smooth": 3,
        "mfi_period": 14,
        "ema_fast": 9, "ema_mid": 21, "ema_slow": 50,
        "macd_fast": 12, "macd_slow": 26, "macd_signal": 9,
        "adx_period": 14,
        "atr_period": 14,
        "bb_period": 20, "bb_std": 2.0,
        "keltner_period": 20, "keltner_atr_mult": 1.5,
        "obv_window": 20,
        "cmf_period": 20,
        "vwap_period": 14,
    },
    # Timeframes suitable for VN30F (short trading session)
    "timeframes": ["1m", "5m", "15m", "30m", "1h"],
}


def _safe_float(v):
    """Convert to float, replacing NaN/Inf with None."""
    if v is None:
        return None
    f = float(v)
    if math.isnan(f) or math.isinf(f):
        return None
    return f


# ============================================================================
# Indicator Wrappers
# ============================================================================

def compute_momentum_indicators(df, config=None):
    """Compute momentum indicators for VN30F data.

    Uses momentum_indicators.py from scripts/technicals/.
    """
    if config is None:
        config = VN30F_CONFIG["indicators"]
    try:
        from technicals.momentum_indicators import (
            calculate_rsi,
            calculate_stochastic,
            calculate_stoch_rsi,
        )
        df = calculate_rsi(df, window=config.get("rsi_period", 14))
        df = calculate_stochastic(
            df,
            window=config.get("stoch_k", 14),
            smooth_window=config.get("stoch_d", 3),
        )
        df = calculate_stoch_rsi(df, window=config.get("rsi_period", 14))
    except ImportError:
        pass
    return df


def compute_trend_indicators(df, config=None):
    """Compute trend indicators for VN30F data.

    Uses trend_indicators.py from scripts/technicals/.
    """
    if config is None:
        config = VN30F_CONFIG["indicators"]
    try:
        from technicals.trend_indicators import (
            calculate_ema,
            calculate_macd,
            calculate_sma,
        )
        df = calculate_sma(df, window=config.get("ema_mid", 21))
        df = calculate_ema(df, window=config.get("ema_fast", 9))
        df = calculate_ema(df, window=config.get("ema_slow", 50))
        df = calculate_macd(
            df,
            window_slow=config.get("macd_slow", 26),
            window_fast=config.get("macd_fast", 12),
            window_sign=config.get("macd_signal", 9),
        )
    except ImportError:
        pass
    return df


def compute_volatility_indicators(df, config=None):
    """Compute volatility indicators for VN30F data.

    Uses volatility_indicators.py from scripts/technicals/.
    """
    if config is None:
        config = VN30F_CONFIG["indicators"]
    try:
        from technicals.volatility_indicators import (
            calculate_atr,
            calculate_bollinger_bands,
            calculate_keltner_channel,
        )
        df = calculate_atr(df, window=config.get("atr_period", 14))
        df = calculate_bollinger_bands(
            df,
            window=config.get("bb_period", 20),
            window_dev=config.get("bb_std", 2.0),
        )
        df = calculate_keltner_channel(df, window=config.get("keltner_period", 20))
    except ImportError:
        pass
    return df


def compute_volume_indicators(df, config=None):
    """Compute volume indicators for VN30F data.

    Uses volume_indicators.py from scripts/technicals/.
    """
    if config is None:
        config = VN30F_CONFIG["indicators"]
    try:
        from technicals.volume_indicators import (
            calculate_cmf,
            calculate_obv,
        )
        df = calculate_obv(df)
        df = calculate_cmf(df, window=config.get("cmf_period", 20))
    except ImportError:
        pass
    return df


def compute_all_indicators(df, config=None):
    """Compute all VN30F indicators on a DataFrame with OHLCV columns."""
    df = compute_momentum_indicators(df, config)
    df = compute_trend_indicators(df, config)
    df = compute_volatility_indicators(df, config)
    df = compute_volume_indicators(df, config)
    return df


# ============================================================================
# VN30F Signal Generation
# ============================================================================

def generate_signals(df, config=None):
    """Generate trading signals based on VN30F indicator thresholds.

    Returns a dict with signal name → signal value.
    """
    if config is None:
        config = VN30F_CONFIG["indicators"]

    signals = {}
    latest = df.iloc[-1] if len(df) > 0 else {}

    # RSI signals
    rsi_val = _safe_float(latest.get("rsi", None))
    if rsi_val is not None:
        signals["rsi_oversold"] = rsi_val < 30
        signals["rsi_overbought"] = rsi_val > 70
        signals["rsi_value"] = rsi_val

    # MACD signals
    macd_val = _safe_float(latest.get("macd", None))
    macd_sig = _safe_float(latest.get("macd_signal", None))
    if macd_val is not None and macd_sig is not None:
        signals["macd_bullish"] = macd_val > macd_sig
        signals["macd_bearish"] = macd_val < macd_sig
        signals["macd_histogram"] = _safe_float(latest.get("macd_diff", None))

    # Bollinger Band signals
    bb_upper = _safe_float(latest.get("bb_bbhi", None))
    bb_lower = _safe_float(latest.get("bb_bbli", None))
    close = _safe_float(latest.get("close", None))
    if close is not None and bb_upper is not None:
        signals["bb_above_upper"] = bb_upper > 0
    if close is not None and bb_lower is not None:
        signals["bb_below_lower"] = bb_lower > 0

    # ATR for position sizing
    atr_val = _safe_float(latest.get("atr", None))
    if atr_val is not None:
        signals["atr_value"] = atr_val
        if close and close > 0:
            signals["atr_pct"] = atr_val / close * 100

    return signals


# ============================================================================
# VN30F Scanner Presets (from AlgoTradingTypes.h pattern)
# ============================================================================

VN30F_SCANNER_PRESETS = [
    {
        "name": "VN30F RSI Oversold",
        "description": "RSI(14) < 30 — Potential mean reversion long",
        "conditions": [{"indicator": "RSI", "params": {"period": 14}, "operator": "<", "value": 30}],
    },
    {
        "name": "VN30F RSI Overbought",
        "description": "RSI(14) > 70 — Potential mean reversion short",
        "conditions": [{"indicator": "RSI", "params": {"period": 14}, "operator": ">", "value": 70}],
    },
    {
        "name": "VN30F MACD Bullish Cross",
        "description": "MACD line crosses above signal line",
        "conditions": [{"indicator": "MACD", "field": "macd", "operator": "crosses_above", "compare": "macd_signal"}],
    },
    {
        "name": "VN30F Bollinger Squeeze",
        "description": "Bollinger bandwidth at 20-day low — breakout imminent",
        "conditions": [{"indicator": "BOLLINGER", "field": "bb_width", "operator": "<", "value": 0.02}],
    },
    {
        "name": "VN30F High ATR",
        "description": "ATR(14) > 15 points — High volatility opportunity",
        "conditions": [{"indicator": "ATR", "params": {"period": 14}, "operator": ">", "value": 15}],
    },
    {
        "name": "VN30F Volume Surge",
        "description": "Volume > 2x 20-day average — Institutional activity",
        "conditions": [{"indicator": "VOLUME", "operator": ">", "compare": "volume_sma20", "multiplier": 2.0}],
    },
]


# ============================================================================
# CLI Dispatch
# ============================================================================

def main():
    if len(sys.argv) < 2:
        print(json.dumps({"success": False, "error": "Usage: vn30f_technicals.py <action>"}))
        sys.exit(1)

    action = sys.argv[1]

    if action == "config":
        print(json.dumps({"success": True, "data": VN30F_CONFIG}))
    elif action == "presets":
        print(json.dumps({"success": True, "data": VN30F_SCANNER_PRESETS}))
    elif action == "compute":
        # Read OHLCV data from stdin as JSON
        import pandas as pd
        data = json.loads(sys.stdin.read())
        df = pd.DataFrame(data)
        df = compute_all_indicators(df)
        # Replace NaN with None for JSON serialization
        result = df.where(df.notna(), None).to_dict(orient="records")
        print(json.dumps({"success": True, "data": result}, default=str))
    elif action == "signals":
        import pandas as pd
        data = json.loads(sys.stdin.read())
        df = pd.DataFrame(data)
        df = compute_all_indicators(df)
        signals = generate_signals(df)
        print(json.dumps({"success": True, "data": signals}, default=str))
    else:
        print(json.dumps({"success": False, "error": f"Unknown action: {action}"}))


if __name__ == "__main__":
    main()
