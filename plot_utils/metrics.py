"""Summary performance metrics for a live trading session.

The backtester logged cash, cumulative cost and equity directly. Live it logs
none of them, so the first job here is to rebuild that book from the executions
parsed out of the trader log (:func:`build_position_frame`); everything
downstream then mirrors the backtest metrics -- a continuous mark-to-market
equity curve, its drawdown, an end-of-day Sharpe, and turnover taken against a
caller-supplied ``normalizer`` (the position cap in dollars).

Two things do not carry over from the backtest and are worth knowing before
reading a number off one of these plots:

  * **Fees are not logged.** The venue charges them, nothing records them, so
    the reconstruction is gross unless a ``fee_rate_bp`` is supplied -- and
    since the ``[Fill]`` line does not say whether the fill was maker or taker,
    even that is one flat rate over both.
  * **Funding is not logged either**, so a perpetual position held across a
    funding stamp shows none of it.

Both make the curve an upper bound on realized PnL, not the account statement.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import pandas as pd

# Periods per year used to annualize the daily-PnL Sharpe. 365 because crypto
# sessions are 24h and every calendar day trades.
ANNUALIZATION_DAYS = 365


def build_position_frame(fills_df: pd.DataFrame, fee_rate_bp: float = 0.0) -> pd.DataFrame:
    """Rebuild the trading book from executions: one row per fill.

    Returns ``time``, ``date``, ``position_qty`` (in the venue's own units, not
    internal lots), ``cash``, ``cum_cost`` and ``cum_transaction``. The columns
    are named and signed to match what the backtester's pnl log wrote, so
    :func:`compute_mtm_equity` is the same calculation on both sides::

        cash            signed proceeds: -px*qty on a buy, +px*qty on a sell
        cum_cost        cumulative fee, 0 unless fee_rate_bp is given
        cum_transaction gross traded notional, both sides, for turnover

    An empty frame is returned unchanged in shape, so a session with no fills
    plots a flat zero line rather than raising.
    """
    columns = ["time", "date", "position_qty", "cash", "cum_cost", "cum_transaction"]
    if fills_df is None or fills_df.empty:
        # Typed, not just named: merge_asof refuses an object-dtype join key, so
        # a bare DataFrame(columns=...) would turn "quoted all session and never
        # got hit" into a crash instead of a flat zero line.
        return pd.DataFrame({
            "time": pd.Series(dtype="datetime64[ns, UTC]"),
            "date": pd.Series(dtype="object"),
            **{c: pd.Series(dtype="float64") for c in columns[2:]},
        })

    df = fills_df.sort_values("time", kind="stable").reset_index(drop=True)
    notional = df["price"] * df["qty"]
    signed_qty = df["qty"].where(df["is_bid"], -df["qty"])

    out = pd.DataFrame({
        "time": df["time"],
        "date": df["date"],
        "position_qty": signed_qty.cumsum(),
        "cash": (-notional).where(df["is_bid"], notional).cumsum(),
        "cum_cost": (notional * fee_rate_bp / 1e4).cumsum(),
        "cum_transaction": notional.cumsum(),
    })
    return out[columns]


def compute_mtm_equity(
    position_df: pd.DataFrame,
    price_df: pd.DataFrame,
    contract_multiplier: float = 1.0,
) -> pd.DataFrame:
    """Mark-to-market equity (cumulative PnL) at every decision-log row.

    The fill-derived book only changes at executions, so on its own it is flat
    between them even while an open position moves with the market. Here cash /
    cum_cost / position are carried forward from it (as-of join) and marked
    against the mid price on every decision row, so the curve varies
    continuously between trades and still passes through the fills::

        equity(t) = cash - cum_cost
                    + position_qty * contract_multiplier * mid(t)

    ``position_qty`` is already in venue units (the ``[Fill]`` line reports the
    executed quantity, not lots), so no lot size enters here -- unlike the
    backtest version, whose position was in internal lots.

    Returns a frame with ``time``, ``date`` and ``equity`` columns.
    """
    merged = pd.merge_asof(
        price_df[["time", "date", "mid"]],
        position_df[["time", "cash", "cum_cost", "position_qty"]],
        on="time",
    )
    for col in ("cash", "cum_cost", "position_qty"):   # flat before the first fill
        merged[col] = merged[col].fillna(0.0)
    merged["equity"] = (
        merged["cash"]
        - merged["cum_cost"]
        + merged["position_qty"] * contract_multiplier * merged["mid"]
    )
    return merged[["time", "date", "equity"]]


@dataclass
class LiveMetrics:
    sharpe: float
    final_pnl: float
    mdd_dollar: float
    mdd_pct: float
    daily_turnover_dollar: float
    daily_turnover_pct: float

    def suptitle_line(self) -> str:
        sharpe = f"{self.sharpe:.2f}" if math.isfinite(self.sharpe) else "n/a"
        return (
            f"PnL ${self.final_pnl:.2f}  |  "
            f"Sharpe {sharpe}  |  "
            f"MDD ${self.mdd_dollar:.2f} ({self.mdd_pct:.2f}%)  |  "
            f"Daily turnover ${self.daily_turnover_dollar:.2f} "
            f"({self.daily_turnover_pct:.2f}%)"
        )


def compute_metrics(
    position_df: pd.DataFrame,
    price_df: pd.DataFrame,
    normalizer: float,
    contract_multiplier: float = 1.0,
) -> LiveMetrics:
    """Final PnL, Sharpe, max drawdown and daily turnover.

    ``normalizer`` is the dollar position cap the percentage figures are taken
    against (``position_limit_in_dollar``, or ``position_limit_in_lots * lot_size
    * mean mid`` in lots mode). NaN leaves them as n/a.
    """
    equity_df = compute_mtm_equity(position_df, price_df, contract_multiplier)
    equity = equity_df["equity"]
    final_pnl = float(equity.iloc[-1]) if len(equity) else 0.0

    # Maximum drawdown: worst peak-to-trough decline of the equity curve.
    drawdown = equity.cummax() - equity
    mdd_dollar = float(drawdown.max()) if len(drawdown) else 0.0

    # Sharpe from end-of-day cumulative-equity changes (first day vs flat start).
    eod_equity = equity_df.groupby("date")["equity"].last()
    daily_pnl = eod_equity.diff()
    if len(daily_pnl):
        daily_pnl.iloc[0] = eod_equity.iloc[0]
    std = daily_pnl.std(ddof=1)
    sharpe = (
        float(daily_pnl.mean() / std * math.sqrt(ANNUALIZATION_DAYS))
        if std and std > 0
        else float("nan")
    )

    # Daily turnover from the cumulative gross traded notional (both sides).
    num_days = int(eod_equity.shape[0])
    total_turnover = (
        float(position_df["cum_transaction"].iloc[-1]) if len(position_df) else 0.0
    )
    daily_turnover_dollar = total_turnover / num_days if num_days else 0.0

    mdd_pct = mdd_dollar / normalizer * 100.0 if normalizer else float("nan")
    daily_turnover_pct = (
        daily_turnover_dollar / normalizer * 100.0 if normalizer else float("nan")
    )
    return LiveMetrics(
        sharpe=sharpe,
        final_pnl=final_pnl,
        mdd_dollar=mdd_dollar,
        mdd_pct=mdd_pct,
        daily_turnover_dollar=daily_turnover_dollar,
        daily_turnover_pct=daily_turnover_pct,
    )


@dataclass
class OrderStats:
    """Order-flow counts from the order record.

    Live-only: a backtest fills against its own book and never has a request
    refused, so there was nothing here to count. A rising reject rate is the
    first visible symptom of most gateway-side faults, which is why it earns a
    place in the title.
    """

    placed: int
    cancelled: int
    amended: int
    rejected: int
    fills: int

    @property
    def reject_pct(self) -> float:
        total = self.placed + self.cancelled + self.amended
        return self.rejected / total * 100.0 if total else float("nan")

    def suptitle_line(self) -> str:
        reject = f"{self.reject_pct:.1f}%" if math.isfinite(self.reject_pct) else "n/a"
        return (
            f"Placed {self.placed:,}  |  Cancelled {self.cancelled:,}  |  "
            f"Amended {self.amended:,}  |  Rejected {self.rejected:,} ({reject})  |  "
            f"Fills {self.fills:,}"
        )


def compute_order_stats(order_df: pd.DataFrame, fills_df: pd.DataFrame) -> OrderStats:
    """Count order actions by type, and rejections across all of them."""
    if order_df is None or order_df.empty or "type" not in order_df:
        counts: dict = {}
        rejected = 0
    else:
        counts = order_df["type"].value_counts().to_dict()
        # `success` is False for a venue rejection and for a request that never
        # left the process; both are failures of the same order action.
        rejected = int((order_df["success"] == False).sum())   # noqa: E712
    return OrderStats(
        placed=int(counts.get("place", 0)),
        cancelled=int(counts.get("cancel", 0)),
        amended=int(counts.get("amend", 0)),
        rejected=rejected,
        fills=0 if fills_df is None else int(len(fills_df)),
    )
