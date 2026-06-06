#include <stdexcept>
#include <chrono>
#include <cmath>
#include <fmt/core.h>
#include <quill/LogMacros.h>

#include "utils/datetime.hpp"
#include "trader/order_handler/order_handler.hpp"

#include "trader/order_gateway/kis/order_gateway.hpp"
#include "trader/order_gateway/binance/order_gateway.hpp"


namespace Omni::Trader {

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;


static std::unique_ptr<Omni::OrderGateway::IOrderGateway> create_order_gateway(
    const TraderConfig& config, quill::Logger* logger
) {
    if (config.exchange == "binance") {
        return std::make_unique<Omni::Binance::BinanceOrderGateway>(config, logger);
    } else if (config.exchange == "kis") {
        return Omni::KIS::create_kis_order_gateway(config, logger);
    }
    throw std::runtime_error(fmt::format("Unsupported exchange: {}", config.exchange));
}


OrderHandler::OrderHandler(
    const MarketConfig& market_config,
    std::shared_ptr<BaseStrategy> strategy,
    const TraderConfig& config,
    quill::Logger* logger
)
:   logger_(logger),
    market_config_(market_config),
    min_tick_size_(market_config.min_tick_size),
    lot_size_(market_config.lot_size),
    strategy_(strategy),
    trade_products_(config.trade_products),
    subscribe_products_(config.subscribe_same_products ? config.trade_products : config.subscribe_products),
    broadcast_host_address_(config.broadcast_host_address),
    broadcast_port_(config.broadcast_port),
    order_update_interval_ns_(config.order_update_interval_ms * 1000000),
    order_gateway_(create_order_gateway(config, logger)),
    pricer_(market_config.min_tick_size, market_config.lot_size),
    product_info_ready_(market_config.min_tick_size > 0 && market_config.lot_size > 0),
    io_context_(std::make_unique<boost::asio::io_context>()),
    tcp_connecting_(false),
    tcp_connected_(false)
{
    for (const auto& product : trade_products_) {
        product_states_[product];
    }
    tcp_client_ = std::make_unique<Connection::TcpClient>(
        *io_context_, logger_, &trader_queue_
    );
    set_order_update_timer();
    start_tcp_client();
}


OrderHandler::~OrderHandler() {
    if (tcp_client_) tcp_client_->disconnect();
    if (io_context_) io_context_->stop();
    if (io_thread_.joinable()) io_thread_.join();
}


int64_t OrderHandler::to_price_in_min_ticks(double price) {
    return static_cast<int64_t>(std::round(price / min_tick_size_));
}

int32_t OrderHandler::to_qty_in_lots(double qty) {
    return static_cast<int32_t>(std::round(qty / lot_size_));
}

double OrderHandler::to_double_price(int64_t price_in_min_ticks) {
    return static_cast<double>(price_in_min_ticks) * min_tick_size_;
}

double OrderHandler::to_double_qty(int32_t qty_in_lots) {
    return static_cast<double>(qty_in_lots) * lot_size_;
}


void OrderHandler::set_order_update_timer() {
    order_update_timer_ = std::make_shared<boost::asio::steady_timer>(*io_context_);
    order_update_timer_->expires_after(std::chrono::nanoseconds(
        order_update_interval_ns_ - (get_curr_tstamp_ns() % order_update_interval_ns_)
    ));

    order_update_timer_->async_wait([this](const boost::system::error_code ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        } else if (!ec) {
            for (const auto& trade_product : trade_products_) {
                trader_queue_.enqueue(OrderUpdate{.product = trade_product});
            }
            set_order_update_timer();
        }
    });
}


void OrderHandler::start_tcp_client() {
    tcp_client_->connect(broadcast_host_address_, broadcast_port_);
    io_thread_ = std::thread([this]() {
        try {
            io_context_->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger_, "Exception within TCP client thread: {}", e.what());
        }
    });
}


void OrderHandler::on_tcp_status(const TcpStatusUpdate& response) {
    if (!tcp_connected_ && response.connected) {
        for (const auto& product : subscribe_products_) {
            tcp_client_->subscribe(product);
            LOG_INFO(logger_, "Subscribed product {} on tcp server", product);
        }
    } else if (tcp_connected_ && !response.connected) {
        tcp_client_->connect(broadcast_host_address_, broadcast_port_);
    }
    tcp_connecting_ = response.connecting;
    tcp_connected_ = response.connected;
}


void OrderHandler::on_tcp_subscribe(const TcpSubscribeUpdate& /*response*/) {
    // no-op for now
}


void OrderHandler::on_orderbook(const std::string& product, const OrderbookData& data) {
    auto& state = product_states_[product];
    if (data.bid_book.empty() || !data.bid_book[0].price.has_value()) {
        state.l1.bbid_price_in_min_ticks = -1;
        state.l1.bbid_qty_in_lots = 0;
    } else {
        state.l1.bbid_price_in_min_ticks = to_price_in_min_ticks(data.bid_book[0].price.value());
        state.l1.bbid_qty_in_lots = to_qty_in_lots(data.bid_book[0].qty.value_or(0.0));
    }
    if (data.ask_book.empty() || !data.ask_book[0].price.has_value()) {
        state.l1.bask_price_in_min_ticks = -1;
        state.l1.bask_qty_in_lots = 0;
    } else {
        state.l1.bask_price_in_min_ticks = to_price_in_min_ticks(data.ask_book[0].price.value());
        state.l1.bask_qty_in_lots = to_qty_in_lots(data.ask_book[0].qty.value_or(0.0));
    }
}


void OrderHandler::on_execution(const std::string& product, const ExecutionData& data) {
    // Route the order to the product that owns it (placed orders are registered in
    // order_no_to_product_); fall back to the message's product for snapshot orders.
    std::string target_product = product;
    auto product_it = order_no_to_product_.find(data.order_no);
    if (product_it != order_no_to_product_.end()) target_product = product_it->second;
    if (target_product.empty()) return;

    auto& state = product_states_[target_product];

    auto find_cid = [&](const std::string& order_no) -> int64_t {
        auto it = state.order_no_to_cid.find(order_no);
        return (it == state.order_no_to_cid.end()) ? -1 : static_cast<int64_t>(it->second);
    };
    auto erase_order = [&](uint32_t cid) {
        auto on_it = state.cid_to_order_no.find(cid);
        if (on_it != state.cid_to_order_no.end()) {
            order_no_to_product_.erase(on_it->second);
            state.order_no_to_cid.erase(on_it->second);
            state.cid_to_order_no.erase(on_it);
        }
        state.outstanding_orders.erase(cid);
    };

    try {
        if (data.is_accept_data) {
            int64_t cid = find_cid(data.order_no);

            if (data.is_rejected) {
                if (cid >= 0) {
                    state.response_waiting.response_waiting_place_orders.erase(cid);
                    erase_order(static_cast<uint32_t>(cid));
                }
                return;
            }

            if (data.is_cancel) {
                if (cid >= 0) {
                    state.response_waiting.response_waiting_cancel_orders.erase(cid);
                    erase_order(static_cast<uint32_t>(cid));
                }
                return;
            }

            if (data.is_place && data.is_accepted) {
                if (cid >= 0) {
                    state.response_waiting.response_waiting_place_orders.erase(cid);
                } else if (data.order_qty.value_or(0.0) > 0) {
                    // Adopt an order we didn't place this session (e.g. openOrders snapshot).
                    uint32_t new_cid = state.next_cid++;
                    state.outstanding_orders[new_cid] = OutstandingOrder{
                        .price_in_min_ticks = to_price_in_min_ticks(data.order_price.value_or(NAN)),
                        .qty_in_lots = to_qty_in_lots(data.order_qty.value_or(0.0)),
                        .is_bid = data.is_bid
                    };
                    state.cid_to_order_no[new_cid] = data.order_no;
                    state.order_no_to_cid[data.order_no] = new_cid;
                    order_no_to_product_[data.order_no] = target_product;
                }
            }
        } else if (data.is_executed && data.execute_qty.value_or(0.0) > 0) {
            int64_t cid = find_cid(data.order_no);
            auto exec_qty_in_lots = to_qty_in_lots(data.execute_qty.value_or(0.0));
            if (cid >= 0) {
                auto& order = state.outstanding_orders[static_cast<uint32_t>(cid)];
                order.qty_in_lots -= exec_qty_in_lots;
                if (order.qty_in_lots <= 0) erase_order(static_cast<uint32_t>(cid));
            }
            if (data.update_position_on_fill) {
                state.position_in_lots += data.is_bid ? exec_qty_in_lots : -exec_qty_in_lots;
            }
            LOG_INFO(
                logger_, "[Fill] {} order_no={} is_bid={} px={} qty={}",
                target_product, data.order_no, data.is_bid,
                data.execute_price.value_or(NAN), data.execute_qty.value_or(NAN)
            );
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Exception {} while processing execution data", e.what());
    }
}


void OrderHandler::on_position(const std::string& product, const PositionData& data) {
    if (!data.position_amt.has_value()) return;
    product_states_[product].position_in_lots = to_qty_in_lots(data.position_amt.value());
    LOG_INFO(logger_, "[Position] {} position={}", product, data.position_amt.value());
}


void OrderHandler::on_product_info(const std::string& product, const ProductInfoData& data) {
    if (!data.min_tick_size.has_value() || !data.lot_size.has_value()) return;
    double min_tick = data.min_tick_size.value();
    double lot = data.lot_size.value();
    if (min_tick <= 0 || lot <= 0) return;

    min_tick_size_ = min_tick;
    lot_size_ = lot;
    pricer_.set_params(min_tick, lot);
    strategy_->set_market_params(min_tick, lot);
    product_info_ready_ = true;
    LOG_INFO(logger_, "[ProductInfo] {} min_tick_size={} lot_size={}", product, min_tick, lot);
}


void OrderHandler::process_market_data(const TcpMarketDataResponse& response) {
    switch (response.feed) {
        case TcpMarketDataResponse::Feed::Orderbook:
            on_orderbook(response.product, std::get<OrderbookData>(response.data));
            break;
        case TcpMarketDataResponse::Feed::Trade:
            break;
        case TcpMarketDataResponse::Feed::Execution:
            on_execution(response.product, std::get<ExecutionData>(response.data));
            break;
        case TcpMarketDataResponse::Feed::Position:
            on_position(response.product, std::get<PositionData>(response.data));
            break;
        case TcpMarketDataResponse::Feed::ProductInfo:
            on_product_info(response.product, std::get<ProductInfoData>(response.data));
            break;
        case TcpMarketDataResponse::Error:
            break;
    }
}


void OrderHandler::update_orders(const std::string& product) {
    auto state_it = product_states_.find(product);
    if (state_it == product_states_.end()) return;
    auto& state = state_it->second;

    // Wait until tick/lot are known (from listener product info or CLI fallback).
    if (!product_info_ready_) return;

    if (!state.response_waiting.no_waiting_orders()) return;

    PriceInfo price_info;
    pricer_.fetch_mid_price(state.l1, price_info);
    if (std::isnan(price_info.mid_price)) return;

    std::map<int64_t, int32_t> bid_place_orders, ask_place_orders;
    std::vector<uint32_t> bid_cancel_orders, ask_cancel_orders;
    strategy_->make_decision(
        price_info, false, state.position_in_lots, state.outstanding_orders,
        bid_place_orders, ask_place_orders, bid_cancel_orders, ask_cancel_orders
    );

    auto submit_cancel = [&](uint32_t cid) {
        auto on_it = state.cid_to_order_no.find(cid);
        if (on_it == state.cid_to_order_no.end()) return;
        Omni::OrderGateway::OrderCancelInfo info{.order_no = on_it->second, .product = product};
        Omni::OrderGateway::OrderResponse resp;
        order_gateway_->cancel_order(info, resp);
        if (resp.success) {
            // synchronous ack: drop the order locally
            order_no_to_product_.erase(on_it->second);
            state.order_no_to_cid.erase(on_it->second);
            state.outstanding_orders.erase(cid);
            state.cid_to_order_no.erase(on_it);
        }
    };
    for (auto cid : bid_cancel_orders) submit_cancel(cid);
    for (auto cid : ask_cancel_orders) submit_cancel(cid);

    auto submit_place = [&](bool is_bid, int64_t price_in_min_ticks, int32_t qty_in_lots) {
        if (qty_in_lots <= 0) return;
        Omni::OrderGateway::OrderPlaceInfo info{
            .is_limit = true,
            .is_bid = is_bid,
            .price = to_double_price(price_in_min_ticks),
            .qty = to_double_qty(qty_in_lots),
            .product = product
        };
        Omni::OrderGateway::OrderResponse resp;
        order_gateway_->place_order(info, resp);
        if (resp.success && !resp.order_no.empty()) {
            uint32_t cid = state.next_cid++;
            state.outstanding_orders[cid] = OutstandingOrder{
                .price_in_min_ticks = price_in_min_ticks,
                .qty_in_lots = qty_in_lots,
                .is_bid = is_bid
            };
            state.cid_to_order_no[cid] = resp.order_no;
            state.order_no_to_cid[resp.order_no] = cid;
            order_no_to_product_[resp.order_no] = product;
            LOG_INFO(
                logger_, "[Order Place] {} order_no={} is_bid={} px={} qty={}",
                product, resp.order_no, is_bid, info.price, info.qty
            );
        }
    };
    for (const auto& [price_in_min_ticks, qty_in_lots] : bid_place_orders) {
        submit_place(true, price_in_min_ticks, qty_in_lots);
    }
    for (const auto& [price_in_min_ticks, qty_in_lots] : ask_place_orders) {
        submit_place(false, price_in_min_ticks, qty_in_lots);
    }
}


void OrderHandler::run() {
    Task task;
    trader_queue_.wait_dequeue(task);

    std::visit(overloaded{
        [&](const TcpStatusUpdate& response) { on_tcp_status(response); },
        [&](const TcpSubscribeUpdate& response) { on_tcp_subscribe(response); },
        [&](const TcpMarketDataResponse& response) { process_market_data(response); },
        [&](const OrderUpdate& update) { update_orders(update.product); }
    }, task);
}

} // namespace Omni::Trader
