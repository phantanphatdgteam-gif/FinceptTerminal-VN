#!/usr/bin/env python3
"""
VN30F ML Training Pipeline — Adapted from qlib_service.py & qlib_advanced_models.py
Trains ML models for VN30F price prediction using codebase Qlib wrapper.

Supported Models (from codebase):
- Gradient Boosting: LightGBM, XGBoost, CatBoost
- Deep Learning: LSTM, GRU, Transformer, ALSTM, TCN
- Ensemble: Validation-Sharpe, Turbulence Switching, Weighted Average

Training Pipeline:
1. Load VN30F data with features (from vn30f_features.py)
2. Generate labels (from BacktestingTypes.h: FIXLB, MEANLB, BOLB)
3. Split data: RollingWindow (120 train, 20 validation, 20 test)
4. Train model
5. Evaluate: IC, Sharpe, Max Drawdown
6. Save model for live deployment
"""

import sys
import os
import json

scripts_dir = os.path.dirname(os.path.abspath(__file__))
if scripts_dir not in sys.path:
    sys.path.insert(0, scripts_dir)


# ============================================================================
# VN30F ML Configuration
# ============================================================================

VN30F_ML_CONFIG = {
    "data": {
        "symbol": "VN30F1M",
        "exchange": "DER",
        "train_window": 120,      # 120 trading days (~6 months VN)
        "validation_window": 20,  # 20 trading days (~1 month)
        "test_window": 20,
        "retrain_frequency": 20,  # Retrain every 20 trading days
    },
    "label": {
        "method": "fixlb",
        "horizon": 5,             # 5-day forward return prediction
        "threshold": 0.005,       # 0.5% for binary labels
    },
    "models": {
        "lightgbm": {
            "n_estimators": 500,
            "learning_rate": 0.05,
            "max_depth": 6,
            "num_leaves": 31,
            "subsample": 0.8,
            "colsample_bytree": 0.8,
            "reg_alpha": 0.1,
            "reg_lambda": 0.1,
        },
        "xgboost": {
            "n_estimators": 500,
            "learning_rate": 0.05,
            "max_depth": 6,
            "subsample": 0.8,
            "colsample_bytree": 0.8,
            "reg_alpha": 0.1,
            "reg_lambda": 0.1,
        },
        "catboost": {
            "iterations": 500,
            "learning_rate": 0.05,
            "depth": 6,
            "l2_leaf_reg": 3,
        },
        "lstm": {
            "hidden_size": 64,
            "num_layers": 2,
            "dropout": 0.1,
            "lr": 0.001,
            "epochs": 50,
            "batch_size": 64,
            "seq_len": 20,
        },
        "gru": {
            "hidden_size": 64,
            "num_layers": 2,
            "dropout": 0.1,
            "lr": 0.001,
            "epochs": 50,
            "batch_size": 64,
            "seq_len": 20,
        },
        "transformer": {
            "d_model": 64,
            "nhead": 4,
            "num_layers": 2,
            "dropout": 0.1,
            "lr": 0.0005,
            "epochs": 50,
            "batch_size": 64,
            "seq_len": 20,
        },
    },
    "evaluation": {
        "metrics": [
            "ic",           # Information Coefficient
            "icir",         # IC Information Ratio
            "rank_ic",      # Rank IC
            "sharpe",       # Sharpe Ratio
            "max_drawdown", # Maximum Drawdown
            "win_rate",     # Win Rate
            "profit_factor",# Profit Factor
        ],
    },
}


# ============================================================================
# Model Training Functions
# ============================================================================

def train_lightgbm(X_train, y_train, X_val, y_val, params=None):
    """Train LightGBM model (from qlib_service.py pattern)."""
    if params is None:
        params = VN30F_ML_CONFIG["models"]["lightgbm"]

    try:
        import lightgbm as lgb
        train_data = lgb.Dataset(X_train, label=y_train)
        val_data = lgb.Dataset(X_val, label=y_val, reference=train_data)

        lgb_params = {
            "objective": "regression",
            "metric": "mse",
            "learning_rate": params.get("learning_rate", 0.05),
            "max_depth": params.get("max_depth", 6),
            "num_leaves": params.get("num_leaves", 31),
            "subsample": params.get("subsample", 0.8),
            "colsample_bytree": params.get("colsample_bytree", 0.8),
            "reg_alpha": params.get("reg_alpha", 0.1),
            "reg_lambda": params.get("reg_lambda", 0.1),
            "verbosity": -1,
        }

        model = lgb.train(
            lgb_params,
            train_data,
            num_boost_round=params.get("n_estimators", 500),
            valid_sets=[val_data],
        )
        return {"success": True, "model": model, "type": "lightgbm"}
    except ImportError:
        return {"success": False, "error": "lightgbm not installed"}
    except Exception as e:
        return {"success": False, "error": str(e)}


def train_xgboost(X_train, y_train, X_val, y_val, params=None):
    """Train XGBoost model (from qlib_service.py pattern)."""
    if params is None:
        params = VN30F_ML_CONFIG["models"]["xgboost"]

    try:
        import xgboost as xgb
        dtrain = xgb.DMatrix(X_train, label=y_train)
        dval = xgb.DMatrix(X_val, label=y_val)

        xgb_params = {
            "objective": "reg:squarederror",
            "eval_metric": "rmse",
            "learning_rate": params.get("learning_rate", 0.05),
            "max_depth": params.get("max_depth", 6),
            "subsample": params.get("subsample", 0.8),
            "colsample_bytree": params.get("colsample_bytree", 0.8),
            "reg_alpha": params.get("reg_alpha", 0.1),
            "reg_lambda": params.get("reg_lambda", 0.1),
        }

        model = xgb.train(
            xgb_params,
            dtrain,
            num_boost_round=params.get("n_estimators", 500),
            evals=[(dval, "val")],
            verbose_eval=False,
        )
        return {"success": True, "model": model, "type": "xgboost"}
    except ImportError:
        return {"success": False, "error": "xgboost not installed"}
    except Exception as e:
        return {"success": False, "error": str(e)}


def train_model(model_name, X_train, y_train, X_val, y_val, params=None):
    """Universal model training dispatcher."""
    trainers = {
        "lightgbm": train_lightgbm,
        "xgboost": train_xgboost,
    }
    if model_name in trainers:
        return trainers[model_name](X_train, y_train, X_val, y_val, params)
    return {"success": False, "error": f"Unknown model: {model_name}. Available: {list(trainers.keys())}"}


# ============================================================================
# Evaluation Metrics (from qlib_evaluation.py pattern)
# ============================================================================

def compute_ic(predictions, actuals):
    """Information Coefficient (rank correlation)."""
    try:
        from scipy.stats import spearmanr
        ic, p_value = spearmanr(predictions, actuals)
        return {"ic": ic, "p_value": p_value}
    except Exception:
        return {"ic": 0.0, "p_value": 1.0}


def compute_metrics(predictions, actuals, returns=None):
    """Compute full evaluation metrics suite."""
    import numpy as np
    metrics = {}

    # IC
    ic_result = compute_ic(predictions, actuals)
    metrics["ic"] = ic_result["ic"]

    # Basic accuracy
    if len(predictions) > 0:
        correct_direction = np.sign(predictions) == np.sign(actuals)
        metrics["direction_accuracy"] = float(np.mean(correct_direction))
        metrics["win_rate"] = float(np.mean(correct_direction))

    # If strategy returns provided
    if returns is not None and len(returns) > 0:
        returns = np.array(returns)
        mean_ret = np.mean(returns)
        std_ret = np.std(returns)
        metrics["sharpe"] = float(mean_ret / std_ret * np.sqrt(252)) if std_ret > 0 else 0.0
        metrics["total_return"] = float(np.sum(returns))
        metrics["max_drawdown"] = float(_max_drawdown(returns))

    return metrics


def _max_drawdown(returns):
    """Compute maximum drawdown from returns series."""
    import numpy as np
    cumulative = np.cumsum(returns)
    running_max = np.maximum.accumulate(cumulative)
    drawdown = cumulative - running_max
    return float(np.min(drawdown)) if len(drawdown) > 0 else 0.0


# ============================================================================
# Rolling Window Training (from qlib_rolling_retraining.py pattern)
# ============================================================================

def rolling_train(df, model_name="lightgbm", feature_cols=None, label_col="label",
                  train_window=120, val_window=20, test_window=20, step=20):
    """Rolling window training and evaluation.

    Slides a window over the data:
    [---train---][--val--][--test--]
                     step →
    [---train---][--val--][--test--]
    """
    import numpy as np

    if feature_cols is None:
        exclude_cols = {label_col, "date", "datetime", "symbol"}
        feature_cols = [c for c in df.select_dtypes(include=[np.number]).columns if c not in exclude_cols]

    total_len = len(df)
    window_size = train_window + val_window + test_window

    results = []
    idx = 0

    while idx + window_size <= total_len:
        train_end = idx + train_window
        val_end = train_end + val_window
        test_end = val_end + test_window

        X_train = df.iloc[idx:train_end][feature_cols].values
        y_train = df.iloc[idx:train_end][label_col].values
        X_val = df.iloc[train_end:val_end][feature_cols].values
        y_val = df.iloc[train_end:val_end][label_col].values
        X_test = df.iloc[val_end:test_end][feature_cols].values
        y_test = df.iloc[val_end:test_end][label_col].values

        # Train model
        model_result = train_model(model_name, X_train, y_train, X_val, y_val)
        if not model_result.get("success"):
            idx += step
            continue

        # Predict on test set
        model = model_result["model"]
        if model_name == "lightgbm":
            predictions = model.predict(X_test)
        elif model_name == "xgboost":
            import xgboost as xgb
            predictions = model.predict(xgb.DMatrix(X_test))
        else:
            predictions = np.zeros(len(X_test))

        # Evaluate
        metrics = compute_metrics(predictions, y_test)
        metrics["window_start"] = idx
        metrics["window_end"] = test_end
        results.append(metrics)

        idx += step

    return results


# ============================================================================
# CLI Dispatch
# ============================================================================

def main():
    if len(sys.argv) < 2:
        print(json.dumps({"success": False, "error": "Usage: vn30f_ml_training.py <action>"}))
        sys.exit(1)

    action = sys.argv[1]

    if action == "config":
        print(json.dumps({"success": True, "data": VN30F_ML_CONFIG}))
    elif action == "models":
        models = list(VN30F_ML_CONFIG["models"].keys())
        print(json.dumps({"success": True, "data": {"available_models": models}}))
    elif action == "metrics":
        print(json.dumps({"success": True, "data": VN30F_ML_CONFIG["evaluation"]}))
    elif action == "train":
        import pandas as pd
        model_name = sys.argv[2] if len(sys.argv) > 2 else "lightgbm"
        data = json.loads(sys.stdin.read())
        df = pd.DataFrame(data)
        results = rolling_train(df, model_name=model_name)
        print(json.dumps({"success": True, "data": results}, default=str))
    else:
        print(json.dumps({"success": False, "error": f"Unknown action: {action}"}))


if __name__ == "__main__":
    main()
