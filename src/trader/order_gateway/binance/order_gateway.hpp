#pragma once
#include <memory>
#include <quill/Logger.h>

#include "trader/trader_dtypes.hpp"
#include "trader/order_gateway/order_gateway.hpp"
#include "trader/order_gateway/binance/rest_order_gateway.hpp"
#include "trader/order_gateway/binance/ws_order_gateway.hpp"
#include "connection_handlers/rest/binance/binance_rest_client.hpp"


namespace Omni::Binance {

// Routes orders to the WS-API gateway when its session is up, falling back to
// REST otherwise (or when a WS request fails/times out).
class BinanceOrderGateway : public Omni::OrderGateway::IOrderGateway {
    public:
        BinanceOrderGateway(const Omni::Trader::TraderConfig& config, quill::Logger* logger);

        // Forwarded to both the WS and REST sub-gateways so either transport's reply
        // reaches the same trader sink.
        void set_response_sink(Omni::OrderGateway::IOrderGateway::ResponseSink sink) override;

        // Both sub-gateways format against one shared BinanceRestClient, so updating
        // its filter cache updates the REST and WS paths together.
        void set_product_grid(
            const std::string& product, double tick_size, double lot_size
        ) override;

        bool place_order(const Omni::OrderGateway::OrderPlaceInfo& info) override;
        bool amend_order(const Omni::OrderGateway::OrderAmendInfo& info) override;
        bool cancel_order(const Omni::OrderGateway::OrderCancelInfo& info) override;

    private:
        quill::Logger* logger_;
        std::shared_ptr<BinanceRestClient> rest_client_;
        std::unique_ptr<RestOrderGateway> rest_gateway_;
        std::unique_ptr<WsOrderGateway> ws_gateway_;

        bool ws_usable() const { return ws_gateway_ && ws_gateway_->connected(); }
};

} // namespace Omni::Binance
