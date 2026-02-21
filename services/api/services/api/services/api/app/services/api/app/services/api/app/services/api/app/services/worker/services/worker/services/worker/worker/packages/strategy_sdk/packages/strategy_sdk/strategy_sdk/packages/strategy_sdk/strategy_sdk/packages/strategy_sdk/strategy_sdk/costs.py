from dataclasses import dataclass
import math

@dataclass
class CostModel:
    spread_bps: float = 2.0     # half-spread is modeled at execution time
    impact_k: float = 0.10      # impact coefficient
    impact_alpha: float = 0.5   # sqrt law default
    fee_bps: float = 0.5        # commissions / taker fees

    def estimate(self, price: float, qty: float, adv_qty: float) -> tuple[float, float]:
        """
        Returns (slippage_dollars, fee_dollars).
        adv_qty: average daily volume (in shares/coins) proxy; if unknown, set to large value.
        """
        notional = abs(price * qty)

        # spread cost (half-spread on each side) -> convert bps to dollars
        spread_cost = notional * (self.spread_bps / 10000.0)

        # impact cost using participation: (qty/adv)^alpha
        adv = max(adv_qty, 1e-9)
        participation = abs(qty) / adv
        impact_cost = notional * self.impact_k * (participation ** self.impact_alpha)

        fees = notional * (self.fee_bps / 10000.0)
        return spread_cost + impact_cost, fees
