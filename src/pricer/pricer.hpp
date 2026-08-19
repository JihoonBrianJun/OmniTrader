#pragma once

#include <map>
#include <memory>
#include <string>
#include <thread>
#include <functional>

#include <quill/Logger.h>
#include <moodycamel/blockingconcurrentqueue.h>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <argparse/argparse.hpp>

#include "connection_handlers/tcp/market_feed_client.hpp"
#include "connection_handlers/tcp/tcp_server.hpp"
#include "pricer/pricer_dtypes.hpp"
#include "pricer/market_state.hpp"
#include "pricer/fair_price/fair_price.hpp"
#include "pricer/factor/base_factor.hpp"


namespace Omni::Pricer {

// The `pricer` executable's engine. It sits between the other two processes and
// speaks both sides of the same internal protocol:
//
//   listener --(market feed)--> [TcpClient] Pricer [TcpServer] --(fair price)--> trader
//
// Upstream it subscribes to the listener exactly as a trader does; downstream it runs
// its own subscribe/broadcast server, so a trader consumes the fair-price feed with
// the same client and handshake it already uses for market data.
//
// Fair price is owned here rather than in the trader so that one definition serves
// every trader on the feed, and so a new fair-price logic is deployed by restarting
// one process. Traders keep their own top-of-book (straight from the listener) and
// use it as the fallback whenever this feed is unavailable.
class Pricer {
public:
    // Builds one factor instance. Factors are stateful per product, so this is
    // called once per configured product. Taking a builder rather than a parser is
    // what lets the pricer be launched either from flags or from a JSON config
    // without the service itself knowing which.
    using FactorBuilder = std::function<std::unique_ptr<BaseFactor>()>;

    Pricer(
        const PricerConfig& config,
        const FactorConfig& factor_config,
        const FactorBuilder& make_factor,
        quill::Logger* logger
    );
    ~Pricer();

    // Blocks on one task (feed event or publish tick) and processes it.
    void run();

private:
    quill::Logger* logger_;
    PricerConfig config_;
    long publish_interval_ns_;

    // Per-product accumulated market state and its (stateful) fair-price calculator.
    struct ProductState {
        MarketState market;
        std::unique_ptr<FairPriceCalculator> fair_price;
        bool book_seen = false;
    };
    std::map<std::string, ProductState> product_states_;

    moodycamel::BlockingConcurrentQueue<Task> task_queue_;

    // Upstream link to the listener. The client owns its socket, io_context and
    // thread, and keeps retrying the listener on its own, so a listener restart
    // doesn't silently end the fair-price feed for every trader downstream.
    std::unique_ptr<Connection::MarketFeedClient<Task>> listener_client_;
    bool listener_connected_;

    // Downstream server that traders subscribe to.
    std::unique_ptr<boost::asio::io_context> server_io_context_;
    std::unique_ptr<Connection::TcpServer> tcp_server_;
    std::thread server_io_thread_;

    // Carries the publish timer alone, so the tick that paces every fair price is
    // never queued behind the upstream feed or a downstream broadcast.
    std::unique_ptr<boost::asio::io_context> timer_io_context_;
    std::thread timer_io_thread_;
    std::shared_ptr<boost::asio::steady_timer> publish_timer_;

    void start_listener_client();
    void start_tcp_server();
    void start_publish_timer();
    void set_publish_timer();

    void on_listener_status(const ListenerStatusUpdate& response);
    void process_market_data(const MarketDataResponse& response);
    void on_orderbook(const std::string& product, const OrderbookData& data);
    void on_trade(const std::string& product, const TradeData& data);

    void publish_fair_price(const std::string& product);
};

} // namespace Omni::Pricer
