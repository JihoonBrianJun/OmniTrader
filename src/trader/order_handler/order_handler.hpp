#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <quill/Logger.h>
#include <moodycamel/blockingconcurrentqueue.h>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include "trader/trader_dtypes.hpp"
#include "trader/price_dtypes.hpp"
#include "trader/strategy/base_strategy.hpp"
#include "trader/order_gateway/order_gateway.hpp"
#include "trader/order_handler/order_handler_dtypes.hpp"
#include "connection_handlers/tcp/market_feed_client.hpp"
#include "connection_handlers/tcp/fair_price_client.hpp"


namespace Omni::Trader {

// Move an outbound order's price/qty from the trader's global normalization grid
// onto the product's real grid on the venue. This is the only place a product's own
// tick/lot is consulted -- see MarketConfig for why it is confined to submission.
// Returns false if the quantity rounds away to nothing on that grid, meaning there
// is no order left to send.
//
// Free rather than a member: it reads no handler state, only its arguments.
bool snap_to_product_grid(
    const Omni::ProductInfoData& info, bool is_bid, double& price, double& qty
);

// The quantity half of the above, on its own. A market order carries no price but
// its size still has to sit on the venue's lot grid.
bool snap_qty_to_product_grid(const Omni::ProductInfoData& info, double& qty);

// Live equivalent of orderbook-backtest BaseOrderHandler. Drives the same
// pricer -> strategy.make_decision -> order submission flow, but sourced from the
// listener's TCP market/user feed and routed through an IOrderGateway (the real
// exchange) instead of a simulated matching engine.
//
// The pricer half is a separate process here: top-of-book arrives from the listener
// and is kept per product, while fair price arrives from the `pricer` executable on
// its own link. The handler assembles the two into the PriceInfo the strategy expects.
class OrderHandler {
public:
    // How the handler gets its gateway. Left empty in production, where the exchange
    // named in the config picks the real one; supplying it lets a test drive the
    // handler against a stand-in exchange. Same inversion the pricer uses for its
    // factor, and for the same reason: the shutdown sequence is worth testing, and
    // testing it means being able to ack a cancel and fill an order on demand.
    using GatewayFactory = std::function<
        std::unique_ptr<Omni::OrderGateway::IOrderGateway>(
            const TraderConfig&, quill::Logger*
        )
    >;

    OrderHandler(
        const MarketConfig& market_config,
        std::shared_ptr<BaseStrategy> strategy,
        const TraderConfig& config,
        quill::Logger* logger,
        GatewayFactory make_gateway = {}
    );
    ~OrderHandler();

    // One turn of the trading loop: take the next event off the queue and handle it.
    // Returns after a short wait if nothing arrived, so the caller can notice a
    // shutdown request on a quiet market.
    void run();

    // Leave the venue clean, then return. Cancels every resting order and (unless
    // flatten_on_shutdown is off) works the position back to zero: passively first,
    // via the strategy's own liquidation quote, then with a market order for whatever
    // is left. Bounded by the two shutdown timeouts either way -- this is called with
    // an operator waiting on it, so it always terminates, and says in the log exactly
    // what it could not finish.
    //
    // Runs on the trader thread, not from a signal handler, and is idempotent.
    void shutdown();

private:
    quill::Logger* logger_;
    MarketConfig market_config_;
    // The process-global normalization units. const: every int64 price and int32
    // quantity in this handler, in ProductState, and inside the strategy is a count
    // of these, so they cannot be rebased once anything has been recorded.
    const double min_tick_size_, default_lot_size_;
    std::shared_ptr<BaseStrategy> strategy_;

    const std::vector<std::string> trade_products_;
    const std::vector<ProductSpec> subscribe_products_;
    const std::string broadcast_host_address_;
    unsigned short broadcast_port_;
    const std::string pricer_host_address_;
    unsigned short pricer_port_;
    long fair_price_max_age_ns_;
    long order_update_interval_ns_;

    // Shutdown policy; see TraderConfig for what each one buys.
    const bool flatten_on_shutdown_;
    const long shutdown_cancel_timeout_ns_;
    const long shutdown_flatten_timeout_ns_;
    const bool shutdown_market_flatten_;
    const bool shutdown_reduce_only_;

    // Set the moment shutdown() starts. Atomic because the order-update timer runs on
    // the io thread and reads it to stop re-arming; everything else reading it is on
    // the trader thread.
    std::atomic<bool> shutting_down_{false};
    bool shutdown_done_ = false;

    std::unique_ptr<Omni::OrderGateway::IOrderGateway> order_gateway_;

    std::map<std::string, ProductState> product_states_;
    std::map<std::string, std::string> order_no_to_product_;

    // Globally-unique client order id (cid) and its product routing. cid is the
    // send-time ns timestamp (monotonic, bumped on the rare same-ns tie so it never
    // repeats), assigned before an order is sent, carried to the exchange as the
    // client order id, and used to route the async order reply and execution updates
    // back to the owning product/order. ProductState's per-order maps are keyed by it.
    uint64_t last_cid_ = 0;
    std::map<uint64_t, std::string> cid_to_product_;

    // A product stops issuing new decisions while it has orders awaiting a reply; if
    // a reply is lost this clears the wait so the product can't stall forever (a real
    // order is still reconciled later via the execution feed, which carries the cid).
    long order_response_timeout_ns_ = 5L * 1000000000;

    // Latest position/balance per product, pushed by the listener (futures position
    // amount, or asset balance for asset-category products). Logged each decision so
    // position/margin-side issues are visible.
    std::map<std::string, PositionData> positions_;

    moodycamel::BlockingConcurrentQueue<Task> trader_queue_;

    // Carries the order-update timer and nothing else. Each feed client owns its own
    // io_context and thread, so the tick is never queued behind a burst on either
    // feed -- it is what paces every decision.
    std::unique_ptr<boost::asio::io_context> io_context_;
    std::thread io_thread_;
    std::shared_ptr<boost::asio::steady_timer> order_update_timer_;

    // Link to the listener (market/user data). Always dialled.
    std::unique_ptr<Connection::MarketFeedClient<Task>> listener_client_;
    bool listener_connecting_, listener_connected_;

    // Second link, to the pricer (fair-price feed). Kept independent of the market
    // data link so a pricer outage degrades pricing to mid rather than cutting the
    // feed the trader runs on. Each client retries its own peer on its own thread, so
    // one being away neither delays the other nor stalls the decision loop.
    std::unique_ptr<Connection::FairPriceClient<Task>> pricer_client_;
    bool pricer_connected_;

    void set_order_update_timer();
    void start_feed_clients();

    // The dispatch half of run(), shared with the shutdown sequence's own pumping.
    void handle_task(const Task& task);
    // Keep handling events until `done` is satisfied or the deadline passes. Used by
    // shutdown() to wait on acks and fills without the trading loop underneath it.
    void pump_until(const std::function<bool()>& done, long deadline_ns);

    void cancel_all_orders(long deadline_ns);
    void flatten_positions(long deadline_ns);
    void submit_market_flatten(const std::string& product, ProductState& state);
    bool no_orders_outstanding() const;
    bool all_flat() const;
    std::string format_residual() const;
    // Qualifies a residual report when the link that would confirm it is down.
    std::string residual_caveat() const;

    void on_listener_status(const ListenerStatusUpdate& response);
    void on_listener_subscribe(const ListenerSubscribeUpdate& response);
    void on_pricer_status(const PricerStatusUpdate& response);
    void process_market_data(const MarketDataResponse& response);

    // Conversions between doubles and the global normalization units.
    int64_t to_price_in_min_ticks(double price) const;
    int32_t to_qty_in_lots(double qty) const;
    double to_double_price(int64_t price_in_min_ticks) const;
    double to_double_qty(int32_t qty_in_lots) const;


    void on_orderbook(const std::string& product, const OrderbookData& data);
    void on_execution(const std::string& product, const ExecutionData& data);
    void on_position(const std::string& product, const PositionData& data);
    void on_product_info(const std::string& product, const ProductInfoData& data);
    void on_fair_price(const std::string& product, const FairPriceData& data);
    void on_order_response(const Omni::OrderGateway::OrderResponse& response);
    std::string format_positions() const;

    // Assemble the strategy's PriceInfo: top-of-book and mid from our own L1, fair
    // price from the pricer feed (falling back to mid when it is absent or stale).
    void build_price_info(const ProductState& state, PriceInfo& price_info);

    // Reserve the next cid (send-time ns, made strictly increasing) and record which
    // product owns it.
    uint64_t reserve_cid(const std::string& product);
    // Drop an order (cid) and all of its index entries.
    void forget_order(ProductState& state, uint64_t cid);

    // Mark a cid as awaiting a reply, stamping the wait start on the first one so the
    // response timeout has a reference point.
    void mark_waiting(ProductState& state, std::set<uint64_t>& waiting_set, uint64_t cid);
    // Send one cancel by cid. No-op if the exchange order_no is not known yet.
    void submit_cancel(ProductState& state, const std::string& product, uint64_t cid);
    // Send one order. Prices and quantities come in on the global grid and are snapped
    // to the product's own on the way out. `price_in_min_ticks` is ignored when
    // `is_limit` is false.
    void submit_place(
        ProductState& state, const std::string& product,
        bool is_bid, int64_t price_in_min_ticks, int32_t qty_in_lots,
        bool is_limit = true, bool reduce_only = false
    );

    // `do_liquidate` puts the strategy in its get-flat branch: quote the whole
    // position out at the touch and cancel everything else. Only the shutdown
    // sequence sets it.
    void update_orders(const std::string& product, bool do_liquidate = false);
};

} // namespace Omni::Trader
