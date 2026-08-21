#pragma once
#include <functional>
#include <string>
#include <utility>

#include "trader/order_gateway/order_gateway_dtypes.hpp"

namespace Omni::OrderGateway {

// Order placement abstraction. KIS implements it over REST; Binance implements it
// over the WS API only. The trader only sees this interface.
//
// Asynchronous by contract: place/amend/cancel only *dispatch* the request and
// return true if it was accepted. The actual OrderResponse (carrying the request's
// cid) is delivered later through the response sink the owner installs. The Binance
// WS gateway delivers from its socket's on_message; the KIS REST gateway queues its
// blocking HTTP call on a worker thread of its own and delivers from there, so no
// implementation ties up the caller for a round trip. A false return means the
// request could not be accepted at all (socket down, backlog full), and the gateway
// delivers a failed response with it, so the owner's bookkeeping does not wait on a
// reply that is not coming.
//
// Ordered: a gateway sends requests to the venue in the order it received them. The
// trader relies on this -- it issues a cancel and the place that replaces it back to
// back, and a place that overtook its cancel would rest at both prices at once. The
// WS gateway gets this from writing to one socket; the KIS REST gateway gets it from
// running one worker rather than a thread per request.
//
// The sink may still fire on any thread, and replies are not ordered against each
// other -- only the sends are.
class IOrderGateway {
    public:
        using ResponseSink = std::function<void(const OrderResponse&)>;

        virtual ~IOrderGateway() = default;

        virtual void set_response_sink(ResponseSink sink) {
            response_sink_ = std::move(sink);
        }

        // Feed this gateway a product's current trading grid, as published by the
        // listener. The trader calls it whenever product_info arrives, so a gateway
        // that formats orders against its own copy of the exchange's filters does
        // not keep serving a copy frozen at construction.
        //
        // Default: nothing. A gateway that does not round or format against a
        // per-product grid (KIS) simply does not override it.
        virtual void set_product_grid(
            const std::string& /*product*/, double /*tick_size*/, double /*lot_size*/
        ) {}

        virtual bool place_order(const OrderPlaceInfo& info) = 0;
        virtual bool amend_order(const OrderAmendInfo& info) = 0;
        virtual bool cancel_order(const OrderCancelInfo& info) = 0;

    protected:
        void deliver(const OrderResponse& response) {
            if (response_sink_) response_sink_(response);
        }

        ResponseSink response_sink_;
};

} // namespace Omni::OrderGateway
