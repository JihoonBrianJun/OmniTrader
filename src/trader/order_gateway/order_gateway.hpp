#pragma once
#include "trader/order_gateway/order_gateway_dtypes.hpp"

namespace Omni::OrderGateway {

// Order placement abstraction. KIS implements it over REST; Binance implements
// it over the WS API with a REST fallback. The trader only sees this interface.
class IOrderGateway {
    public:
        virtual ~IOrderGateway() = default;

        virtual void place_order(
            const OrderPlaceInfo& info, OrderResponse& response
        ) = 0;
        virtual void amend_order(
            const OrderAmendInfo& info, OrderResponse& response
        ) = 0;
        virtual void cancel_order(
            const OrderCancelInfo& info, OrderResponse& response
        ) = 0;
};

} // namespace Omni::OrderGateway
