#pragma once
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <variant>

#include <moodycamel/blockingconcurrentqueue.h>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include "config_handlers/signer.hpp"
#include "market_listener/exchange/exchange_listener.hpp"
#include "market_listener/exchange/binance/binance_ws_client.hpp"
#include "market_listener/exchange/binance/product_manager.hpp"
#include "market_listener/exchange/binance/order_book.hpp"
#include "market_listener/exchange/binance/snapshot_fetcher.hpp"


namespace Omni::Listener::Binance {

namespace OB = Omni::Binance;

// Binance USDⓈ-M adapter. Owns a public market socket (bookTicker / diff depth /
// aggTrade) and a private user socket (ORDER_TRADE_UPDATE / ACCOUNT_UPDATE),
// plus REST snapshot bootstrapping and per-socket reconnect.
class BinanceListener : public IExchangeListener {
    public:
        BinanceListener(
            const ListenerConfig& config,
            quill::Logger* logger,
            moodycamel::BlockingConcurrentQueue<ListenerEvent>* event_queue
        );
        ~BinanceListener() override;

        void start() override;
        void stop() override;

    private:
        enum class Socket { Market, User };
        struct WsStatus { Socket socket; bool connecting; bool opened; };
        struct WsPayload { Socket socket; std::string payload; };
        using WsEvent = std::variant<WsStatus, WsPayload>;

        quill::Logger* logger_;
        ListenerConfig config_;
        moodycamel::BlockingConcurrentQueue<ListenerEvent>* event_queue_;

        std::string rest_domain_, stream_domain_, domain_type_;
        std::vector<std::string> products_;   // upper-case products from config

        std::shared_ptr<Omni::Config::ISigner> signer_;
        std::unique_ptr<OB::ProductManager> product_manager_;
        std::unique_ptr<OB::SnapshotFetcher> snapshot_fetcher_;

        std::map<std::string, OB::OrderBook> order_books_;

        moodycamel::BlockingConcurrentQueue<WsEvent> ws_event_queue_;
        std::unique_ptr<OB::BinanceWebsocketClient> market_client_, user_client_;
        std::string listen_key_;
        bool market_opened_, user_opened_;
        int market_reconnect_cnt_, user_reconnect_cnt_;

        std::unique_ptr<boost::asio::io_context> io_context_;
        std::unique_ptr<boost::asio::steady_timer> market_reconnect_timer_, user_reconnect_timer_;
        std::unique_ptr<boost::asio::steady_timer> keepalive_timer_;
        std::thread io_thread_, worker_thread_;
        std::atomic<bool> running_;

        std::string market_stream_url() const;
        void create_market_client();
        void create_user_client();
        void schedule_market_reconnect();
        void schedule_user_reconnect();
        void schedule_keepalive();

        void worker_loop();
        void on_ws_status(const WsStatus& status);
        void on_market_open();
        void on_user_open();
        void resync_order_book(const std::string& product);

        void handle_market_payload(const std::string& payload);
        void handle_user_payload(const std::string& payload);
};

} // namespace Omni::Listener::Binance
