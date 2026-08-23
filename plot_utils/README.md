# plot_utils

Session plots for **live** OmniTrader runs, in the same format as the
backtester's `plot_maker` (in the `orderbook-backtest` project): the same four
stacked panels on a shared gap-collapsed time axis, and the same title/metrics
line.

```
python plot_utils/run.py --date 20260822 --out result.png
python plot_utils/run.py --start_date 20260820 --end_date 20260822 --out result.png
```

With no `--out` the figure is shown interactively. With no `--product` the only
product found under the logs is used.

| Module | Role |
|---|---|
| `run.py` | CLI entry point |
| `loaders.py` | reads the CSV trails, the trader text log, and the configs |
| `metrics.py` | rebuilds the book from fills; Sharpe / MDD / turnover |
| `plots.py` | the four panels |
| `strategies.py` | per-strategy title params, position unit, normalizer |
| `timeaxis.py` | gap-collapsing multi-day x-axis |

## Where the data comes from

The backtester wrote three purpose-built logs plus a `config.json`. Live writes
none of those, so each panel is sourced from what the running processes
actually record:

| Panel | Backtest source | Live source |
|---|---|---|
| Cumulative PnL | `pnl_logs` (`equity`) | `[Fill]` lines in `logs/trader/trader.log`, marked against mid |
| Position | `pnl_logs` (`position_in_lots`) | `order_decision.position_lots` |
| Mid / fair price | `price_logs` | `order_decision.mid` / `.fair` |
| Order-place count | `order_place_logs` | `order_record` rows with `type == "place"` |
| Settings | `pnl_logs/config.json` | `config/<exchange>/{market0,strategy0,trader0,factor0}.json` |

## Caveats worth knowing before reading a number off the chart

**PnL is gross.** Fees and funding are charged by the venue and logged nowhere,
so the equity curve is an upper bound on realized PnL, not the account
statement. `--fee_rate_bp` subtracts a flat rate from every fill's notional —
but the `[Fill]` line does not say maker or taker, so one rate covers both.

**Fills carry a time of day and no date.** The trader's quill pattern is
`%H:%M:%S.%Qns` in GMT, and `trader.log` accumulates across every session ever
run. `load_fills` recovers dates by counting the points where the clock runs
backwards and anchoring the *last* day it sees to `--fill_anchor_date`
(defaulting to `--end_date`) — the tail of the file is by definition the most
recent session. Override the anchor if you are plotting a range that is not the
newest thing in the log.

**Position has two independent sources**, and `--show_fill_position` draws both.
`position_lots` is what the venue's user stream told the trader it held — the
number the strategy actually sized against. The overlay is what its own fills
imply. They should sit on top of each other; a gap that opens and never closes
means position updates stopped reaching the trader while it went on quoting,
which is exactly the failure mode that let a $1,000 cap run to several thousand
in the 2026-08-22 session.

**`--time_interval` thins the position series too**, since the decision log is
both the price grid and the position source. A position that moved and moved
back inside one bucket disappears. Fills and order records are never thinned.

## Live-only handling in the readers

Two things the backtest logs could never produce, both dropped in
`loaders._read_csv_log`:

- quill opens these files with `open_mode('a')` and writes a header row on every
  process start, so a date with more than one run has header lines scattered
  through the middle of the file;
- a trader killed mid-write leaves a truncated final row, which keeps a valid
  timestamp while losing its mid — enough to put a NaN on the end of the equity
  curve if it survived.

Rotation siblings (`<date>.1.log`, …) are picked up and concatenated
automatically.

## Options

Beyond the backtester's `--rolling_minutes`, `--time_interval`,
`--no_fair_price`, `--split_order_side`, `--out` and `--dpi`:

| Flag | Effect |
|---|---|
| `--exchange`, `--product` | which log tree to read (product auto-detected if unambiguous) |
| `--log_base_path` | the `logs/` directory (default `./logs`) |
| `--config_base_path` | the `config/` directory (default `./config`) |
| `--fee_rate_bp` | flat fee charged against every fill |
| `--fill_anchor_date` | calendar date of the last day present in `trader.log` |
| `--show_quotes` | overlay the trader's own bid/ask on the price panel |
| `--show_fill_position` | overlay the fill-implied position on the venue's |

## Adding a strategy

Subclass `StrategyPlotSpec` in `strategies.py`, override what differs, and
register it under the `strategy_name` from the trader config. Nothing else
changes; an unregistered strategy falls back to the safe default rather than
crashing.
