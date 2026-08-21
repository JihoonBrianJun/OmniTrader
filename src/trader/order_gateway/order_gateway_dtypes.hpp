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
    // Close-only: the venue must reject this order if it would open or increase a
    // position rather than reduce one. Set on the trader's shutdown flattening
    // orders, where a position count that is a fill out of date would otherwise turn
    // a close into a fresh position the other way. Ignored by venues that have no
    // such concept (KIS cash equities).
    bool reduce_only = false;
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
    // The venue rejected this request because the order is not there any more --
    // already filled, already cancelled, expired. Set by the gateway, which is the
    // only place that knows what its exchange's codes mean.
    //
    // It matters because the trader's default reading of a failed cancel is "the
    // order is still working, keep it": correct for a transport failure, and exactly
    // wrong here. Without this distinction a filled order stays in the outstanding
    // set for good and is re-cancelled on every decision, which is what turned a
    // silent user-data feed into tens of thousands of rejected cancels.
    bool order_gone = false;
};

} // namespace Omni::OrderGateway
