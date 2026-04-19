#!/usr/bin/env python3
"""
VN30F Data Connector — Market data for Vietnam VN30F futures.
Provides historical OHLCV, realtime quotes, contract info, margin requirements,
and open interest data for VN30F derivatives on HOSE exchange.

Uses existing codebase utilities and VnPy wrapper where available.
"""

import sys
import json
import math
import os
from datetime import datetime, timedelta

# ============================================================================
# VN30F Market Constants
# ============================================================================

VN30F_TICK_SIZE = 0.1           # Minimum price change: 0.1 points
VN30F_MULTIPLIER = 100_000      # 100,000 VND per point
VN30F_INITIAL_MARGIN_PCT = 0.13 # ~13% of contract value
VN30F_MAINTENANCE_MARGIN_PCT = 0.10
VN30F_FEE_PER_SIDE = 0.00027   # 0.027% per side
VN30F_PRICE_LIMIT_PCT = 0.07   # ±7% daily price limit
VN30F_LOT_SIZE = 1              # 1 contract per lot

# VN Trading Sessions (ICT / UTC+7)
VN_MORNING_OPEN = "08:45"
VN_MORNING_CLOSE = "11:30"
VN_AFTERNOON_OPEN = "13:00"
VN_AFTERNOON_CLOSE = "14:30"
VN_ATO_START = "08:45"
VN_ATO_END = "09:00"
VN_ATC_START = "14:15"
VN_ATC_END = "14:30"

# VN30F Contract Month Codes
MONTH_CODES = {1: "F", 2: "G", 3: "H", 4: "J", 5: "K", 6: "M",
               7: "N", 8: "Q", 9: "U", 10: "V", 11: "X", 12: "Z"}

# ============================================================================
# Safe API Call Wrapper (from akshare_futures pattern)
# ============================================================================

def safe_call(func, *args, **kwargs):
    """Execute function with retry logic and error handling."""
    for attempt in range(2):
        try:
            result = func(*args, **kwargs)
            # Sanitize NaN/Infinity
            if isinstance(result, dict):
                return _sanitize(result)
            if isinstance(result, list):
                return [_sanitize(r) if isinstance(r, dict) else r for r in result]
            return result
        except Exception as e:
            if attempt == 1:
                return {"success": False, "error": str(e), "data": []}
            import time
            time.sleep(1)
    return {"success": False, "error": "Max retries exceeded", "data": []}


def _sanitize(obj):
    """Replace NaN/Infinity with None in a dict."""
    if isinstance(obj, dict):
        return {k: _sanitize(v) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_sanitize(v) for v in obj]
    if isinstance(obj, float) and (math.isnan(obj) or math.isinf(obj)):
        return None
    return obj


# ============================================================================
# VN30F Contract Utilities
# ============================================================================

def get_front_month_symbol(ref_date=None):
    """Get the front-month VN30F contract symbol (e.g., 'VN30F2506')."""
    if ref_date is None:
        ref_date = datetime.now()
    # VN30F expires on 3rd Thursday of expiry month
    year = ref_date.year
    month = ref_date.month
    expiry = _third_thursday(year, month)
    if ref_date.date() > expiry:
        # Roll to next month
        month += 1
        if month > 12:
            month = 1
            year += 1
        expiry = _third_thursday(year, month)
    return f"VN30F{year % 100:02d}{month:02d}"


def get_back_month_symbol(ref_date=None):
    """Get the back-month (second month) VN30F contract symbol."""
    if ref_date is None:
        ref_date = datetime.now()
    front = get_front_month_symbol(ref_date)
    year = int(front[5:7]) + 2000
    month = int(front[7:9])
    month += 1
    if month > 12:
        month = 1
        year += 1
    return f"VN30F{year % 100:02d}{month:02d}"


def _third_thursday(year, month):
    """Calculate the 3rd Thursday of a given month."""
    from datetime import date
    first_day = date(year, month, 1)
    # Find first Thursday (weekday=3)
    offset = (3 - first_day.weekday()) % 7
    first_thu = first_day + timedelta(days=offset)
    third_thu = first_thu + timedelta(weeks=2)
    return third_thu


def get_vn30f_contract_info(symbol=None):
    """Get VN30F contract specification."""
    if symbol is None:
        symbol = get_front_month_symbol()
    year = int(symbol[5:7]) + 2000
    month = int(symbol[7:9])
    expiry = _third_thursday(year, month)
    return {
        "success": True,
        "data": {
            "symbol": symbol,
            "underlying": "VN30",
            "exchange": "HOSE",
            "market": "DER",
            "tick_size": VN30F_TICK_SIZE,
            "multiplier": VN30F_MULTIPLIER,
            "currency": "VND",
            "lot_size": VN30F_LOT_SIZE,
            "expiry_date": expiry.isoformat(),
            "initial_margin_pct": VN30F_INITIAL_MARGIN_PCT,
            "maintenance_margin_pct": VN30F_MAINTENANCE_MARGIN_PCT,
            "fee_per_side": VN30F_FEE_PER_SIDE,
            "price_limit_pct": VN30F_PRICE_LIMIT_PCT,
            "trading_hours": {
                "morning": {"open": VN_MORNING_OPEN, "close": VN_MORNING_CLOSE},
                "afternoon": {"open": VN_AFTERNOON_OPEN, "close": VN_AFTERNOON_CLOSE},
                "ato": {"start": VN_ATO_START, "end": VN_ATO_END},
                "atc": {"start": VN_ATC_START, "end": VN_ATC_END},
            },
            "settlement": "T+0",
            "front_month": get_front_month_symbol(),
            "back_month": get_back_month_symbol(),
        }
    }


# ============================================================================
# Margin Calculator
# ============================================================================

def get_vn30f_margin_requirements(price=1200.0, lots=1):
    """Calculate margin requirements for VN30F contract."""
    price = float(price)
    lots = int(lots)
    contract_value = price * VN30F_MULTIPLIER * lots
    initial_margin = contract_value * VN30F_INITIAL_MARGIN_PCT
    maintenance_margin = contract_value * VN30F_MAINTENANCE_MARGIN_PCT
    fee_per_side = contract_value * VN30F_FEE_PER_SIDE
    return {
        "success": True,
        "data": {
            "price": price,
            "lots": lots,
            "contract_value": contract_value,
            "initial_margin": initial_margin,
            "maintenance_margin": maintenance_margin,
            "margin_pct": VN30F_INITIAL_MARGIN_PCT,
            "fee_per_side": fee_per_side,
            "fee_round_trip": fee_per_side * 2,
            "leverage": round(1.0 / VN30F_INITIAL_MARGIN_PCT, 2),
            "tick_value": VN30F_TICK_SIZE * VN30F_MULTIPLIER * lots,
        }
    }


# ============================================================================
# Historical Data
# ============================================================================

def get_vn30f_historical(symbol=None, start_date=None, end_date=None, resolution="1d"):
    """Fetch VN30F historical OHLCV data.
    
    Tries VnPy wrapper first, then falls back to generating sample data.
    In production, connects to SSI/VPS market data APIs.
    """
    if symbol is None:
        symbol = "VN30F1M"
    if end_date is None:
        end_date = datetime.now().strftime("%Y-%m-%d")
    if start_date is None:
        start_date = (datetime.now() - timedelta(days=365)).strftime("%Y-%m-%d")

    # Try VnPy wrapper first
    try:
        scripts_dir = os.path.dirname(os.path.abspath(__file__))
        vnpy_path = os.path.join(scripts_dir, "Analytics", "vnpy_wrapper")
        if os.path.exists(vnpy_path):
            sys.path.insert(0, vnpy_path)
            from engine import query_history
            bars = query_history(
                engine_id="vn30f",
                symbol=f"{symbol}.HOSE",
                exchange="HOSE",
                interval=resolution,
                start=start_date,
                end=end_date
            )
            if bars and len(bars) > 0:
                return {"success": True, "data": bars, "source": "vnpy"}
    except Exception:
        pass

    # Generate sample data structure (placeholder for actual API integration)
    return {
        "success": True,
        "data": [],
        "source": "placeholder",
        "message": "Connect SSI/VPS API for live data. Use vnpy wrapper for CTP gateway."
    }


# ============================================================================
# Realtime Quotes
# ============================================================================

def get_vn30f_realtime(symbols=None):
    """Get realtime VN30F quotes."""
    if symbols is None:
        symbols = ["VN30F1M", "VN30F2M"]

    # In production, this would connect to SSI WebSocket or API
    return {
        "success": True,
        "data": [{
            "symbol": s,
            "exchange": "DER",
            "last_price": 0.0,
            "bid": 0.0,
            "ask": 0.0,
            "open": 0.0,
            "high": 0.0,
            "low": 0.0,
            "volume": 0,
            "oi": 0,
            "timestamp": datetime.now().isoformat(),
        } for s in (symbols if isinstance(symbols, list) else [symbols])],
        "source": "placeholder"
    }


# ============================================================================
# Open Interest & Basis
# ============================================================================

def get_vn30f_open_interest(symbol=None):
    """Get open interest, basis, and roll information."""
    if symbol is None:
        symbol = get_front_month_symbol()
    return {
        "success": True,
        "data": {
            "symbol": symbol,
            "open_interest": 0,
            "oi_change": 0,
            "basis": 0.0,
            "basis_pct": 0.0,
            "front_month": get_front_month_symbol(),
            "back_month": get_back_month_symbol(),
            "days_to_expiry": 0,
            "roll_date": None,
        },
        "source": "placeholder"
    }


# ============================================================================
# VN30F Trading Session Helpers
# ============================================================================

def is_trading_hours(check_time=None):
    """Check if current time is within VN30F trading hours."""
    if check_time is None:
        # Assume UTC+7
        check_time = datetime.utcnow() + timedelta(hours=7)
    t = check_time.time()
    from datetime import time as dtime
    morning = dtime(8, 45) <= t <= dtime(11, 30)
    afternoon = dtime(13, 0) <= t <= dtime(14, 30)
    return morning or afternoon


def is_lunch_break(check_time=None):
    """Check if current time is during lunch break."""
    if check_time is None:
        check_time = datetime.utcnow() + timedelta(hours=7)
    t = check_time.time()
    from datetime import time as dtime
    return dtime(11, 30) < t < dtime(13, 0)


def get_session_info(check_time=None):
    """Get current VN30F trading session info."""
    if check_time is None:
        check_time = datetime.utcnow() + timedelta(hours=7)
    t = check_time.time()
    from datetime import time as dtime
    
    session = "closed"
    if dtime(8, 45) <= t <= dtime(9, 0):
        session = "ato"
    elif dtime(9, 0) < t <= dtime(11, 30):
        session = "morning"
    elif dtime(11, 30) < t < dtime(13, 0):
        session = "lunch_break"
    elif dtime(13, 0) <= t <= dtime(14, 15):
        session = "afternoon"
    elif dtime(14, 15) < t <= dtime(14, 30):
        session = "atc"
    
    return {
        "success": True,
        "data": {
            "session": session,
            "is_trading": session in ("ato", "morning", "afternoon", "atc"),
            "is_lunch_break": session == "lunch_break",
            "current_time": check_time.strftime("%H:%M:%S"),
            "timezone": "ICT (UTC+7)",
        }
    }


# ============================================================================
# Price Limit Calculator
# ============================================================================

def get_price_limits(reference_price):
    """Calculate daily price limits for VN30F (±7%)."""
    ref = float(reference_price)
    ceil_price = round(ref * (1 + VN30F_PRICE_LIMIT_PCT), 1)
    floor_price = round(ref * (1 - VN30F_PRICE_LIMIT_PCT), 1)
    return {
        "success": True,
        "data": {
            "reference_price": ref,
            "ceiling_price": ceil_price,
            "floor_price": floor_price,
            "limit_pct": VN30F_PRICE_LIMIT_PCT,
            "range_points": ceil_price - floor_price,
        }
    }


# ============================================================================
# CLI Dispatch (C++ PythonRunner integration)
# ============================================================================

def main():
    if len(sys.argv) < 2:
        print(json.dumps({"success": False, "error": "Usage: vn30f_data.py <action> [args...]"}))
        sys.exit(1)

    action = sys.argv[1]
    try:
        if action == "contract_info":
            symbol = sys.argv[2] if len(sys.argv) > 2 else None
            result = safe_call(get_vn30f_contract_info, symbol)
        elif action == "margin":
            price = float(sys.argv[2]) if len(sys.argv) > 2 else 1200.0
            lots = int(sys.argv[3]) if len(sys.argv) > 3 else 1
            result = safe_call(get_vn30f_margin_requirements, price, lots)
        elif action == "historical":
            symbol = sys.argv[2] if len(sys.argv) > 2 else None
            start = sys.argv[3] if len(sys.argv) > 3 else None
            end = sys.argv[4] if len(sys.argv) > 4 else None
            res = sys.argv[5] if len(sys.argv) > 5 else "1d"
            result = safe_call(get_vn30f_historical, symbol, start, end, res)
        elif action == "realtime":
            symbols = sys.argv[2].split(",") if len(sys.argv) > 2 else None
            result = safe_call(get_vn30f_realtime, symbols)
        elif action == "open_interest":
            symbol = sys.argv[2] if len(sys.argv) > 2 else None
            result = safe_call(get_vn30f_open_interest, symbol)
        elif action == "session_info":
            result = safe_call(get_session_info)
        elif action == "price_limits":
            ref_price = float(sys.argv[2]) if len(sys.argv) > 2 else 1200.0
            result = safe_call(get_price_limits, ref_price)
        elif action == "front_month":
            result = {"success": True, "data": {"symbol": get_front_month_symbol()}}
        elif action == "back_month":
            result = {"success": True, "data": {"symbol": get_back_month_symbol()}}
        else:
            result = {"success": False, "error": f"Unknown action: {action}"}
    except Exception as e:
        result = {"success": False, "error": str(e)}

    print(json.dumps(result, default=str))


if __name__ == "__main__":
    main()
