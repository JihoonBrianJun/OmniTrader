#include <algorithm>
#include <cctype>
#include <chrono>
#include <fmt/core.h>
#include <quill/LogMacros.h>
#include <nlohmann/json.hpp>

#include "market_listener/exchange/binance/binance_common.hpp"
#include "market_listener/exchange/binance/binance_listener.hpp"


namespace Omni::Listener::Binance {

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;


static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

static double sd(const nlohmann::json& j, const char* key) {
    return std::stod(j.at(key).get<std::string>());
}


BinanceListener::BinanceListener(
    const ListenerConfig& config,
    quill::Logger* logger,
    moodycamel::BlockingConcurrentQueue<ListenerEvent>* event_queue
)
:   logger_(logger),
    config_(config),
    event_queue_(event_queue),
    domain_type_(config.domain_type),
    products_(config.products),
    market_opened_(false),
    user_opened_(false),
    market_reconnect_cnt_(0),
    user_reconnect_cnt_(0),
    io_context_(std::make_unique<boost::asio::io_context>()),
    running_(false)
{
    rest_domain_ = OB::get_endpoint("rest", domain_type_).second;
    stream_domain_ = OB::get_endpoint("ws_stream", domain_type_).second;
    signer_ = OB::make_signer();

    product_manager_ = std::make_unique<OB::ProductManager>(logger_, rest_domain_);
    snapshot_fetcher_ = std::make_unique<OB::SnapshotFetcher>(logger_, rest_domain_, signer_);

    for (const auto& product : products_) {
        order_books_[product] = OB::OrderBook{};
    }
}


BinanceListener::~BinanceListener() {
    stop();
}


std::string BinanceListener::market_stream_url() const {
    std::string streams;
    for (const auto& product : products_) {
        auto s = to_lower(product);
        if (!streams.empty()) streams += "/";
        streams += fmt::format("{}@bookTicker/{}@depth@100ms/{}@aggTrade", s, s, s);
    }
    return fmt::format("{}/stream?streams={}", stream_domain_, streams);
}


void BinanceListener::create_market_client() {
    auto status_cb = [this](bool connecting, bool opened) {
        ws_event_queue_.enqueue(WsStatus{Socket::Market, connecting, opened});
    };
    auto message_cb = [this](const std::string& payload) {
        ws_event_queue_.enqueue(WsPayload{Socket::Market, payload});
    };
    market_client_ = std::make_unique<OB::BinanceWebsocketClient>(
        market_stream_url(), logger_, status_cb, message_cb
    );
}


void BinanceListener::create_user_client() {
    listen_key_ = snapshot_fetcher_->create_listen_key();
    if (listen_key_.empty()) {
        LOG_WARNING(logger_, "Could not obtain listenKey; user stream disabled");
        return;
    }
    auto status_cb = [this](bool connecting, bool opened) {
        ws_event_queue_.enqueue(WsStatus{Socket::User, connecting, opened});
    };
    auto message_cb = [this](const std::string& payload) {
        ws_event_queue_.enqueue(WsPayload{Socket::User, payload});
    };
    user_client_ = std::make_unique<OB::BinanceWebsocketClient>(
        fmt::format("{}/ws/{}", stream_domain_, listen_key_), logger_, status_cb, message_cb
    );
}


void BinanceListener::start() {
    running_.store(true);

    product_manager_->load();

    // Publish per-product trading parameters (tick/lot from exchangeInfo) so the
    // trader can use them instead of CLI-provided values.
    for (const auto& product : products_) {
        auto f = product_manager_->filter(product);
        Omni::ProductInfoMsg msg;
        msg.product = product;
        msg.product_info_data.min_tick_size = f.tick_size;
        msg.product_info_data.lot_size = f.step_size;
        event_queue_->enqueue(std::move(msg));
    }

    io_thread_ = std::thread([this]() {
        auto work_guard = boost::asio::make_work_guard(*io_context_);
        try {
            io_context_->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger_, "Exception within Binance io_context thread: {}", e.what());
        }
    });

    worker_thread_ = std::thread([this]() { worker_loop(); });

    create_market_client();
    if (signer_) {
        create_user_client();
        schedule_keepalive();
    } else {
        LOG_WARNING(logger_, "No Binance credentials; user stream and signing disabled");
    }
}


void BinanceListener::stop() {
    if (!running_.exchange(false)) return;

    ws_event_queue_.enqueue(WsStatus{Socket::Market, false, false});

    if (io_context_) io_context_->stop();
    if (io_thread_.joinable()) io_thread_.join();
    if (worker_thread_.joinable()) worker_thread_.join();

    market_client_.reset();
    user_client_.reset();
    if (!listen_key_.empty() && snapshot_fetcher_) {
        snapshot_fetcher_->close_listen_key();
    }
}


void BinanceListener::schedule_market_reconnect() {
    market_client_.reset();
    market_reconnect_timer_ = std::make_unique<boost::asio::steady_timer>(*io_context_);
    market_reconnect_timer_->expires_after(std::chrono::seconds(
        std::min(1 << std::min(market_reconnect_cnt_++, 5), 30)
    ));
    market_reconnect_timer_->async_wait([this](const boost::system::error_code ec) {
        if (!ec && running_.load()) {
            LOG_INFO(logger_, "Reconnecting Binance market socket (attempt {})", market_reconnect_cnt_);
            create_market_client();
        }
    });
}


void BinanceListener::schedule_user_reconnect() {
    user_client_.reset();
    user_reconnect_timer_ = std::make_unique<boost::asio::steady_timer>(*io_context_);
    user_reconnect_timer_->expires_after(std::chrono::seconds(
        std::min(1 << std::min(user_reconnect_cnt_++, 5), 30)
    ));
    user_reconnect_timer_->async_wait([this](const boost::system::error_code ec) {
        if (!ec && running_.load()) {
            LOG_INFO(logger_, "Reconnecting Binance user socket (attempt {})", user_reconnect_cnt_);
            create_user_client();
        }
    });
}


void BinanceListener::schedule_keepalive() {
    keepalive_timer_ = std::make_unique<boost::asio::steady_timer>(*io_context_);
    keepalive_timer_->expires_after(std::chrono::minutes(30));
    keepalive_timer_->async_wait([this](const boost::system::error_code ec) {
        if (!ec && running_.load()) {
            if (!listen_key_.empty()) snapshot_fetcher_->keepalive_listen_key();
            schedule_keepalive();
        }
    });
}


void BinanceListener::worker_loop() {
    while (running_.load()) {
        WsEvent event;
        ws_event_queue_.wait_dequeue(event);
        if (!running_.load()) break;

        std::visit(overloaded{
            [&](const WsStatus& status) { on_ws_status(status); },
            [&](const WsPayload& payload) {
                if (payload.socket == Socket::Market) handle_market_payload(payload.payload);
                else handle_user_payload(payload.payload);
            }
        }, event);
    }
}


void BinanceListener::on_ws_status(const WsStatus& status) {
    const char* name = (status.socket == Socket::Market) ? "binance_market" : "binance_user";

    if (status.socket == Socket::Market) {
        if (!market_opened_ && status.opened) {
            market_reconnect_cnt_ = 0;
            on_market_open();
        } else if (market_opened_ && !status.opened) {
            schedule_market_reconnect();
        }
        market_opened_ = status.opened;
    } else {
        if (!user_opened_ && status.opened) {
            user_reconnect_cnt_ = 0;
            on_user_open();
        } else if (user_opened_ && !status.opened) {
            schedule_user_reconnect();
        }
        user_opened_ = status.opened;
    }

    event_queue_->enqueue(ListenerStatusUpdate{
        .connection = name, .connecting = status.connecting, .opened = status.opened
    });
}


void BinanceListener::on_market_open() {
    // (Re)bootstrap each order book from a REST snapshot; the diff stream then
    // keeps it in sequence.
    for (const auto& product : products_) {
        resync_order_book(product);
    }
}


void BinanceListener::on_user_open() {
    // ACCOUNT_UPDATE/ORDER_TRADE_UPDATE only fire on change, so seed authoritative
    // state from REST snapshots on every (re)connect.
    for (auto& msg : snapshot_fetcher_->fetch_positions()) {
        event_queue_->enqueue(std::move(msg));
    }
    for (auto& msg : snapshot_fetcher_->fetch_balances()) {
        event_queue_->enqueue(std::move(msg));
    }
    for (auto& msg : snapshot_fetcher_->fetch_open_orders()) {
        event_queue_->enqueue(std::move(msg));
    }
}


void BinanceListener::resync_order_book(const std::string& product) {
    auto& book = order_books_[product];
    book.mark_desynced();
    auto snapshot = snapshot_fetcher_->fetch_depth(product);
    if (!snapshot.ok) {
        LOG_WARNING(logger_, "Depth snapshot failed for {}; will retry on next diff", product);
        return;
    }
    book.apply_snapshot(snapshot.last_update_id, snapshot.bids, snapshot.asks);

    Omni::OrderbookMsg msg;
    msg.product = product;
    msg.orderbook_data = book.to_data(config_.orderbook_levels);
    event_queue_->enqueue(std::move(msg));
}


void BinanceListener::handle_market_payload(const std::string& payload) {
    try {
        auto outer = nlohmann::json::parse(payload);
        if (!outer.contains("data")) return;
        const auto& data = outer.at("data");
        auto event_type = data.value("e", std::string{});

        if (event_type == "bookTicker") {
            Omni::OrderbookMsg msg;
            msg.product = data.at("s").get<std::string>();
            msg.orderbook_data.bid_book.push_back({sd(data, "b"), sd(data, "B")});
            msg.orderbook_data.ask_book.push_back({sd(data, "a"), sd(data, "A")});
            event_queue_->enqueue(std::move(msg));
        } else if (event_type == "depthUpdate") {
            auto product = data.at("s").get<std::string>();
            auto book_it = order_books_.find(product);
            if (book_it == order_books_.end()) return;

            std::vector<OB::OrderBook::PriceLevel> bids, asks;
            for (const auto& level : data.at("b")) {
                bids.emplace_back(std::stod(level[0].get<std::string>()),
                                  std::stod(level[1].get<std::string>()));
            }
            for (const auto& level : data.at("a")) {
                asks.emplace_back(std::stod(level[0].get<std::string>()),
                                  std::stod(level[1].get<std::string>()));
            }
            long U = data.at("U").get<long>();
            long u = data.at("u").get<long>();
            long pu = data.value("pu", -1L);

            bool ok = book_it->second.apply_diff(U, u, pu, bids, asks);
            if (!ok) {
                LOG_WARNING(logger_, "Order book desynced for {}; resyncing", product);
                resync_order_book(product);
                return;
            }

            Omni::OrderbookMsg msg;
            msg.product = product;
            msg.orderbook_data = book_it->second.to_data(config_.orderbook_levels);
            event_queue_->enqueue(std::move(msg));
        } else if (event_type == "aggTrade") {
            Omni::TradeMsg msg;
            msg.product = data.at("s").get<std::string>();
            msg.trade_data.trade_price = sd(data, "p");
            msg.trade_data.cum_trade_qty = sd(data, "q");
            event_queue_->enqueue(std::move(msg));
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Exception parsing market payload: {}", e.what());
    }
}


void BinanceListener::handle_user_payload(const std::string& payload) {
    try {
        auto j = nlohmann::json::parse(payload);
        auto event_type = j.value("e", std::string{});

        if (event_type == "ORDER_TRADE_UPDATE") {
            const auto& o = j.at("o");
            Omni::ExecutionMsg msg;
            msg.product = o.at("s").get<std::string>();
            auto& d = msg.execution_data;
            d.update_position_on_fill = false;   // ACCOUNT_UPDATE is authoritative
            d.order_no = std::to_string(o.at("i").get<long>());
            d.is_bid = (o.at("S").get<std::string>() == "BUY");
            d.order_price = sd(o, "p");

            auto exec_type = o.at("x").get<std::string>();
            double orig_qty = sd(o, "q");
            double cum_filled = sd(o, "z");

            if (exec_type == "NEW") {
                d.is_accept_data = true;
                d.is_place = true;
                d.is_accepted = true;
                d.order_qty = orig_qty;
            } else if (exec_type == "CANCELED" || exec_type == "EXPIRED") {
                d.is_accept_data = true;
                d.is_cancel = true;
                d.original_order_no = d.order_no;
                d.order_qty = orig_qty - cum_filled;   // remaining open qty removed
            } else if (exec_type == "TRADE") {
                d.is_executed = true;
                d.execute_price = sd(o, "L");
                d.execute_qty = sd(o, "l");
            } else {
                return;   // CALCULATED / AMENDMENT / etc. not modeled yet
            }
            event_queue_->enqueue(std::move(msg));
        } else if (event_type == "ACCOUNT_UPDATE") {
            const auto& a = j.at("a");
            if (a.contains("B")) {
                for (const auto& b : a.at("B")) {
                    Omni::BalanceMsg msg;
                    msg.asset = b.at("a").get<std::string>();
                    // Stream carries wallet balance ("wb") but not availableBalance;
                    // the trader keeps the last snapshot's available until next snapshot.
                    msg.balance_data.wallet_balance = sd(b, "wb");
                    event_queue_->enqueue(std::move(msg));
                }
            }
            if (a.contains("P")) {
                for (const auto& p : a.at("P")) {
                    Omni::PositionMsg msg;
                    msg.product = p.at("s").get<std::string>();
                    msg.position_data.position_amt = sd(p, "pa");
                    msg.position_data.entry_price = sd(p, "ep");
                    msg.position_data.unrealized_pnl = sd(p, "up");
                    event_queue_->enqueue(std::move(msg));
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Exception parsing user payload: {}", e.what());
    }
}

} // namespace Omni::Listener::Binance
