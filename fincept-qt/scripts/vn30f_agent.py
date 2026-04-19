#!/usr/bin/env python3
"""
VN30F Trading Agent — LLM-powered trading agent for VN30F futures.
Adapted from scripts/agents/deepagents/agent.py pattern.

Integrates:
- Multi-agent orchestration (AgentTypes.h: collaborate mode)
- MCP tool-calling (market_data, technical_analysis, backtester, etc.)
- Quant signal fusion (ML models + LLM analysis)
- Risk management (margin check, drawdown limit)

Agent Roles (from AgentService.h subagent specialization):
- Market Analyst: Trend analysis, macro context
- Quant Strategist: ML/RL signal generation
- Risk Manager: Position sizing, drawdown control
- Execution Specialist: Order execution, slippage management
- Portfolio Manager: Overall P&L monitoring, rebalancing
"""

import sys
import os
import json
from datetime import datetime

scripts_dir = os.path.dirname(os.path.abspath(__file__))
if scripts_dir not in sys.path:
    sys.path.insert(0, scripts_dir)


# ============================================================================
# VN30F Agent Configuration (from AgentTypes.h pattern)
# ============================================================================

VN30F_AGENT_CONFIG = {
    "agent": {
        "id": "vn30f_trader",
        "name": "VN30F Trading Agent",
        "description": "AI-powered automated trading agent for VN30F futures",
        "category": "trading",
        "version": "1.0.0",
    },
    "context": {
        "watchlist": ["VN30F1M", "VN30F2M", "VN30", "VNINDEX"],
        "portfolio_id": "vn30f_portfolio",
        "market": "VN",
        "exchange": "DER",
        "currency": "VND",
    },
    "team": {
        "name": "VN30F Trading Team",
        "mode": "collaborate",  # From AgentTypes.h TeamConfig
        "members": [
            {
                "agent_id": "vn30f_market_analyst",
                "name": "Market Analyst",
                "role": "research",
                "instructions": (
                    "Analyze VN30F market trends, VN30 index movement, macro indicators for Vietnam. "
                    "Provide market regime classification: trending/ranging/volatile. "
                    "Consider: foreign flow, VN30 index composition changes, macro news."
                ),
            },
            {
                "agent_id": "vn30f_quant_strategist",
                "name": "Quant Strategist",
                "role": "data-analyst",
                "instructions": (
                    "Generate trading signals from ML models and technical indicators. "
                    "Combine: RSI, MACD, Bollinger, ATR signals with ML model predictions. "
                    "Output: direction (long/short/flat), confidence (0-1), magnitude estimate."
                ),
            },
            {
                "agent_id": "vn30f_risk_manager",
                "name": "Risk Manager",
                "role": "risk-analyzer",
                "instructions": (
                    "Evaluate risk for proposed trades. Check: margin availability, drawdown limit (-10%), "
                    "daily loss limit (-3%), position concentration. "
                    "VN30F margin: 13% initial, 10% maintenance. Price limit: ±7%."
                ),
            },
            {
                "agent_id": "vn30f_execution",
                "name": "Execution Specialist",
                "role": "trading",
                "instructions": (
                    "Execute approved trades with optimal timing. "
                    "Consider: VN session times (8:45-11:30, 13:00-14:30), "
                    "avoid ATO/ATC periods for market orders, manage slippage. "
                    "Use limit orders when spread > 0.5 points."
                ),
            },
            {
                "agent_id": "vn30f_portfolio_mgr",
                "name": "Portfolio Manager",
                "role": "portfolio-opt",
                "instructions": (
                    "Monitor overall portfolio performance. "
                    "Track: NAV, total P&L, Sharpe ratio, max drawdown. "
                    "Rebalance when: drawdown > 5%, or position held > 3 days without profit."
                ),
            },
        ],
    },
    "decision_pipeline": {
        "steps": [
            {"step": 1, "agent": "market_analyst", "action": "assess_market_regime"},
            {"step": 2, "agent": "quant_strategist", "action": "generate_signals"},
            {"step": 3, "agent": "risk_manager", "action": "approve_and_size"},
            {"step": 4, "agent": "execution", "action": "execute_order"},
            {"step": 5, "agent": "portfolio_mgr", "action": "monitor_and_rebalance"},
        ],
    },
    "risk_limits": {
        "max_position_lots": 50,
        "max_drawdown_pct": 0.10,       # 10% of NAV
        "daily_loss_limit_pct": 0.03,    # 3% of NAV
        "margin_utilization_max": 0.70,  # 70% max margin usage
        "stop_loss_per_trade_pct": 0.02, # 2% per trade
        "take_profit_ratio": 2.0,        # TP = 2x SL (risk:reward = 1:2)
    },
}


# ============================================================================
# MCP Tool Wrappers (from deepagents/agent.py pattern)
# ============================================================================

def mcp_market_data(symbols=None):
    """MCP tool: Get VN30F market data."""
    if symbols is None:
        symbols = ["VN30F1M"]
    return {
        "tool": "market_data",
        "params": {"symbols": symbols, "exchange": "DER", "market": "VN"},
    }


def mcp_technical_analysis(symbol="VN30F1M", indicators=None):
    """MCP tool: Run technical analysis on VN30F."""
    if indicators is None:
        indicators = ["rsi", "macd", "bollinger", "atr"]
    return {
        "tool": "technical_analysis",
        "params": {"symbol": symbol, "indicators": indicators},
    }


def mcp_backtest(strategy_id, params=None):
    """MCP tool: Backtest a VN30F strategy."""
    return {
        "tool": "backtester",
        "params": {"strategy_id": strategy_id, "symbol": "VN30F1M", "params": params or {}},
    }


def mcp_compliance_check(order):
    """MCP tool: Check risk/compliance for an order."""
    return {
        "tool": "compliance_check",
        "params": {"order": order, "risk_limits": VN30F_AGENT_CONFIG["risk_limits"]},
    }


# ============================================================================
# LLM-Quant Fusion Decision Engine (from AlphaArenaService.h pattern)
# ============================================================================

def fusion_decision(quant_signals, llm_assessment, risk_check):
    """Combine quantitative signals with LLM analysis for final decision.

    Args:
        quant_signals: dict with direction, confidence, magnitude from ML models
        llm_assessment: dict with sentiment, confidence from LLM agent
        risk_check: dict with approved, position_size, stop_loss, take_profit

    Returns:
        Final trading decision dict
    """
    # Weighted combination (quant: 60%, LLM: 40%)
    quant_weight = 0.6
    llm_weight = 0.4

    # Direction scoring
    direction_map = {"long": 1, "up": 1, "bullish": 1,
                     "short": -1, "down": -1, "bearish": -1,
                     "flat": 0, "neutral": 0, "hold": 0}

    quant_dir = direction_map.get(str(quant_signals.get("direction", "flat")).lower(), 0)
    quant_conf = float(quant_signals.get("confidence", 0.5))

    llm_dir = direction_map.get(str(llm_assessment.get("sentiment", "neutral")).lower(), 0)
    llm_conf = float(llm_assessment.get("confidence", 0.5))

    # Combined signal
    combined_score = (quant_dir * quant_conf * quant_weight +
                      llm_dir * llm_conf * llm_weight)

    # Decision thresholds
    if combined_score > 0.3:
        decision = "BUY"
        direction = "long"
    elif combined_score < -0.3:
        decision = "SELL"
        direction = "short"
    else:
        decision = "HOLD"
        direction = "flat"

    # Risk check override
    if not risk_check.get("approved", False):
        decision = "HOLD"
        direction = "flat"

    return {
        "decision": decision,
        "direction": direction,
        "combined_score": round(combined_score, 4),
        "quant_signal": {"direction": quant_dir, "confidence": quant_conf},
        "llm_signal": {"direction": llm_dir, "confidence": llm_conf},
        "position_size": risk_check.get("position_size", 0),
        "stop_loss": risk_check.get("stop_loss", 0),
        "take_profit": risk_check.get("take_profit", 0),
        "timestamp": datetime.now().isoformat(),
    }


# ============================================================================
# Risk Check Engine
# ============================================================================

def check_risk(proposed_lots, current_position, nav, current_price,
               unrealized_pnl=0, daily_pnl=0):
    """Pre-trade risk check for VN30F order.

    Returns approval/rejection with position sizing.
    """
    limits = VN30F_AGENT_CONFIG["risk_limits"]
    multiplier = 100_000  # VND per point

    # Position limit check
    total_position = abs(current_position + proposed_lots)
    if total_position > limits["max_position_lots"]:
        return {"approved": False, "reason": f"Position limit exceeded: {total_position} > {limits['max_position_lots']}"}

    # Margin check
    margin_required = total_position * current_price * multiplier * 0.13
    margin_utilization = margin_required / nav if nav > 0 else 1.0
    if margin_utilization > limits["margin_utilization_max"]:
        # Reduce to fit within margin limit
        max_lots = int(nav * limits["margin_utilization_max"] / (current_price * multiplier * 0.13))
        proposed_lots = min(abs(proposed_lots), max_lots) * (1 if proposed_lots > 0 else -1)

    # Daily loss limit
    if daily_pnl < -nav * limits["daily_loss_limit_pct"]:
        return {"approved": False, "reason": "Daily loss limit reached"}

    # Drawdown check
    if unrealized_pnl < -nav * limits["max_drawdown_pct"]:
        return {"approved": False, "reason": "Max drawdown limit reached"}

    # Calculate SL/TP levels
    atr_estimate = current_price * 0.01  # ~1% ATR estimate
    stop_loss = current_price - atr_estimate * 2 if proposed_lots > 0 else current_price + atr_estimate * 2
    take_profit = current_price + atr_estimate * 2 * limits["take_profit_ratio"] if proposed_lots > 0 \
        else current_price - atr_estimate * 2 * limits["take_profit_ratio"]

    return {
        "approved": True,
        "position_size": abs(proposed_lots),
        "stop_loss": round(stop_loss, 1),
        "take_profit": round(take_profit, 1),
        "margin_required": margin_required,
        "margin_utilization": round(margin_utilization, 4),
    }


# ============================================================================
# Alpha Arena Integration (from alpha_arena/ pattern)
# ============================================================================

VN30F_ALPHA_ARENA_CONFIG = {
    "competition_name": "VN30F Alpha Arena",
    "strategies": [
        {"id": "FCT-VN30F-DT01", "name": "VN30F Dual Thrust", "type": "quant"},
        {"id": "FCT-VN30F-IBS01", "name": "VN30F Mean Reversion IBS", "type": "quant"},
        {"id": "FCT-VN30F-IR01", "name": "VN30F Intraday Reversal", "type": "quant"},
        {"id": "FCT-VN30F-MOM01", "name": "VN30F Futures Momentum", "type": "quant"},
    ],
    "metrics": ["sharpe", "total_return", "max_drawdown", "win_rate", "profit_factor"],
    "ranking_by": "sharpe",
}


# ============================================================================
# CLI Dispatch
# ============================================================================

def main():
    if len(sys.argv) < 2:
        print(json.dumps({"success": False, "error": "Usage: vn30f_agent.py <action>"}))
        sys.exit(1)

    action = sys.argv[1]

    if action == "config":
        print(json.dumps({"success": True, "data": VN30F_AGENT_CONFIG}, default=str))
    elif action == "team":
        print(json.dumps({"success": True, "data": VN30F_AGENT_CONFIG["team"]}, default=str))
    elif action == "risk_check":
        # Read params from stdin
        params = json.loads(sys.stdin.read())
        result = check_risk(
            proposed_lots=params.get("lots", 1),
            current_position=params.get("current_position", 0),
            nav=params.get("nav", 500_000_000),
            current_price=params.get("price", 1200.0),
            unrealized_pnl=params.get("unrealized_pnl", 0),
            daily_pnl=params.get("daily_pnl", 0),
        )
        print(json.dumps({"success": True, "data": result}, default=str))
    elif action == "fusion":
        params = json.loads(sys.stdin.read())
        result = fusion_decision(
            quant_signals=params.get("quant", {}),
            llm_assessment=params.get("llm", {}),
            risk_check=params.get("risk", {"approved": True}),
        )
        print(json.dumps({"success": True, "data": result}, default=str))
    elif action == "arena":
        print(json.dumps({"success": True, "data": VN30F_ALPHA_ARENA_CONFIG}, default=str))
    elif action == "pipeline":
        print(json.dumps({"success": True, "data": VN30F_AGENT_CONFIG["decision_pipeline"]}, default=str))
    else:
        print(json.dumps({"success": False, "error": f"Unknown action: {action}"}))


if __name__ == "__main__":
    main()
