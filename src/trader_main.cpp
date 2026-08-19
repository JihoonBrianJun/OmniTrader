#include <iostream>
#include <memory>
#include <filesystem>
#include <argparse/argparse.hpp>

#include <quill/Backend.h>
#include <quill/LogMacros.h>
#include "loggers/logger.hpp"

#include "utils/datetime.hpp"
#include "config_handlers/json_config.hpp"
#include "trader/trader_dtypes.hpp"
#include "trader/strategy/init_strategy.hpp"
#include "trader/order_handler/order_handler.hpp"

// Two launch forms:
//   build/trader --exchange binance --min_tick_size 0.1 --order_lots 1 ...
//   build/trader config/binance/trader0.json config/binance/market0.json \
//                config/binance/strategy0.json
//
// The three files are separate because they change on different schedules: wiring
// (trader), the venue's price/qty units (market, shared by every trader on that
// exchange) and the strategy's parameters (swapped per run).


int main(int argc, char* argv[]) {
    Omni::Trader::TraderConfig config;
    // The process-global price/quantity units. Fixed for the run: everything
    // internal is a count of these, and the listener's per-product grids are applied
    // only when an order goes out.
    Omni::Trader::MarketConfig market_config;
    std::shared_ptr<Omni::Trader::BaseStrategy> strategy;

    if (Omni::Config::is_json_launch(argc, argv)) {
        try {
            auto paths = Omni::Config::json_launch_paths(argc, argv);
            if (paths.size() != 3) {
                throw std::runtime_error(
                    "usage: trader <trader_config.json> <market_config.json> "
                    "<strategy_config.json>"
                );
            }
            config.parse_json(Omni::Config::load_json_file(paths[0]));
            market_config.parse_json(
                Omni::Config::load_json_file(paths[1]), config.exchange
            );
            strategy = Omni::Trader::init_strategy_from_json(
                market_config, config.strategy_name,
                Omni::Config::load_json_file(paths[2])
            );
        } catch (const std::exception& e) {
            std::cerr << "Config error: " << e.what() << std::endl;
            return 1;
        }
    } else {
        // Header parse to learn the exchange + strategy before registering their args.
        // default_arguments::none keeps this throwaway parser from owning -h/-v: it
        // would otherwise print its own two-flag usage and exit before the real
        // parser below had registered anything, so --help could never show the
        // strategy's own arguments.
        argparse::ArgumentParser header(
            "HeaderParser", "1.0", argparse::default_arguments::none
        );
        Omni::Trader::TraderConfig::set_header_parser(header);
        header.parse_known_args(argc, argv);
        auto strategy_name = header.get<std::string>("--strategy_name");

        argparse::ArgumentParser program("trader");
        Omni::Trader::TraderConfig::set_parser(program);
        Omni::Trader::MarketConfig::set_parser(program);
        Omni::Trader::set_strategy_specific_parser(strategy_name, program);

        try {
            program.parse_args(argc, argv);
        } catch (const std::exception& e) {
            // A common cause is strategy-specific args (e.g. --spread_const_bp) with a
            // --strategy_name that has no registered parser (note: names are
            // case-sensitive, e.g. "Geuant").
            std::cerr << "Error: " << e.what() << std::endl;
            std::cerr << program;
            return 1;
        }

        config.init(program);
        market_config.init(program);
        strategy = Omni::Trader::init_strategy(market_config, program);
    }

    if (!strategy) {
        std::cerr << "Unknown strategy: " << config.strategy_name << std::endl;
        return 1;
    }

    quill::BackendOptions backend_options;
    quill::Backend::start(backend_options);

    std::filesystem::create_directories(std::filesystem::path(config.log_path).parent_path());
    auto logger_obj = std::make_unique<Omni::Logger::LoggerObj>("trader", config.log_path);
    auto logger = logger_obj->create_or_get_logger();

    std::unique_ptr<Omni::Trader::OrderHandler> order_handler;
    try {
        order_handler = std::make_unique<Omni::Trader::OrderHandler>(
            market_config, strategy, config, logger
        );
    } catch (const std::exception& e) {
        // Misconfiguration (e.g. missing min_tick_size/default_lot_size) should read
        // as an error, not an abort trace.
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    while (
        config.market_end_intraday_minute < 0 ||
        Omni::get_curr_intraday_minute(config.timezone_minute_offset) < config.market_end_intraday_minute
    ) {
        try {
            order_handler->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger, "Exception while trading: {}", e.what());
        }
    }

    return 0;
}
