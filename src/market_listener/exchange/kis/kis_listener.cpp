#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <fmt/core.h>
#include <quill/LogMacros.h>

#include "config_handlers/config_utils.hpp"
#include "config_handlers/kis/kis_config.hpp"
#include "market_listener/exchange/kis/product_manager.hpp"
#include "market_listener/exchange/kis/kis_listener.hpp"


namespace Omni::Listener::KIS {

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

namespace KCfg = Omni::KIS::Config;
namespace CM = Omni::KIS::ProductManager;


KisListener::KisListener(
    const ListenerConfig& config,
    quill::Logger* logger,
    moodycamel::BlockingConcurrentQueue<ListenerEvent>* event_queue
)
:   logger_(logger),
    config_(config),
    event_queue_(event_queue),
    ws_client_connecting_(false),
    ws_client_opened_(false),
    ws_client_reconnect_cnt_(0),
    io_context_(std::make_unique<boost::asio::io_context>()),
    running_(false)
{
    KCfg::update_access_tokens(logger_);

    auto ws_domain_type = (config_.domain_type == "test") ? "ws_test" : "ws_real";
    ws_domain_ = Omni::Config::get_domain(KCfg::EXCHANGE, ws_domain_type).second;
    hts_id_ = KCfg::get_account_info().second.hts_id;

    create_product_manager();
}


KisListener::~KisListener() {
    stop();
}


void KisListener::create_product_manager() {
    auto products_db_path = fmt::format(
        "{}/{}/{}", config_.products_db_base_path, config_.region, config_.market_type
    );

    if (config_.market_type == "derivatives") {
        product_manager_ = std::make_shared<Omni::KIS::KoreanDerivatives::ProductManager>(
            config_.products, products_db_path, hts_id_, config_.is_night, logger_
        );
    }
    if (product_manager_ == nullptr) {
        throw std::runtime_error(fmt::format(
            "KIS market type {} not supported", config_.market_type
        ));
    }

    subscription_messages_.clear();
    for (const auto& product : config_.products) {
        subscription_messages_.emplace_back(write_ws_request_msg(
            product_manager_->get_orderbook_subscription_input(product)
        ));
        subscription_messages_.emplace_back(write_ws_request_msg(
            product_manager_->get_trade_subscription_input(product)
        ));
        subscription_messages_.emplace_back(write_ws_request_msg(
            product_manager_->get_execution_subscription_input(product)
        ));
    }
}


void KisListener::create_ws_client() {
    ws_client_ = std::make_unique<KisWebsocketClient>(
        ws_domain_, logger_, &ws_response_queue_, product_manager_
    );
}


std::string KisListener::write_ws_request_msg(const CM::SubscriptionInput& input) {
    auto ws_access_token_result = KCfg::get_websocket_access_token();
    if (!ws_access_token_result.first) return "";

    return fmt::format(
        R"({{"header":{{"approval_key":"{}","custtype":"P","tr_type":"1","content-type":"utf-8"}},)"
        R"("body":{{"input":{{"tr_id":"{}","tr_key":"{}"}}}}}})",
        ws_access_token_result.second, input.tr_id, input.tr_key
    );
}


void KisListener::start() {
    running_.store(true);
    create_ws_client();

    io_thread_ = std::thread([this]() {
        auto work_guard = boost::asio::make_work_guard(*io_context_);
        try {
            io_context_->run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger_, "Exception within KIS io_context thread: {}", e.what());
        }
    });

    worker_thread_ = std::thread([this]() { worker_loop(); });
}


void KisListener::stop() {
    if (!running_.exchange(false)) return;

    // Unblock the worker loop.
    ws_response_queue_.enqueue(WsStatusUpdate{.connecting = false, .opened = false});

    if (io_context_) io_context_->stop();
    if (io_thread_.joinable()) io_thread_.join();
    if (worker_thread_.joinable()) worker_thread_.join();
    ws_client_.reset();
}


void KisListener::worker_loop() {
    while (running_.load()) {
        WsResponse ws_response;
        ws_response_queue_.wait_dequeue(ws_response);
        if (!running_.load()) break;

        std::visit(overloaded{
            [&](const WsStatusUpdate& response) { on_ws_status(response); },
            [&](const WsMarketDataResponse& response) { on_market_data(response); }
        }, ws_response);
    }
}


void KisListener::on_ws_status(const WsStatusUpdate& update) {
    // Redial whenever the socket is down and not currently dialling. Keying this on
    // the up->down transition (as it used to) meant a dial that never succeeded took
    // no branch at all: a failed first connect was permanent, and a failed redial
    // ended the chain after one attempt.
    if (!ws_client_opened_ && update.opened) {
        on_ws_client_open();
    } else if (!update.opened && !update.connecting && !ws_client_reconnect_pending_.load()) {
        on_ws_client_fail();
    }
    ws_client_connecting_ = update.connecting;
    ws_client_opened_ = update.opened;

    event_queue_->enqueue(ListenerStatusUpdate{
        .connection = "kis", .connecting = update.connecting, .opened = update.opened
    });
}


void KisListener::on_ws_client_open() {
    ws_client_reconnect_cnt_.store(0);
    ws_client_reconnect_pending_.store(false);
    for (const auto& subscription_message : subscription_messages_) {
        ws_client_->send(
            ws_client_->get_connection_hdl(),
            subscription_message,
            websocketpp::frame::opcode::TEXT
        );
        LOG_INFO(logger_, "Sent subscription message ({}) to ws_client", subscription_message);
    }
}


void KisListener::on_ws_client_fail() {
    ws_client_.reset();
    ws_client_reconnect_pending_.store(true);

    // Keeps trying, with the wait capped rather than the attempts. Stopping after
    // five meant a two-minute venue-side outage left the listener permanently mute
    // with the process still running and apparently healthy -- market data simply
    // never came back, and only a restart fixed it. A capped backoff costs one dial
    // every 30s while the venue is away and recovers on its own when it returns.
    constexpr int MAX_BACKOFF_STEP = 5;   // (1 << 5) - 1 = 31s
    auto step = std::min(ws_client_reconnect_cnt_.fetch_add(1), MAX_BACKOFF_STEP);
    if (ws_client_reconnect_cnt_.load() == MAX_BACKOFF_STEP + 1) {
        LOG_WARNING(
            logger_, "ws client reconnect has failed {} times; still retrying every {}s",
            ws_client_reconnect_cnt_.load(), (1 << MAX_BACKOFF_STEP) - 1
        );
    }

    ws_client_reconnect_timer_ = std::make_unique<boost::asio::steady_timer>(*io_context_);
    ws_client_reconnect_timer_->expires_after(std::chrono::seconds((1 << step) - 1));
    ws_client_reconnect_timer_->async_wait([this](const boost::system::error_code ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        } else if (!ec && running_.load()) {
            LOG_INFO(logger_, "Trying {}th ws client reconnect", ws_client_reconnect_cnt_.load());
            ws_client_reconnect_pending_.store(false);
            create_ws_client();
        }
    });
}


void KisListener::on_market_data(const WsMarketDataResponse& response) {
    const auto& market_data = response.market_data;
    size_t data_offset = 0;

    for (size_t data_idx = 0; data_idx < response.data_num; ++data_idx) {
        if (data_offset >= market_data.size()) break;

        switch (response.tr_id_type) {
            case CM::TrIdType::OrderbookTrId: {
                const auto& product = market_data[data_offset];
                Omni::OrderbookMsg parsed_msg;
                data_offset += product_manager_->parse_orderbook_data(
                    product, data_offset, market_data, parsed_msg
                );
                event_queue_->enqueue(parsed_msg);
                break;
            }
            case CM::TrIdType::TradeTrId: {
                const auto& product = market_data[data_offset];
                Omni::TradeMsg parsed_msg;
                data_offset += product_manager_->parse_trade_data(
                    product, data_offset, market_data, parsed_msg
                );
                event_queue_->enqueue(parsed_msg);
                break;
            }
            case CM::TrIdType::ExecutionTrId: {
                Omni::ExecutionMsg parsed_msg;
                data_offset += product_manager_->parse_execution_data(
                    data_offset, market_data, parsed_msg
                );
                event_queue_->enqueue(parsed_msg);
                break;
            }
            case CM::TrIdType::Error: {
                break;
            }
        }
    }
}

} // namespace Omni::Listener::KIS
