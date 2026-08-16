#pragma once

#include <cstdint>
#include <string>
#include <argparse/argparse.hpp>

namespace Omni::Trader {

// Mirrors orderbook-backtest PriceInfo.
struct PriceInfo {
    int64_t bbid_price_in_min_ticks;
    int64_t bask_price_in_min_ticks;
    double mid_price;
    double fair_price;
};

// Live top-of-book snapshot fed to the pricer (mirrors the fields of
// orderbook-backtest L1Tick that the pricer consumes). -1 prices = empty side.
struct L1 {
    int64_t bbid_price_in_min_ticks = -1;
    int64_t bask_price_in_min_ticks = -1;
    int32_t bbid_qty_in_lots = 0;
    int32_t bask_qty_in_lots = 0;
};

// Mirrors orderbook-backtest PricerConfig. The backtest's FACTOR mode is not
// offered here: it reads a per-date gzip CSV of pre-computed factors through the
// backtest's data_reader, which has no live counterpart.
struct PricerConfig {
    enum class Mode {
        MID,
        VWAP
    } mode = Mode::MID;

    static void set_parser(argparse::ArgumentParser& program) {
        program.add_argument("--pricer_mode")
            .default_value(std::string("MID"))
            .choices("MID", "VWAP");
    }

    static Mode get_mode(const std::string& mode) {
        if (mode == "VWAP") return Mode::VWAP;
        else return Mode::MID;
    }

    void init(const argparse::ArgumentParser& program) {
        mode = get_mode(program.get<std::string>("--pricer_mode"));
    }
};

} // namespace Omni::Trader
