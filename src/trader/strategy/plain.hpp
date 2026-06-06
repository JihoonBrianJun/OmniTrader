#pragma once

#include "interface.hpp"

namespace Omni::Strategy {

class PlainStrategy : public IStrategy {
    public:
        PlainStrategy(const StrategyConfig& config, quill::Logger* logger);

        void update_orders(
            const std::string& code,
            const Omni::Trader::BBO& bbo_info,
            long position_in_lots,
            Omni::Trader::CodeOutstandingOrders& outstanding_bid_orders,
            Omni::Trader::CodeOutstandingOrders& outstanding_ask_orders,
            std::vector<Omni::OrderGateway::OrderPlaceInfo>& order_place_infos,
            std::vector<Omni::OrderGateway::OrderAmendInfo>& order_amend_infos,
            std::vector<Omni::OrderGateway::OrderCancelInfo>& order_cancel_infos
        ) override;
};

} // namespace Omni::Strategy
