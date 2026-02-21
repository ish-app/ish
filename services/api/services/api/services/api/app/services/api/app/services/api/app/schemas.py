from pydantic import BaseModel, Field
from typing import Dict, Any, Optional

class BacktestRequest(BaseModel):
    name: str = "run"
    strategy: str = Field(..., description="trend_ema_atr or meanrev_rsi")
    params: Dict[str, Any] = {}
    # For now: CSV paths per symbol on local FS inside worker container
    csv_by_symbol: Dict[str, str] = Field(..., description="e.g. {'SPY':'/data/SPY.csv','BTCUSD':'/data/BTC.csv'}")

class BacktestRunOut(BaseModel):
    id: int
    name: str
    strategy: str
    params: Dict[str, Any]
    start_equity: float
    end_equity: float
    sharpe: float
    max_drawdown: float
    total_return: float

class HealthOut(BaseModel):
    ok: bool = True
