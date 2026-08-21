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
#include "market_listener/exchange/kis/product_manager_base.hpp"
#include "connection_handlers/websocket/kis/kis_ws_client.hpp"
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
        std::shared_ptr<Omni::KIS::ProductManager::IProductManager> product_manager_;
        std::vector<std::string> subscription_messages_;

        moodycamel::BlockingConcurrentQueue<WsResponse> ws_response_queue_;
        std::unique_ptr<KisWebsocketClient> ws_client_;
        bool ws_client_connecting_, ws_client_opened_;
        // See BinanceListener for why a redial must not be keyed on the up->down
        // transition alone, and why the "already scheduled" flag is needed.
        std::atomic<int> ws_client_reconnect_cnt_;
        std::atomic<bool> ws_client_reconnect_pending_{false};
        std::unique_ptr<boost::asio::steady_timer> ws_client_reconnect_timer_;

        std::unique_ptr<boost::asio::io_context> io_context_;
        std::thread io_thread_;
        std::thread worker_thread_;
        std::atomic<bool> running_;

        void create_product_manager();
        void create_ws_client();
        std::string write_ws_request_msg(const Omni::KIS::ProductManager::SubscriptionInput& input);

        void worker_loop();
        void on_ws_status(const WsStatusUpdate& update);
        void on_ws_client_open();
        void on_ws_client_fail();
        void on_market_data(const WsMarketDataResponse& response);
};

} // namespace Omni::Listener::KIS
