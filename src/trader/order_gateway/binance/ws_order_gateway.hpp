#pragma once
#include <string>
#include <memory>
#include <map>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <quill/Logger.h>

#include "config_handlers/signer.hpp"
#include "trader/order_gateway/order_gateway.hpp"
#include "connection_handlers/rest/binance/binance_rest_client.hpp"
#include "connection_handlers/websocket/binance/binance_ws_client.hpp"


namespace Omni::Binance {

namespace OG = Omni::OrderGateway;

// Binance WS-API order gateway (wss://ws-fapi..., order.place/order.cancel).
// Each request is HMAC-signed per the WS-API rules (params sorted ascending).
// Requests block on a matching response id with a short timeout.
class WsOrderGateway : public OG::IOrderGateway {
    public:
        WsOrderGateway(
            quill::Logger* logger,
            const std::string& ws_api_domain,
            std::shared_ptr<Omni::Config::ISigner> signer,
            std::shared_ptr<BinanceRestClient> rest_client
        );
        ~WsOrderGateway() override;

        bool connected() const { return connected_.load(); }

        void place_order(const OG::OrderPlaceInfo& info, OG::OrderResponse& response) override;
        void amend_order(const OG::OrderAmendInfo& info, OG::OrderResponse& response) override;
        void cancel_order(const OG::OrderCancelInfo& info, OG::OrderResponse& response) override;

    private:
        quill::Logger* logger_;
        std::string ws_api_domain_;
        std::shared_ptr<Omni::Config::ISigner> signer_;
        std::shared_ptr<BinanceRestClient> rest_client_;

        std::unique_ptr<BinanceWebsocketClient> client_;
        std::atomic<bool> connected_;
        std::atomic<unsigned long> request_counter_;

        std::mutex mtx_;
        std::condition_variable cv_;
        std::map<std::string, OG::OrderResponse> results_;
        std::map<std::string, bool> ready_;

        void on_message(const std::string& payload);
        // params: ordered map (sorted by key) of request params without apiKey/
        // timestamp/recvWindow/signature, which are added here.
        OG::OrderResponse send_request(
            const std::string& method, std::map<std::string, std::string> params
        );
};

} // namespace Omni::Binance
