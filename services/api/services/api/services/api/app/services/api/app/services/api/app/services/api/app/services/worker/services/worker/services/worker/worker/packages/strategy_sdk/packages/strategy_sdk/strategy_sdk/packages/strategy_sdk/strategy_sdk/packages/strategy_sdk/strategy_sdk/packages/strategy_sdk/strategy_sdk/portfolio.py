from dataclasses import dataclass, field
from typing import Dict, Optional
import pandas as pd

@dataclass
class Position:
    qty: float = 0.0
    avg_price: float = 0.0
    realized_pnl: float = 0.0
    entry_ts: Optional[pd.Timestamp] = None

@dataclass
class Portfolio:
    cash: float
    positions: Dict[str, Position] = field(default_factory=dict)
    equity: float = 0.0
    peak_equity: float = 0.0
    max_drawdown: float = 0.0

    def pos(self, symbol: str) -> Position:
        if symbol not in self.positions:
            self.positions[symbol] = Position()
        return self.positions[symbol]

    def mark(self, prices: Dict[str, float]):
        mv = 0.0
        for sym, p in self.positions.items():
            px = prices.get(sym)
            if px is not None:
                mv += p.qty * px
        self.equity = self.cash + mv
        if self.peak_equity == 0:
            self.peak_equity = self.equity
        self.peak_equity = max(self.peak_equity, self.equity)
        dd = (self.equity / self.peak_equity) - 1.0 if self.peak_equity > 0 else 0.0
        self.max_drawdown = min(self.max_drawdown, dd)

    def apply_fill(self, symbol: str, side: str, qty: float, price: float, fees: float, ts):
        p = self.pos(symbol)
        side = side.upper()

        if side == "BUY":
            # cover short then add long
            if p.qty < 0:
                cover = min(qty, abs(p.qty))
                p.realized_pnl += (p.avg_price - price) * cover
                p.qty += cover
                self.cash -= price * cover + fees * (cover / qty)
                if p.qty == 0:
                    p.avg_price = 0.0
                    p.entry_ts = None
                rem = qty - cover
                if rem > 0:
                    if p.qty == 0:
                        p.entry_ts = ts
                    new_qty = p.qty + rem
                    p.avg_price = (p.avg_price * p.qty + price * rem) / new_qty if new_qty != 0 else price
                    p.qty = new_qty
                    self.cash -= price * rem + fees * (rem / qty)
            else:
                if p.qty == 0:
                    p.entry_ts = ts
                new_qty = p.qty + qty
                p.avg_price = (p.avg_price * p.qty + price * qty) / new_qty if new_qty != 0 else price
                p.qty = new_qty
                self.cash -= price * qty + fees

        elif side == "SELL":
            # sell long then add short
            if p.qty > 0:
                sell = min(qty, p.qty)
                p.realized_pnl += (price - p.avg_price) * sell
                p.qty -= sell
                self.cash += price * sell - fees * (sell / qty)
                if p.qty == 0:
                    p.avg_price = 0.0
                    p.entry_ts = None
                rem = qty - sell
                if rem > 0:
                    if p.qty == 0:
                        p.entry_ts = ts
                        p.avg_price = price
                        p.qty = -rem
                    else:
                        tot = abs(p.qty) + rem
                        p.avg_price = (p.avg_price * abs(p.qty) + price * rem) / tot
                        p.qty -= rem
                    self.cash += price * rem - fees * (rem / qty)
            else:
                if p.qty == 0:
                    p.entry_ts = ts
                tot = abs(p.qty) + qty
                p.avg_price = (p.avg_price * abs(p.qty) + price * qty) / tot if tot != 0 else price
                p.qty -= qty
                self.cash += price * qty - fees
