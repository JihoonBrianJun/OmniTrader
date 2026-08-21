#pragma once
#include <string>
#include <memory>
#include <map>
#include <atomic>
#include <mutex>
#include <thread>
#include <quill/Logger.h>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include "config_handlers/signer.hpp"
#include "trader/order_gateway/order_gateway.hpp"
#include "connection_handlers/rest/binance/binance_rest_client.hpp"
#include "connection_handlers/websocket/binance/binance_ws_client.hpp"


namespace Omni::Binance {

namespace OG = Omni::OrderGateway;

// Binance WS-API order gateway (wss://ws-fapi..., order.place/order.cancel).
// Each request is HMAC-signed per the WS-API rules (params sorted ascending) and
// fired without blocking: the request id is set to the order's cid, so the reply
// (parsed in on_message, success or error) is correlated back by cid and delivered
// through the response sink. place/amend/cancel return false only when the request
// could not be sent (session down / send threw); with no second transport behind it,
// the owner turns that into a failed response for the trader.
//
// The session is kept up on its own: a dropped socket is redialled with a bounded
// backoff, because this is the only way orders reach the venue -- including the
// cancels the shutdown sequence depends on.
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

        bool place_order(const OG::OrderPlaceInfo& info) override;
        bool amend_order(const OG::OrderAmendInfo& info) override;
        bool cancel_order(const OG::OrderCancelInfo& info) override;

    private:
        quill::Logger* logger_;
        std::string ws_api_domain_;
        std::shared_ptr<Omni::Config::ISigner> signer_;
        std::shared_ptr<BinanceRestClient> rest_client_;

        // Guards client_ itself, not the socket. Three threads reach it: the trader
        // thread sending an order, io_thread_ replacing it on a redial, and the
        // owner destroying the gateway. Uncontended in the normal case -- a redial
        // is the only writer, and there is at most one in flight.
        std::unique_ptr<BinanceWebsocketClient> client_;
        mutable std::mutex client_mutex_;
        std::atomic<bool> connected_;

        // Redial machinery. It gets its own context and thread because the client can
        // only be destroyed from a thread that is not its own: the status callback
        // runs on the socket's thread, so the reconnect it asks for is posted here and
        // the teardown happens from this thread instead.
        std::atomic<bool> running_;
        std::atomic<bool> reconnect_pending_;
        int reconnect_cnt_ = 0;               // touched only on io_thread_
        std::unique_ptr<boost::asio::io_context> io_context_;
        std::thread io_thread_;
        std::unique_ptr<boost::asio::steady_timer> reconnect_timer_;

        void create_client();
        void schedule_reconnect();

        void on_message(const std::string& payload);
        // Signs `params` and sends the request with id=cid (so the async reply is
        // correlated by cid). Returns false if the request could not be sent.
        // params: ordered map (sorted by key) of request params without apiKey/
        // timestamp/recvWindow/signature, which are added here.
        bool send_request(
            const std::string& method, std::map<std::string, std::string> params, uint64_t cid
        );
};

} // namespace Omni::Binance
