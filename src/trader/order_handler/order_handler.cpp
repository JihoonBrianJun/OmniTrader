#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>
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
    quill::Logger* logger,
    GatewayFactory make_gateway
)
:   logger_(logger),
    market_config_(market_config),
    min_tick_size_(market_config.min_tick_size),
    default_lot_size_(market_config.default_lot_size),
    strategy_(strategy),
    trade_products_(config.trade_products),
    subscribe_products_(config.subscribe_products),   // already includes the trade-product fallback
    broadcast_host_address_(config.broadcast_host_address),
    broadcast_port_(config.broadcast_port),
    pricer_host_address_(config.pricer_host_address),
    pricer_port_(config.pricer_port),
    fair_price_max_age_ns_(config.fair_price_max_age_ms * 1000000),
    order_update_interval_ns_(config.order_update_interval_ms * 1000000),
    flatten_on_shutdown_(config.flatten_on_shutdown),
    shutdown_cancel_timeout_ns_(config.shutdown_cancel_timeout_ms * 1000000),
    shutdown_flatten_timeout_ns_(config.shutdown_flatten_timeout_ms * 1000000),
    shutdown_market_flatten_(config.shutdown_market_flatten),
    shutdown_reduce_only_(config.shutdown_reduce_only),
    order_gateway_(
        make_gateway ? make_gateway(config, logger) : create_order_gateway(config, logger)
    ),
    io_context_(std::make_unique<boost::asio::io_context>()),
    listener_connecting_(false),
    listener_connected_(false),
    pricer_connected_(false)
{
    // These are the units every internal price and quantity is a count of, so there
    // is no usable default and no later source to fill them in from -- the listener's
    // product_info is a different thing entirely. Fail at startup rather than divide
    // by zero on the first book update.
    if (min_tick_size_ <= 0.0 || default_lot_size_ <= 0.0) {
        throw std::runtime_error(
            "--min_tick_size and --default_lot_size must both be positive"
        );
    }
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
    listener_client_ = std::make_unique<Connection::MarketFeedClient<Task>>(
        logger_, &trader_queue_, broadcast_host_address_, broadcast_port_
    );
    // The fair-price feed is a second, independent link, so a pricer outage degrades
    // the trader to plain mid (via the value ageing out) rather than cutting market
    // data. It is always dialled: with no pricer running the trader just prices off
    // mid, which is what the pricer's own default (MID) would have produced anyway.
    pricer_client_ = std::make_unique<Connection::FairPriceClient<Task>>(
        logger_, &trader_queue_, pricer_host_address_, pricer_port_
    );
    set_order_update_timer();
    start_feed_clients();
}


OrderHandler::~OrderHandler() {
    // Deliberately does not cancel or flatten. Sending orders needs a live feed, a
    // working gateway and somewhere to report failure, none of which a destructor
    // has; the owner calls shutdown() first, on every exit path, and this only tears
    // the links down.
    shutting_down_.store(true, std::memory_order_relaxed);

    // First, and explicitly. The REST gateways run each order on a thread of their
    // own and deliver the reply through the response sink, which enqueues onto
    // trader_queue_. Destroying the gateway drains those threads; leaving it to the
    // member teardown order would destroy trader_queue_ first (it is declared after
    // order_gateway_, so it is destroyed before it) and an in-flight reply would
    // enqueue into freed memory.
    order_gateway_.reset();

    if (listener_client_) listener_client_->stop();
    if (pricer_client_) pricer_client_->stop();
    if (io_context_) io_context_->stop();
    if (io_thread_.joinable()) io_thread_.join();
}


int64_t OrderHandler::to_price_in_min_ticks(double price) const {
    return static_cast<int64_t>(std::round(price / min_tick_size_));
}

int32_t OrderHandler::to_qty_in_lots(double qty) const {
    return static_cast<int32_t>(std::round(qty / default_lot_size_));
}

double OrderHandler::to_double_price(int64_t price_in_min_ticks) const {
    return static_cast<double>(price_in_min_ticks) * min_tick_size_;
}

double OrderHandler::to_double_qty(int32_t qty_in_lots) const {
    return static_cast<double>(qty_in_lots) * default_lot_size_;
}


bool snap_to_product_grid(
    const Omni::ProductInfoData& info, bool /*is_bid*/, double& price, double& qty
) {
    // Round to the nearest grid point, for price and quantity alike: the aim is the
    // order the strategy actually asked for, and directional rounding would bias
    // every order away from the decided price by up to a tick and shave every size
    // by up to a lot. `is_bid` is therefore unused, and kept only so the signature
    // still describes the side for a venue that ever needs it.
    //
    // A field the listener did not publish leaves that dimension on the global grid,
    // which is the correct behaviour for an exchange with no per-product grid to
    // publish (KIS) as well as for the window before the first product_info arrives.
    if (info.tick_size.has_value() && info.tick_size.value() > 0.0) {
        double tick = info.tick_size.value();
        price = std::round(price / tick) * tick;
    }
    // A non-positive price is never a valid limit -- drop it rather than hand it to
    // the exchange.
    return snap_qty_to_product_grid(info, qty) && price > 0.0;
}


bool snap_qty_to_product_grid(const Omni::ProductInfoData& info, double& qty) {
    if (info.lot_size.has_value() && info.lot_size.value() > 0.0) {
        double lot = info.lot_size.value();
        qty = std::round(qty / lot) * lot;
    }
    // A size that rounds away to nothing is not an order.
    return qty > 0.0;
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
            // Checked on the io thread, which is the only one that arms this timer:
            // once shutdown has begun the decision tick stops, so nothing new is
            // quoted underneath the sequence that is trying to get flat.
            if (shutting_down_.load(std::memory_order_relaxed)) return;
            for (const auto& trade_product : trade_products_) {
                trader_queue_.enqueue(OrderUpdate{.product = trade_product});
            }
            set_order_update_timer();
        }
    });
}


void OrderHandler::start_feed_clients() {
    // Each client dials on, and keeps retrying from, its own io thread; this one
    // carries only the order-update timer.
    listener_client_->start();
    if (pricer_client_) pricer_client_->start();

    io_thread_ = std::thread([this]() {
        try {
            io_context_->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger_, "Exception within order update timer thread: {}", e.what());
        }
    });
}


void OrderHandler::on_listener_status(const ListenerStatusUpdate& response) {
    if (!listener_connected_ && response.connected) {
        for (const auto& spec : subscribe_products_) {
            listener_client_->subscribe(spec.product, spec.category);
            LOG_INFO(logger_, "Subscribed product {} on listener server", spec.product);
        }
    } else if (listener_connected_ && !response.connected) {
        LOG_WARNING(logger_, "Listener link down; holding off decisions until it is back");
    }
    listener_connecting_ = response.connecting;
    listener_connected_ = response.connected;
}


void OrderHandler::on_pricer_status(const PricerStatusUpdate& response) {
    if (!pricer_client_) return;

    if (!pricer_connected_ && response.connected) {
        // Only the traded products carry a fair price; the extra subscriptions a
        // trader keeps on the listener (e.g. an asset balance) have none.
        for (const auto& product : trade_products_) {
            pricer_client_->subscribe(product, Category::futures);
            LOG_INFO(logger_, "Subscribed product {} on pricer server", product);
        }
    } else if (pricer_connected_ && !response.connected) {
        LOG_WARNING(logger_, "Pricer link down; pricing off mid until it is back");
    }
    pricer_connected_ = response.connected;
}


void OrderHandler::on_listener_subscribe(const ListenerSubscribeUpdate& /*response*/) {
    // no-op for now
}


void OrderHandler::on_orderbook(const std::string& product, const OrderbookData& data) {
    // find, not operator[]: the subscribe list is wider than the trade list, and a
    // product we only watch must not conjure a ProductState. Every entry in this map
    // is a product we trade, created up front in the constructor.
    auto it = product_states_.find(product);
    if (it == product_states_.end()) return;
    auto& state = it->second;
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
    bool have_cid = false;
    uint64_t cid = 0;
    std::string target_product;
    if (!data.client_order_id.empty()) {
        try {
            uint64_t parsed = std::stoull(data.client_order_id);
            auto cp = cid_to_product_.find(parsed);
            if (cp != cid_to_product_.end()) {
                cid = parsed;
                have_cid = true;
                target_product = cp->second;
            }
        } catch (const std::exception&) { /* not one of our cids */ }
    }
    if (target_product.empty()) {
        auto pit = order_no_to_product_.find(data.order_no);
        target_product = (pit != order_no_to_product_.end()) ? pit->second : product;
    }
    if (target_product.empty()) return;

    // The fallback above can land on a product we only subscribe to; skip it rather
    // than create a state for a product we never trade.
    auto state_it = product_states_.find(target_product);
    if (state_it == product_states_.end()) return;
    auto& state = state_it->second;
    if (!have_cid) {
        auto it = state.order_no_to_cid.find(data.order_no);
        if (it != state.order_no_to_cid.end()) { cid = it->second; have_cid = true; }
    }

    // Bind cid <-> order_no (idempotent): the execution feed can confirm an order
    // before its async place reply arrives, and vice versa.
    auto bind_order_no = [&](uint64_t c) {
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
                if (have_cid) {
                    state.response_waiting.response_waiting_place_orders.erase(cid);
                    state.response_waiting.response_waiting_cancel_orders.erase(cid);
                    forget_order(state, cid);
                    clear_waiting_if_idle();
                }
                return;
            }

            if (data.is_cancel) {
                if (have_cid) {
                    state.response_waiting.response_waiting_cancel_orders.erase(cid);
                    forget_order(state, cid);
                    clear_waiting_if_idle();
                }
                return;
            }

            if (data.is_place && data.is_accepted) {
                if (have_cid) {
                    bind_order_no(cid);
                    state.response_waiting.response_waiting_place_orders.erase(cid);
                    clear_waiting_if_idle();
                } else if (data.order_qty.value_or(0.0) > 0) {
                    // Adopt an order we didn't place this session (e.g. openOrders snapshot).
                    uint64_t new_cid = reserve_cid(target_product);
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
            if (have_cid) {
                auto& order = state.outstanding_orders[cid];
                order.qty_in_lots -= exec_qty_in_lots;
                if (order.qty_in_lots <= 0) forget_order(state, cid);
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


uint64_t OrderHandler::reserve_cid(const std::string& product) {
    // cid is the send-time ns timestamp, made strictly increasing so two orders that
    // land on the same nanosecond (or a clock that didn't advance) never collide.
    uint64_t cid = static_cast<uint64_t>(get_curr_tstamp_ns());
    if (cid <= last_cid_) cid = last_cid_ + 1;
    last_cid_ = cid;
    cid_to_product_[cid] = product;
    return cid;
}


void OrderHandler::forget_order(ProductState& state, uint64_t cid) {
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
    if (!data.tick_size.has_value() || !data.lot_size.has_value()) return;
    double tick = data.tick_size.value();
    double lot = data.lot_size.value();
    if (tick <= 0 || lot <= 0) return;

    // Recorded against the product it describes, and nowhere else. A product we
    // subscribe to but do not trade has no state to hold it, and needs none.
    auto it = product_states_.find(product);
    if (it == product_states_.end()) return;

    auto& info = it->second.product_info;
    bool changed = info.tick_size != data.tick_size || info.lot_size != data.lot_size;
    info.tick_size = tick;
    info.lot_size = lot;

    // The gateway formats the outgoing order against its own copy of the exchange's
    // filters, loaded once when it was constructed. Feed this one through so the
    // rounding and the decimal precision on the wire follow the same refreshed grid
    // the snapping above uses, instead of a snapshot taken at startup.
    order_gateway_->set_product_grid(product, tick, lot);

    // The global unit has to be able to express this product's grid. If it cannot,
    // every internal price quantizes coarser than the venue's own increment: ladder
    // levels one product tick apart land on the same global tick and silently become
    // one order, and no snapping downstream can recover the levels that were already
    // merged. Only a lower --min_tick_size fixes it, so say so loudly and once.
    if (changed && tick < min_tick_size_) {
        LOG_WARNING(
            logger_,
            "[ProductInfo] {} tick_size={} is finer than --min_tick_size={}; prices "
            "quantize coarser than the venue grid and tick-based ladders will merge. "
            "Lower --min_tick_size to {} or below.",
            product, tick, min_tick_size_, tick
        );
    }

    if (changed) {
        LOG_INFO(logger_, "[ProductInfo] {} tick_size={} lot_size={}", product, tick, lot);
    }
}


void OrderHandler::on_fair_price(const std::string& product, const FairPriceData& data) {
    auto it = product_states_.find(product);
    if (it == product_states_.end()) return;

    // A price-less tick is recorded as NaN so we fall back to mid rather than reuse
    // the previous value. `factor` is only ever set when the pricer runs in FACTOR
    // mode, and is carried for logging alone.
    it->second.fair_price = FairPriceState{
        .ts = data.ts,
        .fair_price = data.fair_price.value_or(NAN),
        .factor = data.factor.value_or(NAN)
    };
}


void OrderHandler::build_price_info(const ProductState& state, PriceInfo& price_info) {
    price_info.bbid_price_in_min_ticks = state.l1.bbid_price_in_min_ticks;
    price_info.bask_price_in_min_ticks = state.l1.bask_price_in_min_ticks;
    price_info.applied_factor = NAN;

    // Set before any early return, so the strategy never sees an unset increment.
    // min_tick_size_ is guaranteed positive by the constructor, so this always is.
    double product_tick = state.product_info.tick_size.value_or(0.0);
    price_info.tick_size = product_tick > 0.0 ? product_tick : min_tick_size_;

    if ((state.l1.bbid_price_in_min_ticks == -1) || (state.l1.bask_price_in_min_ticks == -1)) {
        // No two-sided book yet: leave both prices NaN so the caller skips the
        // decision instead of quoting off a half-empty book.
        price_info.mid_price = NAN;
        price_info.fair_price = NAN;
        return;
    }

    price_info.mid_price = to_double_price(
        state.l1.bbid_price_in_min_ticks + state.l1.bask_price_in_min_ticks
    ) / 2;

    // Fair price is the pricer's to define. Anything that stops it reaching us — the
    // process down, the link dropped, its own book not ready — falls back to mid.
    bool usable = !std::isnan(state.fair_price.fair_price)
        && (fair_price_max_age_ns_ <= 0
            || (get_curr_tstamp_ns() - state.fair_price.ts) <= fair_price_max_age_ns_);

    if (usable) {
        price_info.fair_price = state.fair_price.fair_price;
        price_info.applied_factor = state.fair_price.factor;
    } else {
        price_info.fair_price = price_info.mid_price;
    }
}


void OrderHandler::process_market_data(const MarketDataResponse& response) {
    switch (response.feed) {
        case MarketDataResponse::Feed::Orderbook:
            on_orderbook(response.product, std::get<OrderbookData>(response.data));
            break;
        case MarketDataResponse::Feed::Trade:
            break;
        case MarketDataResponse::Feed::Execution:
            on_execution(response.product, std::get<ExecutionData>(response.data));
            break;
        case MarketDataResponse::Feed::Position:
            on_position(response.product, std::get<PositionData>(response.data));
            break;
        case MarketDataResponse::Feed::ProductInfo:
            on_product_info(response.product, std::get<ProductInfoData>(response.data));
            break;
        case MarketDataResponse::Error:
            break;
    }
}


void OrderHandler::update_orders(const std::string& product, bool do_liquidate) {
    // A decision tick queued just before the stop must not turn into fresh quotes
    // behind the shutdown sequence's back. The sequence's own liquidation calls come
    // through here too, and are let past.
    if (shutting_down_.load(std::memory_order_relaxed) && !do_liquidate) return;

    auto state_it = product_states_.find(product);
    if (state_it == product_states_.end()) {
        LOG_WARNING(logger_, "[Decision] {} skipped: no product state", product);
        return;
    }
    auto& state = state_it->second;

    // With the listener away, L1 is frozen at whatever it last was. Quoting off a book
    // that is no longer being updated is worse than not quoting, so hold off until it
    // is back -- the same reason the pricer stops publishing when its own feed drops.
    if (!listener_connected_) {
        LOG_INFO(logger_, "[Decision] {} skipped: listener link down (book is stale)", product);
        return;
    }

    // Do not quote until this product's real tick is known. Everything the strategy
    // steps by one tick -- the caps that hold a quote inside the touch, the
    // liquidation prices, the ladder spacing -- would otherwise fall back to the
    // global --min_tick_size. That is a normalization unit, deliberately set far
    // finer than any venue grid, so the caps would shrink to nothing and a quote
    // meant to be passive would cross the spread.
    //
    // Exempt when the exchange has a tick_func: there the increment is computed from
    // price and needs no product_info, and such a venue publishes none, so gating on
    // product_info alone would stop it trading at all.
    if (!market_config_.tick_func && !state.product_info.tick_size.has_value()) {
        LOG_INFO(
            logger_, "[Decision] {} skipped: product tick not known yet "
            "(no product_info from the listener, and this exchange has no tick_func)",
            product
        );
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
    build_price_info(state, price_info);
    LOG_INFO(
        logger_,
        "[Decision] {} bbid={} bask={} mid={} fair={} factor={} position_lots={} outstanding={} positions=[{}]",
        product,
        to_double_price(price_info.bbid_price_in_min_ticks),
        to_double_price(price_info.bask_price_in_min_ticks),
        price_info.mid_price, price_info.fair_price, price_info.applied_factor,
        state.position_in_lots, state.outstanding_orders.size(),
        format_positions()
    );
    if (std::isnan(price_info.mid_price)) {
        LOG_INFO(logger_, "[Decision] {} skipped: mid price is NaN (no L1 yet)", product);
        return;
    }

    std::map<int64_t, int32_t> bid_place_orders, ask_place_orders;
    std::vector<uint64_t> bid_cancel_orders, ask_cancel_orders;
    strategy_->make_decision(
        price_info, do_liquidate, state.position_in_lots, state.outstanding_orders,
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

    // Orders are fired without blocking; their outcome arrives later via
    // on_order_response (or the execution feed). Cancels go first, so a replace frees
    // its level before the new order is sent.
    for (auto cid : bid_cancel_orders) submit_cancel(state, product, cid);
    for (auto cid : ask_cancel_orders) submit_cancel(state, product, cid);

    for (const auto& [price_in_min_ticks, qty_in_lots] : bid_place_orders) {
        submit_place(state, product, true, price_in_min_ticks, qty_in_lots);
    }
    for (const auto& [price_in_min_ticks, qty_in_lots] : ask_place_orders) {
        submit_place(state, product, false, price_in_min_ticks, qty_in_lots);
    }
}


void OrderHandler::mark_waiting(
    ProductState& state, std::set<uint64_t>& waiting_set, uint64_t cid
) {
    if (state.response_waiting.no_waiting_orders()) {
        state.response_waiting_since_ns = get_curr_tstamp_ns();
    }
    waiting_set.insert(cid);
}


void OrderHandler::submit_cancel(
    ProductState& state, const std::string& product, uint64_t cid
) {
    auto on_it = state.cid_to_order_no.find(cid);
    if (on_it == state.cid_to_order_no.end()) return;   // order_no not known yet
    Omni::OrderGateway::OrderCancelInfo info{
        .order_no = on_it->second, .product = product, .cid = cid
    };
    mark_waiting(state, state.response_waiting.response_waiting_cancel_orders, cid);
    order_gateway_->cancel_order(info);
    LOG_INFO(logger_, "[Order Cancel] {} cid={} order_no={} sent", product, cid, info.order_no);
}


void OrderHandler::submit_place(
    ProductState& state, const std::string& product,
    bool is_bid, int64_t price_in_min_ticks, int32_t qty_in_lots,
    bool is_limit, bool reduce_only
) {
    if (qty_in_lots <= 0) return;

    // The one point where the product's real grid is applied. Everything above this
    // line is in global units; everything the exchange sees is snapped.
    double price = to_double_price(price_in_min_ticks);
    double qty = to_double_qty(qty_in_lots);
    if (is_limit) {
        if (!snap_to_product_grid(state.product_info, is_bid, price, qty)) {
            LOG_INFO(
                logger_, "[Order Place] {} is_bid={} px={} skipped: qty rounds to zero on product grid",
                product, is_bid, price
            );
            return;
        }
    } else {
        // A market order has no price to snap, only a size. It is priced by the book
        // it lands in, so `price` goes out as 0 and the gateways omit it.
        if (!snap_qty_to_product_grid(state.product_info, qty)) {
            LOG_INFO(
                logger_, "[Order Place] {} is_bid={} MARKET skipped: qty rounds to zero on product grid",
                product, is_bid
            );
            return;
        }
        price = 0.0;
    }

    uint64_t cid = reserve_cid(product);
    // Register optimistically so the strategy won't re-issue the order while we
    // await the ack; the order_no is bound on the reply/execution, and the order
    // is dropped again if the place fails.
    //
    // Recorded in global units -- the pre-snap values the strategy asked for, not
    // the post-snap ones sent. choose_orders_to_cancel matches this against the
    // strategy's next set of desired prices, which are computed on the same grid;
    // storing the snapped price would make an order it still wants look like one
    // it does not, and churn a cancel/replace every tick.
    //
    // Limit orders only. A market order does not rest, so recording it as outstanding
    // would make the shutdown's "is anything still working?" check answer yes to an
    // order that is already gone, and send a cancel chasing it. Its cid is still
    // registered (reserve_cid above), which is all the reply and the fill need.
    if (is_limit) {
        state.outstanding_orders[cid] = OutstandingOrder{
            .price_in_min_ticks = price_in_min_ticks,
            .qty_in_lots = qty_in_lots,
            .is_bid = is_bid
        };
    }
    mark_waiting(state, state.response_waiting.response_waiting_place_orders, cid);
    Omni::OrderGateway::OrderPlaceInfo info{
        .is_limit = is_limit,
        .is_bid = is_bid,
        .price = price,
        .qty = qty,
        .product = product,
        .cid = cid,
        .reduce_only = reduce_only
    };
    order_gateway_->place_order(info);
    LOG_INFO(
        logger_, "[Order Place] {} cid={} is_bid={} type={} px={} qty={} reduce_only={} sent",
        product, cid, is_bid, is_limit ? "LIMIT" : "MARKET", info.price, info.qty, reduce_only
    );
}


void OrderHandler::handle_task(const Task& task) {
    std::visit(overloaded{
        [&](const ListenerStatusUpdate& response) { on_listener_status(response); },
        [&](const ListenerSubscribeUpdate& response) { on_listener_subscribe(response); },
        [&](const MarketDataResponse& response) { process_market_data(response); },
        [&](const PricerStatusUpdate& response) { on_pricer_status(response); },
        [&](const PricerSubscribeUpdate& /*response*/) { },
        [&](const FairPriceResponse& response) { on_fair_price(response.product, response.data); },
        [&](const OrderUpdate& update) { update_orders(update.product); },
        [&](const OrderResponseTask& response) { on_order_response(response); }
    }, task);
}


void OrderHandler::run() {
    Task task;
    // Timed rather than an outright block: the caller checks for a shutdown request
    // between turns, and on a market that has gone quiet an untimed wait would sit
    // there through a Ctrl-C until the next event happened to arrive. The timed and
    // untimed waits share the same fast path -- try-dequeue, then spin -- and differ
    // only in whether the final block carries a deadline, so this costs nothing when
    // there is work and about ten idle wakeups a second when there is not.
    if (!trader_queue_.wait_dequeue_timed(task, std::chrono::milliseconds(100))) return;
    handle_task(task);
}


void OrderHandler::pump_until(const std::function<bool()>& done, long deadline_ns) {
    Task task;
    while (!done()) {
        long remaining_ns = deadline_ns - get_curr_tstamp_ns();
        if (remaining_ns <= 0) return;
        // Capped so `done` is re-evaluated regularly even on a silent queue: some of
        // the conditions we wait on (a position update that already arrived, a wait
        // cleared by a timeout) are not themselves signalled by a new event.
        auto wait_us = std::min<long>(remaining_ns / 1000, 20000);
        if (trader_queue_.wait_dequeue_timed(task, std::chrono::microseconds(wait_us))) {
            handle_task(task);
        }
    }
}


bool OrderHandler::no_orders_outstanding() const {
    for (const auto& [product, state] : product_states_) {
        if (!state.outstanding_orders.empty()) return false;
    }
    return true;
}


bool OrderHandler::all_flat() const {
    for (const auto& [product, state] : product_states_) {
        if (state.position_in_lots != 0) return false;
    }
    return true;
}


// Everything the handler knows about position and fills reaches it over the listener
// link. With that link down the numbers below are the last ones we were told, not the
// current ones -- worth saying out loud in a shutdown report, where the difference is
// between "you are holding this" and "I could not find out".
std::string OrderHandler::residual_caveat() const {
    // Only worth saying when there is something to be unsure about. A clean state
    // reported over a dead link is still the answer the operator wanted.
    if (listener_connected_ || (no_orders_outstanding() && all_flat())) return "";
    return " (listener link is down -- this is the last state we were told, not confirmed)";
}


std::string OrderHandler::format_residual() const {
    std::string out;
    for (const auto& [product, state] : product_states_) {
        if (state.outstanding_orders.empty() && state.position_in_lots == 0) continue;
        if (!out.empty()) out += " ";
        out += fmt::format(
            "{}(orders={},position={})",
            product, state.outstanding_orders.size(), to_double_qty(state.position_in_lots)
        );
    }
    return out.empty() ? "(clean)" : out;
}


void OrderHandler::cancel_all_orders(long deadline_ns) {
    // Repeated rather than fired once: an order placed moments before the stop has no
    // exchange order_no yet, so it cannot be cancelled by id until its place ack lands
    // -- and pumping the queue is what brings that ack in. Each round cancels whatever
    // has become cancellable since the last one.
    while (!no_orders_outstanding() && get_curr_tstamp_ns() < deadline_ns) {
        size_t sent = 0, unacked = 0;
        for (auto& [product, state] : product_states_) {
            // Copied: submit_cancel does not erase from outstanding_orders (the ack
            // does), but taking the keys first keeps that independent of it.
            std::vector<uint64_t> cids;
            cids.reserve(state.outstanding_orders.size());
            for (const auto& [cid, order] : state.outstanding_orders) cids.push_back(cid);
            for (auto cid : cids) {
                if (state.response_waiting.response_waiting_cancel_orders.count(cid)) continue;
                if (!state.cid_to_order_no.count(cid)) { ++unacked; continue; }
                submit_cancel(state, product, cid);
                ++sent;
            }
        }
        if (sent == 0 && unacked == 0) break;
        LOG_INFO(
            logger_, "[Shutdown] cancel round: sent={} awaiting_place_ack={}", sent, unacked
        );
        pump_until(
            [this]() { return no_orders_outstanding(); },
            std::min<long>(get_curr_tstamp_ns() + 200L * 1000000, deadline_ns)
        );
    }

    if (no_orders_outstanding()) {
        LOG_INFO(logger_, "[Shutdown] all orders cancelled");
    } else {
        LOG_ERROR(
            logger_, "[Shutdown] gave up cancelling after {} ms; still open: {}",
            shutdown_cancel_timeout_ns_ / 1000000, format_residual()
        );
    }
}


void OrderHandler::submit_market_flatten(const std::string& product, ProductState& state) {
    int32_t position = state.position_in_lots;
    if (position == 0) return;
    // Buy back a short, sell out of a long.
    submit_place(
        state, product, /*is_bid=*/position < 0, /*price_in_min_ticks=*/0,
        /*qty_in_lots=*/position > 0 ? position : -position,
        /*is_limit=*/false, /*reduce_only=*/shutdown_reduce_only_
    );
}


void OrderHandler::flatten_positions(long deadline_ns) {
    if (all_flat()) {
        LOG_INFO(logger_, "[Shutdown] nothing to flatten");
        return;
    }
    LOG_INFO(logger_, "[Shutdown] flattening: {}", format_residual());

    // Passive first. The strategy's liquidation branch quotes the whole position one
    // tick inside the touch, which gets out at the top of the book without paying the
    // spread. It needs a live book to quote against and is not guaranteed to fill,
    // which is what the deadline and the market fallback below are for.
    while (!all_flat() && get_curr_tstamp_ns() < deadline_ns) {
        for (const auto& product : trade_products_) {
            update_orders(product, /*do_liquidate=*/true);
        }
        pump_until(
            [this]() { return all_flat(); },
            std::min<long>(get_curr_tstamp_ns() + order_update_interval_ns_, deadline_ns)
        );
    }
    if (all_flat()) {
        LOG_INFO(logger_, "[Shutdown] flat");
        return;
    }

    if (!shutdown_market_flatten_) {
        LOG_ERROR(
            logger_, "[Shutdown] EXITING WITH A POSITION: {} (market flatten disabled){}",
            format_residual(), residual_caveat()
        );
        return;
    }

    // Pull the resting liquidation quotes before crossing, so the two cannot both fill
    // and leave the position turned around. reduce_only is the second line of defence
    // on a venue that supports it.
    LOG_WARNING(
        logger_, "[Shutdown] passive flatten timed out after {} ms; going to market: {}",
        shutdown_flatten_timeout_ns_ / 1000000, format_residual()
    );
    cancel_all_orders(get_curr_tstamp_ns() + shutdown_cancel_timeout_ns_);

    for (auto& [product, state] : product_states_) {
        submit_market_flatten(product, state);
    }
    pump_until(
        [this]() { return all_flat(); },
        get_curr_tstamp_ns() + shutdown_flatten_timeout_ns_
    );

    if (all_flat()) {
        LOG_INFO(logger_, "[Shutdown] flat");
    } else {
        LOG_ERROR(
            logger_, "[Shutdown] EXITING WITH A POSITION: {}{}",
            format_residual(), residual_caveat()
        );
    }
}


void OrderHandler::shutdown() {
    if (shutdown_done_) return;
    shutdown_done_ = true;
    // Stops the decision tick re-arming, and makes any tick already on the queue a
    // no-op, so nothing quotes underneath what follows.
    shutting_down_.store(true, std::memory_order_relaxed);

    LOG_INFO(logger_, "[Shutdown] starting; state: {}", format_residual());

    // Cancel unconditionally: an order the trader is about to stop managing has no
    // business staying live at the exchange.
    cancel_all_orders(get_curr_tstamp_ns() + shutdown_cancel_timeout_ns_);

    if (!flatten_on_shutdown_) {
        if (!all_flat()) {
            LOG_WARNING(
                logger_, "[Shutdown] leaving position on (flatten_on_shutdown is off): {}{}",
                format_residual(), residual_caveat()
            );
        }
        LOG_INFO(logger_, "[Shutdown] done");
        return;
    }

    flatten_positions(get_curr_tstamp_ns() + shutdown_flatten_timeout_ns_);

    // Nothing this process placed may outlive it. The flatten's own liquidation quote
    // is the last thing that can still be resting -- it is left working whenever the
    // passive stage runs out of time or is not allowed to cross -- and an order with
    // nothing watching it is exactly what the cancel phase existed to prevent.
    if (!no_orders_outstanding()) {
        LOG_INFO(logger_, "[Shutdown] pulling the liquidation quote(s) left working");
        cancel_all_orders(get_curr_tstamp_ns() + shutdown_cancel_timeout_ns_);
    }
    LOG_INFO(
        logger_, "[Shutdown] done; final state: {}{}", format_residual(), residual_caveat()
    );
}

} // namespace Omni::Trader
