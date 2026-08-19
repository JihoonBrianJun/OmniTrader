#include "init_factor.hpp"

namespace Omni::Pricer {

std::unique_ptr<BaseFactor> init_factor(
    const FactorConfig& factor_config,
    const argparse::ArgumentParser& program
) {
    if (factor_config.factor_name == "Microprice") {
        MicropriceParams params;
        params.init(program);
        return std::make_unique<MicropriceFactor>(factor_config, params);
    }
    return nullptr;
}


std::unique_ptr<BaseFactor> init_factor_from_json(
    const FactorConfig& factor_config,
    const nlohmann::json& doc
) {
    if (factor_config.factor_name == "Microprice") {
        MicropriceParams params;
        params.parse_json(doc);
        return std::make_unique<MicropriceFactor>(factor_config, params);
    }
    return nullptr;
}


void set_factor_specific_parser(
    const std::string& factor_name, argparse::ArgumentParser& program
) {
    if (factor_name == "Microprice") {
        MicropriceParams::set_parser(program);
    }
}

} // namespace Omni::Pricer
