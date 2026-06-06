#include <iostream>
#include <boost/program_options.hpp>

#include <quill/Backend.h>
#include <quill/LogMacros.h>
#include "loggers/logger.hpp"

#include "utils/datetime.hpp"
#include "market_listener/market_listener.hpp"


int main(int argc, char* argv[]) {
    namespace po = boost::program_options;
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "produce help message")
        ("exchange", po::value<std::string>()->default_value("binance"), "exchange (binance or kis)")
        ("region", po::value<std::string>()->default_value(""), "region (kis only, e.g. korea)")
        ("market_type", po::value<std::string>()->default_value("derivatives"), "market type")
        ("is_night", po::value<bool>()->default_value(false), "is night trading (kis)")
        ("timezone_minute_offset", po::value<long>()->default_value(0), "timezone minute offset")
        ("market_end_intraday_minute", po::value<long>()->default_value(-1),
            "market end intraday minute; <0 runs until killed (binance)")
        ("codes", po::value<std::vector<std::string>>()->multitoken()->default_value(
            std::vector<std::string>{"BTCUSDT"}, "BTCUSDT"), "codes / symbols")
        ("codes_db_base_path", po::value<std::string>()->default_value("codes"), "codes db base path")
        ("orderbook_save_path", po::value<std::string>()->default_value("orderbook_record"), "orderbook save path")
        ("trade_save_path", po::value<std::string>()->default_value("trade_record"), "trade save path")
        ("orderbook_levels", po::value<int>()->default_value(20), "top-N orderbook levels to broadcast")
        ("broadcast_host_address", po::value<std::string>()->default_value("0.0.0.0"), "broadcast host")
        ("broadcast_port", po::value<unsigned short>()->default_value(8888), "broadcast port")
        ("domain_type", po::value<std::string>()->default_value("real"), "domain type (real or test)")
        ("log_path", po::value<std::string>()->default_value("logs/listener.log"), "log path");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const po::error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cout << desc << std::endl;
        return 1;
    }
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }

    quill::BackendOptions backend_options;
    quill::Backend::start(backend_options);

    auto logger_obj = std::make_unique<Omni::Logger::LoggerObj>(
        "listener", vm["log_path"].as<std::string>()
    );
    auto logger = logger_obj->create_or_get_logger();

    auto timezone_minute_offset = vm["timezone_minute_offset"].as<long>();
    auto listener_config = Omni::Listener::ListenerConfig{
        .exchange = vm["exchange"].as<std::string>(),
        .region = vm["region"].as<std::string>(),
        .market_type = vm["market_type"].as<std::string>(),
        .is_night = vm["is_night"].as<bool>(),
        .timezone_minute_offset = timezone_minute_offset,
        .codes = vm["codes"].as<std::vector<std::string>>(),
        .codes_db_base_path = vm["codes_db_base_path"].as<std::string>(),
        .orderbook_save_path = vm["orderbook_save_path"].as<std::string>(),
        .trade_save_path = vm["trade_save_path"].as<std::string>(),
        .broadcast_host_address = vm["broadcast_host_address"].as<std::string>(),
        .broadcast_port = vm["broadcast_port"].as<unsigned short>(),
        .domain_type = vm["domain_type"].as<std::string>(),
        .orderbook_levels = vm["orderbook_levels"].as<int>()
    };

    auto listener = std::make_unique<Omni::Listener::MarketListener>(listener_config, logger);

    auto market_end = vm["market_end_intraday_minute"].as<long>();
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
