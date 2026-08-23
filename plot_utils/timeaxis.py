"""Gap-collapsing time axis for multi-day continuous plots.

A live position carries across dates -- the trader flattens on shutdown but a
session that spans midnight, or a restart the next morning against an inherited
position, is continuous in PnL and position terms. The raw timestamps, however,
contain the stretches where nothing was running. ``SessionTimeAxis`` maps real
timestamps onto a numeric axis where time within each date advances
proportionally but the gaps between dates are collapsed, so sessions sit
back-to-back with no dead flat stretches.

This is the backtester's plot_maker/timeaxis.py unchanged in behaviour; the
axis only ever sees ``time`` and ``date`` columns, which both log sets provide.
"""

from __future__ import annotations

import numpy as np
import pandas as pd


def _epoch_seconds(times) -> np.ndarray:
    """Convert a tz-aware datetime series to float epoch seconds."""
    return pd.Series(pd.to_datetime(times, utc=True)).astype("int64").to_numpy() / 1e9


class SessionTimeAxis:
    """Maps timestamps to a gap-collapsed numeric x-axis.

    Built from one or more data frames that each carry ``time`` (tz-aware) and
    ``date`` (the ``YYYYMMDD`` log-file stem, or the reconstructed fill date)
    columns. Per-date start/end are taken as the min/max time across all
    supplied frames, so every series fits.
    """

    def __init__(self, dfs, gap_seconds: float = 0.0):
        frames = [df[["time", "date"]] for df in dfs if df is not None and len(df)]
        if not frames:
            raise ValueError("SessionTimeAxis requires at least one non-empty frame")
        rows = pd.concat(frames, ignore_index=True)
        sec = _epoch_seconds(rows["time"])
        agg = (
            pd.DataFrame({"date": rows["date"].to_numpy(), "sec": sec})
            .groupby("date")["sec"]
            .agg(["min", "max"])
            .sort_index()
        )
        self.days: list[str] = list(agg.index)
        self._t0 = agg["min"].to_dict()                      # date -> session start (epoch s)
        self._dur = (agg["max"] - agg["min"]).to_dict()      # date -> session duration (s)

        self._start: dict[str, float] = {}                   # date -> x at session start
        cursor = 0.0
        for day in self.days:
            self._start[day] = cursor
            cursor += self._dur[day] + gap_seconds
        self.total = cursor - gap_seconds if self.days else 0.0

    def to_x(self, times, dates) -> np.ndarray:
        """Map arrays of (time, date) to numeric x positions."""
        sec = _epoch_seconds(times)
        dates = pd.Index(dates)
        start = dates.map(self._start).to_numpy(dtype=float)
        t0 = dates.map(self._t0).to_numpy(dtype=float)
        return start + (sec - t0)

    def _x_to_epoch(self, x: float) -> float:
        """Reverse-map a numeric x back to an epoch-seconds timestamp."""
        for day in self.days:
            if x <= self._start[day] + self._dur[day] + 1e-9:
                return self._t0[day] + (x - self._start[day])
        last = self.days[-1]
        return self._t0[last] + (x - self._start[last])

    def apply(self, ax, num_ticks: int = 10) -> None:
        """Draw day-boundary separators and wall-clock tick labels on ``ax``."""
        for day in self.days[1:]:
            ax.axvline(self._start[day], color="grey", lw=0.6, ls=":", alpha=0.6)
        if self.total <= 0:
            return
        xs = np.linspace(0.0, self.total, num=num_ticks)
        labels, prev_day = [], None
        for x in xs:
            ts = pd.Timestamp(self._x_to_epoch(x), unit="s", tz="UTC")
            day = ts.strftime("%m-%d")
            labels.append(ts.strftime("%m-%d\n%H:%M") if day != prev_day else ts.strftime("%H:%M"))
            prev_day = day
        ax.set_xticks(xs)
        ax.set_xticklabels(labels, fontsize="small")
        ax.set_xlim(0.0, self.total)
