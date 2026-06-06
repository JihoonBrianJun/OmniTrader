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
#include "trader/strategy/interface.hpp"
#include "trader/order_gateway/order_gateway.hpp"
#include "connection_handlers/tcp/tcp_client.hpp"


namespace Omni::Trader {

class BaseTrader {
    public:
        BaseTrader(
            std::shared_ptr<Omni::Strategy::IStrategy> strategy,
            const TraderConfig& config,
            quill::Logger* logger
        );
        virtual ~BaseTrader();

        void run();

    protected:
        quill::Logger* logger_;
        const std::string exchange_;
        const std::vector<std::string> trade_codes_, subscribe_codes_;
        const std::string region_, market_type_, broadcast_host_address_;
        unsigned short broadcast_port_;
        bool is_night_;

        double min_tick_size_, lot_size_;
        long order_update_interval_ns_;
        std::shared_ptr<boost::asio::steady_timer> order_update_timer_;

        std::map<std::string, BBO> bbo_infos_;
        std::map<std::string, long> position_in_lots_;
        std::map<std::string, CodeOutstandingOrders> outstanding_bid_orders_, outstanding_ask_orders_;
        PendingOrders pending_orders_;

        std::shared_ptr<Omni::Strategy::IStrategy> strategy_;
        std::unique_ptr<Omni::OrderGateway::IOrderGateway> order_gateway_;

        moodycamel::BlockingConcurrentQueue<Task> trader_queue_;
        std::unique_ptr<boost::asio::io_context> trader_io_context_;
        std::thread trader_context_thread_;

        std::unique_ptr<Connection::TcpClient> tcp_client_;
        bool tcp_client_connecting_, tcp_client_connected_;

        void set_order_update_timer();
        void start_tcp_client();

        // Code-mapping hooks. Default identity (Binance); KIS overrides.
        virtual std::string parse_code(const std::string& code) { return code; }
        virtual std::string convert_code_for_subscription(std::string code) { return code; }

        void on_tcp_client_open();
        void on_tcp_client_fail();
        void on_tcp_client_status_update(const TcpStatusUpdate& response);
        void on_tcp_subscribe_update(const TcpSubscribeUpdate& response);

        long get_price_in_min_ticks(double price);
        long get_qty_in_lots(double qty);

        void update_bbo(const std::string& code, const BBO& bbo, bool update_mid_vwap = false);
        void update_position(const std::string& code, long position_delta_in_lots);
        void set_position(const std::string& code, long position_in_lots);
        void update_outstanding_orders(
            bool is_bid, const std::string& code,
            const std::string& order_no, long qty_decr_in_lots
        );

        void on_orderbook_data(const std::string& code, const OrderbookData& data);
        void on_trade_data(const std::string& code, const TradeData& data);
        void on_execution_data(const std::string& code, const ExecutionData& data);
        void on_position_data(const std::string& code, const PositionData& data);
        void process_market_data(const TcpMarketDataResponse& response);

        void update_orders(const std::string& code);
};

// Factory: build the order gateway for the configured exchange.
std::unique_ptr<Omni::OrderGateway::IOrderGateway> create_order_gateway(
    const TraderConfig& config, quill::Logger* logger
);

} // namespace Omni::Trader
