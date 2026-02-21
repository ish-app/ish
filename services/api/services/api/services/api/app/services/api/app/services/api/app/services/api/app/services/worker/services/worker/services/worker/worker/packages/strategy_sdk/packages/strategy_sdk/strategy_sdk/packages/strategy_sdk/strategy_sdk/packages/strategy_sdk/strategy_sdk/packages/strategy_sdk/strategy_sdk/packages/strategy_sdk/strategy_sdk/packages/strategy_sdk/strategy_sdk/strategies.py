from dataclasses import dataclass
from typing import Dict, Any, List, Optional
import numpy as np
import pandas as pd
from .types import Order
from .indicators import ema, atr, rsi

class Strategy:
    def prepare(self, frames: Dict[str, pd.DataFrame]) -> Dict[str, pd.DataFrame]:
        return frames

    def on_bar(self, ts: pd.Timestamp, bars: Dict[str, pd.Series], ctx: Dict[str, Any]) -> List[Order]:
        return []

@dataclass
class TrendEMAAtr(Strategy):
    fast: int = 12
    slow: int = 26
    atr_n: int = 14
    atr_mult: float = 2.5
    symbol: str = "SPY"   # primary symbol (can add portfolio logic later)

    def prepare(self, frames):
        d = dict(frames)
        df = d[self.symbol].copy()
        df["ema_fast"] = ema(df["close"], self.fast)
        df["ema_slow"] = ema(df["close"], self.slow)
        df["atr"] = atr(df["high"], df["low"], df["close"], self.atr_n)
        gt = df["ema_fast"] > df["ema_slow"]
        lt = df["ema_fast"] < df["ema_slow"]
        df["sig"] = 0
        df.loc[gt & (~gt.shift(1).fillna(False)), "sig"] = 1
        df.loc[lt & (~lt.shift(1).fillna(False)), "sig"] = -1
        d[self.symbol] = df
        return d

    def on_bar(self, ts, bars, ctx):
        orders = []
        sym = self.symbol
        row = bars.get(sym)
        if row is None:
            return orders

        port = ctx["portfolio"]
        pos = port.pos(sym).qty
        px = float(row["close"])
        sig = int(row.get("sig", 0))
        atrv = float(row.get("atr", np.nan))

        stop = ctx.get("stop_price")
        low, high = float(row["low"]), float(row["high"])

        # stops
        if pos > 0 and stop is not None and low <= stop:
            orders.append(Order(ts, sym, "SELL", abs(pos), "MKT", tag="STOP_LONG"))
            ctx["stop_price"] = None
            return orders
        if pos < 0 and stop is not None and high >= stop:
            orders.append(Order(ts, sym, "BUY", abs(pos), "MKT", tag="STOP_SHORT"))
            ctx["stop_price"] = None
            return orders

        qty = ctx["sizer"](sym, row, ctx)

        if sig == 1 and pos <= 0:
            orders.append(Order(ts, sym, "BUY", qty, "MKT", tag="ENTER_LONG"))
            if not np.isnan(atrv) and atrv > 0:
                ctx["stop_price"] = px - self.atr_mult * atrv

        if sig == -1 and pos >= 0:
            orders.append(Order(ts, sym, "SELL", qty, "MKT", tag="ENTER_SHORT"))
            if not np.isnan(atrv) and atrv > 0:
                ctx["stop_price"] = px + self.atr_mult * atrv

        return orders

@dataclass
class MeanRevRSI(Strategy):
    rsi_n: int = 14
    buy_below: float = 30
    sell_above: float = 70
    exit_at: float = 50
    stop_pct: float = 0.03
    symbol: str = "SPY"

    def prepare(self, frames):
        d = dict(frames)
        df = d[self.symbol].copy()
        df["rsi"] = rsi(df["close"], self.rsi_n)
        d[self.symbol] = df
        return d

    def on_bar(self, ts, bars, ctx):
        orders=[]
        sym=self.symbol
        row=bars.get(sym)
        if row is None:
            return orders
        port=ctx["portfolio"]
        pos=port.pos(sym).qty
        px=float(row["close"])
        rv=float(row.get("rsi", np.nan))

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

        qty = ctx["sizer"](sym, row, ctx)

        if pos==0 and not np.isnan(rv):
            if rv <= self.buy_below:
                orders.append(Order(ts,sym,"BUY",qty,"MKT",tag="MR_LONG"))
                ctx["stop_price"]=px*(1-self.stop_pct)
            elif rv >= self.sell_above:
                orders.append(Order(ts,sym,"SELL",qty,"MKT",tag="MR_SHORT"))
                ctx["stop_price"]=px*(1+self.stop_pct)

        if pos>0 and rv >= self.exit_at:
            orders.append(Order(ts,sym,"SELL",abs(pos),"MKT",tag="MR_EXIT_LONG"))
            ctx["stop_price"]=None
        if pos<0 and rv <= self.exit_at:
            orders.append(Order(ts,sym,"BUY",abs(pos),"MKT",tag="MR_EXIT_SHORT"))
            ctx["stop_price"]=None

        return orders
