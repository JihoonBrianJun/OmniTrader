#pragma once

#include <map>
#include <set>
#include <string>
#include <cstdint>

#include "common/market_msg_types.hpp"
#include "trader/price_dtypes.hpp"
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

// What was actually sent for one order request, held from the moment it goes out
// until its gateway reply lands. It exists only so the order-record csv can put the
// request (side, price, qty, kind) and its outcome (success, order_no) on one row:
// the reply carries neither, and by the time a cancel is acked the order it refers
// to has been forgotten.
struct PendingOrderRecord {
    // Kept here as well as in cid_to_product_, because a reply can arrive after the
    // execution feed already forgot the order, and the row still needs its product.
    std::string product;
    // "place" / "amend" / "cancel"; a literal, so no allocation on the order path.
    const char* type = "place";
    bool is_bid = false;
    // For a place, what went to the venue: post-snap doubles, not the internal
    // tick/lot counts (NaN price for a market order, which carries none). For a
    // cancel, the resting order being pulled, as the trader had it recorded.
    double price = 0.0;
    double qty = 0.0;
};

// Per-traded-product live state. Outstanding orders are keyed by an internal client
// id (cid, mirroring the backtest) and mapped to the exchange order id (order_no).
struct ProductState {
    // This product's real grid on the venue, as last published by the listener.
    // Absent until product_info arrives, and absent forever on an exchange that
    // publishes none -- which is why it is optional-valued rather than a pair of
    // doubles with a sentinel.
    //
    // It is used in exactly one place: snapping an order's price and quantity on the
    // way out (OrderHandler::snap_to_product_grid). Everything else -- L1, position,
    // outstanding orders, the strategy's own arithmetic -- stays denominated in the
    // process-global MarketConfig units, so that this value changing mid-session
    // cannot reinterpret state that was recorded under the old one.
    Omni::ProductInfoData product_info;

    L1 l1;
    // Latest fair price from the pricer executable; left unset (NaN) until one
    // arrives, in which case the order handler prices off mid instead.
    FairPriceState fair_price;
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
