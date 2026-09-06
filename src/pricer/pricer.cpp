#include <cmath>
#include <stdexcept>
#include <chrono>

#include <fmt/core.h>
#include <glaze/glaze.hpp>
#include <quill/LogMacros.h>

#include "utils/datetime.hpp"
#include "pricer/pricer.hpp"
#include "pricer/factor/init_factor.hpp"


namespace Omni::Pricer {

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;


Pricer::Pricer(
    const PricerConfig& config,
    const FactorConfig& factor_config,
    const FactorBuilder& make_factor,
    quill::Logger* logger
)
:   logger_(logger),
    config_(config),
    publish_interval_ns_(config.publish_interval_ms * 1000000),
    listener_connected_(false),
    server_io_context_(std::make_unique<boost::asio::io_context>()),
    timer_io_context_(std::make_unique<boost::asio::io_context>())
{
    // Factors are stateful (EMA), so each product gets its own calculator + factor.
    // Only FACTOR mode builds a factor at all: MID and VWAP need no configuration, so
    // an unknown --factor_name is only an error when the factor is actually used.
    for (const auto& spec : config_.products) {
        std::unique_ptr<BaseFactor> factor;
        if (config_.mode == FairPriceMode::FACTOR) {
            factor = make_factor();
            if (!factor) {
                throw std::runtime_error(
                    fmt::format("Unknown factor: {}", factor_config.factor_name)
                );
            }
        }
        product_states_[spec.product].fair_price =
            std::make_unique<FairPriceCalculator>(config_.mode, std::move(factor));
    }

    listener_client_ = std::make_unique<Connection::MarketFeedClient<Task>>(
        logger_, &task_queue_, config_.broadcast_host_address, config_.broadcast_port
    );
    tcp_server_ = std::make_unique<Connection::TcpServer>(
        *server_io_context_, logger_,
        config_.publish_host_address, config_.publish_port
    );

    LOG_INFO(
        logger_, "Fair price mode {}{}",
        fair_price_mode_name(config_.mode),
        (config_.mode == FairPriceMode::FACTOR)
            ? fmt::format(" (factor {})", factor_config.factor_name) : ""
    );

    start_tcp_server();
    start_publish_timer();
    start_listener_client();
}


Pricer::~Pricer() {
    if (listener_client_) listener_client_->stop();
    if (tcp_server_) tcp_server_->stop();
    if (server_io_context_) server_io_context_->stop();
    if (timer_io_context_) timer_io_context_->stop();
    if (server_io_thread_.joinable()) server_io_thread_.join();
    if (timer_io_thread_.joinable()) timer_io_thread_.join();
}


void Pricer::start_tcp_server() {
    tcp_server_->start();
    server_io_thread_ = std::thread([this]() {
        try {
            server_io_context_->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger_, "Exception within TCP server thread: {}", e.what());
        }
    });
    LOG_INFO(
        logger_, "Fair price server listening on {}:{}",
        config_.publish_host_address, config_.publish_port
    );
}


void Pricer::start_listener_client() {
    // Dials and keeps retrying the listener on its own io thread.
    listener_client_->start();
}


void Pricer::start_publish_timer() {
    set_publish_timer();
    timer_io_thread_ = std::thread([this]() {
        try {
            timer_io_context_->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger_, "Exception within publish timer thread: {}", e.what());
        }
    });
}


void Pricer::set_publish_timer() {
    publish_timer_ = std::make_shared<boost::asio::steady_timer>(*timer_io_context_);
    publish_timer_->expires_after(std::chrono::nanoseconds(
        publish_interval_ns_ - (get_curr_tstamp_ns() % publish_interval_ns_)
    ));

    publish_timer_->async_wait([this](const boost::system::error_code ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        } else if (!ec) {
            for (const auto& spec : config_.products) {
                task_queue_.enqueue(FairPriceUpdate{.product = spec.product});
            }
            set_publish_timer();
        }
    });
}


void Pricer::on_listener_status(const ListenerStatusUpdate& response) {
    if (!listener_connected_ && response.connected) {
        for (const auto& spec : config_.products) {
            listener_client_->subscribe(spec.product, spec.category);
            LOG_INFO(logger_, "Subscribed product {} on listener", spec.product);
        }
    } else if (listener_connected_ && !response.connected) {
        LOG_WARNING(logger_, "Listener link down; suspending fair price publication");
    }
    listener_connected_ = response.connected;
}


void Pricer::on_orderbook(const std::string& product, const OrderbookData& data) {
    auto it = product_states_.find(product);
    if (it == product_states_.end()) return;
    auto& market = it->second.market;

    if (data.bid_book.empty() || !data.bid_book[0].price.has_value()) {
        market.bbid_price = NAN;
        market.bbid_qty = 0.0;
    } else {
        market.bbid_price = data.bid_book[0].price.value();
        market.bbid_qty = data.bid_book[0].qty.value_or(0.0);
    }
    if (data.ask_book.empty() || !data.ask_book[0].price.has_value()) {
        market.bask_price = NAN;
        market.bask_qty = 0.0;
    } else {
        market.bask_price = data.ask_book[0].price.value();
        market.bask_qty = data.ask_book[0].qty.value_or(0.0);
    }
    market.ts = get_curr_tstamp_ns();
    it->second.book_seen = true;
}


void Pricer::on_trade(const std::string& product, const TradeData& data) {
    auto it = product_states_.find(product);
    if (it == product_states_.end()) return;
    auto& market = it->second.market;

    market.last_trade_price = data.trade_price.value_or(market.last_trade_price);
    market.cum_trade_qty = data.cum_trade_qty.value_or(market.cum_trade_qty);
    market.cum_buy_trade_qty = data.cum_buy_trade_qty.value_or(market.cum_buy_trade_qty);
    market.ts = get_curr_tstamp_ns();
}


void Pricer::process_market_data(const MarketDataResponse& response) {
    switch (response.feed) {
        case MarketDataResponse::Feed::Orderbook:
            on_orderbook(response.product, std::get<OrderbookData>(response.data));
            break;
        case MarketDataResponse::Feed::Trade:
            on_trade(response.product, std::get<TradeData>(response.data));
            break;
        default:
            // Execution / position / product_info are account-scoped and the pricer
            // never subscribes as a trading account.
            break;
    }
}


void Pricer::publish_fair_price(const std::string& product) {
    auto it = product_states_.find(product);
    if (it == product_states_.end()) return;
    auto& state = it->second;

    if (!listener_connected_) {
        // The book is no longer being maintained. Publishing anyway would stamp frozen
        // prices with a fresh timestamp, which is exactly what defeats the trader's
        // staleness check — it would keep applying a dead price. Going quiet instead
        // lets the value age out there and the trader fall back to its own mid.
        return;
    }

    if (!state.book_seen) {
        if (!config_.no_pricer_log) {
            LOG_INFO(logger_, "[FairPrice] {} skipped: no book yet", product);
        }
        return;
    }

    // Stamp with publish time so the trader ages the value from when it was computed,
    // not from the last book update.
    state.market.ts = get_curr_tstamp_ns();
    auto result = state.fair_price->compute(state.market);

    if (!result.priceable()) {
        if (!config_.no_pricer_log) {
            LOG_INFO(logger_, "[FairPrice] {} skipped: book not two-sided", product);
        }
        return;
    }

    auto fair_price_msg = FairPriceMsg{
        .product = product,
        .fair_price_data = FairPriceData{
            .ts = state.market.ts,
            .fair_price = result.fair_price,
            .factor = std::isnan(result.factor)
                ? std::nullopt : std::optional<double>(result.factor),
            .forward_vol = result.forward_vol
        }
    };

    std::string json_buffer;
    auto ec = glz::write_json(fair_price_msg, json_buffer);
    if (ec) {
        LOG_WARNING(logger_, "Failed write_json for the fair price msg of product {}", product);
        return;
    }
    tcp_server_->broadcast_to_subscribers(product, json_buffer);
    // One line per product per publish tick: at the default interval this is the whole
    // file, so no_pricer_log turns it off without touching the broadcast above.
    if (config_.no_pricer_log) return;
    LOG_INFO(
        logger_, "[FairPrice] {} fair={} mid={} factor={} forward_vol={} bbid={}@{} bask={}@{}",
        product, result.fair_price, result.mid_price, result.factor, result.forward_vol,
        state.market.bbid_price, state.market.bbid_qty,
        state.market.bask_price, state.market.bask_qty
    );
}


void Pricer::run() {
    Task task;
    task_queue_.wait_dequeue(task);

    std::visit(overloaded{
        [&](const ListenerStatusUpdate& response) { on_listener_status(response); },
        [&](const ListenerSubscribeUpdate& /*response*/) { },
        [&](const MarketDataResponse& response) { process_market_data(response); },
        [&](const FairPriceUpdate& update) { publish_fair_price(update.product); }
    }, task);
}

} // namespace Omni::Pricer
