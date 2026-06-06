#include <stdexcept>
#include <chrono>
#include <cmath>
#include <fmt/core.h>
#include <quill/LogMacros.h>

#include "utils/datetime.hpp"
#include "trader/base/trader.hpp"

#include "trader/order_gateway/kis/order_gateway.hpp"
#include "trader/order_gateway/binance/order_gateway.hpp"


namespace Omni::Trader {

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;


std::unique_ptr<Omni::OrderGateway::IOrderGateway> create_order_gateway(
    const TraderConfig& config, quill::Logger* logger
) {
    if (config.exchange == "binance") {
        return std::make_unique<Omni::Binance::BinanceOrderGateway>(config, logger);
    } else if (config.exchange == "kis") {
        return Omni::KIS::create_kis_order_gateway(config, logger);
    }
    throw std::runtime_error(fmt::format("Unsupported exchange: {}", config.exchange));
}


BaseTrader::BaseTrader(
    std::shared_ptr<Omni::Strategy::IStrategy> strategy,
    const TraderConfig& config,
    quill::Logger* logger
)
:   logger_(logger),
    exchange_(config.exchange),
    trade_codes_(config.trade_codes),
    subscribe_codes_(config.subscribe_same_codes ? config.trade_codes : config.subscribe_codes),
    region_(config.region),
    market_type_(config.market_type),
    broadcast_host_address_(config.broadcast_host_address),
    broadcast_port_(config.broadcast_port),
    is_night_(config.is_night),
    min_tick_size_(config.min_tick_size),
    lot_size_(config.lot_size),
    order_update_interval_ns_(config.order_update_interval_ms * 1000000),
    strategy_(strategy),
    order_gateway_(create_order_gateway(config, logger)),
    trader_io_context_(std::make_unique<boost::asio::io_context>()),
    tcp_client_(std::make_unique<Connection::TcpClient>(
        *trader_io_context_, logger_, &trader_queue_
    )),
    tcp_client_connecting_(false),
    tcp_client_connected_(false)
{
    set_order_update_timer();
    start_tcp_client();
}


BaseTrader::~BaseTrader() {
    if (tcp_client_) {
        tcp_client_->disconnect();
    }
    if (trader_io_context_) {
        trader_io_context_->stop();
    }
    if (trader_context_thread_.joinable()) {
        trader_context_thread_.join();
    }
}


void BaseTrader::set_order_update_timer() {
    order_update_timer_ = std::make_shared<boost::asio::steady_timer>(*trader_io_context_);
    order_update_timer_->expires_after(std::chrono::nanoseconds(
        order_update_interval_ns_ - (get_curr_tstamp_ns() % order_update_interval_ns_)
    ));

    order_update_timer_->async_wait([this](const boost::system::error_code ec) {
        if (ec == boost::asio::error::operation_aborted) {
            LOG_WARNING(logger_, "Order update timer has been aborted previously");
            return;
        } else if (!ec) {
            for (const auto& trade_code : trade_codes_) {
                trader_queue_.enqueue(OrderUpdate{.code = trade_code});
            }
            set_order_update_timer();
        }
    });
}


void BaseTrader::start_tcp_client() {
    tcp_client_->connect(broadcast_host_address_, broadcast_port_);
    trader_context_thread_ = std::thread([this]() {
        try {
            trader_io_context_->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger_, "Exception within TCP client thread: {}", e.what());
        }
    });
}


void BaseTrader::on_tcp_client_open() {
    for (const auto& code : subscribe_codes_) {
        tcp_client_->subscribe(convert_code_for_subscription(code));
        LOG_INFO(logger_, "Sent subscription of code {} to tcp server", code);
    }
}


void BaseTrader::on_tcp_client_fail() {
    tcp_client_->connect(broadcast_host_address_, broadcast_port_);
}


void BaseTrader::on_tcp_client_status_update(const TcpStatusUpdate& response) {
    if (!tcp_client_connected_ && response.connected) {
        on_tcp_client_open();
    } else if (tcp_client_connected_ && !response.connected) {
        on_tcp_client_fail();
    }
    tcp_client_connecting_ = response.connecting;
    tcp_client_connected_ = response.connected;
}


void BaseTrader::on_tcp_subscribe_update(const TcpSubscribeUpdate& /*response*/) {
    // TODO
}


long BaseTrader::get_price_in_min_ticks(double price) {
    return static_cast<long>(std::round(price / min_tick_size_));
}


long BaseTrader::get_qty_in_lots(double qty) {
    return static_cast<long>(std::round(qty / lot_size_));
}


void BaseTrader::update_bbo(const std::string& code, const BBO& bbo, bool update_mid_vwap) {
    auto& code_bbo = bbo_infos_[code];
    code_bbo = bbo;
    LOG_INFO(logger_, "{} bbid_price={}, bask_price={}", code, bbo.bbid_price, bbo.bask_price);

    if (update_mid_vwap) {
        if ((bbo.bbid_price > 0) && (bbo.bask_price > 0)) {
            code_bbo.mid_price = (bbo.bbid_price + bbo.bask_price) / 2.0;
            code_bbo.vwap = (bbo.bbid_price + bbo.bask_price)
                / (bbo.bbid_qty + bbo.bask_qty + 1e-8);
        } else if ((bbo.bbid_price > 0) && (bbo.bask_price <= 0)) {
            code_bbo.mid_price = bbo.bbid_price;
            code_bbo.vwap = bbo.bbid_price;
        } else if ((bbo.bbid_price <= 0) && (bbo.bask_price > 0)) {
            code_bbo.mid_price = bbo.bask_price;
            code_bbo.vwap = bbo.bask_price;
        } else {
            code_bbo.mid_price = NAN;
            code_bbo.vwap = NAN;
        }
    } else {
        code_bbo.mid_price = NAN;
        code_bbo.vwap = NAN;
    }
}


void BaseTrader::update_position(const std::string& code, long position_delta_in_lots) {
    auto code_it = position_in_lots_.find(code);
    if (code_it == position_in_lots_.end()) {
        position_in_lots_[code] = position_delta_in_lots;
    } else {
        code_it->second += position_delta_in_lots;
    }
}


void BaseTrader::set_position(const std::string& code, long position_in_lots) {
    position_in_lots_[code] = position_in_lots;
}


void BaseTrader::update_outstanding_orders(
    bool is_bid, const std::string& code,
    const std::string& order_no, long qty_decr_in_lots
) {
    auto& outstanding_orders = is_bid ? outstanding_bid_orders_ : outstanding_ask_orders_;
    auto code_it = outstanding_orders.find(code);
    if (code_it != outstanding_orders.end()) {
        auto order_it = code_it->second.find(order_no);
        if (order_it != code_it->second.end()) {
            order_it->second.left_qty_in_lots -= qty_decr_in_lots;
            if (order_it->second.left_qty_in_lots <= 0) {
                code_it->second.erase(order_it);
            }
        }
        if (code_it->second.empty()) {
            outstanding_orders.erase(code_it);
        }
    }
}


void BaseTrader::on_orderbook_data(const std::string& code, const OrderbookData& data) {
    try {
        update_bbo(
            parse_code(code),
            BBO{
                .bbid_price = data.bid_book.empty() ? NAN : data.bid_book[0].price.value_or(NAN),
                .bask_price = data.ask_book.empty() ? NAN : data.ask_book[0].price.value_or(NAN),
                .bbid_qty = data.bid_book.empty() ? NAN : data.bid_book[0].qty.value_or(NAN),
                .bask_qty = data.ask_book.empty() ? NAN : data.ask_book[0].qty.value_or(NAN)
            }
        );
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Exception {} while processing orderbook data", e.what());
    }
}


void BaseTrader::on_trade_data(const std::string& /*code*/, const TradeData& /*data*/) {
    // TODO
}


void BaseTrader::on_execution_data(const std::string& code, const ExecutionData& data) {
    try {
        auto parsed_code = parse_code(code);

        if (data.is_accept_data) {
            if (data.is_place) {
                pending_orders_.pending_place_orders.erase(data.order_no);
            } else if (data.is_cancel) {
                pending_orders_.pending_cancel_orders.erase(data.order_no);
            } else {
                pending_orders_.pending_amend_orders.erase(data.order_no);
            }

            if (!data.is_cancel && data.is_accepted && (data.order_qty > 0)) {
                auto& outstanding_orders = data.is_bid ?
                    outstanding_bid_orders_ : outstanding_ask_orders_;
                outstanding_orders[parsed_code][data.order_no] = OutstandingOrderInfo{
                    .price = data.order_price.value_or(NAN),
                    .left_qty_in_lots = get_qty_in_lots(data.order_qty.value_or(NAN))
                };
                LOG_INFO(
                    logger_, "[Order Add] order_no={}, is_bid={}, price={}, qty={}",
                    data.order_no, data.is_bid, data.order_price.value_or(NAN), data.order_qty.value_or(NAN)
                );
            }
            if (!data.is_place) {
                update_outstanding_orders(
                    data.is_bid, parsed_code, data.original_order_no, get_qty_in_lots(data.order_qty.value_or(NAN))
                );
                LOG_INFO(
                    logger_, "[Order Delete] order_no={}, is_bid={}, price={}, qty={}",
                    data.original_order_no, data.is_bid,
                    data.order_price.value_or(NAN), data.order_qty.value_or(NAN)
                );
            }
        } else if (data.is_executed && (data.execute_qty > 0)) {
            auto execute_qty_in_lots = get_qty_in_lots(data.execute_qty.value_or(NAN));
            update_outstanding_orders(
                data.is_bid, parsed_code, data.order_no, execute_qty_in_lots
            );
            // KIS derives position from fills. Binance instead sends authoritative
            // ACCOUNT_UPDATE position snapshots, so it disables this to avoid
            // double-counting (set_position is the source of truth there).
            if (data.update_position_on_fill) {
                update_position(
                    parsed_code, data.is_bid ? execute_qty_in_lots : -execute_qty_in_lots
                );
            }
            LOG_INFO(
                logger_, "[Order Execution] order_no={}, is_bid={}, price={}, qty={}",
                data.order_no, data.is_bid, data.execute_price.value_or(NAN), data.execute_qty.value_or(NAN)
            );
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Exception {} while processing execution data", e.what());
    }
}


void BaseTrader::on_position_data(const std::string& code, const PositionData& data) {
    try {
        if (data.position_amt.has_value()) {
            auto parsed_code = parse_code(code);
            set_position(parsed_code, get_qty_in_lots(data.position_amt.value()));
            LOG_INFO(logger_, "[Position] {} position_amt={}", parsed_code, data.position_amt.value());
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Exception {} while processing position data", e.what());
    }
}


void BaseTrader::process_market_data(const TcpMarketDataResponse& response) {
    switch (response.feed) {
        case TcpMarketDataResponse::Feed::Orderbook: {
            on_orderbook_data(response.code, std::get<OrderbookData>(response.data));
            break;
        }
        case TcpMarketDataResponse::Feed::Trade: {
            on_trade_data(response.code, std::get<TradeData>(response.data));
            break;
        }
        case TcpMarketDataResponse::Feed::Execution: {
            on_execution_data(response.code, std::get<ExecutionData>(response.data));
            break;
        }
        case TcpMarketDataResponse::Feed::Position: {
            on_position_data(response.code, std::get<PositionData>(response.data));
            break;
        }
        case TcpMarketDataResponse::Error: {
            break;
        }
    }
}


void BaseTrader::update_orders(const std::string& code) {
    auto bbo_code_it = bbo_infos_.find(code);
    if (bbo_code_it == bbo_infos_.end()) {
        LOG_WARNING(logger_, "BBO info does not exist for code {} yet", code);
        return;
    }

    std::vector<Omni::OrderGateway::OrderPlaceInfo> order_place_infos;
    std::vector<Omni::OrderGateway::OrderAmendInfo> order_amend_infos;
    std::vector<Omni::OrderGateway::OrderCancelInfo> order_cancel_infos;
    strategy_->update_orders(
        code,
        bbo_code_it->second,
        position_in_lots_[code],
        outstanding_bid_orders_[code],
        outstanding_ask_orders_[code],
        order_place_infos,
        order_amend_infos,
        order_cancel_infos
    );

    for (const auto& order_cancel_info : order_cancel_infos) {
        Omni::OrderGateway::OrderResponse cancel_order_response;
        order_gateway_->cancel_order(order_cancel_info, cancel_order_response);
        pending_orders_.pending_cancel_orders.insert(cancel_order_response.order_no);
    }

    for (const auto& order_amend_info : order_amend_infos) {
        Omni::OrderGateway::OrderResponse amend_order_response;
        order_gateway_->amend_order(order_amend_info, amend_order_response);
        pending_orders_.pending_amend_orders.insert(amend_order_response.order_no);
    }

    for (const auto& order_place_info : order_place_infos) {
        Omni::OrderGateway::OrderResponse place_order_response;
        order_gateway_->place_order(order_place_info, place_order_response);
        pending_orders_.pending_place_orders.insert(place_order_response.order_no);
        LOG_INFO(
            logger_, "[Order Place] order_no={}, is_bid={}, price={}, qty={}",
            place_order_response.order_no, order_place_info.is_bid,
            order_place_info.price, order_place_info.qty
        );
    }
}


void BaseTrader::run() {
    Task task;
    trader_queue_.wait_dequeue(task);

    std::visit(overloaded{
        [&](const TcpStatusUpdate& response) {on_tcp_client_status_update(response);},
        [&](const TcpSubscribeUpdate& response) {on_tcp_subscribe_update(response);},
        [&](const TcpMarketDataResponse& response) {process_market_data(response);},
        [&](const OrderUpdate& update) {update_orders(update.code);}
    }, task);
}

} // namespace Omni::Trader
