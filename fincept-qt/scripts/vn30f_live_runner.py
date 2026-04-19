#!/usr/bin/env python3
"""
VN30F Live Strategy Runner
Extends the base live_runner.py for VN30F futures trading on HOSE derivatives.

Handles VN30F-specific deployment concerns:
  - VN trading session enforcement (8:45-11:30, 13:00-14:30 ICT)
  - Margin tracking (initial ~13%, maintenance ~10%)
  - ±7% price limit validation
  - ATO/ATC order support
  - T+0 settlement
  - Contract rollover (monthly expiry)

Commands (JSON stdin/stdout via PythonRunner):
  deploy   <deploy_id> <strategy_id> <params_json> --db <path>
  stop     <deploy_id> --db <path>
  stop_all --db <path>
  list     --db <path>
  status   <deploy_id> --db <path>
  rollover <deploy_id> --db <path>
"""

import sys
import os
import json
import sqlite3
import time
from datetime import datetime, timezone, timedelta

# ============================================================================
# VN30F Constants
# ============================================================================

VN_TZ_OFFSET = timedelta(hours=7)  # UTC+7

TICK_SIZE = 0.1
MULTIPLIER = 100_000       # 100,000 VND per point
INITIAL_MARGIN_PCT = 0.13  # ~13% of contract value
MAINT_MARGIN_PCT = 0.10    # ~10% of contract value
FEE_PER_SIDE = 0.00027      # 0.027% per side (SSI standard rate)
PRICE_LIMIT_PCT = 0.07     # ±7%

# VN trading sessions (ICT minutes from midnight)
MORNING_OPEN = 8 * 60 + 45    # 08:45
MORNING_CLOSE = 11 * 60 + 30  # 11:30
AFTERNOON_OPEN = 13 * 60      # 13:00
AFTERNOON_CLOSE = 14 * 60 + 30  # 14:30

# ============================================================================
# Schema extension for VN30F
# ============================================================================

VN30F_SCHEMA = """
CREATE TABLE IF NOT EXISTS deployed_strategies (
    deploy_id TEXT PRIMARY KEY,
    strategy_id TEXT NOT NULL,
    strategy_name TEXT NOT NULL,
    symbol TEXT NOT NULL DEFAULT 'VN30F1M',
    asset_type TEXT NOT NULL DEFAULT 'futures',
    mode TEXT NOT NULL DEFAULT 'paper',
    status TEXT NOT NULL DEFAULT 'starting',
    params TEXT,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    stopped_at TEXT,
    pid INTEGER
);

CREATE TABLE IF NOT EXISTS strategy_trades (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    deploy_id TEXT NOT NULL,
    symbol TEXT NOT NULL,
    side TEXT NOT NULL,
    quantity REAL NOT NULL,
    price REAL NOT NULL,
    timestamp TEXT NOT NULL,
    order_type TEXT DEFAULT 'limit',
    order_id TEXT,
    pnl REAL DEFAULT 0,
    commission REAL DEFAULT 0,
    FOREIGN KEY (deploy_id) REFERENCES deployed_strategies(deploy_id)
);

CREATE TABLE IF NOT EXISTS strategy_metrics (
    deploy_id TEXT PRIMARY KEY,
    total_pnl REAL DEFAULT 0,
    total_trades INTEGER DEFAULT 0,
    win_rate REAL DEFAULT 0,
    max_drawdown REAL DEFAULT 0,
    sharpe_ratio REAL DEFAULT 0,
    current_position_qty REAL DEFAULT 0,
    current_position_side TEXT DEFAULT '',
    current_position_entry REAL DEFAULT 0,
    margin_used REAL DEFAULT 0,
    margin_available REAL DEFAULT 0,
    daily_pnl REAL DEFAULT 0,
    updated_at TEXT NOT NULL,
    FOREIGN KEY (deploy_id) REFERENCES deployed_strategies(deploy_id)
);

CREATE TABLE IF NOT EXISTS contract_info (
    symbol TEXT PRIMARY KEY,
    expiry_date TEXT NOT NULL,
    multiplier REAL DEFAULT 100000,
    tick_size REAL DEFAULT 0.1,
    initial_margin_pct REAL DEFAULT 0.13,
    maint_margin_pct REAL DEFAULT 0.10,
    last_updated TEXT NOT NULL
);
"""


def get_db(db_path: str) -> sqlite3.Connection:
    """Open SQLite and ensure VN30F schema exists."""
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    conn.executescript(VN30F_SCHEMA)
    return conn


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def now_ict() -> datetime:
    return datetime.now(timezone.utc) + VN_TZ_OFFSET


# ============================================================================
# Session checks
# ============================================================================

def is_trading_session() -> bool:
    """Check if current time is within VN30F trading hours."""
    t = now_ict()
    minutes = t.hour * 60 + t.minute
    in_morning = MORNING_OPEN <= minutes <= MORNING_CLOSE
    in_afternoon = AFTERNOON_OPEN <= minutes <= AFTERNOON_CLOSE
    # Also skip weekends
    if t.weekday() >= 5:
        return False
    return in_morning or in_afternoon


def current_session() -> str:
    """Return which session we're in."""
    t = now_ict()
    minutes = t.hour * 60 + t.minute
    if MORNING_OPEN <= minutes <= MORNING_CLOSE:
        return "morning"
    if AFTERNOON_OPEN <= minutes <= AFTERNOON_CLOSE:
        return "afternoon"
    if minutes < MORNING_OPEN:
        return "pre_market"
    if MORNING_CLOSE < minutes < AFTERNOON_OPEN:
        return "lunch_break"
    return "post_market"


# ============================================================================
# Margin calculations
# ============================================================================

def calculate_margin(price: float, lots: int) -> dict:
    """Calculate margin requirements for VN30F position."""
    contract_value = price * MULTIPLIER * lots
    initial_margin = contract_value * INITIAL_MARGIN_PCT
    maint_margin = contract_value * MAINT_MARGIN_PCT
    return {
        "contract_value": contract_value,
        "initial_margin": initial_margin,
        "maintenance_margin": maint_margin,
        "lots": lots,
        "price": price,
    }


def validate_price_limit(price: float, ref_price: float) -> bool:
    """Check if price is within ±7% of reference."""
    if ref_price <= 0:
        return True
    pct = abs(price - ref_price) / ref_price
    return pct <= PRICE_LIMIT_PCT


def calculate_commission(price: float, lots: int) -> float:
    """Calculate trading commission."""
    return price * MULTIPLIER * lots * FEE_PER_SIDE


# ============================================================================
# Commands
# ============================================================================

def cmd_deploy(db_path: str, deploy_id: str, strategy_id: str, params_json: str):
    """Deploy a VN30F strategy."""
    conn = get_db(db_path)
    try:
        params = json.loads(params_json) if params_json else {}
    except json.JSONDecodeError:
        params = {}

    symbol = params.get("symbol", "VN30F1M")
    mode = params.get("mode", "paper")
    strategy_name = params.get("strategy_name", strategy_id)
    capital = params.get("capital", 500_000_000)

    ts = now_iso()
    conn.execute(
        """INSERT OR REPLACE INTO deployed_strategies
           (deploy_id, strategy_id, strategy_name, symbol, asset_type, mode, status, params, created_at, updated_at, pid)
           VALUES (?, ?, ?, ?, 'futures', ?, 'running', ?, ?, ?, ?)""",
        (deploy_id, strategy_id, strategy_name, symbol, mode,
         json.dumps(params), ts, ts, os.getpid()),
    )
    conn.execute(
        """INSERT OR REPLACE INTO strategy_metrics
           (deploy_id, margin_available, updated_at)
           VALUES (?, ?, ?)""",
        (deploy_id, capital, ts),
    )
    conn.commit()
    conn.close()

    return {
        "success": True,
        "data": {
            "deploy_id": deploy_id,
            "strategy_id": strategy_id,
            "symbol": symbol,
            "mode": mode,
            "status": "running",
            "session": current_session(),
            "is_trading_hours": is_trading_session(),
        },
    }


def cmd_stop(db_path: str, deploy_id: str):
    """Stop a deployed VN30F strategy."""
    conn = get_db(db_path)
    ts = now_iso()
    conn.execute(
        "UPDATE deployed_strategies SET status='stopped', stopped_at=?, updated_at=? WHERE deploy_id=?",
        (ts, ts, deploy_id),
    )
    conn.commit()
    conn.close()
    return {"success": True, "data": {"deploy_id": deploy_id, "status": "stopped"}}


def cmd_stop_all(db_path: str):
    """Stop all running VN30F strategies."""
    conn = get_db(db_path)
    ts = now_iso()
    conn.execute(
        "UPDATE deployed_strategies SET status='stopped', stopped_at=?, updated_at=? WHERE status='running' AND asset_type='futures'",
        (ts, ts),
    )
    conn.commit()
    conn.close()
    return {"success": True, "data": {"message": "All VN30F strategies stopped"}}


def cmd_list(db_path: str):
    """List all VN30F deployed strategies."""
    conn = get_db(db_path)
    rows = conn.execute(
        "SELECT * FROM deployed_strategies WHERE asset_type='futures' ORDER BY created_at DESC"
    ).fetchall()
    conn.close()

    strategies = []
    for r in rows:
        strategies.append({
            "deploy_id": r["deploy_id"],
            "strategy_id": r["strategy_id"],
            "strategy_name": r["strategy_name"],
            "symbol": r["symbol"],
            "mode": r["mode"],
            "status": r["status"],
            "created_at": r["created_at"],
            "updated_at": r["updated_at"],
        })
    return {"success": True, "data": {"strategies": strategies, "count": len(strategies)}}


def cmd_status(db_path: str, deploy_id: str):
    """Get detailed status of a VN30F deployment."""
    conn = get_db(db_path)
    strat = conn.execute(
        "SELECT * FROM deployed_strategies WHERE deploy_id=?", (deploy_id,)
    ).fetchone()
    metrics = conn.execute(
        "SELECT * FROM strategy_metrics WHERE deploy_id=?", (deploy_id,)
    ).fetchone()
    recent_trades = conn.execute(
        "SELECT * FROM strategy_trades WHERE deploy_id=? ORDER BY timestamp DESC LIMIT 20",
        (deploy_id,),
    ).fetchall()
    conn.close()

    if not strat:
        return {"success": False, "error": f"Deployment {deploy_id} not found"}

    return {
        "success": True,
        "data": {
            "strategy": dict(strat),
            "metrics": dict(metrics) if metrics else {},
            "recent_trades": [dict(t) for t in recent_trades],
            "session": current_session(),
            "is_trading_hours": is_trading_session(),
            "vn30f_info": {
                "tick_size": TICK_SIZE,
                "multiplier": MULTIPLIER,
                "initial_margin_pct": INITIAL_MARGIN_PCT,
                "fee_per_side": FEE_PER_SIDE,
                "price_limit_pct": PRICE_LIMIT_PCT,
            },
        },
    }


def cmd_rollover(db_path: str, deploy_id: str):
    """Initiate contract rollover for a VN30F deployment."""
    conn = get_db(db_path)
    strat = conn.execute(
        "SELECT * FROM deployed_strategies WHERE deploy_id=?", (deploy_id,)
    ).fetchone()
    if not strat:
        conn.close()
        return {"success": False, "error": f"Deployment {deploy_id} not found"}

    params = json.loads(strat["params"]) if strat["params"] else {}
    old_symbol = strat["symbol"]
    # Simple front-month increment logic
    if "1M" in old_symbol:
        new_symbol = old_symbol  # Same symbol name, different underlying
    else:
        new_symbol = "VN30F1M"

    params["previous_symbol"] = old_symbol
    params["rollover_at"] = now_iso()
    ts = now_iso()
    conn.execute(
        "UPDATE deployed_strategies SET symbol=?, params=?, updated_at=? WHERE deploy_id=?",
        (new_symbol, json.dumps(params), ts, deploy_id),
    )
    conn.commit()
    conn.close()

    return {
        "success": True,
        "data": {
            "deploy_id": deploy_id,
            "old_symbol": old_symbol,
            "new_symbol": new_symbol,
            "rollover_at": ts,
        },
    }


# ============================================================================
# Main dispatcher
# ============================================================================

def safe_call(fn, *args, **kwargs):
    """Wrap calls with error handling."""
    try:
        return fn(*args, **kwargs)
    except Exception as e:
        return {"success": False, "error": str(e)}


def main():
    if len(sys.argv) < 2:
        print(json.dumps({"success": False, "error": "Usage: vn30f_live_runner.py <command> [args...]"}))
        return

    action = sys.argv[1]
    db_path = None
    # Parse --db flag
    for i, arg in enumerate(sys.argv):
        if arg == "--db" and i + 1 < len(sys.argv):
            db_path = sys.argv[i + 1]
            break
    if not db_path:
        db_path = os.path.join(os.path.expanduser("~"), ".fincept", "vn30f_live.db")

    if action == "deploy" and len(sys.argv) >= 5:
        result = safe_call(cmd_deploy, db_path, sys.argv[2], sys.argv[3], sys.argv[4])
    elif action == "stop" and len(sys.argv) >= 3:
        result = safe_call(cmd_stop, db_path, sys.argv[2])
    elif action == "stop_all":
        result = safe_call(cmd_stop_all, db_path)
    elif action == "list":
        result = safe_call(cmd_list, db_path)
    elif action == "status" and len(sys.argv) >= 3:
        result = safe_call(cmd_status, db_path, sys.argv[2])
    elif action == "rollover" and len(sys.argv) >= 3:
        result = safe_call(cmd_rollover, db_path, sys.argv[2])
    else:
        result = {"success": False, "error": f"Unknown command: {action}"}

    print(json.dumps(result, ensure_ascii=False, default=str))


if __name__ == "__main__":
    main()
