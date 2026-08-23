"""Strategy-specific plotting knowledge, kept out of the generic pipeline.

Loading, metrics and plots all work off the strategy-agnostic CSV trails. The
few things that *do* depend on the strategy -- how to summarize its params in
the title, what unit its position is in, and the dollar cap that percentage
metrics are normalized against -- live here behind :class:`StrategyPlotSpec`.

To support a new strategy, subclass :class:`StrategyPlotSpec`, override what it
needs, and register the instance in :data:`STRATEGY_SPECS` under the
``strategy_name`` in the trader config. Nothing else in plot_utils needs to
change; an unregistered strategy falls back to the base spec (no param line,
lots position, no normalizer) instead of crashing.

This mirrors the backtester's plot_maker/strategies.py. The one live difference
is in :meth:`GeuantSpec.normalizer`: internal lots are converted to dollars
through ``lot_size``, because live positions are logged in the trader's internal
lot unit while the backtest's cap was already expressed in its own.
"""

from __future__ import annotations

import pandas as pd


class StrategyPlotSpec:
    """Default, strategy-agnostic plotting behavior.

    Subclass per strategy and override the pieces that differ. The defaults are
    deliberately safe for an unknown strategy: no title param line, a ``lots``
    position unit, and a NaN normalizer (which leaves percentage metrics as
    n/a rather than raising on missing config keys).
    """

    def param_summary(self, cfg: dict, exclude: tuple[str, ...] = ()) -> str | None:
        """One-line ``key=value`` summary of the strategy params for the title,
        omitting any keys in ``exclude``. ``None`` means "nothing to show"."""
        return None

    def position_unit(self, cfg: dict) -> str:
        """``"lots"`` or ``"dollars"`` -- how the position plot is denominated."""
        return "lots"

    def normalizer(self, cfg: dict, price_df: pd.DataFrame) -> float:
        """Dollar position cap that MDD / turnover percentages are taken against.
        NaN disables the percentage figures."""
        return float("nan")


class GeuantSpec(StrategyPlotSpec):
    """Plot spec for the Geuant market-making strategy."""

    def _param_parts(self, cfg: dict) -> dict[str, str]:
        """Ordered ``{config_key: 'key=value'}`` for the params shown in titles.
        Keyed by config field so callers can drop specific params."""
        parts = {
            "spread_const_bp": f"spread_const_bp={cfg['spread_const_bp']:.2f}",
            "skew_ratio": f"skew_ratio={cfg['skew_ratio']:.2f}",
            "spread_cap_bp": f"spread_cap_bp={cfg['spread_cap_bp']:.2f}",
            "bbo_cap_buffer_bp": f"bbo_cap_buffer_bp={cfg['bbo_cap_buffer_bp']:.2f}",
        }
        if cfg.get("position_in_dollar"):
            parts["position_limit_in_dollar"] = (
                f"position_limit_in_dollar=${cfg['position_limit_in_dollar']:.2f}"
            )
        else:
            parts["position_limit_in_lots"] = (
                f"position_limit_in_lots={cfg['position_limit_in_lots']:.2f}"
            )
        parts["order_num"] = f"order_num={cfg['order_num']}"
        return parts

    def param_summary(self, cfg: dict, exclude: tuple[str, ...] = ()) -> str | None:
        try:
            parts = [v for k, v in self._param_parts(cfg).items() if k not in exclude]
        except KeyError:
            # Live config is assembled from whatever files are on disk; a run
            # launched purely from CLI arguments leaves gaps here. A missing
            # param line is better than refusing to plot the session.
            return None
        return "  |  ".join(parts) if parts else None

    def position_unit(self, cfg: dict) -> str:
        return "dollars" if cfg.get("position_in_dollar") else "lots"

    def normalizer(self, cfg: dict, price_df: pd.DataFrame) -> float:
        # Dollar mode uses position_limit_in_dollar directly; lots mode converts
        # the cap to dollars via lot_size and the mean mid over the horizon.
        if cfg.get("position_in_dollar"):
            return float(cfg["position_limit_in_dollar"])
        if "position_limit_in_lots" not in cfg or price_df.empty:
            return float("nan")
        return (
            float(cfg["position_limit_in_lots"])
            * float(cfg.get("lot_size", 1.0))
            * float(price_df["mid"].mean())
        )


# Registered strategies, keyed by the strategy_name in the trader config.
STRATEGY_SPECS: dict[str, StrategyPlotSpec] = {
    "Geuant": GeuantSpec(),
}

_DEFAULT_SPEC = StrategyPlotSpec()


def get_strategy_spec(cfg: dict) -> StrategyPlotSpec:
    """The plot spec for this run's ``strategy_name``, or the safe default."""
    return STRATEGY_SPECS.get(cfg.get("strategy_name"), _DEFAULT_SPEC)
