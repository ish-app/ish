FROM python:3.11-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# install the SDK package
COPY ../../packages/strategy_sdk /packages/strategy_sdk
RUN pip install -e /packages/strategy_sdk

COPY worker ./worker
CMD ["python", "-m", "worker.run_backtest"]
