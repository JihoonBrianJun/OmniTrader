#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <fmt/core.h>
#include <quill/LogMacros.h>

#include "market_listener/exchange/binance/binance_common.hpp"
#include "market_listener/exchange/binance/binance_listener.hpp"
#include "market_listener/exchange/binance/market_dtypes.hpp"


namespace Omni::Listener::Binance {

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;


static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Binance encodes decimals as JSON strings; the dtype structs keep them as
// std::string and this converts on use (empty -> 0.0).
static double sd(const std::string& s) {
    return s.empty() ? 0.0 : std::stod(s);
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

    rest_client_ = std::make_unique<OB::BinanceRestClient>(logger_, rest_domain_, signer_);

    // Split configured products by category: futures get market streams + order
    // books + positionRisk; asset gets balance only; spot is not implemented yet.
    for (const auto& spec : config.product_specs) {
        switch (spec.category) {
            case Category::futures:
                futures_products_.push_back(spec.product);
                order_books_[spec.product] = OB::OrderBook{};
                break;
            case Category::asset:
                asset_products_.push_back(spec.product);
                break;
            case Category::spot:
                LOG_WARNING(logger_, "spot category not implemented yet; skipping {}", spec.product);
                break;
        }
    }
}


BinanceListener::~BinanceListener() {
    stop();
}


std::string BinanceListener::market_stream_url() const {
    std::string streams;
    for (const auto& product : futures_products_) {
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
    listen_key_ = rest_client_->create_listen_key();
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
    if (!listen_key_.empty() && rest_client_) {
        rest_client_->close_listen_key();
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
            if (!listen_key_.empty()) rest_client_->keepalive_listen_key();
            schedule_keepalive();
        }
    });
}


// Called by MarketListener, at startup and on its refresh timer -- always from that
// one thread, which is why rest_client_'s filter cache needs no lock. filter() has
// no other caller inside the adapter.
void BinanceListener::publish_product_info() {
    rest_client_->load();
    for (const auto& product : futures_products_) {
        auto f = rest_client_->filter(product);
        Omni::ProductInfoMsg msg;
        msg.product = product;
        msg.product_info_data.tick_size = f.tick_size;
        msg.product_info_data.lot_size = f.step_size;
        event_queue_->enqueue(std::move(msg));
    }
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
    for (const auto& product : futures_products_) {
        resync_order_book(product);
    }
}


void BinanceListener::on_user_open() {
    // ACCOUNT_UPDATE/ORDER_TRADE_UPDATE only fire on change, so seed authoritative
    // state from REST snapshots on every (re)connect, per configured category.
    if (!futures_products_.empty()) {
        for (auto& msg : rest_client_->fetch_positions()) {   // positionRisk
            event_queue_->enqueue(std::move(msg));
        }
    }
    if (!asset_products_.empty()) {
        for (auto& msg : rest_client_->fetch_balances()) {    // /fapi/v2/balance
            event_queue_->enqueue(std::move(msg));
        }
    }
    for (auto& msg : rest_client_->fetch_open_orders()) {
        event_queue_->enqueue(std::move(msg));
    }
}


void BinanceListener::resync_order_book(const std::string& product) {
    auto& book = order_books_[product];
    book.mark_desynced();
    auto snapshot = rest_client_->fetch_depth(product);
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
    // Classify on data.e (partial read) before fully parsing the per-event struct.
    constexpr glz::opts classify_opts{
        .format = glz::JSON, .error_on_unknown_keys = false, .partial_read = true
    };
    constexpr glz::opts read_opts{.format = glz::JSON, .error_on_unknown_keys = false};

    try {
        MarketEventClassifier classifier;
        if (glz::read<classify_opts>(classifier, payload)) return;
        const std::string& event_type = classifier.data.e;

        if (event_type == "bookTicker") {
            BookTickerEnvelope env;
            if (glz::read<read_opts>(env, payload)) return;
            const auto& t = env.data;
            Omni::OrderbookMsg msg;
            msg.product = t.s;
            msg.orderbook_data.bid_book.push_back({sd(t.b), sd(t.B)});
            msg.orderbook_data.ask_book.push_back({sd(t.a), sd(t.A)});
            event_queue_->enqueue(std::move(msg));
        } else if (event_type == "depthUpdate") {
            DepthUpdateEnvelope env;
            if (glz::read<read_opts>(env, payload)) return;
            const auto& d = env.data;
            auto book_it = order_books_.find(d.s);
            if (book_it == order_books_.end()) return;

            std::vector<OB::OrderBook::PriceLevel> bids, asks;
            bids.reserve(d.b.size());
            asks.reserve(d.a.size());
            for (const auto& level : d.b) bids.emplace_back(sd(level[0]), sd(level[1]));
            for (const auto& level : d.a) asks.emplace_back(sd(level[0]), sd(level[1]));

            bool ok = book_it->second.apply_diff(d.U, d.u, d.pu, bids, asks);
            if (!ok) {
                LOG_WARNING(logger_, "Order book desynced for {}; resyncing", d.s);
                resync_order_book(d.s);
                return;
            }

            Omni::OrderbookMsg msg;
            msg.product = d.s;
            msg.orderbook_data = book_it->second.to_data(config_.orderbook_levels);
            event_queue_->enqueue(std::move(msg));
        } else if (event_type == "aggTrade") {
            AggTradeEnvelope env;
            if (glz::read<read_opts>(env, payload)) return;
            Omni::TradeMsg msg;
            msg.product = env.data.s;
            msg.trade_data.trade_price = sd(env.data.p);
            msg.trade_data.cum_trade_qty = sd(env.data.q);
            event_queue_->enqueue(std::move(msg));
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Exception parsing market payload: {}", e.what());
    }
}


void BinanceListener::handle_user_payload(const std::string& payload) {
    // Classify on the top-level e (partial read) before fully parsing.
    constexpr glz::opts classify_opts{
        .format = glz::JSON, .error_on_unknown_keys = false, .partial_read = true
    };
    constexpr glz::opts read_opts{.format = glz::JSON, .error_on_unknown_keys = false};

    try {
        UserEventClassifier classifier;
        if (glz::read<classify_opts>(classifier, payload)) return;

        if (classifier.e == "ORDER_TRADE_UPDATE") {
            OrderTradeUpdate update;
            if (glz::read<read_opts>(update, payload)) return;
            const auto& o = update.o;

            Omni::ExecutionMsg msg;
            msg.product = o.s;
            auto& d = msg.execution_data;
            d.update_position_on_fill = false;   // ACCOUNT_UPDATE is authoritative
            d.order_no = std::to_string(o.i);
            d.client_order_id = o.c;             // cid for trader routing
            d.is_bid = (o.S == "BUY");
            d.order_price = sd(o.p);

            double orig_qty = sd(o.q);
            double cum_filled = sd(o.z);

            if (o.x == "NEW") {
                d.is_accept_data = true;
                d.is_place = true;
                d.is_accepted = true;
                d.order_qty = orig_qty;
            } else if (o.x == "CANCELED" || o.x == "EXPIRED") {
                d.is_accept_data = true;
                d.is_cancel = true;
                d.original_order_no = d.order_no;
                d.order_qty = orig_qty - cum_filled;   // remaining open qty removed
            } else if (o.x == "TRADE") {
                d.is_executed = true;
                d.execute_price = sd(o.L);
                d.execute_qty = sd(o.l);
            } else {
                return;   // CALCULATED / AMENDMENT / etc. not modeled yet
            }
            event_queue_->enqueue(std::move(msg));
        } else if (classifier.e == "ACCOUNT_UPDATE") {
            AccountUpdate update;
            if (glz::read<read_opts>(update, payload)) return;

            for (const auto& b : update.a.B) {
                Omni::PositionMsg msg;
                msg.product = b.a;   // asset symbol
                // Stream carries wallet balance ("wb") but not availableBalance;
                // the trader keeps the last snapshot's available until next snapshot.
                msg.position_data.balance = sd(b.wb);
                event_queue_->enqueue(std::move(msg));
            }
            for (const auto& p : update.a.P) {
                Omni::PositionMsg msg;
                msg.product = p.s;
                msg.position_data.balance = sd(p.pa);   // signed position amount
                event_queue_->enqueue(std::move(msg));
            }
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Exception parsing user payload: {}", e.what());
    }
}

} // namespace Omni::Listener::Binance
