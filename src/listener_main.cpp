#include <iostream>
#include <memory>
#include <vector>
#include <filesystem>
#include <argparse/argparse.hpp>

#include <quill/Backend.h>
#include <quill/LogMacros.h>
#include "loggers/logger.hpp"

#include "utils/datetime.hpp"
#include "market_listener/market_listener.hpp"


int main(int argc, char* argv[]) {
    argparse::ArgumentParser program("listener");
    program.add_argument("--exchange").default_value(std::string("binance"));
    program.add_argument("--region").default_value(std::string(""));
    program.add_argument("--market_type").default_value(std::string("derivatives"));
    program.add_argument("--is_night").flag();
    program.add_argument("--timezone_minute_offset").scan<'i', int64_t>().default_value(int64_t{0});
    program.add_argument("--market_end_intraday_minute").scan<'i', int64_t>().default_value(int64_t{-1});
    program.add_argument("--codes")
        .nargs(argparse::nargs_pattern::any)
        .default_value(std::vector<std::string>{"BTCUSDT"});
    program.add_argument("--codes_db_base_path").default_value(std::string("codes"));
    program.add_argument("--orderbook_save_path").default_value(std::string("orderbook_record"));
    program.add_argument("--trade_save_path").default_value(std::string("trade_record"));
    program.add_argument("--orderbook_levels").scan<'i', int>().default_value(20);
    program.add_argument("--broadcast_host_address").default_value(std::string("0.0.0.0"));
    program.add_argument("--broadcast_port").scan<'i', int>().default_value(8888);
    program.add_argument("--domain_type").default_value(std::string("real"));
    program.add_argument("--log_path").default_value(std::string("logs/listener.log"));

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    quill::BackendOptions backend_options;
    quill::Backend::start(backend_options);

    auto log_path = program.get<std::string>("--log_path");
    std::filesystem::create_directories(std::filesystem::path(log_path).parent_path());
    auto logger_obj = std::make_unique<Omni::Logger::LoggerObj>("listener", log_path);
    auto logger = logger_obj->create_or_get_logger();

    auto timezone_minute_offset = program.get<int64_t>("--timezone_minute_offset");
    auto listener_config = Omni::Listener::ListenerConfig{
        .exchange = program.get<std::string>("--exchange"),
        .region = program.get<std::string>("--region"),
        .market_type = program.get<std::string>("--market_type"),
        .is_night = program.get<bool>("--is_night"),
        .timezone_minute_offset = timezone_minute_offset,
        .codes = program.get<std::vector<std::string>>("--codes"),
        .codes_db_base_path = program.get<std::string>("--codes_db_base_path"),
        .orderbook_save_path = program.get<std::string>("--orderbook_save_path"),
        .trade_save_path = program.get<std::string>("--trade_save_path"),
        .broadcast_host_address = program.get<std::string>("--broadcast_host_address"),
        .broadcast_port = static_cast<unsigned short>(program.get<int>("--broadcast_port")),
        .domain_type = program.get<std::string>("--domain_type"),
        .orderbook_levels = program.get<int>("--orderbook_levels")
    };

    auto listener = std::make_unique<Omni::Listener::MarketListener>(listener_config, logger);

    auto market_end = program.get<int64_t>("--market_end_intraday_minute");
    while (
        market_end < 0 ||
        Omni::get_curr_intraday_minute(timezone_minute_offset) < market_end
    ) {
        try {
            listener->listen();
        } catch (const std::exception& e) {
            LOG_WARNING(logger, "Exception while listening: {}", e.what());
        }
    }

    return 0;
}
