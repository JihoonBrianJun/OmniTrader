#pragma once
#include <string>
#include <vector>
#include <quill/Logger.h>
#include "trader/trader_dtypes.hpp"
#include "trader/order_gateway/order_gateway_dtypes.hpp"
#include "strategy_dtypes.hpp"

namespace Omni::Strategy {

class IStrategy {
    public:
        IStrategy(const StrategyConfig& config, quill::Logger* logger);
        virtual ~IStrategy() = default;

        virtual void update_orders(
            const std::string& code,
            const Omni::Trader::BBO& bbo_info,
            long position_in_lots,
            Omni::Trader::CodeOutstandingOrders& outstanding_bid_orders,
            Omni::Trader::CodeOutstandingOrders& outstanding_ask_orders,
            std::vector<Omni::OrderGateway::OrderPlaceInfo>& order_place_infos,
            std::vector<Omni::OrderGateway::OrderAmendInfo>& order_amend_infos,
            std::vector<Omni::OrderGateway::OrderCancelInfo>& order_cancel_infos
        ) = 0;

    protected:
        quill::Logger* logger_;
        double min_tick_size_, lot_size_;

        long get_price_in_min_ticks(double price);
        long get_qty_in_lots(double qty);
};

} // namespace Omni::Strategy
