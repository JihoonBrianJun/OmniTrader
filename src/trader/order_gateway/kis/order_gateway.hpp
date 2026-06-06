#pragma once
#include <memory>
#include <quill/Logger.h>
#include "trader/trader_dtypes.hpp"
#include "trader/order_gateway/order_gateway.hpp"

namespace Omni::KIS {

// Build a KIS REST order gateway for the configured market.
std::unique_ptr<Omni::OrderGateway::IOrderGateway> create_kis_order_gateway(
    const Omni::Trader::TraderConfig& config, quill::Logger* logger
);

} // namespace Omni::KIS
