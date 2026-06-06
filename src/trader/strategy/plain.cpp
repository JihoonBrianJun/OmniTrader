#include "plain.hpp"

namespace Omni::Strategy {

PlainStrategy::PlainStrategy(const StrategyConfig& config, quill::Logger* logger)
:   IStrategy(config, logger)
{
}


void PlainStrategy::update_orders(
    const std::string& code,
    const Omni::Trader::BBO& bbo_info,
    long position_in_lots,
    Omni::Trader::CodeOutstandingOrders& outstanding_bid_orders,
    Omni::Trader::CodeOutstandingOrders& outstanding_ask_orders,
    std::vector<Omni::OrderGateway::OrderPlaceInfo>& order_place_infos,
    std::vector<Omni::OrderGateway::OrderAmendInfo>& order_amend_infos,
    std::vector<Omni::OrderGateway::OrderCancelInfo>& order_cancel_infos
) {
    if (position_in_lots == 0) {
        auto target_bid_price = bbo_info.bbid_price * (1 - 0.0 / 10000.0);
        auto bid_price_in_min_ticks = get_price_in_min_ticks(target_bid_price);

        bool place_bid = true;
        for (const auto& [order_no, outstanding_bid_order] : outstanding_bid_orders) {
            if (get_price_in_min_ticks(outstanding_bid_order.price) == bid_price_in_min_ticks) {
                place_bid = false;
            } else {
                order_cancel_infos.emplace_back(Omni::OrderGateway::OrderCancelInfo{
                    .order_no = order_no,
                    .is_limit = true,
                    .qty = outstanding_bid_order.left_qty_in_lots * lot_size_,
                    .code = code
                });
            }
        }

        if (place_bid) {
            order_place_infos.emplace_back(Omni::OrderGateway::OrderPlaceInfo{
                .is_limit = true,
                .is_bid = true,
                .price = bid_price_in_min_ticks * min_tick_size_,
                .qty = lot_size_,
                .code = code
            });
        }
    }

    if (position_in_lots == 1) {
        auto target_ask_price = bbo_info.bask_price * (1 + 0.0 / 10000.0);
        auto ask_price_in_min_ticks = get_price_in_min_ticks(target_ask_price);

        bool place_ask = true;
        for (const auto& [order_no, outstanding_ask_order] : outstanding_ask_orders) {
            if (get_price_in_min_ticks(outstanding_ask_order.price) == ask_price_in_min_ticks) {
                place_ask = false;
            } else {
                order_cancel_infos.emplace_back(Omni::OrderGateway::OrderCancelInfo{
                    .order_no = order_no,
                    .is_limit = true,
                    .qty = outstanding_ask_order.left_qty_in_lots * lot_size_,
                    .code = code
                });
            }
        }

        if (place_ask) {
            order_place_infos.emplace_back(Omni::OrderGateway::OrderPlaceInfo{
                .is_limit = true,
                .is_bid = false,
                .price = ask_price_in_min_ticks * min_tick_size_,
                .qty = lot_size_,
                .code = code
            });
        }
    }
}

} // namespace Omni::Strategy
