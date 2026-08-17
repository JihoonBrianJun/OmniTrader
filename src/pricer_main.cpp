#include <iostream>
#include <memory>
#include <filesystem>
#include <argparse/argparse.hpp>

#include <quill/Backend.h>
#include <quill/LogMacros.h>
#include "loggers/logger.hpp"

#include "utils/datetime.hpp"
#include "pricer/pricer.hpp"
#include "pricer/factor/init_factor.hpp"


int main(int argc, char* argv[]) {
    // Header parse to learn the factor before registering its args.
    argparse::ArgumentParser header("HeaderParser");
    Omni::Pricer::FactorConfig::set_header_parser(header);
    header.parse_known_args(argc, argv);
    auto factor_name = header.get<std::string>("--factor_name");

    argparse::ArgumentParser program("pricer");
    Omni::Pricer::PricerConfig::set_parser(program);
    Omni::Pricer::FactorConfig::set_parser(program);
    Omni::Pricer::set_factor_specific_parser(factor_name, program);

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& e) {
        // A common cause is factor-specific args (e.g. --imbalance_exponent) with a
        // --factor_name that has no registered parser (names are case-sensitive,
        // e.g. "Microprice").
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    Omni::Pricer::PricerConfig config;
    config.init(program);

    Omni::Pricer::FactorConfig factor_config;
    factor_config.init(program);

    quill::BackendOptions backend_options;
    quill::Backend::start(backend_options);

    std::filesystem::create_directories(std::filesystem::path(config.log_path).parent_path());
    auto logger_obj = std::make_unique<Omni::Logger::LoggerObj>("pricer", config.log_path);
    auto logger = logger_obj->create_or_get_logger();

    std::unique_ptr<Omni::Pricer::Pricer> pricer;
    try {
        pricer = std::make_unique<Omni::Pricer::Pricer>(
            config, factor_config, program, logger
        );
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    while (
        config.market_end_intraday_minute < 0 ||
        Omni::get_curr_intraday_minute(config.timezone_minute_offset) < config.market_end_intraday_minute
    ) {
        try {
            pricer->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger, "Exception while computing fair price: {}", e.what());
        }
    }

    return 0;
}
