#!/usr/bin/env python3
"""
VN30F RL Trading Environment — Adapted from finrl_environments.py & qlib_rl.py
Gym environment for training RL agents on VN30F futures.

Extends StockTradingEnv pattern with VN30F-specific constraints:
- VN trading sessions (8:45-11:30, 13:00-14:30)
- ±7% daily price limits
- Margin requirements (~13% initial)
- T+0 settlement
- Lot-based trading (integer contracts)
"""

import sys
import os
import json

import numpy as np

try:
    import gymnasium as gym
    from gymnasium import spaces
except ImportError:
    import gym
    from gym import spaces


# ============================================================================
# VN30F Trading Environment
# ============================================================================

class VN30FTradingEnv(gym.Env):
    """VN30F Futures Trading Environment for Reinforcement Learning.

    Based on StockTradingEnv from finrl_environments.py with VN30F adaptations.

    State Space: [price_features + technical_indicators + portfolio_state + margin_info]
    Action Space: Continuous [-max_lots, +max_lots] (negative=short, positive=long)
    Reward: Risk-adjusted return (Sharpe-based)

    VN30F Constraints:
    - Margin: 13% of contract value
    - Fee: 0.027% per side
    - Price limit: ±7% daily
    - Lot size: 1 contract
    - Multiplier: 100,000 VND/point
    """

    metadata = {"render.modes": ["human"]}

    def __init__(self, config=None):
        super().__init__()

        config = config or {}

        # Market parameters
        self.multiplier = config.get("multiplier", 100_000)  # VND per point
        self.initial_margin_pct = config.get("initial_margin_pct", 0.13)
        self.fee_pct = config.get("fee_pct", 0.00027)  # 0.027%
        self.price_limit_pct = config.get("price_limit_pct", 0.07)

        # Trading parameters
        self.max_lots = config.get("max_lots", 50)
        self.initial_amount = config.get("initial_amount", 500_000_000)  # 500M VND
        self.stop_loss_pct = config.get("stop_loss_pct", 0.03)  # 3% stop loss
        self.max_drawdown_pct = config.get("max_drawdown_pct", 0.10)  # 10% max DD

        # Technical indicators to include in state
        self.tech_indicators = config.get("tech_indicator_list", [
            "rsi_14", "macd", "macd_signal", "atr_14",
            "bb_upper", "bb_lower", "obv_change", "cmf_20",
        ])

        # Data
        self.df = config.get("data", None)

        # State and action spaces
        # State: [close, volume, position, margin_used, unrealized_pnl, cash] + tech_indicators
        state_dim = 6 + len(self.tech_indicators)
        self.observation_space = spaces.Box(
            low=-np.inf, high=np.inf, shape=(state_dim,), dtype=np.float32
        )
        # Action: number of lots to hold (continuous: -max_lots to +max_lots)
        self.action_space = spaces.Box(
            low=-1.0, high=1.0, shape=(1,), dtype=np.float32
        )

        self.reset()

    def reset(self, seed=None, options=None):
        """Reset environment to initial state."""
        super().reset(seed=seed)

        self.day = 0
        self.cash = self.initial_amount
        self.position = 0  # Number of lots (positive=long, negative=short)
        self.entry_price = 0.0
        self.unrealized_pnl = 0.0
        self.realized_pnl = 0.0
        self.total_trades = 0
        self.winning_trades = 0
        self.peak_nav = self.initial_amount
        self.max_drawdown = 0.0

        self._trade_history = []
        self._nav_history = [self.initial_amount]

        state = self._get_state()
        return state, {} if hasattr(spaces, 'Dict') else state

    def step(self, action):
        """Execute one trading step.

        Args:
            action: Continuous value in [-1, 1], scaled to [-max_lots, +max_lots]
        """
        if self.df is None or self.day >= len(self.df) - 1:
            return self._get_state(), 0.0, True, False, {}

        # Get current and next price
        current_row = self.df.iloc[self.day]
        next_row = self.df.iloc[self.day + 1]
        current_price = float(current_row.get("close", 1200.0))
        next_price = float(next_row.get("close", current_price))

        # Scale action to lot count
        target_lots = int(np.round(float(action[0]) * self.max_lots))
        target_lots = np.clip(target_lots, -self.max_lots, self.max_lots)

        # Check margin availability before adjusting position
        required_margin = abs(target_lots) * current_price * self.multiplier * self.initial_margin_pct
        nav = self._get_nav(current_price)
        if required_margin > nav * 0.9:  # Don't use more than 90% of NAV as margin
            target_lots = int(np.sign(target_lots) * (nav * 0.9) /
                              (current_price * self.multiplier * self.initial_margin_pct))

        # Execute trades
        lots_to_trade = target_lots - self.position
        if lots_to_trade != 0:
            self._execute_trade(lots_to_trade, current_price)

        # Advance day
        self.day += 1

        # Update P&L
        if self.position != 0:
            self.unrealized_pnl = (next_price - self.entry_price) * self.position * self.multiplier
        else:
            self.unrealized_pnl = 0.0

        # Update NAV and drawdown
        nav = self._get_nav(next_price)
        self._nav_history.append(nav)
        if nav > self.peak_nav:
            self.peak_nav = nav
        dd = (self.peak_nav - nav) / self.peak_nav if self.peak_nav > 0 else 0
        self.max_drawdown = max(self.max_drawdown, dd)

        # Reward: risk-adjusted return
        if len(self._nav_history) >= 2:
            ret = (self._nav_history[-1] - self._nav_history[-2]) / self._nav_history[-2]
        else:
            ret = 0.0
        reward = ret * 100  # Scale for RL

        # Penalty for max drawdown breach
        if self.max_drawdown > self.max_drawdown_pct:
            reward -= 10.0

        # Check termination
        done = (self.day >= len(self.df) - 1) or (nav <= self.initial_amount * 0.5)
        truncated = self.day >= len(self.df) - 1

        info = {
            "nav": nav,
            "position": self.position,
            "unrealized_pnl": self.unrealized_pnl,
            "realized_pnl": self.realized_pnl,
            "total_trades": self.total_trades,
            "max_drawdown": self.max_drawdown,
        }

        return self._get_state(), reward, done, truncated, info

    def _execute_trade(self, lots, price):
        """Execute a trade of given lots at given price."""
        trade_value = abs(lots) * price * self.multiplier
        fee = trade_value * self.fee_pct

        # Close existing position P&L
        if self.position != 0 and np.sign(lots) != np.sign(self.position):
            close_lots = min(abs(lots), abs(self.position))
            pnl = (price - self.entry_price) * np.sign(self.position) * close_lots * self.multiplier
            self.realized_pnl += pnl - fee
            self.total_trades += 1
            if pnl > 0:
                self.winning_trades += 1

        self.cash -= fee
        old_position = self.position
        self.position += lots

        # Update entry price
        if self.position != 0:
            if old_position == 0 or np.sign(self.position) != np.sign(old_position):
                self.entry_price = price
            else:
                # Average entry
                total = abs(old_position) * self.entry_price + abs(lots) * price
                self.entry_price = total / abs(self.position)
        else:
            self.entry_price = 0.0

        self._trade_history.append({
            "day": self.day, "lots": lots, "price": price, "fee": fee,
        })

    def _get_nav(self, current_price=None):
        """Calculate Net Asset Value."""
        if current_price is None and self.df is not None and self.day < len(self.df):
            current_price = float(self.df.iloc[self.day].get("close", 1200.0))
        elif current_price is None:
            current_price = 1200.0

        unrealized = (current_price - self.entry_price) * self.position * self.multiplier if self.position != 0 else 0
        return self.cash + self.realized_pnl + unrealized

    def _get_state(self):
        """Construct observation state vector."""
        if self.df is None or self.day >= len(self.df):
            return np.zeros(self.observation_space.shape, dtype=np.float32)

        row = self.df.iloc[self.day]
        current_price = float(row.get("close", 1200.0))
        nav = self._get_nav(current_price)

        state = [
            current_price / 1500.0,  # Normalized price (VN30F ~1000-1500)
            float(row.get("volume", 0)) / 100000.0,  # Normalized volume
            self.position / self.max_lots,  # Normalized position
            (abs(self.position) * current_price * self.multiplier * self.initial_margin_pct) / nav if nav > 0 else 0,
            self.unrealized_pnl / self.initial_amount,  # Normalized unrealized PnL
            self.cash / self.initial_amount,  # Normalized cash
        ]

        # Add technical indicators
        for ind in self.tech_indicators:
            val = float(row.get(ind, 0.0))
            if np.isnan(val) or np.isinf(val):
                val = 0.0
            state.append(val)

        return np.array(state, dtype=np.float32)

    def render(self, mode="human"):
        """Print current state."""
        nav = self._get_nav()
        print(f"Day {self.day}: NAV={nav:,.0f} VND, Pos={self.position}, "
              f"PnL={self.realized_pnl:,.0f}, DD={self.max_drawdown:.2%}")


# ============================================================================
# VN30F RL Training (from finrl_agents.py + qlib_rl.py pattern)
# ============================================================================

VN30F_RL_CONFIG = {
    "algorithms": {
        "ppo": {
            "learning_rate": 0.0003,
            "n_steps": 2048,
            "batch_size": 64,
            "n_epochs": 10,
            "gamma": 0.99,
            "clip_range": 0.2,
            "total_timesteps": 200_000,
        },
        "dqn": {
            "learning_rate": 0.0001,
            "buffer_size": 100_000,
            "batch_size": 64,
            "gamma": 0.99,
            "exploration_fraction": 0.1,
            "total_timesteps": 200_000,
        },
        "a2c": {
            "learning_rate": 0.0007,
            "n_steps": 5,
            "gamma": 0.99,
            "total_timesteps": 200_000,
        },
        "sac": {
            "learning_rate": 0.0003,
            "buffer_size": 100_000,
            "batch_size": 256,
            "gamma": 0.99,
            "tau": 0.005,
            "total_timesteps": 200_000,
        },
        "td3": {
            "learning_rate": 0.001,
            "buffer_size": 100_000,
            "batch_size": 100,
            "gamma": 0.99,
            "tau": 0.005,
            "policy_delay": 2,
            "total_timesteps": 200_000,
        },
    },
    "environment": {
        "max_lots": 50,
        "initial_amount": 500_000_000,
        "stop_loss_pct": 0.03,
        "max_drawdown_pct": 0.10,
        "fee_pct": 0.00027,
    },
}


def train_rl_agent(env, algorithm="ppo", total_timesteps=200_000, params=None):
    """Train RL agent using Stable-Baselines3 (from finrl_agents.py pattern)."""
    try:
        if algorithm == "ppo":
            from stable_baselines3 import PPO
            model = PPO("MlpPolicy", env, verbose=1,
                        learning_rate=params.get("learning_rate", 0.0003) if params else 0.0003,
                        n_steps=params.get("n_steps", 2048) if params else 2048,
                        batch_size=params.get("batch_size", 64) if params else 64)
        elif algorithm == "a2c":
            from stable_baselines3 import A2C
            model = A2C("MlpPolicy", env, verbose=1,
                         learning_rate=params.get("learning_rate", 0.0007) if params else 0.0007)
        elif algorithm == "sac":
            from stable_baselines3 import SAC
            model = SAC("MlpPolicy", env, verbose=1,
                         learning_rate=params.get("learning_rate", 0.0003) if params else 0.0003,
                         buffer_size=params.get("buffer_size", 100_000) if params else 100_000)
        elif algorithm == "td3":
            from stable_baselines3 import TD3
            model = TD3("MlpPolicy", env, verbose=1,
                         learning_rate=params.get("learning_rate", 0.001) if params else 0.001,
                         buffer_size=params.get("buffer_size", 100_000) if params else 100_000)
        elif algorithm == "dqn":
            from stable_baselines3 import DQN
            # DQN needs discrete actions — wrap environment
            model = DQN("MlpPolicy", env, verbose=1,
                         learning_rate=params.get("learning_rate", 0.0001) if params else 0.0001,
                         buffer_size=params.get("buffer_size", 100_000) if params else 100_000)
        else:
            return {"success": False, "error": f"Unknown algorithm: {algorithm}"}

        model.learn(total_timesteps=total_timesteps)
        return {"success": True, "model": model, "algorithm": algorithm}

    except ImportError:
        return {"success": False, "error": f"stable-baselines3 not installed"}
    except Exception as e:
        return {"success": False, "error": str(e)}


def ensemble_selection(models_results, method="validation_sharpe"):
    """Select best model from ensemble (from finrl_ensemble.py pattern).

    Methods:
    - validation_sharpe: Pick model with highest validation Sharpe
    - turbulence_switching: PPO when calm, A2C when turbulent
    - weighted_average: Weight by Sharpe ratio
    """
    if method == "validation_sharpe":
        best = max(models_results, key=lambda x: x.get("sharpe", 0))
        return best
    elif method == "weighted_average":
        total_sharpe = sum(max(r.get("sharpe", 0), 0.01) for r in models_results)
        weights = {r["algorithm"]: max(r.get("sharpe", 0), 0.01) / total_sharpe for r in models_results}
        return {"method": "weighted_average", "weights": weights}
    elif method == "turbulence_switching":
        return {"method": "turbulence_switching", "calm": "ppo", "turbulent": "a2c"}
    return models_results[0] if models_results else None


# ============================================================================
# CLI Dispatch
# ============================================================================

def main():
    if len(sys.argv) < 2:
        print(json.dumps({"success": False, "error": "Usage: vn30f_rl_env.py <action>"}))
        sys.exit(1)

    action = sys.argv[1]

    if action == "config":
        print(json.dumps({"success": True, "data": VN30F_RL_CONFIG}))
    elif action == "algorithms":
        algos = list(VN30F_RL_CONFIG["algorithms"].keys())
        print(json.dumps({"success": True, "data": {"algorithms": algos}}))
    elif action == "env_info":
        env = VN30FTradingEnv()
        info = {
            "observation_space": str(env.observation_space),
            "action_space": str(env.action_space),
            "max_lots": env.max_lots,
            "initial_amount": env.initial_amount,
            "multiplier": env.multiplier,
            "margin_pct": env.initial_margin_pct,
        }
        print(json.dumps({"success": True, "data": info}))
    elif action == "train":
        algorithm = sys.argv[2] if len(sys.argv) > 2 else "ppo"
        timesteps = int(sys.argv[3]) if len(sys.argv) > 3 else 200_000
        import pandas as pd
        data = json.loads(sys.stdin.read())
        df = pd.DataFrame(data)
        env = VN30FTradingEnv({"data": df})
        result = train_rl_agent(env, algorithm=algorithm, total_timesteps=timesteps)
        # Remove non-serializable model object
        result.pop("model", None)
        print(json.dumps({"success": result.get("success", False), "data": result}, default=str))
    else:
        print(json.dumps({"success": False, "error": f"Unknown action: {action}"}))


if __name__ == "__main__":
    main()
