#!/usr/bin/env python3
"""Plot a live trading session from the logs the running processes wrote.

This is the live counterpart of the backtester's plot_maker: same four stacked
panels (mark-to-market PnL, position, mid price, rolling order-place count) and
the same title/metrics format, sourced from what live actually records.

Where the data comes from is the whole difference, and it is worth knowing
before reading a number off the chart:

  * PnL is rebuilt from the ``[Fill]`` lines in ``logs/trader/trader.log``,
    because live writes no pnl log. It is **gross of fees and funding** -- the
    venue charges both and logs neither -- unless ``--fee_rate_bp`` is given, and
    the fill line does not say maker or taker, so that one rate covers both.
  * Prices come from ``order_decision``, which records mid and fair on every
    decision tick, so it stands in for the backtest's price log.
  * Settings come from ``config/<exchange>/*.json``, since live saves no
    config.json beside its logs. Pass ``--config_base_path`` if the run used a
    different config directory.

Single day::

    python plot_utils/run.py --date 20260822 --out result.png

Multi-day continuous (idle stretches collapsed, PnL/position carry across days)::

    python plot_utils/run.py --start_date 20260820 --end_date 20260822 --out result.png

With no --out the figure is shown interactively. With no --product the only
product found under the logs is used.
"""

from __future__ import annotations

import argparse
import os
import sys

# Run as `python plot_utils/run.py` from anywhere: the sibling modules import each
# other by bare name, matching the backtester's plot_maker layout.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import matplotlib

from loaders import (
    LogPaths,
    discover_products,
    downsample_to_interval,
    load_config,
    load_fills,
    load_logs_range,
)
from metrics import build_position_frame, compute_metrics, compute_order_stats
from plots import PlotConfig, plot_live_result
from strategies import get_strategy_spec


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("--log_base_path", default="./logs",
                   help="the logs/ directory the *_save_path settings sit under")
    p.add_argument("--config_base_path", default="./config",
                   help="directory holding <exchange>/{market0,strategy0,trader0}.json")
    p.add_argument("--exchange", default="binance")
    p.add_argument("--product", default=None,
                   help="symbol to plot, e.g. BTCUSDC (default: the only one logged)")

    p.add_argument("--date", help="single log date, e.g. 20260822 (shorthand "
                   "for --start_date == --end_date)")
    p.add_argument("--start_date", help="first log date of the range, e.g. 20260820")
    p.add_argument("--end_date", help="last log date of the range, e.g. 20260822")

    p.add_argument("--fee_rate_bp", type=float, default=None,
                   help="flat fee in bp charged against every fill's notional. "
                        "Live fees are not logged, so without this the PnL curve "
                        "is gross")
    p.add_argument("--fill_anchor_date", default=None,
                   help="calendar date of the LAST day present in trader.log, "
                        "used to date the fills (which carry a time of day only). "
                        "Defaults to --end_date")

    p.add_argument("--rolling_minutes", type=int, default=5,
                   help="window N for the rolling order-place count")
    p.add_argument("--time_interval", default=None,
                   help="thin the decision grid to one point per interval, e.g. "
                        "'1s', '500ms', '1min' (default: every decision row). "
                        "Thins the position series too; fills are never thinned")

    p.add_argument("--no_fair_price", action="store_true",
                   help="do not overlay the fair price on the mid-price plot")
    p.add_argument("--show_quotes", action="store_true",
                   help="also overlay the trader's own best bid/ask on that plot")
    p.add_argument("--split_order_side", action="store_true",
                   help="also plot bid/ask order counts separately")
    p.add_argument("--show_fill_position", action="store_true",
                   help="overlay the fill-implied position on the venue position, "
                        "so a user-stream feed that stopped arriving is visible")

    p.add_argument("--out", default=None,
                   help="save the figure to this path instead of showing it")
    p.add_argument("--dpi", type=int, default=150, help="dpi for --out")
    return p.parse_args()


def resolve_date_range(args: argparse.Namespace) -> tuple[str, str]:
    """Resolve [start_date, end_date] from --date or --start_date/--end_date."""
    if args.date:
        if args.start_date or args.end_date:
            raise SystemExit("use either --date or --start_date/--end_date, not both")
        return args.date, args.date
    if not args.start_date or not args.end_date:
        raise SystemExit("provide --date, or both --start_date and --end_date")
    if args.start_date > args.end_date:
        raise SystemExit("--start_date must not be after --end_date")
    return args.start_date, args.end_date


def resolve_product(args: argparse.Namespace) -> str:
    """The product to plot: the one given, or the only one that has logs."""
    if args.product:
        return args.product
    products = discover_products(args.log_base_path, args.exchange)
    if not products:
        raise SystemExit(
            f"no products under {args.log_base_path}/order_decision/{args.exchange}"
        )
    if len(products) > 1:
        raise SystemExit(f"several products logged ({', '.join(products)}); pick one "
                         f"with --product")
    return products[0]


def main() -> None:
    args = parse_args()
    if args.out:
        matplotlib.use("Agg")   # headless backend when only saving
    import matplotlib.pyplot as plt

    start_date, end_date = resolve_date_range(args)
    product = resolve_product(args)

    cfg = load_config(args.config_base_path, args.exchange, product)
    if args.fee_rate_bp is not None:
        cfg["fee_rate_bp"] = args.fee_rate_bp

    decision_df, order_df = load_logs_range(
        args.log_base_path, args.exchange, product, start_date, end_date
    )
    if args.time_interval:
        # thin the dense decision grid; the equity/position/mid plots and the
        # equity-based metrics follow it. Fills and order records are left intact
        # (subsampling them would drop executions / undercount placements).
        decision_df = downsample_to_interval(decision_df, args.time_interval)

    trader_log = LogPaths(args.log_base_path, args.exchange, product).trader_log()
    fills_df = load_fills(
        trader_log, args.fill_anchor_date or end_date, product=product
    )
    # trader.log is one file for every session ever run, so trim to the plotted
    # range once the reconstructed dates are known.
    if len(fills_df):
        in_range = (fills_df["date"] >= start_date) & (fills_df["date"] <= end_date)
        dropped = int((~in_range).sum())
        fills_df = fills_df[in_range].reset_index(drop=True)
        if dropped:
            print(f"note: {dropped} fill(s) outside [{start_date}, {end_date}] ignored")
    if fills_df.empty:
        print(f"note: no fills found in {trader_log} for [{start_date}, {end_date}]; "
              f"the PnL curve will be flat")

    position_df = build_position_frame(fills_df, float(cfg["fee_rate_bp"]))

    # strategy-specific bits (position unit, normalizer, param line) go through
    # the strategy registry so a new strategy needs no change here.
    spec = get_strategy_spec(cfg)
    lot_size = float(cfg["lot_size"])
    contract_multiplier = float(cfg["contract_multiplier"])

    config = PlotConfig(
        position_unit=spec.position_unit(cfg),
        lot_size=lot_size,
        contract_multiplier=contract_multiplier,
        rolling_minutes=args.rolling_minutes,
        show_fair_price=not args.no_fair_price,
        show_quotes=args.show_quotes,
        split_order_side=args.split_order_side,
        show_fill_position=args.show_fill_position,
    )

    normalizer = spec.normalizer(cfg, decision_df)
    metrics = compute_metrics(position_df, decision_df, normalizer, contract_multiplier)
    order_stats = compute_order_stats(order_df, fills_df)

    # session name from the config, e.g. "binance BTCUSDC (fee_rate_bp=0.00)"
    session_name = f"{cfg['exchange']} {product} (fee_rate_bp={cfg['fee_rate_bp']:.2f})"
    span = start_date if start_date == end_date else f"{start_date}..{end_date}"
    lines = [f"{session_name} {span}  [live]"]
    strategy_line = spec.param_summary(cfg)
    if strategy_line:
        lines.append(strategy_line)     # before the metrics (Sharpe) line
    lines.append(metrics.suptitle_line())
    lines.append(order_stats.suptitle_line())
    title = "\n".join(lines)

    fig, _ = plot_live_result(decision_df, position_df, order_df, config, title=title)

    if args.out:
        fig.savefig(args.out, dpi=args.dpi, bbox_inches="tight")
        print(f"saved {args.out}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
