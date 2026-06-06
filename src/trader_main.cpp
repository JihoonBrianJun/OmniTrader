#include <iostream>
#include <boost/program_options.hpp>

#include <quill/Backend.h>
#include <quill/LogMacros.h>
#include "loggers/logger.hpp"

#include "utils/datetime.hpp"
#include "trader/strategy/strategy_dtypes.hpp"
#include "trader/strategy/plain.hpp"
#include "trader/base/trader.hpp"


int main(int argc, char* argv[]) {
    namespace po = boost::program_options;
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "produce help message")
        ("exchange", po::value<std::string>()->default_value("binance"), "exchange (binance or kis)")
        ("region", po::value<std::string>()->default_value(""), "region (kis only)")
        ("market_type", po::value<std::string>()->default_value("derivatives"), "market type")
        ("is_night", po::value<bool>()->default_value(false), "is night trading (kis)")
        ("timezone_minute_offset", po::value<long>()->default_value(0), "timezone minute offset")
        ("market_end_intraday_minute", po::value<long>()->default_value(-1),
            "market end intraday minute; <0 runs until killed (binance)")
        ("min_tick_size", po::value<double>()->default_value(0.1), "min tick size")
        ("lot_size", po::value<double>()->default_value(0.001), "lot size")
        ("trade_codes", po::value<std::vector<std::string>>()->multitoken()->default_value(
            std::vector<std::string>{"BTCUSDT"}, "BTCUSDT"), "trade codes")
        ("subscribe_same_codes", po::value<bool>()->default_value(true), "subscribe same as trade codes")
        ("subscribe_codes", po::value<std::vector<std::string>>()->multitoken()->default_value(
            std::vector<std::string>{"BTCUSDT"}, "BTCUSDT"), "subscribe codes")
        ("broadcast_host_address", po::value<std::string>()->default_value("0.0.0.0"), "broadcast host")
        ("broadcast_port", po::value<unsigned short>()->default_value(8888), "broadcast port")
        ("http_domain_type", po::value<std::string>()->default_value("rest_real"),
            "http domain type (rest_real or rest_test)")
        ("order_update_interval_ms", po::value<long>()->default_value(1000), "order update interval ms")
        ("log_path", po::value<std::string>()->default_value("logs/trader.log"), "log path");

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
        "trader", vm["log_path"].as<std::string>()
    );
    auto logger = logger_obj->create_or_get_logger();

    auto strategy_config = Omni::Strategy::StrategyConfig{
        .min_tick_size = vm["min_tick_size"].as<double>(),
        .lot_size = vm["lot_size"].as<double>()
    };
    auto strategy = std::make_shared<Omni::Strategy::PlainStrategy>(strategy_config, logger);

    auto trader_config = Omni::Trader::TraderConfig{
        .exchange = vm["exchange"].as<std::string>(),
        .region = vm["region"].as<std::string>(),
        .market_type = vm["market_type"].as<std::string>(),
        .is_night = vm["is_night"].as<bool>(),
        .min_tick_size = vm["min_tick_size"].as<double>(),
        .lot_size = vm["lot_size"].as<double>(),
        .trade_codes = vm["trade_codes"].as<std::vector<std::string>>(),
        .subscribe_same_codes = vm["subscribe_same_codes"].as<bool>(),
        .subscribe_codes = vm["subscribe_codes"].as<std::vector<std::string>>(),
        .broadcast_host_address = vm["broadcast_host_address"].as<std::string>(),
        .broadcast_port = vm["broadcast_port"].as<unsigned short>(),
        .http_domain_type = vm["http_domain_type"].as<std::string>(),
        .order_update_interval_ms = vm["order_update_interval_ms"].as<long>()
    };

    auto trader = std::make_unique<Omni::Trader::BaseTrader>(strategy, trader_config, logger);

    auto timezone_minute_offset = vm["timezone_minute_offset"].as<long>();
    auto market_end = vm["market_end_intraday_minute"].as<long>();
    while (
        market_end < 0 ||
        Omni::get_curr_intraday_minute(timezone_minute_offset) < market_end
    ) {
        try {
            trader->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger, "Exception while trading: {}", e.what());
        }
    }

    return 0;
}
