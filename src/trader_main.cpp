#include <iostream>
#include <memory>
#include <vector>
#include <filesystem>
#include <argparse/argparse.hpp>

#include <quill/Backend.h>
#include <quill/LogMacros.h>
#include "loggers/logger.hpp"

#include "utils/datetime.hpp"
#include "trader/strategy/init_strategy.hpp"
#include "trader/order_handler/order_handler.hpp"


int main(int argc, char* argv[]) {
    // Header parse to learn the exchange + strategy before adding their args.
    argparse::ArgumentParser header("HeaderParser");
    header.add_argument("--exchange").default_value(std::string("binance"));
    header.add_argument("--strategy_name").default_value(std::string("Geuant"));
    header.parse_known_args(argc, argv);
    auto exchange = header.get<std::string>("--exchange");
    auto strategy_name = header.get<std::string>("--strategy_name");

    argparse::ArgumentParser program("trader");
    program.add_argument("--exchange").default_value(std::string("binance"));
    program.add_argument("--strategy_name").default_value(std::string("Geuant"));
    program.add_argument("--region").default_value(std::string(""));
    program.add_argument("--market_type").default_value(std::string("derivatives"));
    program.add_argument("--product").default_value(std::string("BTCUSDT"));
    program.add_argument("--trade_codes")
        .nargs(argparse::nargs_pattern::any)
        .default_value(std::vector<std::string>{"BTCUSDT"});
    program.add_argument("--subscribe_codes")
        .nargs(argparse::nargs_pattern::any)
        .default_value(std::vector<std::string>{});
    program.add_argument("--broadcast_host_address").default_value(std::string("0.0.0.0"));
    program.add_argument("--broadcast_port").scan<'i', int>().default_value(8888);
    program.add_argument("--http_domain_type").default_value(std::string("rest_real"));
    program.add_argument("--order_update_interval_ms").scan<'i', int64_t>().default_value(int64_t{1000});
    program.add_argument("--timezone_minute_offset").scan<'i', int64_t>().default_value(int64_t{0});
    program.add_argument("--market_end_intraday_minute").scan<'i', int64_t>().default_value(int64_t{-1});
    program.add_argument("--log_base_path").default_value(std::string("./log"));
    program.add_argument("--log_path").default_value(std::string("logs/trader.log"));

    Omni::Trader::MarketConfig::set_parser(program);
    Omni::Trader::set_strategy_specific_parser(strategy_name, program);
    program.parse_args(argc, argv);

    quill::BackendOptions backend_options;
    quill::Backend::start(backend_options);

    auto log_path = program.get<std::string>("--log_path");
    std::filesystem::create_directories(std::filesystem::path(log_path).parent_path());
    auto logger_obj = std::make_unique<Omni::Logger::LoggerObj>("trader", log_path);
    auto logger = logger_obj->create_or_get_logger();

    Omni::Trader::MarketConfig market_config;
    market_config.init(program);

    auto strategy = Omni::Trader::init_strategy(market_config, program);
    if (!strategy) {
        std::cerr << "Unknown strategy: " << strategy_name << std::endl;
        return 1;
    }

    auto trade_codes = program.get<std::vector<std::string>>("--trade_codes");
    auto subscribe_codes = program.get<std::vector<std::string>>("--subscribe_codes");
    bool subscribe_same = subscribe_codes.empty();

    auto trader_config = Omni::Trader::TraderConfig{
        .exchange = exchange,
        .region = program.get<std::string>("--region"),
        .market_type = program.get<std::string>("--market_type"),
        .trade_codes = trade_codes,
        .subscribe_same_codes = subscribe_same,
        .subscribe_codes = subscribe_same ? trade_codes : subscribe_codes,
        .broadcast_host_address = program.get<std::string>("--broadcast_host_address"),
        .broadcast_port = static_cast<unsigned short>(program.get<int>("--broadcast_port")),
        .http_domain_type = program.get<std::string>("--http_domain_type"),
        .order_update_interval_ms = program.get<int64_t>("--order_update_interval_ms")
    };

    auto order_handler = std::make_unique<Omni::Trader::OrderHandler>(
        market_config, strategy, trader_config, logger
    );

    auto timezone_minute_offset = program.get<int64_t>("--timezone_minute_offset");
    auto market_end = program.get<int64_t>("--market_end_intraday_minute");
    while (
        market_end < 0 ||
        Omni::get_curr_intraday_minute(timezone_minute_offset) < market_end
    ) {
        try {
            order_handler->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger, "Exception while trading: {}", e.what());
        }
    }

    return 0;
}
