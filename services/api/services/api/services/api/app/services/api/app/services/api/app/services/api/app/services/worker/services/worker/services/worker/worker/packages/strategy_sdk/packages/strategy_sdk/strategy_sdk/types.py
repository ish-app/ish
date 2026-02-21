from dataclasses import dataclass, field
from typing import Dict, Optional, List
import pandas as pd

@dataclass
class Order:
    ts: pd.Timestamp
    symbol: str
    side: str         # BUY/SELL
    qty: float
    order_type: str = "MKT"   # MKT/LMT/STOP (engine supports bar-based)
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
    fees: float
    slip: float
    tag: str = ""

@dataclass
class MarketDataBundle:
    frames: Dict[str, pd.DataFrame] = field(default_factory=dict)

@dataclass
class BacktestArtifacts:
    equity_curve: pd.DataFrame
    fills: pd.DataFrame
    metrics: Dict[str, float]
