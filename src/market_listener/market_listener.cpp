#include <stdexcept>
#include <cmath>
#include <fmt/core.h>

#include "utils/datetime.hpp"
#include "market_listener/market_listener.hpp"

#include "market_listener/exchange/kis/kis_listener.hpp"
#include "market_listener/exchange/binance/binance_listener.hpp"


namespace Omni::Listener {

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;


std::unique_ptr<IExchangeListener> create_exchange_listener(
    const ListenerConfig& config,
    quill::Logger* logger,
    moodycamel::BlockingConcurrentQueue<ListenerEvent>* queue
) {
    if (config.exchange == "binance") {
        return std::make_unique<Binance::BinanceListener>(config, logger, queue);
    } else if (config.exchange == "kis") {
        return std::make_unique<KIS::KisListener>(config, logger, queue);
    }
    throw std::runtime_error(fmt::format("Unsupported exchange: {}", config.exchange));
}


MarketListener::MarketListener(const ListenerConfig& config, quill::Logger* logger)
:   logger_(logger),
    config_(config),
    tcp_io_context_(std::make_unique<boost::asio::io_context>()),
    tcp_server_(std::make_unique<Connection::TcpServer>(
        *tcp_io_context_, logger_,
        config.broadcast_host_address, config.broadcast_port
    )),
    product_info_io_context_(std::make_unique<boost::asio::io_context>())
{
    start_tcp_server();
    adapter_ = create_exchange_listener(config_, logger_, &event_queue_);
    adapter_->start();
    // Owned here, not by the adapter: the schedule is the same for every exchange,
    // and only what gets fetched differs. An adapter that has nothing to publish
    // inherits the no-op and this simply costs one idle timer.
    start_product_info_refresh();
}


MarketListener::~MarketListener() {
    // Tear the refresh down before the adapter: the timer handler calls into it.
    if (product_info_timer_) {
        product_info_timer_->cancel();
    }
    if (product_info_io_context_) {
        product_info_io_context_->stop();
    }
    if (product_info_thread_.joinable()) {
        product_info_thread_.join();
    }
    if (adapter_) {
        adapter_->stop();
    }
    if (tcp_server_) {
        tcp_server_->stop();
    }
    if (tcp_io_context_) {
        tcp_io_context_->stop();
    }
    if (tcp_context_thread_.joinable()) {
        tcp_context_thread_.join();
    }
    adapter_.reset();
}


void MarketListener::start_tcp_server() {
    tcp_server_->start();
    tcp_context_thread_ = std::thread([this]() {
        try {
            tcp_io_context_->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger_, "Exception within TCP server thread: {}", e.what());
        }
    });
}


void MarketListener::start_product_info_refresh() {
    product_info_thread_ = std::thread([this]() {
        try {
            // Publish once at startup whatever the interval is, so a listener running
            // with the refresh disabled still tells its traders the grids.
            //
            // Guarded separately from the loop below: a throw here used to take the
            // whole thread with it, so the refresh timer was never armed and the
            // listener ran for the rest of the session with no product info at all.
            // That is not a small outage -- a trader will not quote a product whose
            // real tick it does not know, so one failed exchangeInfo call at startup
            // silently stopped trading until someone restarted the listener. Log it
            // and let the schedule below pick it up on the next tick instead.
            try {
                adapter_->publish_product_info();
            } catch (const std::exception& e) {
                LOG_ERROR(
                    logger_,
                    "Initial product info fetch failed: {}; retrying on the refresh timer",
                    e.what()
                );
            }
            schedule_product_info_refresh();
            product_info_io_context_->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger_, "Exception within product info thread: {}", e.what());
        }
    });
}


void MarketListener::schedule_product_info_refresh() {
    if (config_.product_info_refresh_sec <= 0) return;   // startup publish only

    product_info_timer_ = std::make_unique<boost::asio::steady_timer>(
        *product_info_io_context_
    );
    product_info_timer_->expires_after(
        std::chrono::seconds(config_.product_info_refresh_sec)
    );
    product_info_timer_->async_wait([this](const boost::system::error_code ec) {
        if (ec) return;   // cancelled on shutdown
        // A failed fetch must not kill the schedule: log it and try again next time.
        try {
            adapter_->publish_product_info();
        } catch (const std::exception& e) {
            LOG_WARNING(logger_, "Failed to refresh product info: {}", e.what());
        }
        schedule_product_info_refresh();
    });
}


Logger::CsvLogger<OrderbookCsvSchema>* MarketListener::get_orderbook_logger(
    const std::string& product
) {
    auto it = orderbook_loggers_.find(product);
    if (it != orderbook_loggers_.end()) return it->second.get();

    auto curr_date = get_curr_date(config_.timezone_minute_offset);
    auto logger = std::make_shared<Logger::CsvLogger<OrderbookCsvSchema>>(
        fmt::format("{}_{}_orderbook_logger", config_.exchange, product),
        fmt::format(
            "{}/{}/{}/{}.log",
            config_.orderbook_save_path, config_.exchange, product, curr_date
        )
    );
    orderbook_loggers_[product] = logger;
    return logger.get();
}


Logger::CsvLogger<TradeCsvSchema>* MarketListener::get_trade_logger(
    const std::string& product
) {
    auto it = trade_loggers_.find(product);
    if (it != trade_loggers_.end()) return it->second.get();

    auto curr_date = get_curr_date(config_.timezone_minute_offset);
    auto logger = std::make_shared<Logger::CsvLogger<TradeCsvSchema>>(
        fmt::format("{}_{}_trade_logger", config_.exchange, product),
        fmt::format(
            "{}/{}/{}/{}.log",
            config_.trade_save_path, config_.exchange, product, curr_date
        )
    );
    trade_loggers_[product] = logger;
    return logger.get();
}


void MarketListener::log_orderbook_data(const OrderbookMsg& msg) {
    auto* csv = get_orderbook_logger(msg.product);
    auto local_tstamp = get_curr_tstamp_ns();
    for (const auto& bid_level : msg.orderbook_data.bid_book) {
        csv->write_log(local_tstamp, true, bid_level.price.value_or(NAN), bid_level.qty.value_or(NAN));
    }
    for (const auto& ask_level : msg.orderbook_data.ask_book) {
        csv->write_log(local_tstamp, false, ask_level.price.value_or(NAN), ask_level.qty.value_or(NAN));
    }
}


void MarketListener::log_trade_data(const TradeMsg& msg) {
    auto* csv = get_trade_logger(msg.product);
    auto local_tstamp = get_curr_tstamp_ns();
    csv->write_log(
        local_tstamp,
        msg.trade_data.trade_price.value_or(NAN),
        msg.trade_data.cum_trade_qty.value_or(NAN),
        msg.trade_data.cum_buy_trade_qty.value_or(NAN)
    );
}


void MarketListener::listen() {
    ListenerEvent event;
    event_queue_.wait_dequeue(event);

    std::visit(overloaded{
        [&](const ListenerStatusUpdate& ev) {
            LOG_INFO(
                logger_, "[{}] connecting={} opened={}",
                ev.connection, ev.connecting, ev.opened
            );
        },
        [&](const OrderbookMsg& msg) {
            broadcast_market_data<OrderbookMsg>(msg);
            log_orderbook_data(msg);
        },
        [&](const TradeMsg& msg) {
            broadcast_market_data<TradeMsg>(msg);
            log_trade_data(msg);
        },
        [&](const ExecutionMsg& msg) {
            broadcast_market_data<ExecutionMsg>(msg);
        },
        [&](const PositionMsg& msg) {
            // Covers both futures positions and asset balances
            broadcast_market_data<PositionMsg>(msg);
        },
        [&](const ProductInfoMsg& msg) {
            broadcast_market_data<ProductInfoMsg>(msg);
            tcp_server_->set_product_info(msg.product, msg.product_info_data);
        }
    }, event);
}

} // namespace Omni::Listener
