#!/usr/bin/env python3
"""
VN30F Feature Engineering — Adapted from qlib_feature_engineering.py
Generates ML features for VN30F prediction models using codebase normalizers.

Uses:
- 10 normalizers from qlib_feature_engineering.py: zscore, winsorize, cs_rank, tanh, fillna
- Technical indicators from scripts/technicals/
- VN30F-specific microstructure features (basis, OI, session)
"""

import sys
import os
import json
import math
import numpy as np

scripts_dir = os.path.dirname(os.path.abspath(__file__))
if scripts_dir not in sys.path:
    sys.path.insert(0, scripts_dir)


# ============================================================================
# VN30F Feature Configuration
# ============================================================================

VN30F_FEATURE_CONFIG = {
    # Price features
    "price_features": [
        "open", "high", "low", "close", "volume",
        "returns_1d", "returns_5d", "returns_10d", "returns_20d",
        "log_return_1d", "log_return_5d",
    ],
    # Technical indicator features (from codebase technicals)
    "technical_features": [
        "rsi_14", "stochastic_k", "stochastic_d", "mfi_14",
        "macd", "macd_signal", "macd_hist",
        "ema_9", "ema_21", "ema_50", "sma_20",
        "adx_14",
        "atr_14", "atr_ratio",
        "bb_upper", "bb_lower", "bb_width", "bb_position",
        "obv", "obv_change", "cmf_20", "vwap_deviation",
    ],
    # Volatility features
    "volatility_features": [
        "realized_vol_5", "realized_vol_10", "realized_vol_20",
        "historical_vol_20",
        "atr_14",
        "intraday_range",  # (High - Low) / Close
    ],
    # VN30F Microstructure features
    "microstructure_features": [
        "basis",            # VN30F - VN30 spot
        "basis_pct",        # basis as % of spot
        "oi_change",        # Open interest change
        "roll_yield",       # Basis annualized
    ],
    # Calendar features
    "calendar_features": [
        "session",          # 0=morning, 1=afternoon
        "day_of_week",      # 0-4
        "days_to_expiry",   # Days until VN30F contract expiry
        "is_near_expiry",   # Boolean: < 5 days to expiry
    ],
    # Normalization methods (from qlib_feature_engineering.py)
    "normalizers": ["zscore", "winsorize", "fillna"],
}


# ============================================================================
# Normalizers (from qlib_feature_engineering.py)
# ============================================================================

def zscore_normalize(series, window=20):
    """Z-score normalization: (x - mean) / std."""
    mean = series.rolling(window).mean()
    std = series.rolling(window).std()
    std = std.replace(0, 1)  # Avoid division by zero
    return (series - mean) / std


def winsorize(series, lower_pct=0.01, upper_pct=0.99):
    """Winsorize extreme values at given percentiles."""
    lower = series.quantile(lower_pct)
    upper = series.quantile(upper_pct)
    return series.clip(lower, upper)


def tanh_normalize(series):
    """Tanh normalization to [-1, 1] range."""
    return np.tanh(series)


def fillna_forward(series):
    """Forward fill NaN values."""
    return series.ffill().bfill()


def cs_rank(series):
    """Cross-sectional rank (percentile rank)."""
    return series.rank(pct=True)


# ============================================================================
# Feature Engineering Pipeline
# ============================================================================

def compute_price_features(df):
    """Compute price-based features."""
    df = df.copy()

    # Returns
    df["returns_1d"] = df["close"].pct_change(1)
    df["returns_5d"] = df["close"].pct_change(5)
    df["returns_10d"] = df["close"].pct_change(10)
    df["returns_20d"] = df["close"].pct_change(20)

    # Log returns
    df["log_return_1d"] = np.log(df["close"] / df["close"].shift(1))
    df["log_return_5d"] = np.log(df["close"] / df["close"].shift(5))

    return df


def compute_technical_features(df):
    """Compute technical indicator features using codebase modules."""
    df = df.copy()

    try:
        from technicals.momentum_indicators import calculate_rsi, calculate_stochastic
        df = calculate_rsi(df, window=14)
        df.rename(columns={"rsi": "rsi_14"}, inplace=True)
        df = calculate_stochastic(df, window=14, smooth_window=3)
    except ImportError:
        pass

    try:
        from technicals.trend_indicators import calculate_ema, calculate_sma, calculate_macd
        for period in [9, 21, 50]:
            ema_df = calculate_ema(df, window=period)
            df[f"ema_{period}"] = ema_df.get("ema", df["close"])
        sma_df = calculate_sma(df, window=20)
        df["sma_20"] = sma_df.get("sma", df["close"])
        df = calculate_macd(df)
    except ImportError:
        pass

    try:
        from technicals.volatility_indicators import calculate_atr, calculate_bollinger_bands
        df = calculate_atr(df, window=14)
        df.rename(columns={"atr": "atr_14"}, inplace=True)
        if "close" in df.columns and "atr_14" in df.columns:
            df["atr_ratio"] = df["atr_14"] / df["close"]
        df = calculate_bollinger_bands(df, window=20, window_dev=2)
    except ImportError:
        pass

    try:
        from technicals.volume_indicators import calculate_obv, calculate_cmf
        df = calculate_obv(df)
        if "obv" in df.columns:
            df["obv_change"] = df["obv"].pct_change(1)
        df = calculate_cmf(df, window=20)
        df.rename(columns={"cmf": "cmf_20"}, inplace=True)
    except ImportError:
        pass

    return df


def compute_volatility_features(df):
    """Compute volatility features."""
    df = df.copy()

    # Realized volatility (annualized)
    for window in [5, 10, 20]:
        df[f"realized_vol_{window}"] = df["log_return_1d"].rolling(window).std() * np.sqrt(252)

    # Historical volatility
    df["historical_vol_20"] = df["returns_1d"].rolling(20).std() * np.sqrt(252)

    # Intraday range
    if "high" in df.columns and "low" in df.columns:
        df["intraday_range"] = (df["high"] - df["low"]) / df["close"]

    return df


def compute_microstructure_features(df, vn30_spot=None):
    """Compute VN30F-specific microstructure features."""
    df = df.copy()

    if vn30_spot is not None:
        df["basis"] = df["close"] - vn30_spot
        df["basis_pct"] = df["basis"] / vn30_spot * 100
        # Annualized roll yield (basis / spot * 365 / days_to_expiry)
        if "days_to_expiry" in df.columns:
            denom = df["days_to_expiry"].replace(0, 30)
            df["roll_yield"] = df["basis_pct"] / denom * 365
    else:
        df["basis"] = 0.0
        df["basis_pct"] = 0.0
        df["roll_yield"] = 0.0

    if "oi" in df.columns:
        df["oi_change"] = df["oi"].pct_change(1)
    else:
        df["oi_change"] = 0.0

    return df


def compute_calendar_features(df):
    """Compute calendar/session features."""
    df = df.copy()

    if "datetime" in df.columns or "date" in df.columns:
        import pandas as pd
        date_col = "datetime" if "datetime" in df.columns else "date"
        dates = pd.to_datetime(df[date_col])
        df["day_of_week"] = dates.dt.dayofweek
    else:
        df["day_of_week"] = 0

    # Session indicator (placeholder: 0=morning, 1=afternoon)
    df["session"] = 0
    df["days_to_expiry"] = 15  # placeholder
    df["is_near_expiry"] = 0

    return df


def normalize_features(df, methods=None):
    """Apply normalization pipeline to all numeric features."""
    if methods is None:
        methods = ["fillna", "winsorize", "zscore"]

    df = df.copy()
    numeric_cols = df.select_dtypes(include=[np.number]).columns

    for method in methods:
        for col in numeric_cols:
            if method == "fillna":
                df[col] = fillna_forward(df[col])
            elif method == "winsorize":
                df[col] = winsorize(df[col])
            elif method == "zscore":
                df[col] = zscore_normalize(df[col])
            elif method == "tanh":
                df[col] = tanh_normalize(df[col])
            elif method == "cs_rank":
                df[col] = cs_rank(df[col])

    return df


def compute_all_features(df, vn30_spot=None, normalize=True):
    """Full feature engineering pipeline for VN30F ML models."""
    df = compute_price_features(df)
    df = compute_technical_features(df)
    df = compute_volatility_features(df)
    df = compute_microstructure_features(df, vn30_spot)
    df = compute_calendar_features(df)

    if normalize:
        df = normalize_features(df)

    return df


# ============================================================================
# Label Generation (from BacktestingTypes.h pattern)
# ============================================================================

def generate_labels(df, method="fixlb", horizon=5, threshold=0.01):
    """Generate prediction labels for supervised learning.

    Label methods from BacktestingTypes.h:
    - fixlb: Fixed horizon return (default)
    - meanlb: Mean return over horizon
    - trendlb: Trend direction (consecutive returns)
    - bolb: Binary label (up=1, down=0)
    """
    df = df.copy()

    if method == "fixlb":
        # Fixed horizon return
        df["label"] = df["close"].pct_change(horizon).shift(-horizon)
    elif method == "meanlb":
        # Mean return over horizon
        df["label"] = df["close"].rolling(horizon).mean().pct_change(1).shift(-horizon)
    elif method == "trendlb":
        # Trend direction: count of positive returns in horizon
        future_returns = df["close"].pct_change(1).shift(-1)
        df["label"] = future_returns.rolling(horizon).apply(lambda x: (x > 0).sum() / len(x)).shift(-1)
    elif method == "bolb":
        # Binary label: 1 if return > threshold, 0 otherwise
        future_return = df["close"].pct_change(horizon).shift(-horizon)
        df["label"] = (future_return > threshold).astype(int)
    else:
        df["label"] = df["close"].pct_change(horizon).shift(-horizon)

    return df


# ============================================================================
# CLI Dispatch
# ============================================================================

def main():
    if len(sys.argv) < 2:
        print(json.dumps({"success": False, "error": "Usage: vn30f_features.py <action>"}))
        sys.exit(1)

    action = sys.argv[1]

    if action == "config":
        print(json.dumps({"success": True, "data": VN30F_FEATURE_CONFIG}))
    elif action == "compute":
        import pandas as pd
        data = json.loads(sys.stdin.read())
        df = pd.DataFrame(data)
        df = compute_all_features(df)
        result = df.where(df.notna(), None).to_dict(orient="records")
        print(json.dumps({"success": True, "data": result}, default=str))
    elif action == "labels":
        import pandas as pd
        method = sys.argv[2] if len(sys.argv) > 2 else "fixlb"
        horizon = int(sys.argv[3]) if len(sys.argv) > 3 else 5
        data = json.loads(sys.stdin.read())
        df = pd.DataFrame(data)
        df = generate_labels(df, method=method, horizon=horizon)
        result = df.where(df.notna(), None).to_dict(orient="records")
        print(json.dumps({"success": True, "data": result}, default=str))
    else:
        print(json.dumps({"success": False, "error": f"Unknown action: {action}"}))


if __name__ == "__main__":
    main()
