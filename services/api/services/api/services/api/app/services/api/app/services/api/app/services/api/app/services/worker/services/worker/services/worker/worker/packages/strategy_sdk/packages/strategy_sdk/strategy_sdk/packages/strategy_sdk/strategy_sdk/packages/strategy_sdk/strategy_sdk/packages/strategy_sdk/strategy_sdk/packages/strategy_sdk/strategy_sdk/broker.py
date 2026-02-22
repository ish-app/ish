from dataclasses import dataclass
from typing import Dict, Optional
import pandas as pd
from .types import Order, Fill
from .costs import CostModel

@dataclass
class BrokerConstraints:
    max_leverage: float = 1.0
    max_pos_pct_equity: float = 1.0

class PaperBroker:
    def __init__(self, costs: CostModel, allow_short: bool = True, constraints: Optional[BrokerConstraints] = None):
        self.costs = costs
        self.allow_short = allow_short
        self.constraints = constraints or BrokerConstraints()

    def _apply_constraints(self, equity: float, current_qty: float, price: float, desired_delta: float) -> float:
        if equity <= 0:
            return 0.0
        cap = min(self.constraints.max_leverage * equity, self.constraints.max_pos_pct_equity * equity)
        proposed_qty = current_qty + desired_delta
        proposed_exposure = abs(proposed_qty * price)
        if proposed_exposure <= cap + 1e-9:
            return desired_delta
        max_qty = cap / max(price, 1e-12)
        allowed = max_qty - abs(current_qty)
        if allowed <= 0:
            return 0.0
        return (1 if desired_delta > 0 else -1) * min(abs(desired_delta), allowed)

    def fill_bar(self, order: Order, bar: pd.Series, equity: float, current_qty: float, adv_qty: float = 1e9) -> Optional[Fill]:
        side = order.side.upper()
        if side not in ("BUY","SELL"):
            return None
        if order.qty <= 0:
            return None

        if (not self.allow_short) and side == "SELL" and current_qty <= 0:
            return None

        low, high, close = float(bar["low"]), float(bar["high"]), float(bar["close"])
        trigger = None

        ot = order.order_type.upper()
        if ot == "MKT":
            trigger = close
        elif ot == "LMT":
            if order.limit_price is None:
                return None
            lp = float(order.limit_price)
            if side == "BUY" and low <= lp:
                trigger = lp
            if side == "SELL" and high >= lp:
                trigger = lp
        elif ot == "STOP":
            if order.stop_price is None:
                return None
            sp = float(order.stop_price)
            if side == "BUY" and high >= sp:
                trigger = sp
            if side == "SELL" and low <= sp:
                trigger = sp
        else:
            return None

        if trigger is None:
            return None

        desired_delta = order.qty if side == "BUY" else -order.qty
        delta = self._apply_constraints(equity, current_qty, close, desired_delta)
        if delta == 0:
            return None

        qty = abs(delta)

        slip_dollars, fees = self.costs.estimate(price=trigger, qty=qty, adv_qty=adv_qty)
        # apply slippage directionally by adjusting price
        # (simple: convert slip dollars to per-unit and worsen price)
        slip_per_unit = slip_dollars / max(qty, 1e-12)
        fill_price = trigger + slip_per_unit if side == "BUY" else trigger - slip_per_unit

        return Fill(
            ts=order.ts,
            symbol=order.symbol,
            side=side,
            qty=qty,
            price=fill_price,
            fees=fees,
            slip=slip_dollars,
            tag=order.tag
        )
