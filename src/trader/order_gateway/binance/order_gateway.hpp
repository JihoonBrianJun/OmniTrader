#pragma once
#include <memory>
#include <quill/Logger.h>

#include "trader/trader_dtypes.hpp"
#include "trader/order_gateway/order_gateway.hpp"
#include "trader/order_gateway/binance/ws_order_gateway.hpp"
#include "connection_handlers/rest/binance/binance_rest_client.hpp"


namespace Omni::Binance {

// Binance order routing: the WS-API session, and nothing else.
//
// There used to be a signed-REST gateway behind this one, taken whenever the WS
// session was not usable. It was removed: the WS session had no redial, so it went
// down for good one ping interval into a run and every order after that went over
// REST -- one TLS handshake per request, serialized behind every other request, and
// arriving at the venue seconds after the price that justified it. Limit orders sent
// on a book that had since moved crossed the spread and filled as takers, which is
// not something this trader ever intends to do outside its shutdown flatten.
//
// So there is deliberately no fallback here. An order the session cannot take is
// reported failed immediately, which the trader can see and act on, rather than
// being sent by a slower path that quietly changes what the order means.
class BinanceOrderGateway : public Omni::OrderGateway::IOrderGateway {
    public:
        BinanceOrderGateway(const Omni::Trader::TraderConfig& config, quill::Logger* logger);

        // Kept on this object as well as forwarded, so the failure replies this class
        // synthesizes for un-sendable requests reach the same trader sink.
        void set_response_sink(Omni::OrderGateway::IOrderGateway::ResponseSink sink) override;

        // The WS gateway formats against this shared BinanceRestClient, so updating
        // its filter cache updates what goes out on the wire.
        void set_product_grid(
            const std::string& product, double tick_size, double lot_size
        ) override;

        bool place_order(const Omni::OrderGateway::OrderPlaceInfo& info) override;
        bool amend_order(const Omni::OrderGateway::OrderAmendInfo& info) override;
        bool cancel_order(const Omni::OrderGateway::OrderCancelInfo& info) override;

    private:
        quill::Logger* logger_;
        std::shared_ptr<BinanceRestClient> rest_client_;
        std::unique_ptr<WsOrderGateway> ws_gateway_;

        // Report a request that never left as a failed response. Without it the
        // trader would hold the order in its response-waiting set until the timeout,
        // blocking that product's decisions for a reply that is not coming.
        bool report_not_sent(uint64_t cid, const char* what);
};

} // namespace Omni::Binance
