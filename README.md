# OmniTrader

Generic, multi-exchange market-listener / trader framework in C++23, generalized
from `kis_cpp`. The exchange-agnostic core lives under the `Omni::` namespace;
exchange adapters live under `Omni::<Exchange>::` (`Omni::Binance::`, `Omni::KIS::`)
and are extendible to new exchanges.

Three executables:

- **listener** — connects to an exchange's market data (and, where applicable,
  user-data) feeds, normalizes everything into a common message model, CSV-logs
  market data, and broadcasts normalized JSON to traders over an internal TCP
  server.
- **pricer** — subscribes to the listener like a trader does, computes a live
  **fair price** per product, and serves it on its own TCP server. This is where the
  fair-price definition lives, so one process serves every trader on the feed and a
  new definition is deployed by restarting it alone.
- **trader** — connects to the listener's TCP server, consumes normalized
  market/execution/position data, runs a strategy, and places/amends/cancels via
  an exchange order gateway. It takes top-of-book from the listener and fair price
  from the pricer, on a second, independent link.

## Architecture

```
                 ┌──────────── listener ────────────┐
exchange feeds → │ IExchangeListener adapter         │ → normalized events
                 │   (Binance: market WS + user WS   │      │
                 │    + REST snapshot resync;        │      ▼
                 │    KIS: single market WS)         │   TcpServer ──┬──────┐
                 └───────────────────────────────────┘               │ TCP  │ TCP
                 ┌──────────── pricer ──────────────┐                 │      │
                 │ Pricer ← MarketFeedClient ─────────┼────────────────┼──────┘
                 │   FairPriceCalculator (MID/VWAP/   │                │
                 │     FACTOR → BaseFactor.compute)   │                │
                 │   → TcpServer ───────────────────────┐              │
                 └───────────────────────────────────┘  │ TCP (fair px)│
                 ┌──────────── trader ──────────────┐    │              │
                 │ OrderHandler ← MarketFeedClient ───┼────┼──────────────┘
                 │              ← FairPriceClient ────┼────┘
                 │   Strategy.make_decision           │
                 │   → IOrderGateway                  │ → exchange orders
                 │   (Binance: WS-API + REST fallback;
                 │    KIS: REST)                     │
                 └───────────────────────────────────┘
```

The **pricer → strategy → order_handler** flow of `orderbook-backtest` is split
across two processes here. [src/pricer/](src/pricer/) is the pricer half:
`FairPriceCalculator` turns the accumulated `MarketState` into a fair price and
publishes it. In the trader, [strategy/](src/trader/strategy/)
(`BaseStrategy`/`GeuantStrategy` with `make_decision`) decides orders in tick/lot
units from the `PriceInfo` it is handed, and
[order_handler/](src/trader/order_handler/) assembles that `PriceInfo` — top-of-book
and mid from its own L1, fair price from the pricer feed — then maps decisions to the
`IOrderGateway` and tracks per-product position/outstanding state. CLI parsing uses
the `argparse` library.

### Fair price and factors

Fair price is computed **once, in the pricer**, and travels to traders as a plain
number. A trader never derives it: it keeps its own top-of-book (straight from the
listener, in ticks/lots) and asks the pricer for the rest.

`--fair_price_mode` on the pricer mirrors the backtest's `PricerConfig::Mode`:

| mode | `fair_price` |
|---|---|
| `MID` (default) | mid |
| `VWAP` | size-weighted top-of-book |
| `FACTOR` | `factor * mid` |

The default needs no configuration at all, so `build/pricer` with nothing but its
ports is a working mid publisher. Adding a fair-price mode is an enum value plus a
case in [fair_price.cpp](src/pricer/fair_price/fair_price.cpp); anything that needs
its own params or per-product state should be written as a factor instead.

The factor module mirrors the trader's strategy module one-for-one:

| strategy | factor |
|---|---|
| `MarketConfig` (common config) | `FactorConfig` (`--factor_ema_alpha`, `--factor_cap_bp`) |
| `BaseStrategy` + protected tick/lot helpers | `BaseFactor` + protected `clamp_factor` / `apply_ema` |
| `GeuantParams` / `GeuantStrategy` | `MicropriceParams` / `MicropriceFactor` |
| `init_strategy` on `--strategy_name` | `init_factor` on `--factor_name` |

So smoothing and clamping are handled once in `BaseFactor`
([base_factor.hpp](src/pricer/factor/base_factor.hpp)) and an implementation only
produces a raw multiplier on mid from a `MarketState` (top-of-book plus latest trade
print, in raw exchange units — a factor is dimensionless, so the pricer needs no
tick/lot info). One instance is held per product, so a factor may keep per-product
state. The shipped `Microprice` factor is the size-weighted top-of-book as a ratio to
mid, with `--imbalance_exponent` on the size weights.

In the backtest `FACTOR` reads `(ts, factor)` rows from a per-date gzip CSV of
pre-computed factors; here the same pairs are computed live, and `fair = factor * mid`
is applied identically.

### When the pricer is unavailable

The trader falls back to its own mid whenever a fair price is missing or stale, so a
pricer that is down, restarting, or never started at all degrades pricing rather than
stopping the trader. Three things make that reliable, none of which has a backtest
counterpart (a file of factors cannot go stale or disappear mid-run):

- The fair price ages out after `--fair_price_max_age_ms` (default 1000). The decision
  log prints the factor actually applied, `nan` once the fallback is in effect.
- The pricer **stops publishing** while its own listener link is down. Otherwise it
  would stamp frozen prices with a fresh timestamp, which is exactly what would defeat
  the check above.
- Both links reconnect on a 5s retry, so any process can be restarted independently
  and the others pick it back up. Each link drives its own retry from its own io
  thread, so one peer being away neither delays the other nor stalls the decision
  loop. The trader likewise holds off decisions while its listener link is down,
  rather than quoting off a book that is no longer updating.

Key generic seams: `Omni::Listener::IExchangeListener`
([exchange_listener.hpp](src/market_listener/exchange/exchange_listener.hpp)) and
`Omni::OrderGateway::IOrderGateway`
([order_gateway.hpp](src/trader/order_gateway/order_gateway.hpp)). The normalized
wire model is in [market_msg_types.hpp](src/common/market_msg_types.hpp).

### Internal feed clients

Both internal links are the same protocol, so everything they share — dialling, the
reconnect timer, line framing, the write queue and the subscribe handshake — lives in
`Connection::TcpClientBase`
([tcp_client_base.hpp](src/connection_handlers/tcp/tcp_client_base.hpp)). A subclass
supplies only which feeds its own link carries and what to put on its consumer's
queue: `MarketFeedClient` for the listener's market/user feed, `FairPriceClient` for
the pricer's. The base knows nothing about either consumer, so adding a third feed
does not touch it.

Each client owns its socket, io_context and thread. That keeps the links independent
(a burst on one cannot delay reads on another, and reconnection never runs on the
decision loop), and it makes the two links produce *different task types* rather than
one type carrying a source tag — so a consumer holding both dispatches them by
`std::visit` with no branching. The tasks are defined in
[feed_msg_types.hpp](src/common/feed_msg_types.hpp).

## Build

```bash
bash build.sh    # conan install + cmake + build  → build/listener, build/pricer, build/trader
```

Requires conan and cmake. Dependencies (boost, fmt, quill, nlohmann_json, glaze,
libcurl, websocketpp, concurrentqueue, openssl) are resolved by conan.

## Configuration

Credentials and launch configuration are kept in separate trees: `account/` holds
secrets and is gitignored, `config/` holds launch configs and is committed.

- `domain/<exchange>.json` — flat `{name: url}` map of endpoints (committed).
- `account/<exchange>/auth_keys.json` — `{"key","secret"}` API credentials
  (gitignored; see the `*.example.json` templates).
- KIS additionally uses `account/kis/account_info.json` and runtime-written
  `account/kis/rest_access.json` / `websocket_access.json`.
- `config/<exchange>/*.json` — launch configs, the file-based alternative to the CLI
  (committed).

### Launch configs

Every process can be started either from flags or from JSON files; the two forms are
equivalent and cannot be mixed on one command line. Each config struct that has
`set_parser()`/`init()` also has a `parse_json()` reading the same fields.

```
config/<exchange>/
  listener0.json   { "listener_config": { ... } }
  pricer0.json     { "pricer_config":   { ... } }
  factor0.json     { "factor_config":   { ... }, "factor_params": { ... } }
  trader0.json     { "trader_config":   { ... } }
  market0.json     { "market_config":   { ... } }
  strategy0.json   { "strategy_params": { ... } }
```

```bash
build/listener config/binance/listener0.json
build/pricer   config/binance/pricer0.json config/binance/factor0.json
build/trader   config/binance/trader0.json config/binance/market0.json \
               config/binance/strategy0.json
```

The trailing `0` is the default file name; further variants (`trader1.json`, …) are
added alongside and named explicitly. The files are split by what changes together:
wiring (listener/pricer/trader), the venue's price/quantity units (market, shared by
every trader on that exchange) and the strategy's parameters (swapped per run).

The pricer's factor file is only required in `FACTOR` mode — `MID` and `VWAP` need no
factor configuration, exactly as on the CLI. The market file may omit `exchange` and
inherit it from the trader config; if it states one, the two must agree.

Field names match the CLI flags without the `--` prefix, with two exceptions in
`factor_config`, where the flag prefix is dropped: `--factor_ema_alpha` is
`ema_alpha` and `--factor_cap_bp` is `cap_bp`. Any field may be omitted to take its
default, but an *unrecognized* field is an error rather than being ignored — a
misspelled `order_lts` would otherwise leave the trader silently running at zero size.

### Product arguments

`--products`, `--trade_products`, and `--subscribe_products` take space-separated
tokens of the form `SYMBOL[:category]`, where `category` is one of `futures`
(default if omitted), `asset`, or `spot`. The category determines how the listener
sources data per symbol:

- `futures` — market streams + order book + `product_info`, with positions seeded
  from `positionRisk` on (re)connect.
- `asset` — no book/stream; the wallet balance is fetched (`/fapi/v2/balance`) and
  broadcast as a `position` feed keyed by the asset symbol (e.g. `USDT`).
- `spot` — plumbed but not implemented; logs a warning and is skipped.

Positions and balances now share one subscription-based `position` feed (an asset
balance is just a position keyed by the asset symbol).

## Run (Binance USDⓈ-M, testnet)

```bash
# terminal 1  (BTCUSDT futures market/position + USDT wallet balance)
build/listener --exchange binance --domain_type test \
    --products BTCUSDT:futures USDT:asset --broadcast_port 8888
# terminal 2  (--min_tick_size/--default_lot_size are required: they are the units
#              every internal price and quantity is a count of)
build/trader --exchange binance --domain_type test \
    --trade_products BTCUSDT:futures --subscribe_products BTCUSDT:futures USDT:asset \
    --min_tick_size 0.1 --default_lot_size 0.001 \
    --broadcast_port 8888
```

`--subscribe_products` defaults to `--trade_products` when omitted.

### Stopping the trader

The trader traps `SIGINT` and `SIGTERM`. It does not just exit: whatever it had
resting at the exchange would stay resting, and whatever it was holding would stay
held, with nothing watching either. So on a stop -- Ctrl-C, a supervisor's `SIGTERM`,
or the configured market close -- it runs a bounded shutdown:

1. **Cancel everything.** Unconditional. Repeated rather than fired once, because an
   order placed moments before the stop has no exchange order id yet and cannot be
   cancelled until its place ack arrives.
2. **Get flat, passively.** The strategy's liquidation branch quotes the whole
   position one tick inside the touch, which gets out at the top of the book without
   paying the spread.
3. **Cross if it has to.** If the passive quote has not filled by
   `shutdown_flatten_timeout_ms`, the resting quote is pulled and the remainder goes
   out as a `reduceOnly` market order.
4. **Report.** Anything it could not finish is logged as
   `[Shutdown] EXITING WITH A POSITION`, with the residual.

Nothing the process placed is left working, on any path -- including the one where
flattening is turned off or gives up.

A **second** signal is not swallowed. It restores the default disposition and
re-raises, so a shutdown that is hanging can always be cut short; that leaves orders
behind, which is the operator's call to make.

| Setting | Default | What it does |
| --- | --- | --- |
| `flatten_on_shutdown` | `true` | Off = cancel only, leave the position on. |
| `shutdown_cancel_timeout_ms` | `5000` | How long to chase cancel acks. Cancels go out one at a time (see below), so this scales with how many orders are resting. |
| `shutdown_flatten_timeout_ms` | `5000` | How long to work the position passively before crossing. |
| `shutdown_market_flatten` | `true` | Off = never cross; exit with the residual and say so. |
| `shutdown_reduce_only` | `true` | Tags the flattening orders `reduceOnly`. **Turn off for a Binance futures account in Hedge Mode**, which rejects the flag. |

On the CLI the three defaults-on switches are `--no_flatten_on_shutdown`,
`--no_shutdown_market_flatten` and `--no_shutdown_reduce_only`, since a flag can only
turn something on; in JSON they are stated positively.

Orders leave a gateway **in the order the trader issued them**: the WS-API gateway
writes to one socket, and the REST gateways run one worker thread rather than a thread
per request. That is what makes cancel-then-place safe -- a place that overtook its
cancel would rest at both prices at once. The cost is that a burst of REST requests
costs one round trip each rather than overlapping, which is why the cancel timeout
above is sized the way it is.

**Stop the trader before the listener.** Position and fills both reach the trader over
the listener link, so with the listener already gone the trader cannot work the
position out passively and cannot confirm that a market order filled. It says so
rather than guessing -- the report is tagged *"listener link is down -- this is the
last state we were told, not confirmed"* -- but it is a worse shutdown. The listener
and pricer do not trap signals; they are stateless at the venue and safe to Ctrl-C.

### Two grids, on purpose

The trader runs on two different notions of tick and lot, and keeping them apart is
what lets one process trade several products safely.

| | source | scope | used for |
|---|---|---|---|
| `--min_tick_size` / `--default_lot_size` | trader CLI, required | process-global, fixed for the run | normalizing every price to an `int64` tick count and every quantity to an `int32` lot count |
| `product_info` (`tick_size`, `lot_size`) | listener, per product, refreshed | one product, may change mid-session | snapping an order's price and quantity on the way to the exchange |

Everything the trader holds — L1, positions, outstanding orders, the strategy's own
limits and arithmetic — is denominated in the **global** units. They are read once
from the CLI and never rewritten, because a venue can change a product's filter
under a live session and rebasing the unit would silently reinterpret every number
already recorded in it: a position of 50 lots means something different the instant
the lot changes underneath it. The trader refuses to start if either is missing or
non-positive; there is no sane default for a unit.

**`--min_tick_size` must be at least as fine as the finest product you trade.** It is
the resolution of every internal price, so a product whose real tick is finer cannot
be represented: prices quantize coarser than the venue's own grid, and tick-based
ladder levels merge into a single order before anything downstream can separate them.
The trader logs a warning naming the product and the value to drop to when it sees
this, but it cannot fix it — only a lower `--min_tick_size` can.

The **per-product** grid is applied at order submission, in `snap_to_product_grid`,
which rounds price and quantity to the *nearest* grid point — the goal is the order
the strategy actually asked for, and directional rounding would bias every order
away from the decided price by up to a tick and shave every size by up to a lot. An
order whose quantity rounds away to nothing, or whose price rounds to zero, is
dropped rather than sent. The outstanding-order record keeps the *pre-snap* values,
so the strategy's next decision compares like with like instead of churning a
cancel/replace every tick.

`order_interval_ticks` counts **that product's** ticks, not global min ticks. Spacing
a ladder on the global unit collapses it onto one price the moment the handler snaps
to a coarser venue grid — the strategy would believe it had a ladder the exchange
never saw. Spacing on the real increment survives the snap: N real ticks apart before
it, N after it, because both levels shift by the same fraction of a tick.

The same grid is pushed into the order gateway, which formats the outgoing request
against its own copy of the exchange's filters. Without that push the gateway would
keep formatting against the copy it loaded when it was constructed, so a filter
change mid-session would reach the snapping but not the decimal precision on the
wire. `IOrderGateway::set_product_grid` is a defaulted no-op, so a gateway that does
not format against a per-product grid (KIS) ignores it.

A product with no `product_info` (KIS, or the window before the first message)
simply stays on the global grid, which is the correct behaviour for a venue whose
tick is a function of price rather than a per-product constant.

### Product info is the listener's to own

The listener is the process that talks to the exchange, so it is the one that can
know the grids. `MarketListener` publishes them once at startup and then every
`--product_info_refresh_sec` (default 300, `<= 0` for startup-only), on its own
context and thread so a blocking REST fetch never sits in front of the broadcast
server's work. Each message goes only to the clients subscribed to that product,
and is replayed to each newly subscribed trader.

The schedule lives in `MarketListener`, not in any adapter, because it is identical
for every exchange; only what gets fetched differs.
`IExchangeListener::publish_product_info()` is a defaulted no-op, so an exchange
with nothing to publish says so by not overriding it:

| exchange | overrides `publish_product_info` | why |
|---|---|---|
| Binance | yes — `exchangeInfo` filters | per-product tick/step size, and a filter can change |
| KIS | no — inherits the no-op | tick size is a function of price, not a product constant |

### With the pricer

Start the pricer between the listener and the trader. It takes `--broadcast_port`
(the listener it reads from) and `--publish_port` (the server traders read from);
the trader points at the latter with `--pricer_port` (default 8889, so the example
above already dials it and simply prices off mid when nothing answers).

```bash
# terminal 1
build/listener --exchange binance --domain_type test \
    --products BTCUSDT:futures --broadcast_port 8888
# terminal 2  (reads the listener on 8888, serves fair prices on 8889)
build/pricer --products BTCUSDT:futures \
    --broadcast_port 8888 --publish_port 8889 \
    --fair_price_mode FACTOR --factor_name Microprice \
    --factor_ema_alpha 0.2 --publish_interval_ms 100
# terminal 3
build/trader --exchange binance --domain_type test \
    --trade_products BTCUSDT:futures --broadcast_port 8888 \
    --pricer_port 8889
```

The pricer subscribes to the trade products it is configured with, so its
`--products` should cover every trader's `--trade_products`. The trader needs no
fair-price arguments beyond where to find the pricer: the mode, the factor and their
parameters are all the pricer's, which is what lets one definition serve every trader.

## Run (KIS)

```bash
build/listener --exchange kis --region korea --market_type derivatives --products 101W12
build/trader  --exchange kis --region korea --market_type stock --trade_products 005930

# or, equivalently, from launch configs
build/listener config/kis/listener0.json
build/trader   config/kis/trader0.json config/kis/market0.json config/kis/strategy0.json
```

KIS has no spot/futures/asset distinction, so the `:category` suffix is unnecessary
(symbols default to `futures`); positions are derived from execution deltas.

## Status / scope

- Binance USDⓈ-M: market WS (bookTicker / diff depth + REST snapshot resync /
  aggTrade), user WS (ORDER_TRADE_UPDATE / ACCOUNT_UPDATE) with REST position &
  open-order snapshots on (re)connect, and order placement over the WS-API with
  REST fallback (HMAC-SHA256 signing).
- KIS: korean_derivatives market listener and korean_stock REST order gateway,
  ported onto the generic interfaces.
- Pricer: `MID`/`VWAP`/`FACTOR` fair price over the listener's L1, served to traders
  on its own TCP server, with a `Microprice` factor behind `FACTOR`. `MarketState`
  already carries the latest trade print, so trade-flow factors can be added without
  touching the transport.

Out of scope for now: KIS us_stock, Binance COIN-M/options/batch orders, Binance
order amend `side` param, and Ed25519/session.logon (HMAC only).

The pricer is exchange-agnostic (it only consumes the normalized feed) but is
untested against KIS. It also holds no persistence: fair prices are computed from live
state only, with no warm start after a restart, and are not written to disk for the
backtest to replay. Link loss is detected from the socket, so a half-open connection
(no data, no FIN) would not trip the staleness guards; that needs a heartbeat.
