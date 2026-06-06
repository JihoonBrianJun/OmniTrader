#pragma once

#include <memory>
#include <map>
#include <string>
#include <thread>

#include <glaze/glaze.hpp>
#include <quill/Logger.h>
#include <quill/LogMacros.h>
#include <moodycamel/blockingconcurrentqueue.h>
#include <boost/asio.hpp>

#include "loggers/csv_logger.hpp"
#include "connection_handlers/tcp/tcp_server.hpp"
#include "market_listener/market_listener_dtypes.hpp"
#include "market_listener/exchange/exchange_listener.hpp"


namespace Omni::Listener {

class MarketListener {
    public:
        MarketListener(const ListenerConfig& config, quill::Logger* logger);
        ~MarketListener();

        // Blocks on one normalized event and processes it (broadcast + log).
        void listen();

    private:
        quill::Logger* logger_;
        ListenerConfig config_;

        moodycamel::BlockingConcurrentQueue<ListenerEvent> event_queue_;

        std::unique_ptr<boost::asio::io_context> tcp_io_context_;
        std::unique_ptr<Connection::TcpServer> tcp_server_;
        std::thread tcp_context_thread_;

        std::unique_ptr<IExchangeListener> adapter_;

        std::map<std::string, std::shared_ptr<Logger::CsvLogger<OrderbookCsvSchema>>> orderbook_loggers_;
        std::map<std::string, std::shared_ptr<Logger::CsvLogger<TradeCsvSchema>>> trade_loggers_;

        void start_tcp_server();

        template<typename T>
        void broadcast_market_data(const T& msg) {
            if (!tcp_server_) return;
            try {
                std::string json_buffer;
                auto ec = glz::write_json(msg, json_buffer);
                if (ec) {
                    LOG_WARNING(logger_, "Failed write_json while broadcasting");
                } else {
                    tcp_server_->broadcast_to_subscribers(msg.code, json_buffer);
                    LOG_INFO(logger_, "{}", json_buffer);
                }
            } catch (const std::exception& e) {
                LOG_WARNING(logger_, "Exception while broadcasting: {}", e.what());
            }
        }

        Logger::CsvLogger<OrderbookCsvSchema>* get_orderbook_logger(const std::string& code);
        Logger::CsvLogger<TradeCsvSchema>* get_trade_logger(const std::string& code);
        void log_orderbook_data(const OrderbookMsg& msg);
        void log_trade_data(const TradeMsg& msg);
};

// Factory: build the adapter for the configured exchange. Defined in the .cpp so
// the per-exchange headers are only pulled in there.
std::unique_ptr<IExchangeListener> create_exchange_listener(
    const ListenerConfig& config,
    quill::Logger* logger,
    moodycamel::BlockingConcurrentQueue<ListenerEvent>* queue
);

} // namespace Omni::Listener
