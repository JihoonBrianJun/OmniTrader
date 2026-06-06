# OmniTrader

Generic, multi-exchange market-listener / trader framework in C++23, generalized
from `kis_cpp`. The exchange-agnostic core lives under the `Omni::` namespace;
exchange adapters live under `Omni::<Exchange>::` (`Omni::Binance::`, `Omni::KIS::`)
and are extendible to new exchanges.

Two executables:

- **listener** — connects to an exchange's market data (and, where applicable,
  user-data) feeds, normalizes everything into a common message model, CSV-logs
  market data, and broadcasts normalized JSON to traders over an internal TCP
  server.
- **trader** — connects to the listener's TCP server, consumes normalized
  market/execution/position data, runs a strategy, and places/amends/cancels via
  an exchange order gateway.

## Architecture

```
                 ┌──────────── listener ────────────┐
exchange feeds → │ IExchangeListener adapter         │ → normalized events
                 │   (Binance: market WS + user WS   │      │
                 │    + REST snapshot resync;        │      ▼
                 │    KIS: single market WS)         │   TcpServer ──┐
                 └───────────────────────────────────┘               │ TCP
                 ┌──────────── trader ──────────────┐                 │
                 │ OrderHandler ← TcpClient feed ─────┼─────────────────┘
                 │   Pricer → Strategy.make_decision  │
                 │   → IOrderGateway                  │ → exchange orders
                 │   (Binance: WS-API + REST fallback;
                 │    KIS: REST)                     │
                 └───────────────────────────────────┘
```

The trader is structured as **pricer → strategy → order_handler** (mirroring the
`orderbook-backtest` project): [pricer/](src/trader/pricer/) computes `PriceInfo`
from the live L1, [strategy/](src/trader/strategy/) (`BaseStrategy`/`GeuantStrategy`
with `make_decision`) decides orders in tick/lot units, and
[order_handler/](src/trader/order_handler/) maps those to the `IOrderGateway` and
tracks per-product position/outstanding state. CLI parsing uses the `argparse`
library.

Key generic seams: `Omni::Listener::IExchangeListener`
([exchange_listener.hpp](src/market_listener/exchange/exchange_listener.hpp)) and
`Omni::OrderGateway::IOrderGateway`
([order_gateway.hpp](src/trader/order_gateway/order_gateway.hpp)). The normalized
wire model is in [market_msg_types.hpp](src/common/market_msg_types.hpp).

## Build

```bash
./build.sh    # conan install + cmake + build  → build/listener, build/trader
```

Requires conan and cmake. Dependencies (boost, fmt, quill, nlohmann_json, glaze,
libcurl, websocketpp, concurrentqueue, openssl) are resolved by conan.

## Configuration

- `domain/<exchange>.json` — flat `{name: url}` map of endpoints (committed).
- `config/<exchange>/auth_keys.json` — `{"key","secret"}` API credentials
  (gitignored; see the `*.example.json` templates).
- KIS additionally uses `config/kis/account_info.json` and runtime-written
  `config/kis/rest_access.json` / `websocket_access.json`.

## Run (Binance USDⓈ-M, testnet)

```bash
# terminal 1
./build/listener --exchange binance --domain_type test --products BTCUSDT --broadcast_port 8888
# terminal 2  (tick/lot are fetched from the listener; --min_tick_size/--lot_size
#              are optional CLI fallbacks used only if the listener doesn't publish them)
./build/trader --exchange binance --domain_type test --trade_products BTCUSDT \
    --broadcast_port 8888
```

The listener publishes per-product trading parameters (tick/lot from Binance
`exchangeInfo`) as a retained `product_info` message; the trader consumes it and
configures its pricer/strategy, so you don't pass tick/lot on the trader CLI.

## Run (KIS)

```bash
./build/listener --exchange kis --region korea --market_type derivatives --products 101W12
./build/trader  --exchange kis --region korea --market_type stock --trade_products 005930
```

## Status / scope

- Binance USDⓈ-M: market WS (bookTicker / diff depth + REST snapshot resync /
  aggTrade), user WS (ORDER_TRADE_UPDATE / ACCOUNT_UPDATE) with REST position &
  open-order snapshots on (re)connect, and order placement over the WS-API with
  REST fallback (HMAC-SHA256 signing).
- KIS: korean_derivatives market listener and korean_stock REST order gateway,
  ported onto the generic interfaces.

Out of scope for now: KIS us_stock, Binance COIN-M/options/batch orders, Binance
order amend `side` param, and Ed25519/session.logon (HMAC only).
