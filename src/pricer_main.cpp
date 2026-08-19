#include <iostream>
#include <memory>
#include <filesystem>
#include <argparse/argparse.hpp>
#include <nlohmann/json.hpp>

#include <quill/Backend.h>
#include <quill/LogMacros.h>
#include "loggers/logger.hpp"

#include "utils/datetime.hpp"
#include "config_handlers/json_config.hpp"
#include "pricer/pricer.hpp"
#include "pricer/factor/init_factor.hpp"

// Two launch forms:
//   build/pricer --fair_price_mode FACTOR --factor_name Microprice ...
//   build/pricer config/binance/pricer0.json [config/binance/factor0.json]
//
// The factor file is only needed in FACTOR mode, mirroring the CLI: MID and VWAP
// need no factor configuration at all.


int main(int argc, char* argv[]) {
    Omni::Pricer::PricerConfig config;
    Omni::Pricer::FactorConfig factor_config;
    Omni::Pricer::Pricer::FactorBuilder make_factor;

    // Kept alive for the whole run: the CLI path's builder captures the parser.
    argparse::ArgumentParser program("pricer");
    nlohmann::json factor_doc;

    if (Omni::Config::is_json_launch(argc, argv)) {
        try {
            auto paths = Omni::Config::json_launch_paths(argc, argv);
            if (paths.empty() || paths.size() > 2) {
                throw std::runtime_error(
                    "usage: pricer <pricer_config.json> [factor_config.json]"
                );
            }
            config.parse_json(Omni::Config::load_json_file(paths[0]));

            if (paths.size() == 2) {
                factor_doc = Omni::Config::load_json_file(paths[1]);
                factor_config.parse_json(factor_doc);
            } else if (config.mode == Omni::Pricer::FairPriceMode::FACTOR) {
                throw std::runtime_error(
                    "fair_price_mode FACTOR needs a factor config: "
                    "pricer <pricer_config.json> <factor_config.json>"
                );
            }
        } catch (const std::exception& e) {
            std::cerr << "Config error: " << e.what() << std::endl;
            return 1;
        }
        make_factor = [&factor_config, &factor_doc]() {
            return Omni::Pricer::init_factor_from_json(factor_config, factor_doc);
        };
    } else {
        // Header parse to learn the factor before registering its args.
        // default_arguments::none keeps this throwaway parser from owning -h/-v: it
        // would otherwise print its own one-flag usage and exit before the real
        // parser below had registered anything, so --help could never show the
        // factor's own arguments.
        argparse::ArgumentParser header(
            "HeaderParser", "1.0", argparse::default_arguments::none
        );
        Omni::Pricer::FactorConfig::set_header_parser(header);
        header.parse_known_args(argc, argv);
        auto factor_name = header.get<std::string>("--factor_name");

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

        config.init(program);
        factor_config.init(program);
        make_factor = [&factor_config, &program]() {
            return Omni::Pricer::init_factor(factor_config, program);
        };
    }

    quill::BackendOptions backend_options;
    quill::Backend::start(backend_options);

    std::filesystem::create_directories(std::filesystem::path(config.log_path).parent_path());
    auto logger_obj = std::make_unique<Omni::Logger::LoggerObj>("pricer", config.log_path);
    auto logger = logger_obj->create_or_get_logger();

    std::unique_ptr<Omni::Pricer::Pricer> pricer;
    try {
        pricer = std::make_unique<Omni::Pricer::Pricer>(
            config, factor_config, make_factor, logger
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
