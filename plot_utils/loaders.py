"""Loading helpers for the CSV trails written by the live OmniTrader processes.

Live logging is not backtest logging, and the differences drive most of what is
in here:

  * **There is no pnl log.** The backtester wrote cash / cum_cost / equity on
    every position change. Live, the only record that an execution happened is
    the ``[Fill]`` line in the trader's text log, so the PnL curve has to be
    rebuilt from those -- see :func:`load_fills` and
    ``metrics.build_position_frame``.
  * **There is no price log.** ``order_decision`` carries mid and fair on every
    decision tick, so it doubles as the price grid the equity curve is marked
    against.
  * **Nothing writes a config.json next to the logs.** :func:`load_config`
    assembles the equivalent from ``config/<exchange>/*.json``, which is where
    the live processes actually read their settings from.

Layout, from the ``*_save_path`` settings in TraderConfig / ListenerConfig::

    logs/order_decision/<exchange>/<product>/<date>.log
    logs/order_record/<exchange>/<product>/<date>.log
    logs/trader/trader.log

Every CSV timestamp is int64 nanoseconds since the unix epoch (UTC); every
loader adds a tz-aware ``time`` column and the ``date`` (YYYYMMDD) the row
belongs to, which is what ``timeaxis.SessionTimeAxis`` groups on.

Two live-only hazards the readers here have to survive, neither of which the
backtest logs could produce:

  * quill opens these files with ``open_mode('a')`` and writes a header row
    every time the process starts, so a date that saw more than one trader run
    has header lines scattered through the middle of the file.
  * a trader killed mid-write leaves a truncated final row.

Both are dropped in :func:`_read_csv_log` rather than being left to trip up
``read_csv``'s type inference.
"""

from __future__ import annotations

import glob
import json
import os
import re

import pandas as pd
from tqdm import tqdm

# Raw timestamp column name, shared by both trader CSV schemas
# (see OrderRecordCsvSchema / OrderDecisionCsvSchema in src/trader/trader_dtypes.hpp).
_TS = "local_tstamp"

# Column typing per schema. Everything is read as text first and cast from these,
# because the repeated-header rows above would otherwise poison read_csv's
# inference for the whole file.
_DECISION_NUMERIC = (
    "local_tstamp", "bbid", "bask", "mid", "fair", "factor",
    "position_lots", "outstanding", "bid_price", "ask_price",
)
_RECORD_NUMERIC = ("local_tstamp", "server_tstamp", "cid", "price", "qty")
_RECORD_BOOL = ("success",)


def _rotated_paths(path: str) -> list[str]:
    """``path`` plus its quill rotation siblings, oldest first.

    ``RotatingFileSink`` caps each file at 100MB and rolls the old content into
    ``<stem>.<n><ext>`` with n=1 the most recent, so the chronological order is
    the highest index first and the live file last. Only the order matters for
    :func:`load_fills`, which walks the trader log as a stream; the CSV readers
    re-sort by timestamp anyway.
    """
    stem, ext = os.path.splitext(path)
    indexed = []
    for sibling in glob.glob(f"{stem}.*{ext}"):
        suffix = sibling[len(stem) + 1:-len(ext)] if ext else sibling[len(stem) + 1:]
        if suffix.isdigit():
            indexed.append((int(suffix), sibling))
    ordered = [p for _, p in sorted(indexed, reverse=True)]
    if os.path.exists(path):
        ordered.append(path)
    return ordered


def _read_csv_log(
    path: str,
    date: str,
    numeric: tuple[str, ...] = (),
    boolean: tuple[str, ...] = (),
) -> pd.DataFrame:
    """Read one dated CSV trail (with its rotation siblings) into a frame.

    Read as text, then cast: see the module docstring for why. Rows whose
    timestamp does not parse are dropped, which covers both the repeated header
    lines and a truncated final row.
    """
    paths = _rotated_paths(path)
    if not paths:
        raise FileNotFoundError(path)
    frames = [
        pd.read_csv(p, dtype=str, keep_default_na=False, on_bad_lines="skip")
        for p in paths
    ]
    df = pd.concat(frames, ignore_index=True)

    # Two kinds of junk row to drop, and both have to go before the cast, which
    # would leave them indistinguishable from real values:
    #   * a line cut short by a killed process, padded out to the header width
    #     with empty cells. The writers never emit an empty field -- a missing
    #     number is "nan" and an absent message is "-" -- so a blank is proof of
    #     truncation. It matters because such a row keeps a perfectly valid
    #     timestamp while losing its mid, which would put a NaN on the end of
    #     the equity curve and turn the reported final PnL into nan.
    #   * a repeated header row, written by every restart onto the same date.
    text = df.fillna("").astype(str).apply(lambda s: s.str.strip())
    df = df[(text != "").all(axis=1) & (text[_TS] != _TS)].reset_index(drop=True)

    for col in numeric:
        df[col] = pd.to_numeric(df[col], errors="coerce")
    for col in boolean:
        df[col] = df[col].str.strip().str.lower().map({"true": True, "false": False})

    df = df[df[_TS].notna()].reset_index(drop=True)
    df[_TS] = df[_TS].astype("int64")
    df["time"] = pd.to_datetime(df[_TS], unit="ns", utc=True)
    df["date"] = date
    return df.sort_values("time", kind="stable").reset_index(drop=True)


class LogPaths:
    """Resolves the per-trail paths for one exchange / product / date.

    Mirrors the ``fmt::format("{}/{}/{}/{}.log", save_path, exchange, product,
    date)`` layout that ``OrderHandler::get_order_*_logger`` and
    ``MarketListener::get_*_logger`` build. ``log_base_path`` is the ``logs/``
    directory those save paths all sit under.
    """

    def __init__(self, log_base_path: str, exchange: str, product: str, date: str = ""):
        self.log_base_path = log_base_path
        self.exchange = exchange
        self.product = product
        self.date = date

    def _dir(self, record: str) -> str:
        return os.path.join(self.log_base_path, record, self.exchange, self.product)

    def _csv(self, record: str) -> str:
        return os.path.join(self._dir(record), f"{self.date}.log")

    @property
    def order_decision_log(self) -> str:
        return self._csv("order_decision")

    @property
    def order_record_log(self) -> str:
        return self._csv("order_record")

    def order_decision_dir(self) -> str:
        return self._dir("order_decision")

    def order_record_dir(self) -> str:
        return self._dir("order_record")

    def trader_log(self) -> str:
        # Not per-product and not per-date: one rotating text file for the process.
        return os.path.join(self.log_base_path, "trader", "trader.log")


def load_order_decision(paths: LogPaths) -> pd.DataFrame:
    """Columns: local_tstamp, bbid, bask, mid, fair, factor, position_lots,
    outstanding, bid_price, ask_price, time, date.

    One row per decision that reached the strategy. This is the live equivalent
    of the backtest price log -- mid and fair on a dense grid -- and also the
    authoritative position series, since ``position_lots`` is what the venue's
    user stream told the trader it was holding.
    """
    return _read_csv_log(paths.order_decision_log, paths.date, numeric=_DECISION_NUMERIC)


def load_order_record(paths: LogPaths) -> pd.DataFrame:
    """Columns: local_tstamp, server_tstamp, symbol, cid, order_no, type, side,
    success, price, qty, msg, time, date.

    One row per order action once the gateway's reply landed. ``type`` is
    place/amend/cancel; the rolling order-count plot filters to ``place``.
    """
    return _read_csv_log(
        paths.order_record_log, paths.date,
        numeric=_RECORD_NUMERIC, boolean=_RECORD_BOOL,
    )


# --- fills ------------------------------------------------------------------
# The trader's quill pattern is "%H:%M:%S.%Qns" in GMT (see src/loggers/logger.hpp),
# so a log line carries a time of day and no date. The fill payload is written by
# OrderHandler::process_execution_data.
_TIME_RE = re.compile(r"^(\d{2}):(\d{2}):(\d{2})\.(\d{9})\s")
_FILL_RE = re.compile(
    r"\[Fill\] (?P<product>\S+) order_no=(?P<order_no>\S+) "
    r"is_bid=(?P<is_bid>\S+) px=(?P<px>\S+) qty=(?P<qty>\S+)"
)


def load_fills(
    trader_log_path: str, anchor_date: str, product: str | None = None
) -> pd.DataFrame:
    """Executions parsed out of the trader's text log.

    Columns: time, date, product, order_no, is_bid, price, qty.

    Live has no execution CSV, so this is the only place a fill price is
    recorded, and therefore the only thing a PnL curve can be built from.

    Dating the rows takes some care. The log line has a time of day but no date,
    and the file accumulates across runs and across midnights. Days are
    recovered by walking every line in order and counting the points where the
    clock runs backwards, then anchoring the *last* observed day to
    ``anchor_date`` -- the tail of the file is by definition the most recent
    session, which makes it a far safer anchor than the head (which may sit
    several rotations back).

    ``product`` filters to a single symbol; the log is not per-product.
    """
    paths = _rotated_paths(trader_log_path)
    if not paths:
        raise FileNotFoundError(trader_log_path)

    rows: list[tuple[int, str, dict]] = []   # (day_index, hms, fill fields)
    day_index = 0
    prev_hms = None
    for path in paths:
        with open(path, errors="replace") as fh:
            for line in fh:
                m = _TIME_RE.match(line)
                if not m:
                    continue   # continuation line of a multi-line message
                hms = line[:m.end() - 1]
                if prev_hms is not None and hms < prev_hms:
                    day_index += 1   # the clock wrapped: a midnight passed
                prev_hms = hms
                fill = _FILL_RE.search(line)
                if fill:
                    rows.append((day_index, hms, fill.groupdict()))

    if not rows:
        # Typed rather than bare, so a fill-less session flows through the
        # as-of joins downstream instead of tripping on an object-dtype key.
        return pd.DataFrame({
            "time": pd.Series(dtype="datetime64[ns, UTC]"),
            "date": pd.Series(dtype="object"),
            "product": pd.Series(dtype="object"),
            "order_no": pd.Series(dtype="object"),
            "is_bid": pd.Series(dtype="bool"),
            "price": pd.Series(dtype="float64"),
            "qty": pd.Series(dtype="float64"),
        })

    # Anchor the newest day seen anywhere in the stream to anchor_date and count
    # backwards, so every earlier day lands on its own calendar date.
    last_day = day_index
    anchor = pd.Timestamp(anchor_date, tz="UTC")
    records = []
    for idx, hms, fields in rows:
        day = anchor - pd.Timedelta(days=last_day - idx)
        time = pd.Timestamp(f"{day.strftime('%Y-%m-%d')}T{hms}", tz="UTC")
        records.append({
            "time": time,
            "date": day.strftime("%Y%m%d"),
            "product": fields["product"],
            "order_no": fields["order_no"],
            "is_bid": fields["is_bid"].strip().lower() == "true",
            "price": float(fields["px"]),
            "qty": float(fields["qty"]),
        })

    df = pd.DataFrame.from_records(records)
    if product:
        df = df[df["product"] == product]
    return df.sort_values("time", kind="stable").reset_index(drop=True)


def downsample_to_interval(df: pd.DataFrame, interval: str) -> pd.DataFrame:
    """Thin a densely sampled log to ~one row per ``interval`` bucket.

    Keeps the last row in each floored-time bucket. ``interval`` is a pandas
    offset alias ('1s', '500ms', '1min', ...). Buckets are absolute wall-clock,
    so this stays consistent across day boundaries.

    Applied to the decision log this thins the position series as well as the
    price grid, so a position that moved and moved back inside one bucket is not
    visible. Fills are never thinned, so the PnL curve keeps every execution.
    """
    floored = df["time"].dt.floor(interval)
    return df[~floored.duplicated(keep="last")].reset_index(drop=True)


# --- config -----------------------------------------------------------------
# The backtester saved a config.json beside its pnl logs. The live processes read
# config/<exchange>/*.json at startup and save nothing, so the equivalent is
# assembled here from the same files, flattened into one dict whose keys match
# what the backtest config.json used (so strategies.py can stay a close parallel
# of the backtester's).
_CONFIG_FILES = ("market0.json", "strategy0.json", "trader0.json", "factor0.json")


def load_config(
    config_base_path: str, exchange: str, product: str | None = None
) -> dict:
    """Flattened live configuration for ``config/<exchange>/``.

    Missing files are skipped rather than raising: a run may have been launched
    with CLI arguments instead, in which case the caller still gets whatever is
    on disk plus the defaults it fills in.
    """
    cfg: dict = {}
    for name in _CONFIG_FILES:
        path = os.path.join(config_base_path, exchange, name)
        if not os.path.exists(path):
            continue
        with open(path) as f:
            doc = json.load(f)
        for section in doc.values():     # e.g. {"strategy_params": {...}}
            if isinstance(section, dict):
                cfg.update(section)

    cfg["exchange"] = exchange
    # The backtest config named a single instrument; live trades a list, so the
    # plotted product wins, falling back to the first configured one.
    trade_products = cfg.get("trade_products") or []
    cfg["product"] = product or (
        trade_products[0].split(":")[0] if trade_products else ""
    )
    # Internal units are process-global (see market0.json), not per product.
    cfg.setdefault("lot_size", cfg.get("default_lot_size", 1.0))
    # No venue in this system quotes a contract multiplier; kept so the metrics
    # and plots stay signature-compatible with the backtest versions.
    cfg.setdefault("contract_multiplier", 1.0)
    # Live fees are charged by the venue and never reach a log, so there is no
    # honest value to read here. run.py's --fee_rate_bp overrides it.
    cfg.setdefault("fee_rate_bp", 0.0)
    return cfg


# --- ranges -----------------------------------------------------------------

def discover_dates(paths: LogPaths, start_date: str, end_date: str) -> list[str]:
    """Available log dates within [start_date, end_date], from the decision logs."""
    found = set()
    for path in glob.glob(os.path.join(paths.order_decision_dir(), "*.log")):
        date = os.path.basename(path).split(".")[0]   # strips any rotation index
        if date.isdigit() and start_date <= date <= end_date:
            found.add(date)
    return sorted(found)


def discover_products(log_base_path: str, exchange: str) -> list[str]:
    """Products that have an order_decision directory under ``exchange``."""
    root = os.path.join(log_base_path, "order_decision", exchange)
    if not os.path.isdir(root):
        return []
    return sorted(p for p in os.listdir(root) if os.path.isdir(os.path.join(root, p)))


def _concat_time(frames: list[pd.DataFrame]) -> pd.DataFrame:
    # Per-file frames are each time-sorted, but a restarted trader can append a
    # session that overlaps the previous file's tail, so re-sort globally.
    # merge_asof and the step plots both require a monotonic time axis.
    return (
        pd.concat(frames, ignore_index=True)
        .sort_values("time", kind="stable")
        .reset_index(drop=True)
    )


def load_logs_range(
    log_base_path: str,
    exchange: str,
    product: str,
    start_date: str,
    end_date: str,
) -> tuple[pd.DataFrame, pd.DataFrame]:
    """Load and concatenate (order_decision, order_record) across the date range.

    Each frame stays time-sorted and keeps its per-row ``date`` column for the
    gap-collapsing time axis. A date with decisions but no order record yields
    an empty order frame rather than an error -- a session that quoted nothing
    is a real outcome, not a missing file.
    """
    dates = discover_dates(LogPaths(log_base_path, exchange, product), start_date, end_date)
    if not dates:
        raise FileNotFoundError(
            f"no order_decision logs in "
            f"{LogPaths(log_base_path, exchange, product).order_decision_dir()} "
            f"for dates [{start_date}, {end_date}]"
        )

    decisions, orders = [], []
    for date in tqdm(dates, desc="loading logs", unit="day"):
        paths = LogPaths(log_base_path, exchange, product, date)
        decisions.append(load_order_decision(paths))
        try:
            orders.append(load_order_record(paths))
        except FileNotFoundError:
            pass

    order_df = (
        _concat_time(orders) if orders
        else pd.DataFrame(columns=["time", "date", "type", "side", "success"])
    )
    return _concat_time(decisions), order_df
