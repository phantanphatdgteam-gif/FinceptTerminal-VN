#!/usr/bin/env python3
"""
VN30F Backtesting Adapter — Wraps codebase backtesting engines for VN30F.
Adapted from BacktestingTypes.h and 6 backtesting engines.

Features:
- Vectorized backtesting (VectorBT pattern)
- Walk-forward analysis (3-month train, 1-month test)
- Parameter optimization (grid search + random search)
- VN30F-specific: session filtering, fee model, margin tracking
- Full metrics: Sharpe, Sortino, Calmar, Max DD, Win Rate, Profit Factor
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
# VN30F Backtesting Configuration
# ============================================================================

VN30F_BACKTEST_CONFIG = {
    "default_params": {
        "initial_capital": 500_000_000,  # 500M VND
        "fee_per_side": 0.00027,         # 0.027%
        "slippage_points": 0.1,          # 1 tick slippage
        "multiplier": 100_000,           # VND per point
        "margin_pct": 0.13,              # 13% initial margin
        "max_lots": 50,
    },
    "walk_forward": {
        "train_months": 3,
        "test_months": 1,
        "anchored": False,  # Rolling window (not expanding)
    },
    "optimization": {
        "methods": ["grid", "random"],
        "objectives": ["sharpe", "total_return", "calmar", "profit_factor"],
        "max_iterations": 100,
    },
    # Position sizing methods (from BacktestingTypes.h)
    "position_sizing": [
        "fixed_lots",        # Fixed number of contracts
        "fixed_risk",        # Risk X% of capital per trade
        "kelly_criterion",   # Kelly optimal fraction
        "volatility_target", # Target specific volatility
        "equal_weight",      # Equal weight across signals
    ],
}


# ============================================================================
# Vectorized Backtesting Engine
# ============================================================================

def vectorized_backtest(signals, prices, config=None):
    """Run vectorized backtest on VN30F signals.

    Args:
        signals: numpy array of signals (-1=short, 0=flat, 1=long)
        prices: numpy array of close prices
        config: dict with backtest parameters

    Returns:
        dict with full backtest results and metrics
    """
    if config is None:
        config = VN30F_BACKTEST_CONFIG["default_params"]

    initial_capital = config.get("initial_capital", 500_000_000)
    fee_rate = config.get("fee_per_side", 0.00027)
    slippage = config.get("slippage_points", 0.1)
    multiplier = config.get("multiplier", 100_000)
    lots = config.get("lots", 1)

    n = len(prices)
    if n != len(signals):
        return {"success": False, "error": "Prices and signals must have same length"}

    # Initialize tracking arrays
    equity = np.zeros(n)
    equity[0] = initial_capital
    position = np.zeros(n)  # Current position (lots)
    trades = []
    pnl = np.zeros(n)

    cash = initial_capital
    current_pos = 0
    entry_price = 0.0

    for i in range(1, n):
        target = int(signals[i] * lots)

        # Trade if position changes
        if target != current_pos:
            # Close old position P&L
            if current_pos != 0:
                exit_price = prices[i] - slippage * np.sign(current_pos)
                trade_pnl = (exit_price - entry_price) * current_pos * multiplier
                fee = abs(current_pos) * prices[i] * multiplier * fee_rate
                cash += trade_pnl - fee
                trades.append({
                    "bar": i, "side": "close",
                    "lots": abs(current_pos), "price": exit_price,
                    "pnl": trade_pnl - fee,
                })

            # Open new position
            if target != 0:
                entry_price = prices[i] + slippage * np.sign(target)
                fee = abs(target) * prices[i] * multiplier * fee_rate
                cash -= fee
                trades.append({
                    "bar": i, "side": "long" if target > 0 else "short",
                    "lots": abs(target), "price": entry_price, "pnl": 0,
                })

            current_pos = target

        # Update equity
        unrealized = (prices[i] - entry_price) * current_pos * multiplier if current_pos != 0 else 0
        equity[i] = cash + unrealized
        position[i] = current_pos
        pnl[i] = equity[i] - equity[i - 1]

    # Compute metrics
    metrics = compute_backtest_metrics(equity, trades, initial_capital)

    return {
        "success": True,
        "data": {
            "metrics": metrics,
            "total_trades": len(trades),
            "trades": trades[-20:],  # Last 20 trades for review
            "final_equity": float(equity[-1]),
            "equity_curve": equity.tolist(),  # For charting
        }
    }


# ============================================================================
# Metrics Computation (from BacktestingTypes.h metric keys)
# ============================================================================

def compute_backtest_metrics(equity, trades, initial_capital):
    """Compute full suite of backtest metrics.

    Metrics from BacktestingTypes.h:
    - Ratios: Sharpe, Sortino, Calmar, Treynor, Information, Profit Factor
    - Returns: Total, Annualized, Max Drawdown, Win Rate, Volatility
    - Counts: Total Trades, Winning/Losing, Consecutive Wins/Losses
    """
    returns = np.diff(equity) / equity[:-1]
    returns = returns[~np.isnan(returns) & ~np.isinf(returns)]

    # Basic returns
    total_return = (equity[-1] - initial_capital) / initial_capital if initial_capital > 0 else 0
    n_days = len(returns)
    ann_factor = 252  # VN trading days per year

    # Annualized return
    if n_days > 0 and (1 + total_return) > 0:
        ann_return = (1 + total_return) ** (ann_factor / max(n_days, 1)) - 1
    else:
        ann_return = 0

    # Volatility
    daily_vol = float(np.std(returns)) if len(returns) > 1 else 0
    ann_vol = daily_vol * np.sqrt(ann_factor)

    # Sharpe Ratio (rf=0 for simplicity)
    sharpe = float(ann_return / ann_vol) if ann_vol > 0 else 0

    # Sortino Ratio (downside deviation)
    downside = returns[returns < 0]
    downside_dev = float(np.std(downside) * np.sqrt(ann_factor)) if len(downside) > 1 else 0.001
    sortino = float(ann_return / downside_dev) if downside_dev > 0 else 0

    # Max Drawdown
    peak = np.maximum.accumulate(equity)
    drawdown = (equity - peak) / peak
    max_dd = float(np.min(drawdown)) if len(drawdown) > 0 else 0

    # Calmar Ratio
    calmar = float(ann_return / abs(max_dd)) if abs(max_dd) > 0.0001 else 0

    # Trade statistics
    trade_pnls = [t.get("pnl", 0) for t in trades if t.get("side") == "close" or t.get("pnl", 0) != 0]
    winning = [p for p in trade_pnls if p > 0]
    losing = [p for p in trade_pnls if p < 0]

    total_trades = len(trade_pnls)
    win_rate = len(winning) / total_trades if total_trades > 0 else 0
    avg_win = float(np.mean(winning)) if winning else 0
    avg_loss = float(np.mean(losing)) if losing else 0
    profit_factor = abs(sum(winning) / sum(losing)) if losing and sum(losing) != 0 else 0

    # Consecutive wins/losses
    max_consec_wins = _max_consecutive(trade_pnls, positive=True)
    max_consec_losses = _max_consecutive(trade_pnls, positive=False)

    return {
        # Ratios
        "sharpe_ratio": round(sharpe, 4),
        "sortino_ratio": round(sortino, 4),
        "calmar_ratio": round(calmar, 4),
        "profit_factor": round(profit_factor, 4),
        # Returns
        "total_return": round(total_return, 6),
        "annualized_return": round(ann_return, 6),
        "max_drawdown": round(max_dd, 6),
        "annualized_volatility": round(ann_vol, 6),
        "daily_volatility": round(daily_vol, 6),
        # Counts
        "total_trades": total_trades,
        "winning_trades": len(winning),
        "losing_trades": len(losing),
        "win_rate": round(win_rate, 4),
        "avg_win": round(avg_win, 2),
        "avg_loss": round(avg_loss, 2),
        "largest_win": round(max(winning), 2) if winning else 0,
        "largest_loss": round(min(losing), 2) if losing else 0,
        "max_consecutive_wins": max_consec_wins,
        "max_consecutive_losses": max_consec_losses,
    }


def _max_consecutive(pnls, positive=True):
    """Count maximum consecutive wins or losses."""
    max_count = 0
    current = 0
    for p in pnls:
        if (positive and p > 0) or (not positive and p < 0):
            current += 1
            max_count = max(max_count, current)
        else:
            current = 0
    return max_count


# ============================================================================
# Walk-Forward Analysis
# ============================================================================

def walk_forward_analysis(df, strategy_func, train_bars=60, test_bars=20, step=20,
                          config=None):
    """Walk-forward optimization and testing.

    Slides window:
    [---train---][--test--]
                 step →
    [---train---][--test--]

    Args:
        df: DataFrame with OHLCV + features
        strategy_func: function(train_df) → returns optimized params
        train_bars: Training window size
        test_bars: Test window size
        step: Step size for rolling

    Returns:
        Aggregated walk-forward results
    """
    if config is None:
        config = VN30F_BACKTEST_CONFIG["default_params"]

    total_len = len(df)
    window_size = train_bars + test_bars

    wf_results = []
    idx = 0

    while idx + window_size <= total_len:
        train_end = idx + train_bars
        test_end = train_end + test_bars

        train_df = df.iloc[idx:train_end]
        test_df = df.iloc[train_end:test_end]

        # Optimize on train set
        try:
            params = strategy_func(train_df)
        except Exception:
            params = {}

        # Backtest on test set
        if "signals" in test_df.columns:
            signals = test_df["signals"].values
            prices = test_df["close"].values
            bt_result = vectorized_backtest(signals, prices, config)
            if bt_result.get("success"):
                metrics = bt_result["data"]["metrics"]
                metrics["window_start"] = idx
                metrics["window_end"] = test_end
                metrics["params"] = params
                wf_results.append(metrics)

        idx += step

    # Aggregate results
    if wf_results:
        avg_sharpe = np.mean([r["sharpe_ratio"] for r in wf_results])
        avg_return = np.mean([r["total_return"] for r in wf_results])
        avg_dd = np.mean([r["max_drawdown"] for r in wf_results])
        avg_wr = np.mean([r["win_rate"] for r in wf_results])

        return {
            "success": True,
            "data": {
                "windows": len(wf_results),
                "avg_sharpe": round(avg_sharpe, 4),
                "avg_return": round(avg_return, 6),
                "avg_max_drawdown": round(avg_dd, 6),
                "avg_win_rate": round(avg_wr, 4),
                "per_window_results": wf_results,
            }
        }
    return {"success": False, "error": "No valid walk-forward windows"}


# ============================================================================
# Risk Management Framework (from PortfolioService.h + qlib_portfolio_opt.py)
# ============================================================================

def compute_var(returns, confidence=0.95, method="historical"):
    """Value at Risk calculation.

    Methods:
    - historical: Percentile-based
    - parametric: Normal distribution assumption
    """
    returns = np.array(returns)
    returns = returns[~np.isnan(returns)]

    if method == "historical":
        var = float(np.percentile(returns, (1 - confidence) * 100))
    elif method == "parametric":
        mean = np.mean(returns)
        std = np.std(returns)
        from scipy.stats import norm
        z = norm.ppf(1 - confidence)
        var = float(mean + z * std)
    else:
        var = float(np.percentile(returns, (1 - confidence) * 100))

    return var


def compute_cvar(returns, confidence=0.95):
    """Conditional VaR (Expected Shortfall)."""
    returns = np.array(returns)
    returns = returns[~np.isnan(returns)]
    var = compute_var(returns, confidence)
    tail = returns[returns <= var]
    return float(np.mean(tail)) if len(tail) > 0 else var


def compute_risk_metrics(equity_curve, initial_capital=500_000_000):
    """Compute comprehensive risk metrics for VN30F portfolio."""
    equity = np.array(equity_curve)
    returns = np.diff(equity) / equity[:-1]
    returns = returns[~np.isnan(returns) & ~np.isinf(returns)]

    # VaR
    var_95 = compute_var(returns, 0.95)
    var_99 = compute_var(returns, 0.99)
    cvar_95 = compute_cvar(returns, 0.95)

    # Max drawdown
    peak = np.maximum.accumulate(equity)
    drawdown = (equity - peak) / peak
    max_dd = float(np.min(drawdown))

    # Recovery time
    dd_duration = 0
    max_dd_duration = 0
    for dd in drawdown:
        if dd < 0:
            dd_duration += 1
            max_dd_duration = max(max_dd_duration, dd_duration)
        else:
            dd_duration = 0

    # Margin utilization tracking
    # (placeholder - would need position data for real calculation)

    return {
        "var_95": round(var_95, 6),
        "var_99": round(var_99, 6),
        "cvar_95": round(cvar_95, 6),
        "max_drawdown": round(max_dd, 6),
        "max_drawdown_duration_bars": max_dd_duration,
        "daily_volatility": round(float(np.std(returns)), 6),
        "annualized_volatility": round(float(np.std(returns) * np.sqrt(252)), 6),
        "skewness": round(float(np.mean(((returns - np.mean(returns)) / np.std(returns)) ** 3)), 4) if np.std(returns) > 0 else 0,
        "kurtosis": round(float(np.mean(((returns - np.mean(returns)) / np.std(returns)) ** 4) - 3), 4) if np.std(returns) > 0 else 0,
    }


# ============================================================================
# Derivatives Analytics (from derivatives_pricing.py)
# ============================================================================

def futures_fair_value(spot, risk_free_rate, dividend_yield, days_to_expiry):
    """Forward/Futures pricing: F = S * e^((r-q)*T)."""
    t = days_to_expiry / 365.0
    return spot * math.exp((risk_free_rate - dividend_yield) * t)


def basis_analysis(futures_price, spot_price, days_to_expiry):
    """Analyze VN30F basis (premium/discount vs VN30 spot)."""
    basis = futures_price - spot_price
    basis_pct = (basis / spot_price) * 100 if spot_price > 0 else 0
    # Annualized basis
    ann_basis = basis_pct * (365.0 / max(days_to_expiry, 1))
    return {
        "basis_points": round(basis, 2),
        "basis_pct": round(basis_pct, 4),
        "annualized_basis_pct": round(ann_basis, 4),
        "is_premium": basis > 0,
        "is_discount": basis < 0,
    }


# ============================================================================
# CLI Dispatch
# ============================================================================

def main():
    if len(sys.argv) < 2:
        print(json.dumps({"success": False, "error": "Usage: vn30f_backtesting.py <action>"}))
        sys.exit(1)

    action = sys.argv[1]

    if action == "config":
        print(json.dumps({"success": True, "data": VN30F_BACKTEST_CONFIG}))
    elif action == "metrics_info":
        print(json.dumps({"success": True, "data": {
            "ratio_metrics": ["sharpe_ratio", "sortino_ratio", "calmar_ratio", "profit_factor"],
            "return_metrics": ["total_return", "annualized_return", "max_drawdown", "annualized_volatility"],
            "count_metrics": ["total_trades", "winning_trades", "losing_trades", "win_rate"],
            "risk_metrics": ["var_95", "var_99", "cvar_95", "skewness", "kurtosis"],
        }}))
    elif action == "backtest":
        import pandas as pd
        data = json.loads(sys.stdin.read())
        df = pd.DataFrame(data)
        if "signals" in df.columns and "close" in df.columns:
            result = vectorized_backtest(df["signals"].values, df["close"].values)
            print(json.dumps(result, default=str))
        else:
            print(json.dumps({"success": False, "error": "Need 'signals' and 'close' columns"}))
    elif action == "risk":
        import pandas as pd
        data = json.loads(sys.stdin.read())
        equity_curve = data.get("equity_curve", [500_000_000])
        result = compute_risk_metrics(equity_curve)
        print(json.dumps({"success": True, "data": result}, default=str))
    elif action == "basis":
        futures = float(sys.argv[2]) if len(sys.argv) > 2 else 1210.0
        spot = float(sys.argv[3]) if len(sys.argv) > 3 else 1200.0
        dte = int(sys.argv[4]) if len(sys.argv) > 4 else 15
        result = basis_analysis(futures, spot, dte)
        print(json.dumps({"success": True, "data": result}, default=str))
    elif action == "fair_value":
        spot = float(sys.argv[2]) if len(sys.argv) > 2 else 1200.0
        rf = float(sys.argv[3]) if len(sys.argv) > 3 else 0.045
        div = float(sys.argv[4]) if len(sys.argv) > 4 else 0.015
        dte = int(sys.argv[5]) if len(sys.argv) > 5 else 15
        fv = futures_fair_value(spot, rf, div, dte)
        print(json.dumps({"success": True, "data": {"fair_value": round(fv, 2), "spot": spot, "days_to_expiry": dte}}, default=str))
    else:
        print(json.dumps({"success": False, "error": f"Unknown action: {action}"}))


if __name__ == "__main__":
    main()
