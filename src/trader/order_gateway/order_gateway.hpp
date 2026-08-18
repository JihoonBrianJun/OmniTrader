#pragma once
#include <functional>
#include <string>
#include <utility>

#include "trader/order_gateway/order_gateway_dtypes.hpp"

namespace Omni::OrderGateway {

// Order placement abstraction. KIS implements it over REST; Binance implements it
// over the WS API with a REST fallback. The trader only sees this interface.
//
// Asynchronous by contract: place/amend/cancel only *dispatch* the request and
// return true if it was sent. The actual OrderResponse (carrying the request's cid)
// is delivered later through the response sink the owner installs. Synchronous
// gateways (REST/KIS) simply deliver inline once their HTTP call returns; the
// Binance WS gateway delivers from its socket's on_message. A false return means
// the request could not be dispatched at all (e.g. socket down) and no response
// will be delivered for it -- the caller may fall back to another transport.
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
