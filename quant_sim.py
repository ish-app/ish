#!/usr/bin/env python3
"""
Quant Simulator (single-file research platform)

Includes:
- Data layer (CSV provider + validation + resample)
- Strategy module (trend-following + mean reversion + pairs stub)
- Execution engine (MKT/LMT/STOP) with slippage + commissions
- Portfolio / risk constraints (leverage, max position %, margin-ish)
- Backtester + optimizer (grid + simple genetic algorithm)
- Performance analytics + exports
- Optional Plotly visualization

CSV expected columns (case-insensitive):
timestamp, open, high, low, close, volume
"""

from __future__ import annotations
from dataclasses import dataclass, field
from typing import Dict, Any, List, Optional, Tuple, Callable
import argparse
import math
import random
import pandas as pd
import numpy as np

# -----------------------------
# Data Infrastructure
# -----------------------------

class DataError(Exception):
    pass

class DataProvider:
    """Base class for historical or live data providers."""
    def get(self, symbol: str, start: Optional[str] = None, end: Optional[str] = None) -> pd.DataFrame:
        raise NotImplementedError

class CSVDataProvider(DataProvider):
    def __init__(self, csv_path: str):
        self.csv_path = csv_path

    def get(self, symbol: str, start: Optional[str] = None, end: Optional[str] = None) -> pd.DataFrame:
        df = pd.read_csv(self.csv_path)
        df.columns = [c.lower().strip() for c in df.columns]

        if "timestamp" not in df.columns:
            raise DataError("CSV missing 'timestamp' column.")
        # parse timestamps (datetime or epoch)
        try:
            dt = pd.to_datetime(df["timestamp"], utc=True, errors="raise")
        except Exception:
            dt = pd.to_datetime(df["timestamp"].astype(float), unit="s", utc=True, errors="coerce")

        df = df.drop(columns=["timestamp"])
        df.index = dt
        df = df.sort_index()

        # filter
        if start:
            df = df[df.index >= pd.to_datetime(start, utc=True)]
        if end:
            df = df[df.index <= pd.to_datetime(end, utc=True)]

        return df

def validate_ohlcv(df: pd.DataFrame) -> pd.DataFrame:
    required = ["open", "high", "low", "close"]
    for c in required:
        if c not in df.columns:
            raise DataError(f"Missing required column: {c}")
    for c in ["open", "high", "low", "close", "volume"]:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")
    df = df.dropna(subset=required)
    # basic sanity: high >= max(open,close,low), low <= min(...)
    df = df[(df["high"] >= df[["open","close","low"]].max(axis=1)) & (df["low"] <= df[["open","close","high"]].min(axis=1))]
    return df

def resample_ohlcv(df: pd.DataFrame, rule: str) -> pd.DataFrame:
    """Resample to another bar timeframe (e.g., '1H', '1D')."""
    agg = {
        "open": "first",
        "high": "max",
        "low": "min",
        "close": "last",
        "volume": "sum" if "volume" in df.columns else "sum",
    }
    out = df.resample(rule).agg(agg).dropna(subset=["open","high","low","close"])
    return out

# -----------------------------
# Indicators
# -----------------------------

def sma(s: pd.Series, n: int) -> pd.Series:
    return s.rolling(n, min_periods=n).mean()

def ema(s: pd.Series, n: int) -> pd.Series:
    return s.ewm(span=n, adjust=False, min_periods=n).mean()

def rsi(close: pd.Series, n: int = 14) -> pd.Series:
    d = close.diff()
    up = d.clip(lower=0.0)
    dn = (-d).clip(lower=0.0)
    gain = up.ewm(alpha=1/n, adjust=False, min_periods=n).mean()
    loss = dn.ewm(alpha=1/n, adjust=False, min_periods=n).mean()
    rs = gain / loss.replace(0, np.nan)
    return 100 - (100 / (1 + rs))

def true_range(high: pd.Series, low: pd.Series, close: pd.Series) -> pd.Series:
    pc = close.shift(1)
    return pd.concat([(high-low), (high-pc).abs(), (low-pc).abs()], axis=1).max(axis=1)

def atr(high: pd.Series, low: pd.Series, close: pd.Series, n: int = 14) -> pd.Series:
    tr = true_range(high, low, close)
    return tr.ewm(alpha=1/n, adjust=False, min_periods=n).mean()

# -----------------------------
# Orders / Execution
# -----------------------------

@dataclass
class Order:
    ts: pd.Timestamp
    symbol: str
    side: str            # "BUY" / "SELL"
    qty: float
    type: str = "MKT"    # MKT / LMT / STOP
    limit_price: Optional[float] = None
    stop_price: Optional[float] = None
    tag: str = ""

@dataclass
class Fill:
    ts: pd.Timestamp
    symbol: str
    side: str
    qty: float
    price: float
    commission: float
    slippage_cost: float
    tag: str = ""

@dataclass
class ExecutionConfig:
    commission_per_trade: float = 0.0
    commission_bps: float = 0.0
    slippage_bps: float = 0.0
    max_leverage: float = 1.0
    max_pos_pct_equity: float = 1.0
    allow_short: bool = True

class ExecutionEngine:
    """
    Bar-based execution model:
    - MKT fills at close +/- slippage
    - LMT fills if price crosses limit within bar (low/high)
    - STOP fills if price crosses stop within bar
    This is NOT tick-accurate; you can replace fill logic with tick/L2 model later.
    """
    def __init__(self, cfg: ExecutionConfig):
        self.cfg = cfg

    def _slipped_price(self, side: str, ref_price: float) -> Tuple[float, float]:
        slip = self.cfg.slippage_bps / 10000.0
        fill = ref_price * (1 + slip) if side == "BUY" else ref_price * (1 - slip)
        slip_cost = abs(fill - ref_price)
        return fill, slip_cost

    def _commission(self, notional: float) -> float:
        return self.cfg.commission_per_trade + (self.cfg.commission_bps / 10000.0) * abs(notional)

    def _apply_constraints(self, equity: float, current_qty: float, price: float, desired_delta: float) -> float:
        # Single-asset constraints: keep abs(position_notional) <= max_pos_pct_equity*equity and leverage <= max_leverage
        if equity <= 0:
            return 0.0
        proposed_qty = current_qty + desired_delta
        proposed_exposure = abs(proposed_qty * price)

        cap_exposure = min(self.cfg.max_pos_pct_equity * equity, self.cfg.max_leverage * equity)
        if proposed_exposure <= cap_exposure + 1e-9:
            return desired_delta

        # scale delta down
        max_qty = cap_exposure / max(price, 1e-12)
        allowed = max_qty - abs(current_qty)
        if allowed <= 0:
            return 0.0
        return math.copysign(min(abs(desired_delta), allowed), desired_delta)

    def try_fill(
        self,
        order: Order,
        bar: pd.Series,
        equity: float,
        current_pos_qty: float
    ) -> Optional[Fill]:
        side = order.side.upper()
        otype = order.type.upper()
        qty = float(order.qty)
        if qty <= 0:
            return None

        if side == "SELL" and not self.cfg.allow_short and current_pos_qty <= 0:
            return None

        # Determine if order triggers
        low, high, close = float(bar["low"]), float(bar["high"]), float(bar["close"])

        trigger_price: Optional[float] = None

        if otype == "MKT":
            trigger_price = close

        elif otype == "LMT":
            if order.limit_price is None:
                return None
            lp = float(order.limit_price)
            # Buy limit fills if low <= lp, Sell limit fills if high >= lp
            if side == "BUY" and low <= lp:
                trigger_price = lp
            elif side == "SELL" and high >= lp:
                trigger_price = lp

        elif otype == "STOP":
            if order.stop_price is None:
                return None
            sp = float(order.stop_price)
            # Buy stop triggers if high >= sp, Sell stop triggers if low <= sp
            if side == "BUY" and high >= sp:
                trigger_price = sp
            elif side == "SELL" and low <= sp:
                trigger_price = sp

        else:
            raise ValueError(f"Unknown order type: {otype}")

        if trigger_price is None:
            return None

        # Apply constraints
        desired_delta = qty if side == "BUY" else -qty
        constrained_delta = self._apply_constraints(equity, current_pos_qty, close, desired_delta)
        if constrained_delta == 0:
            return None

        fill_qty = abs(constrained_delta)
        fill_price, slip_unit = self._slipped_price(side, trigger_price)
        notional = fill_price * fill_qty
        comm = self._commission(notional)
        slip_cost = slip_unit * fill_qty

        return Fill(order.ts, order.symbol, side, fill_qty, fill_price, comm, slip_cost, order.tag)

# -----------------------------
# Portfolio Simulator
# -----------------------------

@dataclass
class Position:
    qty: float = 0.0
    avg_price: float = 0.0
    realized_pnl: float = 0.0
    entry_ts: Optional[pd.Timestamp] = None  # for hold time (single-lot approximation)

@dataclass
class Portfolio:
    cash: float
    pos: Position = field(default_factory=Position)
    equity: float = 0.0
    peak_equity: float = 0.0
    max_drawdown: float = 0.0

    def mark(self, price: float):
        self.equity = self.cash + self.pos.qty * price
        if self.peak_equity == 0:
            self.peak_equity = self.equity
        self.peak_equity = max(self.peak_equity, self.equity)
        dd = (self.equity / self.peak_equity) - 1.0 if self.peak_equity > 0 else 0.0
        self.max_drawdown = min(self.max_drawdown, dd)

    def apply_fill(self, fill: Fill):
        side = fill.side
        qty = fill.qty
        px = fill.price
        fees = fill.commission

        p = self.pos

        if side == "BUY":
            # cover short then add long
            if p.qty < 0:
                cover = min(qty, abs(p.qty))
                p.realized_pnl += (p.avg_price - px) * cover
                p.qty += cover
                self.cash -= px * cover + fees * (cover / qty)

                if p.qty == 0:
                    p.avg_price = 0.0
                    p.entry_ts = None

                rem = qty - cover
                if rem > 0:
                    # new/increase long
                    new_qty = p.qty + rem
                    p.avg_price = (p.avg_price * p.qty + px * rem) / new_qty if new_qty != 0 else px
                    if p.qty == 0:
                        p.entry_ts = fill.ts
                    p.qty = new_qty
                    self.cash -= px * rem + fees * (rem / qty)
            else:
                # increase long
                if p.qty == 0:
                    p.entry_ts = fill.ts
                new_qty = p.qty + qty
                p.avg_price = (p.avg_price * p.qty + px * qty) / new_qty if new_qty != 0 else px
                p.qty = new_qty
                self.cash -= px * qty + fees

        elif side == "SELL":
            # sell long then add short
            if p.qty > 0:
                sell = min(qty, p.qty)
                p.realized_pnl += (px - p.avg_price) * sell
                p.qty -= sell
                self.cash += px * sell - fees * (sell / qty)

                if p.qty == 0:
                    p.avg_price = 0.0
                    p.entry_ts = None

                rem = qty - sell
                if rem > 0:
                    # new/increase short
                    if p.qty == 0:
                        p.entry_ts = fill.ts
                        p.avg_price = px
                        p.qty = -rem
                    else:
                        tot = abs(p.qty) + rem
                        p.avg_price = (p.avg_price * abs(p.qty) + px * rem) / tot
                        p.qty -= rem
                    self.cash += px * rem - fees * (rem / qty)
            else:
                # increase short
                if p.qty == 0:
                    p.entry_ts = fill.ts
                tot = abs(p.qty) + qty
                p.avg_price = (p.avg_price * abs(p.qty) + px * qty) / tot if tot != 0 else px
                p.qty -= qty
                self.cash += px * qty - fees

# -----------------------------
# Strategy Definition Module
# -----------------------------

class Strategy:
    def prepare(self, data: pd.DataFrame) -> pd.DataFrame:
        return data

    def on_bar(self, i: int, ts: pd.Timestamp, row: pd.Series, ctx: Dict[str, Any]) -> List[Order]:
        return []

class TrendFollowingEMA(Strategy):
    """Classic trend-following: EMA fast/slow cross + ATR stop + optional TP."""
    def __init__(self, fast: int = 12, slow: int = 26, atr_n: int = 14, atr_stop: float = 2.5, tp_r: Optional[float] = None):
        if fast >= slow:
            raise ValueError("fast must be < slow")
        self.fast, self.slow = fast, slow
        self.atr_n, self.atr_stop = atr_n, atr_stop
        self.tp_r = tp_r

    def prepare(self, data: pd.DataFrame) -> pd.DataFrame:
        d = data.copy()
        d["ema_fast"] = ema(d["close"], self.fast)
        d["ema_slow"] = ema(d["close"], self.slow)
        d["atr"] = atr(d["high"], d["low"], d["close"], self.atr_n)

        fast_gt = d["ema_fast"] > d["ema_slow"]
        fast_lt = d["ema_fast"] < d["ema_slow"]
        d["sig"] = 0
        d.loc[fast_gt & (~fast_gt.shift(1).fillna(False)), "sig"] = 1
        d.loc[fast_lt & (~fast_lt.shift(1).fillna(False)), "sig"] = -1
        return d

    def on_bar(self, i, ts, row, ctx):
        orders = []
        sym = ctx["symbol"]
        port: Portfolio = ctx["portfolio"]
        pos_qty = port.pos.qty

        # Stop/TP management (bar-based)
        stop = ctx.get("stop_price")
        tp = ctx.get("tp_price")
        low, high = float(row["low"]), float(row["high"])

        if pos_qty > 0:
            if stop is not None and low <= stop:
                orders.append(Order(ts, sym, "SELL", abs(pos_qty), "MKT", tag="STOP_LONG"))
                ctx["stop_price"] = None
                ctx["tp_price"] = None
                return orders
            if tp is not None and high >= tp:
                orders.append(Order(ts, sym, "SELL", abs(pos_qty), "MKT", tag="TP_LONG"))
                ctx["stop_price"] = None
                ctx["tp_price"] = None
                return orders

        if pos_qty < 0:
            if stop is not None and high >= stop:
                orders.append(Order(ts, sym, "BUY", abs(pos_qty), "MKT", tag="STOP_SHORT"))
                ctx["stop_price"] = None
                ctx["tp_price"] = None
                return orders
            if tp is not None and low <= tp:
                orders.append(Order(ts, sym, "BUY", abs(pos_qty), "MKT", tag="TP_SHORT"))
                ctx["stop_price"] = None
                ctx["tp_price"] = None
                return orders

        sig = int(row.get("sig", 0))
        px = float(row["close"])
        atrv = float(row.get("atr", np.nan))

        qty = ctx["sizer"](row, ctx)

        if sig == 1 and pos_qty <= 0:
            orders.append(Order(ts, sym, "BUY", qty, "MKT", tag="ENTER_LONG"))
            if not np.isnan(atrv) and atrv > 0:
                ctx["stop_price"] = px - self.atr_stop * atrv
                if self.tp_r is not None:
                    ctx["tp_price"] = px + self.tp_r * (self.atr_stop * atrv)

        if sig == -1 and pos_qty >= 0:
            orders.append(Order(ts, sym, "SELL", qty, "MKT", tag="ENTER_SHORT"))
            if not np.isnan(atrv) and atrv > 0:
                ctx["stop_price"] = px + self.atr_stop * atrv
                if self.tp_r is not None:
                    ctx["tp_price"] = px - self.tp_r * (self.atr_stop * atrv)

        return orders

class MeanReversionRSI(Strategy):
    """Mean reversion: RSI extremes + revert to neutral; uses fixed stops."""
    def __init__(self, rsi_n=14, buy_below=30, sell_above=70, exit_at=50, stop_pct=0.03):
        self.rsi_n=rsi_n; self.buy_below=buy_below; self.sell_above=sell_above
        self.exit_at=exit_at; self.stop_pct=stop_pct

    def prepare(self, data):
        d=data.copy()
        d["rsi"]=rsi(d["close"], self.rsi_n)
        return d

    def on_bar(self, i, ts, row, ctx):
        orders=[]
        sym=ctx["symbol"]
        port: Portfolio = ctx["portfolio"]
        px=float(row["close"])
        r=float(row.get("rsi", np.nan))
        pos=port.pos.qty

        # stop management
        stop=ctx.get("stop_price")
        low, high = float(row["low"]), float(row["high"])
        if pos>0 and stop is not None and low<=stop:
            orders.append(Order(ts,sym,"SELL",abs(pos),"MKT",tag="STOP_LONG"))
            ctx["stop_price"]=None
            return orders
        if pos<0 and stop is not None and high>=stop:
            orders.append(Order(ts,sym,"BUY",abs(pos),"MKT",tag="STOP_SHORT"))
            ctx["stop_price"]=None
            return orders

        qty=ctx["sizer"](row, ctx)

        # entries
        if pos==0 and not np.isnan(r):
            if r <= self.buy_below:
                orders.append(Order(ts,sym,"BUY",qty,"MKT",tag="MR_LONG"))
                ctx["stop_price"]=px*(1-self.stop_pct)
            elif r >= self.sell_above:
                orders.append(Order(ts,sym,"SELL",qty,"MKT",tag="MR_SHORT"))
                ctx["stop_price"]=px*(1+self.stop_pct)

        # exits
        if pos>0 and r >= self.exit_at:
            orders.append(Order(ts,sym,"SELL",abs(pos),"MKT",tag="MR_EXIT_LONG"))
            ctx["stop_price"]=None
        if pos<0 and r <= self.exit_at:
            orders.append(Order(ts,sym,"BUY",abs(pos),"MKT",tag="MR_EXIT_SHORT"))
            ctx["stop_price"]=None

        return orders

# -----------------------------
# Position Sizing (plug-in)
# -----------------------------

def sizer_fixed_notional(pct_equity: float) -> Callable[[pd.Series, Dict[str, Any]], float]:
    def _s(row, ctx):
        equity = ctx["portfolio"].equity
        px = float(row["close"])
        if equity <= 0 or px <= 0:
            return 0.0
        return (pct_equity * equity) / px
    return _s

def sizer_risk_atr(risk_per_trade: float, atr_mult: float) -> Callable[[pd.Series, Dict[str, Any]], float]:
    def _s(row, ctx):
        equity = ctx["portfolio"].equity
        px = float(row["close"])
        atrv = float(row.get("atr", np.nan))
        if equity <= 0 or px <= 0 or np.isnan(atrv) or atrv <= 0:
            return 0.0
        risk_dollars = risk_per_trade * equity
        stop_dist = atr_mult * atrv
        return max(risk_dollars / stop_dist, 0.0)
    return _s

# -----------------------------
# Backtesting & Optimization
# -----------------------------

@dataclass
class BacktestConfig:
    initial_cash: float = 100000.0
    max_drawdown_halt: Optional[float] = None  # e.g. -0.2
    flatten_on_halt: bool = True

@dataclass
class BacktestResult:
    equity: pd.DataFrame
    fills: pd.DataFrame
    metrics: Dict[str, float]

class Backtester:
    def __init__(self, exec_engine: ExecutionEngine, cfg: BacktestConfig):
        self.exec_engine = exec_engine
        self.cfg = cfg

    def run(self, data: pd.DataFrame, strategy: Strategy, symbol: str, sizer_fn: Callable) -> BacktestResult:
        port = Portfolio(cash=self.cfg.initial_cash)
        prepared = strategy.prepare(data)
        prepared = prepared.dropna(subset=["open","high","low","close"])

        fills: List[Fill] = []
        rows: List[Dict[str, Any]] = []

        ctx: Dict[str, Any] = {
            "portfolio": port,
            "symbol": symbol,
            "sizer": sizer_fn,
            "stop_price": None,
            "tp_price": None,
        }

        halted = False

        for i, (ts, row) in enumerate(prepared.iterrows()):
            px = float(row["close"])
            port.mark(px)

            # Halt logic
            if self.cfg.max_drawdown_halt is not None and port.max_drawdown <= self.cfg.max_drawdown_halt:
                halted = True

            if halted and self.cfg.flatten_on_halt and port.pos.qty != 0:
                side = "SELL" if port.pos.qty > 0 else "BUY"
                o = Order(ts, symbol, side, abs(port.pos.qty), "MKT", tag="HALT_FLATTEN")
                fill = self.exec_engine.try_fill(o, row, port.equity, port.pos.qty)
                if fill:
                    port.apply_fill(fill)
                    fills.append(fill)
                port.mark(px)

            if halted:
                rows.append(self._snap(ts, row, port))
                continue

            orders = strategy.on_bar(i, ts, row, ctx) or []
            for o in orders:
                fill = self.exec_engine.try_fill(o, row, port.equity, port.pos.qty)
                if fill:
                    port.apply_fill(fill)
                    fills.append(fill)
            port.mark(px)

            rows.append(self._snap(ts, row, port))

        eq = pd.DataFrame(rows).set_index("ts")
        fills_df = pd.DataFrame([f.__dict__ for f in fills]) if fills else pd.DataFrame(columns=[f.name for f in Fill.__dataclass_fields__.values()])
        metrics = compute_metrics(eq, fills_df)
        return BacktestResult(eq, fills_df, metrics)

    def _snap(self, ts, row, port):
        return {
            "ts": ts,
            "close": float(row["close"]),
            "cash": port.cash,
            "pos_qty": port.pos.qty,
            "pos_avg": port.pos.avg_price,
            "equity": port.equity,
            "realized_pnl": port.pos.realized_pnl,
            "max_drawdown": port.max_drawdown,
            "entry_ts": port.pos.entry_ts,
        }

def annual_factor(index: pd.DatetimeIndex) -> float:
    freq = pd.infer_freq(index)
    if not freq:
        # fallback: estimate average bar seconds
        if len(index) < 3:
            return 252.0
        dt = (index[1:] - index[:-1]).total_seconds().mean()
        if dt <= 0:
            return 252.0
        # approx trading seconds per year
        return (252 * 6.5 * 3600) / dt
    f = freq.lower()
    if "d" in f:
        return 252.0
    if "h" in f:
        return 252.0 * 6.5
    if "t" in f or "min" in f:
        return 252.0 * 6.5 * 60.0
    return 252.0

def compute_metrics(eq: pd.DataFrame, fills: pd.DataFrame) -> Dict[str, float]:
    if eq.empty:
        return {}

    equity = eq["equity"].astype(float)
    rets = equity.pct_change().fillna(0.0)
    ann = annual_factor(eq.index)

    total_return = (equity.iloc[-1] / equity.iloc[0]) - 1.0 if equity.iloc[0] != 0 else np.nan

    # CAGR
    days = (eq.index[-1] - eq.index[0]).total_seconds() / (3600*24)
    years = max(days / 365.25, 1e-12)
    cagr = (equity.iloc[-1] / equity.iloc[0]) ** (1/years) - 1.0 if equity.iloc[0] > 0 else np.nan

    mu = rets.mean()
    sd = rets.std(ddof=0)
    sharpe = (mu / sd) * math.sqrt(ann) if sd > 0 else np.nan

    downside = rets[rets < 0]
    dd = downside.std(ddof=0)
    sortino = (mu / dd) * math.sqrt(ann) if dd > 0 else np.nan

    running_max = equity.cummax()
    drawdown = (equity / running_max) - 1.0
    max_dd = drawdown.min()

    exposure = (eq["pos_qty"].abs() > 0).mean()

    # Approx hold time (single-lot approximation)
    # We'll treat a "trade" as periods between pos goes 0->!=0 and back to 0
    pos = eq["pos_qty"]
    enter = (pos.shift(1).fillna(0) == 0) & (pos != 0)
    exit_ = (pos.shift(1).fillna(0) != 0) & (pos == 0)
    enter_times = eq.index[enter]
    exit_times = eq.index[exit_]
    hold_hours = []
    for et in enter_times:
        xt = exit_times[exit_times > et]
        if len(xt) > 0:
            hold_hours.append((xt[0] - et).total_seconds() / 3600.0)
    avg_hold_hours = float(np.mean(hold_hours)) if hold_hours else np.nan

    # Costs
    total_comm = float(fills["commission"].sum()) if not fills.empty and "commission" in fills.columns else 0.0
    total_slip = float(fills["slippage_cost"].sum()) if not fills.empty and "slippage_cost" in fills.columns else 0.0

    # Approx win rate using realized pnl deltas at exits
    realized = eq["realized_pnl"].astype(float)
    realized_delta = realized.diff().fillna(0.0)
    trade_pnls = realized_delta[exit_]
    wins = (trade_pnls > 0).sum()
    losses = (trade_pnls < 0).sum()
    win_rate = wins / (wins + losses) if (wins + losses) > 0 else np.nan

    return {
        "start_equity": float(equity.iloc[0]),
        "end_equity": float(equity.iloc[-1]),
        "total_return": float(total_return),
        "cagr": float(cagr),
        "sharpe": float(sharpe),
        "sortino": float(sortino),
        "max_drawdown": float(max_dd),
        "exposure": float(exposure),
        "avg_hold_hours": float(avg_hold_hours) if not np.isnan(avg_hold_hours) else np.nan,
        "num_fills": float(len(fills)) if fills is not None else 0.0,
        "approx_win_rate": float(win_rate) if not np.isnan(win_rate) else np.nan,
        "total_commission": total_comm,
        "total_slippage_cost": total_slip,
    }

# --- Optimization ---

def grid_search(
    bt: Backtester,
    data: pd.DataFrame,
    symbol: str,
    strategy_factory: Callable[[Dict[str, Any]], Strategy],
    sizer_factory: Callable[[Dict[str, Any]], Callable],
    param_grid: Dict[str, List[Any]],
    score_fn: Callable[[Dict[str, float]], float],
    top_k: int = 10
) -> pd.DataFrame:
    keys = list(param_grid.keys())
    combos = [[]]
    for k in keys:
        combos = [c + [v] for c in combos for v in param_grid[k]]

    results = []
    for vals in combos:
        params = dict(zip(keys, vals))
        strat = strategy_factory(params)
        sizer = sizer_factory(params)
        res = bt.run(data, strat, symbol, sizer)
        score = score_fn(res.metrics)
        results.append({**params, **res.metrics, "score": score})

    out = pd.DataFrame(results).sort_values("score", ascending=False)
    return out.head(top_k)

def genetic_optimize(
    bt: Backtester,
    data: pd.DataFrame,
    symbol: str,
    strategy_factory: Callable[[Dict[str, Any]], Strategy],
    sizer_factory: Callable[[Dict[str, Any]], Callable],
    space: Dict[str, Tuple[Any, Any, str]],  # (min,max,type) where type in {"int","float"}
    score_fn: Callable[[Dict[str, float]], float],
    pop_size: int = 25,
    generations: int = 12,
    elite: int = 5,
    mutation_rate: float = 0.25,
    seed: int = 7
) -> pd.DataFrame:
    random.seed(seed)

    def sample_one():
        p={}
        for k,(lo,hi,t) in space.items():
            if t=="int":
                p[k]=random.randint(int(lo), int(hi))
            else:
                p[k]=random.uniform(float(lo), float(hi))
        return p

    def mutate(p):
        q=p.copy()
        for k,(lo,hi,t) in space.items():
            if random.random() < mutation_rate:
                if t=="int":
                    q[k]=random.randint(int(lo), int(hi))
                else:
                    # gaussian-ish nudge
                    span=float(hi)-float(lo)
                    q[k]=min(float(hi), max(float(lo), float(q[k]) + random.uniform(-0.15, 0.15)*span))
        return q

    def crossover(a,b):
        c={}
        for k in space.keys():
            c[k] = a[k] if random.random() < 0.5 else b[k]
        return c

    pop=[sample_one() for _ in range(pop_size)]
    history=[]

    for g in range(generations):
        scored=[]
        for p in pop:
            strat=strategy_factory(p)
            sizer=sizer_factory(p)
            res=bt.run(data, strat, symbol, sizer)
            score=score_fn(res.metrics)
            scored.append((score,p,res.metrics))
        scored.sort(key=lambda x: x[0], reverse=True)

        # record best
        best_score, best_p, best_m = scored[0]
        history.append({**best_p, **best_m, "score": best_score, "gen": g})

        # elite selection
        elites=[p for _,p,_ in scored[:elite]]

        # breed next gen
        next_pop=elites[:]
        while len(next_pop) < pop_size:
            a,b = random.sample(elites, 2)
            child = crossover(a,b)
            child = mutate(child)
            next_pop.append(child)
        pop=next_pop

    return pd.DataFrame(history).sort_values(["score","gen"], ascending=[False, False])

# -----------------------------
# UI / Visualization helpers
# -----------------------------

def plot_equity_plotly(eq: pd.DataFrame, title: str = "Equity Curve"):
    try:
        import plotly.graph_objects as go
    except Exception:
        print("Plotly not installed. pip install plotly")
        return
    fig = go.Figure()
    fig.add_trace(go.Scatter(x=eq.index, y=eq["equity"], mode="lines", name="Equity"))
    fig.update_layout(title=title, xaxis_title="Time", yaxis_title="Equity")
    fig.show()

# -----------------------------
# CLI
# -----------------------------

def print_metrics(m: Dict[str, float]):
    keys = [
        "start_equity","end_equity","total_return","cagr","sharpe","sortino",
        "max_drawdown","exposure","avg_hold_hours","approx_win_rate",
        "num_fills","total_commission","total_slippage_cost",
    ]
    for k in keys:
        if k in m:
            v = m[k]
            if isinstance(v, float) and k not in ("start_equity","end_equity","num_fills","total_commission","total_slippage_cost"):
                print(f"{k:>20}: {v: .6f}")
            else:
                print(f"{k:>20}: {v}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", required=True)
    ap.add_argument("--symbol", default="ASSET")
    ap.add_argument("--start", default=None)
    ap.add_argument("--end", default=None)
    ap.add_argument("--resample", default=None, help="e.g. '1H' or '1D'")

    ap.add_argument("--strategy", choices=["trend","meanrev"], default="trend")
    ap.add_argument("--initial-cash", type=float, default=100000.0)

    # execution realism
    ap.add_argument("--slippage-bps", type=float, default=1.0)
    ap.add_argument("--commission-bps", type=float, default=0.5)
    ap.add_argument("--commission-per-trade", type=float, default=0.0)

    ap.add_argument("--max-leverage", type=float, default=1.0)
    ap.add_argument("--max-pos-pct-equity", type=float, default=1.0)
    ap.add_argument("--allow-short", action="store_true", default=False)

    # risk halt
    ap.add_argument("--max-dd-halt", type=float, default=None, help="e.g. -0.2")

    # trend params
    ap.add_argument("--fast", type=int, default=12)
    ap.add_argument("--slow", type=int, default=26)
    ap.add_argument("--atr-n", type=int, default=14)
    ap.add_argument("--atr-stop", type=float, default=2.5)
    ap.add_argument("--tp-r", type=float, default=None)

    # mean reversion params
    ap.add_argument("--rsi-n", type=int, default=14)
    ap.add_argument("--buy-below", type=float, default=30)
    ap.add_argument("--sell-above", type=float, default=70)
    ap.add_argument("--exit-at", type=float, default=50)
    ap.add_argument("--stop-pct", type=float, default=0.03)

    # sizing
    ap.add_argument("--sizer", choices=["risk_atr","fixed_pct"], default="risk_atr")
    ap.add_argument("--risk-per-trade", type=float, default=0.01)
    ap.add_argument("--fixed-pct", type=float, default=1.0)

    # optimization
    ap.add_argument("--opt", choices=["none","grid","ga"], default="none")
    ap.add_argument("--plot", action="store_true", default=False)

    ap.add_argument("--out-equity", default="equity_curve.csv")
    ap.add_argument("--out-fills", default="fills.csv")

    args = ap.parse_args()

    provider = CSVDataProvider(args.csv)
    df = provider.get(args.symbol, args.start, args.end)
    df = validate_ohlcv(df)
    if args.resample:
        df = resample_ohlcv(df, args.resample)

    exec_cfg = ExecutionConfig(
        commission_per_trade=args.commission_per_trade,
        commission_bps=args.commission_bps,
        slippage_bps=args.slippage_bps,
        max_leverage=args.max_leverage,
        max_pos_pct_equity=args.max_pos_pct_equity,
        allow_short=args.allow_short
    )
    engine = ExecutionEngine(exec_cfg)
    bt = Backtester(engine, BacktestConfig(initial_cash=args.initial_cash, max_drawdown_halt=args.max_dd_halt))

    def strat_factory(p: Dict[str, Any]) -> Strategy:
        if args.strategy == "trend":
            return TrendFollowingEMA(
                fast=int(p.get("fast", args.fast)),
                slow=int(p.get("slow", args.slow)),
                atr_n=int(p.get("atr_n", args.atr_n)),
                atr_stop=float(p.get("atr_stop", args.atr_stop)),
                tp_r=p.get("tp_r", args.tp_r),
            )
        else:
            return MeanReversionRSI(
                rsi_n=int(p.get("rsi_n", args.rsi_n)),
                buy_below=float(p.get("buy_below", args.buy_below)),
                sell_above=float(p.get("sell_above", args.sell_above)),
                exit_at=float(p.get("exit_at", args.exit_at)),
                stop_pct=float(p.get("stop_pct", args.stop_pct)),
            )

    def sizer_factory(p: Dict[str, Any]) -> Callable:
        if args.sizer == "fixed_pct":
            return sizer_fixed_notional(float(p.get("fixed_pct", args.fixed_pct)))
        # risk_atr
        return sizer_risk_atr(float(p.get("risk_per_trade", args.risk_per_trade)), float(p.get("atr_stop", args.atr_stop)))

    # scoring: prefer Sharpe but penalize huge drawdowns
    def score_fn(m: Dict[str, float]) -> float:
        sharpe = m.get("sharpe", float("-inf"))
        mdd = m.get("max_drawdown", -1.0)
        if np.isnan(sharpe):
            sharpe = -999.0
        # drawdown is negative; penalize if worse than -25%
        penalty = 0.0
        if mdd < -0.25:
            penalty = abs(mdd + 0.25) * 10.0
        return sharpe - penalty

    if args.opt == "none":
        strat = strat_factory({})
        sizer = sizer_factory({})
        res = bt.run(df, strat, args.symbol, sizer)
        print("\n--- METRICS ---")
        print_metrics(res.metrics)
        res.equity.to_csv(args.out_equity)
        res.fills.to_csv(args.out_fills, index=False)
        print(f"\nSaved: {args.out_equity}, {args.out_fills}")
        if args.plot:
            plot_equity_plotly(res.equity, title=f"{args.strategy} equity")

    elif args.opt == "grid":
        # reasonable starter grids
        if args.strategy == "trend":
            grid = {
                "fast": [8, 12, 16],
                "slow": [26, 40, 60],
                "atr_stop": [2.0, 2.5, 3.0],
                "risk_per_trade": [0.005, 0.01, 0.02] if args.sizer == "risk_atr" else [args.risk_per_trade],
            }
        else:
            grid = {
                "buy_below": [25, 30, 35],
                "sell_above": [65, 70, 75],
                "stop_pct": [0.02, 0.03, 0.04],
            }
        top = grid_search(bt, df, args.symbol, strat_factory, sizer_factory, grid, score_fn, top_k=10)
        print(top.to_string(index=False))

    elif args.opt == "ga":
        # genetic space
        if args.strategy == "trend":
            space = {
                "fast": (5, 25, "int"),
                "slow": (26, 100, "int"),
                "atr_stop": (1.5, 4.0, "float"),
                "risk_per_trade": (0.002, 0.03, "float"),
            }
        else:
            space = {
                "buy_below": (15, 40, "float"),
                "sell_above": (60, 85, "float"),
                "stop_pct": (0.01, 0.06, "float"),
            }
        hist = genetic_optimize(bt, df, args.symbol, strat_factory, sizer_factory, space, score_fn)
        print(hist.head(20).to_string(index=False))

if __name__ == "__main__":
    main()