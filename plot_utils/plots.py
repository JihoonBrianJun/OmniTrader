"""Matplotlib plotting routines for live trading results.

Every series is drawn against a shared :class:`SessionTimeAxis` so that the four
stacked plots line up and multi-day runs render continuously with the idle
stretches collapsed. ``plot_live_result`` assembles the figure.

The four panels are the backtester's, sourced from what live actually writes:

    1. cumulative mark-to-market PnL   fills (trader.log) marked against mid
    2. position                        order_decision.position_lots
    3. mid price (+ fair)              order_decision.mid / .fair
    4. rolling order-place count       order_record, type == "place"
"""

from __future__ import annotations

from dataclasses import dataclass

import matplotlib.pyplot as plt
import pandas as pd
from tqdm import tqdm

from metrics import compute_mtm_equity
from timeaxis import SessionTimeAxis


@dataclass
class PlotConfig:
    """Tunables for the live plots.

    position_unit:         "lots" or "dollars".
    lot_size:              internal lot size (market0.json default_lot_size).
    contract_multiplier:   contract multiplier; 1.0 on every venue here.
    rolling_minutes:       window N for the rolling order-place count.
    show_fair_price:       overlay the pricer's fair price on the mid plot.
    show_quotes:           overlay the trader's own best bid/ask quotes there too.
    split_order_side:      plot bid/ask order counts separately as well as total.
    show_fill_position:    overlay the fill-reconstructed position on the venue's.
    """

    position_unit: str = "lots"
    lot_size: float = 1.0
    contract_multiplier: float = 1.0
    rolling_minutes: int = 5
    show_fair_price: bool = True
    show_quotes: bool = False
    split_order_side: bool = False
    show_fill_position: bool = False


def plot_cumulative_pnl(
    ax: plt.Axes,
    position_df: pd.DataFrame,
    decision_df: pd.DataFrame,
    config: PlotConfig,
    xaxis: SessionTimeAxis,
) -> None:
    """Plot1: cumulative mark-to-market PnL, recomputed at every decision tick.

    The fill-derived book only changes at executions, so on its own it is flat
    between them even while an open position moves with the market. Marking it
    against the decision log's mid gives a curve that varies continuously
    between trades and still passes through every fill.

    Gross of fees and funding unless ``--fee_rate_bp`` was supplied; neither is
    logged anywhere live. See metrics.py.
    """
    equity_df = compute_mtm_equity(position_df, decision_df, config.contract_multiplier)
    ax.plot(
        xaxis.to_x(equity_df["time"], equity_df["date"]), equity_df["equity"], color="C0"
    )
    ax.axhline(0.0, color="grey", lw=0.8, ls="--")
    ax.set_ylabel("Cumulative PnL")
    ax.set_title("Cumulative PnL (mark-to-market)")
    ax.grid(True, alpha=0.3)


def plot_position(
    ax: plt.Axes,
    decision_df: pd.DataFrame,
    position_df: pd.DataFrame,
    config: PlotConfig,
    xaxis: SessionTimeAxis,
) -> None:
    """Plot2: position over time, in lots or dollars.

    Sourced from ``order_decision.position_lots``, which is the position the
    trader believed it held at the moment it quoted -- fed from the venue's user
    stream. That is the number the strategy actually sized against, so it is the
    one worth plotting even where it is wrong.

    ``show_fill_position`` overlays the position implied by the fills instead.
    The two agreeing is the check that the user stream is being received and
    applied; a gap that opens and never closes means position updates stopped
    reaching the trader while it went on quoting.
    """
    unit = config.position_unit.lower()
    x = xaxis.to_x(decision_df["time"], decision_df["date"])
    # position_lots is the trader's internal lot unit; lot_size converts to the
    # venue's quantity, and the mid on the same row converts that to notional.
    qty = decision_df["position_lots"] * config.lot_size

    if unit == "lots":
        venue = decision_df["position_lots"]
        ax.set_ylabel("Position (lots)")
        ax.set_title("Position (lots)")
    elif unit in ("dollars", "dollar", "usd", "notional"):
        venue = qty * config.contract_multiplier * decision_df["mid"]
        ax.set_ylabel("Position ($)")
        ax.set_title("Position (dollars)")
    else:
        raise ValueError(f"unknown position_unit: {config.position_unit!r}")

    ax.plot(x, venue, drawstyle="steps-post", color="C2", label="venue")

    if config.show_fill_position and position_df is not None and len(position_df):
        merged = pd.merge_asof(
            decision_df[["time", "date", "mid"]],
            position_df[["time", "position_qty"]],
            on="time",
        )
        merged["position_qty"] = merged["position_qty"].fillna(0.0)
        recon = (
            merged["position_qty"] / config.lot_size if unit == "lots"
            else merged["position_qty"] * config.contract_multiplier * merged["mid"]
        )
        ax.plot(
            xaxis.to_x(merged["time"], merged["date"]), recon,
            drawstyle="steps-post", color="C3", lw=0.8, alpha=0.7, label="from fills",
        )
        ax.legend(loc="upper left", fontsize="small")

    ax.axhline(0.0, color="grey", lw=0.8, ls="--")
    ax.grid(True, alpha=0.3)


def plot_mid_price(
    ax: plt.Axes, decision_df: pd.DataFrame, config: PlotConfig, xaxis: SessionTimeAxis
) -> None:
    """Plot3: market mid price, optionally with the pricer's fair price and the
    trader's own quotes.

    ``bid_price`` / ``ask_price`` are the top of each side's ladder as the
    strategy asked for it, NaN where it wanted no quote on that side -- so a
    broken line there is the strategy declining to quote, not missing data.
    """
    x = xaxis.to_x(decision_df["time"], decision_df["date"])
    ax.plot(x, decision_df["mid"], color="C1", label="mid")
    labelled = False
    if config.show_fair_price and "fair" in decision_df:
        ax.plot(x, decision_df["fair"], color="C3", lw=0.8, alpha=0.7, label="fair")
        labelled = True
    if config.show_quotes:
        ax.plot(x, decision_df["bid_price"], color="C0", lw=0.6, alpha=0.6, label="our bid")
        ax.plot(x, decision_df["ask_price"], color="C4", lw=0.6, alpha=0.6, label="our ask")
        labelled = True
    if labelled:
        ax.legend(loc="upper left", fontsize="small")
    ax.set_ylabel("Price")
    ax.set_title("Market mid price")
    ax.grid(True, alpha=0.3)


def _rolling_count_per_day(df: pd.DataFrame, window: str) -> pd.DataFrame:
    """Trailing time-window row count, restarted within each date so the first
    minutes of a session don't pull in the prior session's tail."""
    parts = []
    for date, grp in df.groupby("date", sort=True):
        ones = pd.Series(1, index=grp["time"].to_numpy())
        counts = ones.rolling(window).count()
        parts.append(pd.DataFrame({
            "time": grp["time"].to_numpy(), "date": date, "count": counts.to_numpy()
        }))
    return pd.concat(parts, ignore_index=True)


def plot_order_place_counts(
    ax: plt.Axes, order_df: pd.DataFrame, config: PlotConfig, xaxis: SessionTimeAxis
) -> None:
    """Plot4: rolling N-minute count of placed orders (reset each date).

    Counts ``type == "place"`` rows from the order record. Those rows are
    written when the gateway's reply lands, so this is the rate of requests the
    venue answered, rejections included -- which is the rate that matters for a
    rate limit.
    """
    n = config.rolling_minutes
    window = f"{n}min"

    places = order_df[order_df["type"] == "place"] if len(order_df) else order_df
    if places.empty:
        ax.set_ylabel(f"Orders / {n}min")
        ax.set_title(f"Rolling {n}-minute order-place count (none placed)")
        ax.grid(True, alpha=0.3)
        return

    total = _rolling_count_per_day(places, window)
    ax.plot(xaxis.to_x(total["time"], total["date"]), total["count"], color="C4", label="total")
    if config.split_order_side and "side" in places:
        for side, color in (("bid", "C0"), ("ask", "C3")):
            sub = places[places["side"] == side]
            if sub.empty:
                continue
            counts = _rolling_count_per_day(sub, window)
            ax.plot(
                xaxis.to_x(counts["time"], counts["date"]), counts["count"],
                color=color, lw=0.8, alpha=0.7, label=side,
            )
        ax.legend(loc="upper left", fontsize="small")
    ax.set_ylabel(f"Orders / {n}min")
    ax.set_title(f"Rolling {n}-minute order-place count")
    ax.grid(True, alpha=0.3)


def plot_live_result(
    decision_df: pd.DataFrame,
    position_df: pd.DataFrame,
    order_df: pd.DataFrame,
    config: PlotConfig | None = None,
    title: str | None = None,
):
    """Stack the four standard plots into one shared, gap-collapsed figure.

    Works for a single date or a concatenated multi-day range. Returns
    ``(fig, axes)``.
    """
    config = config or PlotConfig()
    xaxis = SessionTimeAxis([decision_df, position_df, order_df])

    fig, axes = plt.subplots(4, 1, figsize=(14, 12), sharex=True)
    steps = [
        ("cumulative pnl", lambda: plot_cumulative_pnl(axes[0], position_df, decision_df, config, xaxis)),
        ("position", lambda: plot_position(axes[1], decision_df, position_df, config, xaxis)),
        ("mid price", lambda: plot_mid_price(axes[2], decision_df, config, xaxis)),
        ("order counts", lambda: plot_order_place_counts(axes[3], order_df, config, xaxis)),
    ]
    for _, draw in tqdm(steps, desc="plotting", unit="plot"):
        draw()

    xaxis.apply(axes[-1])
    axes[-1].set_xlabel(
        "Session time (UTC, gaps collapsed)" if len(xaxis.days) > 1 else "Time (UTC)"
    )
    if title:
        # parse_math=False: the metrics line contains literal '$' (dollar
        # amounts), which matplotlib would otherwise read as a math delimiter.
        fig.suptitle(title, fontsize="medium", parse_math=False)
    fig.tight_layout()
    return fig, axes
