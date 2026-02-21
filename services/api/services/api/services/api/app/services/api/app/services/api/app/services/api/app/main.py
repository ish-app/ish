from fastapi import FastAPI, Depends, HTTPException
from sqlalchemy.orm import Session
from .db import SessionLocal, init_db
from . import models, schemas
from datetime import datetime

app = FastAPI(title="WallStreetAlg API")

@app.on_event("startup")
def _startup():
    init_db()

def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()

@app.get("/health", response_model=schemas.HealthOut)
def health():
    return {"ok": True}

@app.post("/backtests", response_model=schemas.BacktestRunOut)
def create_backtest(req: schemas.BacktestRequest, db: Session = Depends(get_db)):
    """
    MVP: create a DB record. Worker is run manually for now.
    Next: enqueue to Redis (RQ/Celery) and stream progress.
    """
    run = models.BacktestRun(
        created_at=datetime.utcnow(),
        name=req.name,
        strategy=req.strategy,
        params=req.params,
    )
    db.add(run)
    db.commit()
    db.refresh(run)
    return schemas.BacktestRunOut(
        id=run.id,
        name=run.name,
        strategy=run.strategy,
        params=run.params,
        start_equity=run.start_equity,
        end_equity=run.end_equity,
        sharpe=run.sharpe,
        max_drawdown=run.max_drawdown,
        total_return=run.total_return,
    )

@app.get("/backtests/{run_id}", response_model=schemas.BacktestRunOut)
def get_backtest(run_id: int, db: Session = Depends(get_db)):
    run = db.get(models.BacktestRun, run_id)
    if not run:
        raise HTTPException(404, "Not found")
    return schemas.BacktestRunOut(
        id=run.id,
        name=run.name,
        strategy=run.strategy,
        params=run.params,
        start_equity=run.start_equity,
        end_equity=run.end_equity,
        sharpe=run.sharpe,
        max_drawdown=run.max_drawdown,
        total_return=run.total_return,
    )
