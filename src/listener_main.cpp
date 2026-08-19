#include <iostream>
#include <memory>
#include <filesystem>
#include <argparse/argparse.hpp>

#include <quill/Backend.h>
#include <quill/LogMacros.h>
#include "loggers/logger.hpp"

#include "utils/datetime.hpp"
#include "config_handlers/json_config.hpp"
#include "market_listener/market_listener.hpp"

// Two launch forms:
//   build/listener --exchange binance --products BTCUSDT:futures ...
//   build/listener config/binance/listener0.json


int main(int argc, char* argv[]) {
    Omni::Listener::ListenerConfig config;

    if (Omni::Config::is_json_launch(argc, argv)) {
        try {
            auto paths = Omni::Config::json_launch_paths(argc, argv);
            if (paths.size() != 1) {
                throw std::runtime_error(
                    "usage: listener <listener_config.json>"
                );
            }
            config.parse_json(Omni::Config::load_json_file(paths[0]));
        } catch (const std::exception& e) {
            std::cerr << "Config error: " << e.what() << std::endl;
            return 1;
        }
    } else {
        argparse::ArgumentParser program("listener");
        Omni::Listener::ListenerConfig::set_parser(program);

        try {
            program.parse_args(argc, argv);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            std::cerr << program;
            return 1;
        }
        config.init(program);
    }

    quill::BackendOptions backend_options;
    quill::Backend::start(backend_options);

    std::filesystem::create_directories(std::filesystem::path(config.log_path).parent_path());
    auto logger_obj = std::make_unique<Omni::Logger::LoggerObj>("listener", config.log_path);
    auto logger = logger_obj->create_or_get_logger();

    auto listener = std::make_unique<Omni::Listener::MarketListener>(config, logger);

    while (
        config.market_end_intraday_minute < 0 ||
        Omni::get_curr_intraday_minute(config.timezone_minute_offset) < config.market_end_intraday_minute
    ) {
        try {
            listener->listen();
        } catch (const std::exception& e) {
            LOG_WARNING(logger, "Exception while listening: {}", e.what());
        }
    }

    return 0;
}
