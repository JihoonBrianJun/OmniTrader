#pragma once

#include <map>
#include <set>
#include <string>
#include <cstdint>

#include "trader/pricer/pricer_dtypes.hpp"
#include "trader/strategy/base_strategy.hpp"

namespace Omni::Trader {

// Mirrors orderbook-backtest ResponseWaitingOrders.
struct ResponseWaitingOrders {
    std::set<uint64_t> response_waiting_place_orders;
    std::set<uint64_t> response_waiting_cancel_orders;

    bool no_waiting_orders() const {
        return response_waiting_place_orders.empty()
            && response_waiting_cancel_orders.empty();
    }
};

// Per-traded-product live state. Outstanding orders are keyed by an internal client
// id (cid, mirroring the backtest) and mapped to the exchange order id (order_no).
struct ProductState {
    L1 l1;
    int32_t position_in_lots = 0;
    std::map<uint64_t, OutstandingOrder> outstanding_orders;
    std::map<uint64_t, std::string> cid_to_order_no;
    std::map<std::string, uint64_t> order_no_to_cid;
    ResponseWaitingOrders response_waiting;
    // When the response-waiting set became non-empty (ns). Used to time out a lost
    // reply so the product doesn't stall. 0 when nothing is waiting. cids are now
    // assigned by the OrderHandler (globally unique), so there is no per-product
    // counter here.
    int64_t response_waiting_since_ns = 0;
};

} // namespace Omni::Trader
