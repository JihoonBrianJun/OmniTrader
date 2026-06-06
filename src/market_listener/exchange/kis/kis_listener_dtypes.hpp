#pragma once
#include <string>
#include <vector>
#include <variant>
#include "market_listener/exchange/kis/product_manager_base.hpp"

namespace Omni::Listener::KIS {

struct WsStatusUpdate {
    bool connecting;
    bool opened;
};

struct WsMarketDataResponse {
    Omni::KIS::ProductManager::TrIdType tr_id_type;
    unsigned long data_num;
    std::vector<std::string> market_data;
};

using WsResponse = std::variant<WsStatusUpdate, WsMarketDataResponse>;

} // namespace Omni::Listener::KIS
