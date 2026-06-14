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
    subscribe_products_(config.subscribe_products),   // already includes the trade-product fallback
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
    // Order replies come back asynchronously: the gateway delivers each OrderResponse
    // here, and we route it onto the trader queue so it is handled on the trader
    // thread (in run()) like every other event. The sink may fire on the websocket
    // thread (WS-API) or inline on the trader thread (REST/KIS); enqueue is safe for
    // both.
    order_gateway_->set_response_sink(
        [this](const Omni::OrderGateway::OrderResponse& response) {
            trader_queue_.enqueue(response);
        }
    );
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
        for (const auto& spec : subscribe_products_) {
            tcp_client_->subscribe(spec.product, spec.category);
            LOG_INFO(logger_, "Subscribed product {} on tcp server", spec.product);
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
    // Route by cid (the client order id we stamp on our orders); fall back to the
    // exchange order_no index for orders we didn't place this session (openOrders
    // snapshot, manual orders, or exchanges that don't echo our client id).
    int64_t cid = -1;
    std::string target_product;
    if (!data.client_order_id.empty()) {
        try {
            uint32_t parsed = static_cast<uint32_t>(std::stoul(data.client_order_id));
            auto cp = cid_to_product_.find(parsed);
            if (cp != cid_to_product_.end()) {
                cid = static_cast<int64_t>(parsed);
                target_product = cp->second;
            }
        } catch (const std::exception&) { /* not one of our cids */ }
    }
    if (target_product.empty()) {
        auto pit = order_no_to_product_.find(data.order_no);
        target_product = (pit != order_no_to_product_.end()) ? pit->second : product;
    }
    if (target_product.empty()) return;

    auto& state = product_states_[target_product];
    if (cid < 0) {
        auto it = state.order_no_to_cid.find(data.order_no);
        if (it != state.order_no_to_cid.end()) cid = static_cast<int64_t>(it->second);
    }

    // Bind cid <-> order_no (idempotent): the execution feed can confirm an order
    // before its async place reply arrives, and vice versa.
    auto bind_order_no = [&](uint32_t c) {
        if (data.order_no.empty()) return;
        state.cid_to_order_no[c] = data.order_no;
        state.order_no_to_cid[data.order_no] = c;
        order_no_to_product_[data.order_no] = target_product;
    };
    auto clear_waiting_if_idle = [&]() {
        if (state.response_waiting.no_waiting_orders()) state.response_waiting_since_ns = 0;
    };

    try {
        if (data.is_accept_data) {
            if (data.is_rejected) {
                if (cid >= 0) {
                    state.response_waiting.response_waiting_place_orders.erase(cid);
                    state.response_waiting.response_waiting_cancel_orders.erase(cid);
                    forget_order(state, static_cast<uint32_t>(cid));
                    clear_waiting_if_idle();
                }
                return;
            }

            if (data.is_cancel) {
                if (cid >= 0) {
                    state.response_waiting.response_waiting_cancel_orders.erase(cid);
                    forget_order(state, static_cast<uint32_t>(cid));
                    clear_waiting_if_idle();
                }
                return;
            }

            if (data.is_place && data.is_accepted) {
                if (cid >= 0) {
                    bind_order_no(static_cast<uint32_t>(cid));
                    state.response_waiting.response_waiting_place_orders.erase(cid);
                    clear_waiting_if_idle();
                } else if (data.order_qty.value_or(0.0) > 0) {
                    // Adopt an order we didn't place this session (e.g. openOrders snapshot).
                    uint32_t new_cid = reserve_cid(target_product);
                    state.outstanding_orders[new_cid] = OutstandingOrder{
                        .price_in_min_ticks = to_price_in_min_ticks(data.order_price.value_or(NAN)),
                        .qty_in_lots = to_qty_in_lots(data.order_qty.value_or(0.0)),
                        .is_bid = data.is_bid
                    };
                    bind_order_no(new_cid);
                }
            }
        } else if (data.is_executed && data.execute_qty.value_or(0.0) > 0) {
            auto exec_qty_in_lots = to_qty_in_lots(data.execute_qty.value_or(0.0));
            if (cid >= 0) {
                auto& order = state.outstanding_orders[static_cast<uint32_t>(cid)];
                order.qty_in_lots -= exec_qty_in_lots;
                if (order.qty_in_lots <= 0) forget_order(state, static_cast<uint32_t>(cid));
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


uint32_t OrderHandler::reserve_cid(const std::string& product) {
    uint32_t cid = next_cid_++;
    cid_to_product_[cid] = product;
    return cid;
}


void OrderHandler::forget_order(ProductState& state, uint32_t cid) {
    auto on_it = state.cid_to_order_no.find(cid);
    if (on_it != state.cid_to_order_no.end()) {
        order_no_to_product_.erase(on_it->second);
        state.order_no_to_cid.erase(on_it->second);
        state.cid_to_order_no.erase(on_it);
    }
    state.outstanding_orders.erase(cid);
    cid_to_product_.erase(cid);
}


void OrderHandler::on_order_response(const Omni::OrderGateway::OrderResponse& response) {
    auto cp = cid_to_product_.find(response.cid);
    if (cp == cid_to_product_.end()) return;   // unknown, or already reconciled/forgotten
    const std::string product = cp->second;
    auto& state = product_states_[product];

    // Which kind of request this was is inferred from the waiting sets. A reply can
    // arrive after the execution feed already cleared the wait (the false/false case
    // below), in which case the order is already reconciled and we only (idempotently)
    // bind the order_no on success.
    bool was_place = state.response_waiting.response_waiting_place_orders.erase(response.cid) > 0;
    bool was_cancel = state.response_waiting.response_waiting_cancel_orders.erase(response.cid) > 0;
    if (state.response_waiting.no_waiting_orders()) state.response_waiting_since_ns = 0;

    if (was_cancel) {
        // Cancel ack: drop the order on success; on failure leave it (still open).
        if (response.success) forget_order(state, response.cid);
        LOG_INFO(
            logger_, "[Order Cancel] {} cid={} success={} msg={}",
            product, response.cid, response.success, response.msg
        );
        return;
    }

    // Place ack (or a late place reply the execution feed already reconciled): bind
    // the exchange order_no on success; drop the optimistic order if the place failed.
    if (response.success) {
        if (!response.order_no.empty()) {
            state.cid_to_order_no[response.cid] = response.order_no;
            state.order_no_to_cid[response.order_no] = response.cid;
            order_no_to_product_[response.order_no] = product;
        }
    } else if (was_place) {
        forget_order(state, response.cid);
    }
    LOG_INFO(
        logger_, "[Order Place] {} cid={} success={} order_no={} msg={}",
        product, response.cid, response.success, response.order_no, response.msg
    );
}


void OrderHandler::on_position(const std::string& product, const PositionData& data) {
    if (!data.balance.has_value()) return;

    // Retain for visibility/logging (merge: an asset stream update carries balance
    // but not available; keep the last known available until the next snapshot).
    auto& pos = positions_[product];
    pos.balance = data.balance;
    if (data.available_balance.has_value()) pos.available_balance = data.available_balance;

    // For a traded (futures) product, `balance` is the signed position amount that
    // drives the strategy. Asset-category products have no product_states_ entry, so
    // their balance is only retained above (no meaningless lot conversion).
    auto it = product_states_.find(product);
    if (it != product_states_.end()) {
        it->second.position_in_lots = to_qty_in_lots(data.balance.value());
    }
    LOG_INFO(logger_, "[Position] {} balance={}", product, data.balance.value());
}


std::string OrderHandler::format_positions() const {
    if (positions_.empty()) return "(none)";
    std::string out;
    for (const auto& [product, pos] : positions_) {
        if (!out.empty()) out += " ";
        out += fmt::format(
            "{}(balance={},avail={})",
            product, pos.balance.value_or(NAN), pos.available_balance.value_or(NAN)
        );
    }
    return out;
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
    if (state_it == product_states_.end()) {
        LOG_WARNING(logger_, "[Decision] {} skipped: no product state", product);
        return;
    }
    auto& state = state_it->second;

    // Wait until tick/lot are known (from listener product info or CLI fallback).
    if (!product_info_ready_) {
        LOG_INFO(logger_, "[Decision] {} skipped: product info (tick/lot) not ready", product);
        return;
    }

    if (!state.response_waiting.no_waiting_orders()) {
        // A lost reply must not stall the product forever. After the timeout, clear
        // the wait and re-decide; any order that was actually live is still reconciled
        // by the execution feed (which carries the cid).
        int64_t now_ns = get_curr_tstamp_ns();
        if (state.response_waiting_since_ns > 0 &&
            now_ns - state.response_waiting_since_ns > order_response_timeout_ns_) {
            LOG_WARNING(
                logger_, "[Decision] {} order responses timed out; clearing wait (place={}, cancel={})",
                product,
                state.response_waiting.response_waiting_place_orders.size(),
                state.response_waiting.response_waiting_cancel_orders.size()
            );
            state.response_waiting.response_waiting_place_orders.clear();
            state.response_waiting.response_waiting_cancel_orders.clear();
            state.response_waiting_since_ns = 0;
        } else {
            LOG_INFO(
                logger_, "[Decision] {} skipped: waiting on responses (place={}, cancel={})",
                product,
                state.response_waiting.response_waiting_place_orders.size(),
                state.response_waiting.response_waiting_cancel_orders.size()
            );
            return;
        }
    }

    PriceInfo price_info;
    pricer_.fetch_mid_price(state.l1, price_info);
    LOG_INFO(
        logger_,
        "[Decision] {} bbid={} bask={} mid={} fair={} position_lots={} outstanding={} positions=[{}]",
        product,
        to_double_price(price_info.bbid_price_in_min_ticks),
        to_double_price(price_info.bask_price_in_min_ticks),
        price_info.mid_price, price_info.fair_price,
        state.position_in_lots, state.outstanding_orders.size(),
        format_positions()
    );
    if (std::isnan(price_info.mid_price)) {
        LOG_INFO(logger_, "[Decision] {} skipped: mid price is NaN (no L1 yet)", product);
        return;
    }

    std::map<int64_t, int32_t> bid_place_orders, ask_place_orders;
    std::vector<uint32_t> bid_cancel_orders, ask_cancel_orders;
    strategy_->make_decision(
        price_info, false, state.position_in_lots, state.outstanding_orders,
        bid_place_orders, ask_place_orders, bid_cancel_orders, ask_cancel_orders
    );

    for (const auto& [price_in_min_ticks, qty_in_lots] : bid_place_orders) {
        LOG_INFO(logger_, "[Decision] {} want BID px={} qty={}",
                 product, to_double_price(price_in_min_ticks), to_double_qty(qty_in_lots));
    }
    for (const auto& [price_in_min_ticks, qty_in_lots] : ask_place_orders) {
        LOG_INFO(logger_, "[Decision] {} want ASK px={} qty={}",
                 product, to_double_price(price_in_min_ticks), to_double_qty(qty_in_lots));
    }
    LOG_INFO(
        logger_, "[Decision] {} decided: place(bid={}, ask={}) cancel(bid={}, ask={})",
        product, bid_place_orders.size(), ask_place_orders.size(),
        bid_cancel_orders.size(), ask_cancel_orders.size()
    );

    // Mark a cid as awaiting a reply, stamping the wait start on the first one so the
    // timeout above has a reference point. Orders are fired without blocking; their
    // outcome arrives later via on_order_response (or the execution feed).
    auto mark_waiting = [&](std::set<uint32_t>& waiting_set, uint32_t cid) {
        if (state.response_waiting.no_waiting_orders()) {
            state.response_waiting_since_ns = get_curr_tstamp_ns();
        }
        waiting_set.insert(cid);
    };

    auto submit_cancel = [&](uint32_t cid) {
        auto on_it = state.cid_to_order_no.find(cid);
        if (on_it == state.cid_to_order_no.end()) return;   // order_no not known yet
        Omni::OrderGateway::OrderCancelInfo info{
            .order_no = on_it->second, .product = product, .cid = cid
        };
        mark_waiting(state.response_waiting.response_waiting_cancel_orders, cid);
        order_gateway_->cancel_order(info);
        LOG_INFO(logger_, "[Order Cancel] {} cid={} order_no={} sent", product, cid, info.order_no);
    };
    for (auto cid : bid_cancel_orders) submit_cancel(cid);
    for (auto cid : ask_cancel_orders) submit_cancel(cid);

    auto submit_place = [&](bool is_bid, int64_t price_in_min_ticks, int32_t qty_in_lots) {
        if (qty_in_lots <= 0) return;
        uint32_t cid = reserve_cid(product);
        // Register optimistically so the strategy won't re-issue the order while we
        // await the ack; the order_no is bound on the reply/execution, and the order
        // is dropped again if the place fails.
        state.outstanding_orders[cid] = OutstandingOrder{
            .price_in_min_ticks = price_in_min_ticks,
            .qty_in_lots = qty_in_lots,
            .is_bid = is_bid
        };
        mark_waiting(state.response_waiting.response_waiting_place_orders, cid);
        Omni::OrderGateway::OrderPlaceInfo info{
            .is_limit = true,
            .is_bid = is_bid,
            .price = to_double_price(price_in_min_ticks),
            .qty = to_double_qty(qty_in_lots),
            .product = product,
            .cid = cid
        };
        order_gateway_->place_order(info);
        LOG_INFO(
            logger_, "[Order Place] {} cid={} is_bid={} px={} qty={} sent",
            product, cid, is_bid, info.price, info.qty
        );
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
        [&](const OrderUpdate& update) { update_orders(update.product); },
        [&](const OrderResponseTask& response) { on_order_response(response); }
    }, task);
}

} // namespace Omni::Trader
