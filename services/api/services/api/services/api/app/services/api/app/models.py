from sqlalchemy import String, DateTime, Float, Integer, JSON
from sqlalchemy.orm import Mapped, mapped_column
from datetime import datetime
from .db import Base

class BacktestRun(Base):
    __tablename__ = "backtest_runs"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    created_at: Mapped[datetime] = mapped_column(DateTime, default=datetime.utcnow)

    name: Mapped[str] = mapped_column(String(120), default="run")
    strategy: Mapped[str] = mapped_column(String(80))
    params: Mapped[dict] = mapped_column(JSON, default=dict)

    start_equity: Mapped[float] = mapped_column(Float, default=0.0)
    end_equity: Mapped[float] = mapped_column(Float, default=0.0)
    sharpe: Mapped[float] = mapped_column(Float, default=0.0)
    max_drawdown: Mapped[float] = mapped_column(Float, default=0.0)
    total_return: Mapped[float] = mapped_column(Float, default=0.0)
