#pragma once
#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <atomic>

#include <moodycamel/blockingconcurrentqueue.h>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include "market_listener/exchange/exchange_listener.hpp"
#include "market_listener/exchange/kis/code_manager_base.hpp"
#include "market_listener/exchange/kis/kis_ws_client.hpp"
#include "market_listener/exchange/kis/kis_listener_dtypes.hpp"


namespace Omni::Listener::KIS {

// KIS adapter: a single plain-ws market socket carrying orderbook/trade/execution
// frames. Owns subscription, reconnect, and parse->normalize dispatch.
class KisListener : public IExchangeListener {
    public:
        KisListener(
            const ListenerConfig& config,
            quill::Logger* logger,
            moodycamel::BlockingConcurrentQueue<ListenerEvent>* event_queue
        );
        ~KisListener() override;

        void start() override;
        void stop() override;

    private:
        quill::Logger* logger_;
        ListenerConfig config_;
        moodycamel::BlockingConcurrentQueue<ListenerEvent>* event_queue_;

        std::string ws_domain_, hts_id_;
        std::shared_ptr<Omni::KIS::CodeManager::ICodeManager> code_manager_;
        std::vector<std::string> subscription_messages_;

        moodycamel::BlockingConcurrentQueue<WsResponse> ws_response_queue_;
        std::unique_ptr<KisWebsocketClient> ws_client_;
        bool ws_client_connecting_, ws_client_opened_;
        int ws_client_reconnect_cnt_;
        std::unique_ptr<boost::asio::steady_timer> ws_client_reconnect_timer_;

        std::unique_ptr<boost::asio::io_context> io_context_;
        std::thread io_thread_;
        std::thread worker_thread_;
        std::atomic<bool> running_;

        void create_code_manager();
        void create_ws_client();
        std::string write_ws_request_msg(const Omni::KIS::CodeManager::SubscriptionInput& input);

        void worker_loop();
        void on_ws_status(const WsStatusUpdate& update);
        void on_ws_client_open();
        void on_ws_client_fail();
        void on_market_data(const WsMarketDataResponse& response);
};

} // namespace Omni::Listener::KIS
