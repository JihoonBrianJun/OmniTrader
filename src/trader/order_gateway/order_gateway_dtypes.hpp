#pragma once
#include <cstdint>
#include <string>

namespace Omni::OrderGateway {

// cid: the trader's globally-unique internal order id (the send-time ns timestamp).
// It is assigned before the order is sent, carried to the exchange as the
// client-order-id, and echoed back on the (async) response so the trader can
// correlate the reply to the order.

struct OrderPlaceInfo {
    bool is_limit;
    bool is_bid;
    double price;
    double qty;
    std::string product;
    uint64_t cid = 0;
};

struct OrderAmendInfo {
    std::string order_no;
    bool amend_all = false;
    bool is_limit = true;
    double price = 0.0;
    double qty = 0.0;
    std::string product = "";
    uint64_t cid = 0;
};

struct OrderCancelInfo {
    std::string order_no;
    bool cancel_all = true;
    bool is_limit = true;
    double qty = 0.0;
    std::string product = "";
    uint64_t cid = 0;
};

struct OrderResponse {
    bool success = false;
    std::string code = "";   // exchange response/message code (not an instrument)
    std::string msg = "";
    std::string order_no = "";
    uint64_t cid = 0;        // echoes the request's cid for correlation
};

} // namespace Omni::OrderGateway
