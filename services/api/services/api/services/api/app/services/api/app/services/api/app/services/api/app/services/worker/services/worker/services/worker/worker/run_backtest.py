import os
import json
import pandas as pd
from sqlalchemy import create_engine, select, update
from sqlalchemy.orm import Session

from strategy_sdk.engine import EngineConfig, BacktestEngine
from strategy_sdk.broker import PaperBroker
from strategy_sdk.costs import CostModel
from strategy_sdk.strategies import TrendEMAAtr, MeanRevRSI
from strategy_sdk.types import MarketDataBundle

DATABASE_URL = os.getenv("DATABASE_URL", "postgresql+psycopg://app:app@db:5432/wallstreet")

# --- minimal import of API model (duplicated path kept tiny for MVP) ---
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column
from sqlalchemy import Integer, String, DateTime, Float, JSON
from datetime import datetime

class Base(DeclarativeBase): pass

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

def load_csv(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)
    df.columns = [c.lower().strip() for c in df.columns]
    ts = pd.to_datetime(df["timestamp"], utc=True, errors="coerce")
    df = df.drop(columns=["timestamp"])
    df.index = ts
    df = df.sort_index()
    for c in ["open","high","low","close","volume"]:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")
    df = df.dropna(subset=["open","high","low","close"])
    return df

def main():
    engine = create_engine(DATABASE_URL, pool_pre_ping=True)
    Base.metadata.create_all(bind=engine)

    # Pick latest run to execute (MVP)
    with Session(engine) as s:
        run = s.scalars(select(BacktestRun).order_by(BacktestRun.id.desc())).first()
        if not run:
            print("No backtest run found. Create one via API first.")
            return

        params = run.params or {}
        print(f"Executing run id={run.id} strategy={run.strategy} params={json.dumps(params)}")

        # For MVP: pass csv paths via ENV as JSON (or hardcode)
        # Example:
        #   export CSV_BY_SYMBOL='{"SPY":"/data/SPY.csv","BTCUSD":"/data/BTC.csv"}'
        csv_by_symbol = json.loads(os.getenv("CSV_BY_SYMBOL", "{}"))
        if not csv_by_symbol:
            raise SystemExit("Set CSV_BY_SYMBOL env var, e.g. '{\"SPY\":\"/data/SPY.csv\"}'")

        bundle = MarketDataBundle()
        for sym, p in csv_by_symbol.items():
            bundle.frames[sym] = load_csv(p)

        # Costs + broker
        costs = CostModel(
            spread_bps=float(params.get("spread_bps", 2.0)),
            impact_k=float(params.get("impact_k", 0.10)),
            impact_alpha=float(params.get("impact_alpha", 0.5)),
            fee_bps=float(params.get("fee_bps", 0.5)),
        )
        broker = PaperBroker(costs=costs, allow_short=bool(params.get("allow_short", True)))

        # Strategy baseline (kept)
        if run.strategy == "trend_ema_atr":
            strat = TrendEMAAtr(
                fast=int(params.get("fast", 12)),
                slow=int(params.get("slow", 26)),
                atr_n=int(params.get("atr_n", 14)),
                atr_mult=float(params.get("atr_mult", 2.5)),
            )
        elif run.strategy == "meanrev_rsi":
            strat = MeanRevRSI(
                rsi_n=int(params.get("rsi_n", 14)),
                buy_below=float(params.get("buy_below", 30)),
                sell_above=float(params.get("sell_above", 70)),
                exit_at=float(params.get("exit_at", 50)),
                stop_pct=float(params.get("stop_pct", 0.03)),
            )
        else:
            raise SystemExit(f"Unknown strategy: {run.strategy}")

        cfg = EngineConfig(
            initial_cash=float(params.get("initial_cash", 100_000.0)),
            max_leverage=float(params.get("max_leverage", 1.0)),
            max_pos_pct_equity=float(params.get("max_pos_pct_equity", 1.0)),
            max_drawdown_halt=float(params.get("max_drawdown_halt", -0.25)),
        )

        eng = BacktestEngine(cfg=cfg, broker=broker, strategy=strat)
        result = eng.run(bundle)

        # write metrics back
        m = result.metrics
        s.execute(
            update(BacktestRun)
            .where(BacktestRun.id == run.id)
            .values(
                start_equity=m["start_equity"],
                end_equity=m["end_equity"],
                sharpe=m["sharpe"],
                max_drawdown=m["max_drawdown"],
                total_return=m["total_return"],
            )
        )
        s.commit()

        # save artifacts
        out_dir = f"/app/worker/out/run_{run.id}"
        os.makedirs(out_dir, exist_ok=True)
        result.equity_curve.to_csv(f"{out_dir}/equity_curve.csv")
        result.fills.to_csv(f"{out_dir}/fills.csv", index=False)
        print(f"Saved artifacts to {out_dir}")
        print(m)

if __name__ == "__main__":
    main()
