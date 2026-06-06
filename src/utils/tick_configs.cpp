#include "tick_configs.hpp"

namespace Omni {

// Per-exchange variable tick-size functions. Mirrors orderbook-backtest; returns
// nullptr (fixed min_tick_size) until exchange-specific schedules are added.
TickFunc get_tick_func(const std::string& /*exchange*/) {
    return nullptr;
}

double get_min_tick(const std::string& /*exchange*/) {
    return 0.0;
}

} // namespace Omni
