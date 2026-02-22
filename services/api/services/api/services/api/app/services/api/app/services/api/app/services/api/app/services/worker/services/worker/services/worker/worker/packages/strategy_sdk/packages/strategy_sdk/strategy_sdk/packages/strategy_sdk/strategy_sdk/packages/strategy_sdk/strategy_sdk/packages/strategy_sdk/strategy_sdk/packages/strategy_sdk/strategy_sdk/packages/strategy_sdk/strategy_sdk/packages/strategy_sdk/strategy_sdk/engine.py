from dataclasses import dataclass
from typing import Dict, Any, List, Optional
import pandas as pd
import numpy as np
import math

from .types import BacktestArtifacts, MarketDataBundle, Order
from .portfolio import Portfolio
from .broker import PaperBroker, BrokerConstraints

@dataclass
class EngineConfig:
    initial_cash: float = 100_000.0
    max_leverage: float = 1.0
    max_pos_pct_equity: float = 1.0
    max_drawdown_halt: float = -0.25

@dataclass
class StrategyContext:
    portfolio: Portfolio
    sizer: Any

class BacktestEngine:
    def __init__(self, cfg: EngineConfig, broker: PaperBroker, strategy):
        self.cfg = cfg
        self.broker = broker
        self.strategy = strategy
        # wire constraints
        self.broker.constraints = BrokerConstraints(
            max_leverage=cfg.max_leverage,
            max_pos_pct_equity=cfg.max_pos_pct_equity
        )

    def run(self, bundle: MarketDataBundle) -> BacktestArtifacts:
        frames = {k: v.copy() for k, v in bundle.frames.items()}
        # standardize cols
        for sym, df in frames.items():
            df.columns = [c.lower().strip() for c in df.columns]
            df = df.dropna(subset=["open","high","low","close"])
            frames[sym] = df.sort_index()

        frames = self.strategy.prepare(frames)

        # unified timeline = union of all timestamps
        all_ts = sorted(set().union(*[set(df.index) for df in frames.values()]))

        port = Portfolio(cash=self.cfg.initial_cash)
        fills_rows = []
        eq_rows = []

        # Strategy state
        ctx: Dict[str, Any] = {
            "portfolio": port,
            "sizer": self._risk_sizer(0.01),  # default risk sizing; replace as needed
            "stop_price": None,              # baseline per-strategy (single symbol) state
        }

        halted = False

        for ts in all_ts:
            # build bars for symbols that have this ts
            bars = {}
            prices = {}
            for sym, df in frames.items():
                if ts in df.index:
                    row = df.loc[ts]
                    bars[sym] = row
                    prices[sym] = float(row["close"])

            # mark portfolio (use last known prices for held assets if missing this ts)
            # quick approach: carry forward last close from previous equity row
            if eq_rows:
                last_prices = eq_rows[-1]["prices"]
                for sym, p in last_prices.items():
                    prices.setdefault(sym, p)

            port.mark(prices)

            if port.max_drawdown <= self.cfg.max_drawdown_halt:
                halted = True

            if halted:
                # flatten all positions
                for sym, pos in list(port.positions.items()):
                    if pos.qty != 0 and sym in bars:
                        side = "SELL" if pos.qty > 0 else "BUY"
                        o = Order(ts, sym, side, abs(pos.qty), "MKT", tag="HALT_FLATTEN")
                        fill = self.broker.fill_bar(o, bars[sym], port.equity, pos.qty)
                        if fill:
                            port.apply_fill(fill.symbol, fill.side, fill.qty, fill.price, fill.fees, fill.ts)
                            fills_rows.append(fill.__dict__)
                port.mark(prices)
                eq_rows.append(self._eq_row(ts, port, prices))
                continue

            # strategy emits orders
            orders: List[Order] = self.strategy.on_bar(ts, bars, ctx) or []

            # execute (bar-based)
            for o in orders:
                if o.symbol not in bars:
                    continue
                cur_qty = port.pos(o.symbol).qty
                adv_qty = float(bars[o.symbol].get("volume", 1e9)) * 20.0  # crude proxy; replace with rolling ADV
                fill = self.broker.fill_bar(o, bars[o.symbol], port.equity, cur_qty, adv_qty=adv_qty)
                if fill:
                    port.apply_fill(fill.symbol, fill.side, fill.qty, fill.price, fill.fees, fill.ts)
                    fills_rows.append(fill.__dict__)

            port.mark(prices)
            eq_rows.append(self._eq_row(ts, port, prices))

        equity_curve = pd.DataFrame(eq_rows).set_index("ts")
        fills = pd.DataFrame(fills_rows) if fills_rows else pd.DataFrame(columns=["ts","symbol","side","qty","price","fees","slip","tag"])
        metrics = self._metrics(equity_curve)
        return BacktestArtifacts(equity_curve=equity_curve, fills=fills, metrics=metrics)

    def _eq_row(self, ts, port: Portfolio, prices: Dict[str, float]) -> Dict[str, Any]:
        # keep prices snapshot for carry-forward
        return {
            "ts": ts,
            "cash": port.cash,
            "equity": port.equity,
            "max_drawdown": port.max_drawdown,
            "prices": dict(prices),
        }

    def _metrics(self, eq: pd.DataFrame) -> Dict[str, float]:
        equity = eq["equity"].astype(float)
        rets = equity.pct_change().fillna(0.0)

        # annualization heuristic
        ann = 252.0
        if len(eq.index) > 2:
            dt = (eq.index[1:] - eq.index[:-1]).astype("timedelta64[s]").astype(float).mean()
            if dt > 0:
                ann = (252 * 6.5 * 3600) / dt

        mu = rets.mean()
        sd = rets.std(ddof=0)
        sharpe = float((mu / sd) * math.sqrt(ann)) if sd > 0 else float("nan")

        run_max = equity.cummax()
        dd = (equity / run_max) - 1.0
        max_dd = float(dd.min())

        total_return = float((equity.iloc[-1] / equity.iloc[0]) - 1.0) if equity.iloc[0] != 0 else float("nan")

        return {
            "start_equity": float(equity.iloc[0]),
            "end_equity": float(equity.iloc[-1]),
            "total_return": total_return,
            "sharpe": sharpe,
            "max_drawdown": max_dd,
        }

    def _risk_sizer(self, risk_per_trade: float):
        """
        Default sizer: invest a fixed % of equity notionally per trade (MVP).
        Swap this with ATR-risk sizing per symbol later.
        """
        def _s(symbol: str, row: pd.Series, ctx: Dict[str, Any]) -> float:
            port: Portfolio = ctx["portfolio"]
            px = float(row["close"])
            if px <= 0 or port.equity <= 0:
                return 0.0
            notional = risk_per_trade * port.equity * 10.0  # 10x "risk budget" to make it trade; tune later
            return max(notional / px, 0.0)
        return _s
