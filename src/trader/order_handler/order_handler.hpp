#pragma once

#include <memory>
#include <map>
#include <string>
#include <thread>

#include <quill/Logger.h>
#include <moodycamel/blockingconcurrentqueue.h>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include "trader/trader_dtypes.hpp"
#include "trader/pricer/pricer.hpp"
#include "trader/strategy/base_strategy.hpp"
#include "trader/order_gateway/order_gateway.hpp"
#include "trader/order_handler/order_handler_dtypes.hpp"
#include "connection_handlers/tcp/tcp_client.hpp"


namespace Omni::Trader {

// Live equivalent of orderbook-backtest BaseOrderHandler. Drives the same
// pricer -> strategy.make_decision -> order submission flow, but sourced from the
// listener's TCP market/user feed and routed through an IOrderGateway (the real
// exchange) instead of a simulated matching engine.
class OrderHandler {
public:
    OrderHandler(
        const MarketConfig& market_config,
        const PricerConfig& pricer_config,
        std::shared_ptr<BaseStrategy> strategy,
        const TraderConfig& config,
        quill::Logger* logger
    );
    ~OrderHandler();

    void run();

private:
    quill::Logger* logger_;
    MarketConfig market_config_;
    double min_tick_size_, lot_size_;
    std::shared_ptr<BaseStrategy> strategy_;

    const std::vector<std::string> trade_products_;
    const std::vector<ProductSpec> subscribe_products_;
    const std::string broadcast_host_address_;
    unsigned short broadcast_port_;
    long order_update_interval_ns_;

    std::unique_ptr<Omni::OrderGateway::IOrderGateway> order_gateway_;
    Pricer pricer_;

    // tick/lot become ready either from CLI fallback (if provided) or once the
    // listener publishes product info; orders wait until then.
    bool product_info_ready_;

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
    std::unique_ptr<boost::asio::io_context> io_context_;
    std::thread io_thread_;
    std::shared_ptr<boost::asio::steady_timer> order_update_timer_;

    std::unique_ptr<Connection::TcpClient> tcp_client_;
    bool tcp_connecting_, tcp_connected_;

    void set_order_update_timer();
    void start_tcp_client();

    void on_tcp_status(const TcpStatusUpdate& response);
    void on_tcp_subscribe(const TcpSubscribeUpdate& response);
    void process_market_data(const TcpMarketDataResponse& response);

    int64_t to_price_in_min_ticks(double price);
    int32_t to_qty_in_lots(double qty);
    double to_double_price(int64_t price_in_min_ticks);
    double to_double_qty(int32_t qty_in_lots);

    void on_orderbook(const std::string& product, const OrderbookData& data);
    void on_execution(const std::string& product, const ExecutionData& data);
    void on_position(const std::string& product, const PositionData& data);
    void on_product_info(const std::string& product, const ProductInfoData& data);
    void on_order_response(const Omni::OrderGateway::OrderResponse& response);
    std::string format_positions() const;

    // Reserve the next cid (send-time ns, made strictly increasing) and record which
    // product owns it.
    uint64_t reserve_cid(const std::string& product);
    // Drop an order (cid) and all of its index entries.
    void forget_order(ProductState& state, uint64_t cid);
    void update_orders(const std::string& product);
};

} // namespace Omni::Trader
