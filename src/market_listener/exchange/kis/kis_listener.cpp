#include <stdexcept>
#include <chrono>
#include <fmt/core.h>
#include <quill/LogMacros.h>

#include "config_handlers/config_utils.hpp"
#include "config_handlers/kis/kis_config.hpp"
#include "market_listener/exchange/kis/code_manager.hpp"
#include "market_listener/exchange/kis/kis_listener.hpp"


namespace Omni::Listener::KIS {

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

namespace KCfg = Omni::KIS::Config;
namespace CM = Omni::KIS::CodeManager;


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

    create_code_manager();
}


KisListener::~KisListener() {
    stop();
}


void KisListener::create_code_manager() {
    auto codes_db_path = fmt::format(
        "{}/{}/{}", config_.codes_db_base_path, config_.region, config_.market_type
    );

    if (config_.market_type == "derivatives") {
        code_manager_ = std::make_shared<Omni::KIS::KoreanDerivatives::CodeManager>(
            config_.codes, codes_db_path, hts_id_, config_.is_night, logger_
        );
    }
    if (code_manager_ == nullptr) {
        throw std::runtime_error(fmt::format(
            "KIS market type {} not supported", config_.market_type
        ));
    }

    subscription_messages_.clear();
    for (const auto& code : config_.codes) {
        subscription_messages_.emplace_back(write_ws_request_msg(
            code_manager_->get_orderbook_subscription_input(code)
        ));
        subscription_messages_.emplace_back(write_ws_request_msg(
            code_manager_->get_trade_subscription_input(code)
        ));
        subscription_messages_.emplace_back(write_ws_request_msg(
            code_manager_->get_execution_subscription_input(code)
        ));
    }
}


void KisListener::create_ws_client() {
    ws_client_ = std::make_unique<KisWebsocketClient>(
        ws_domain_, logger_, &ws_response_queue_, code_manager_
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
    if (!ws_client_opened_ && update.opened) {
        on_ws_client_open();
    } else if (ws_client_opened_ && !update.opened) {
        on_ws_client_fail();
    }
    ws_client_connecting_ = update.connecting;
    ws_client_opened_ = update.opened;

    event_queue_->enqueue(ListenerStatusUpdate{
        .connection = "kis", .connecting = update.connecting, .opened = update.opened
    });
}


void KisListener::on_ws_client_open() {
    ws_client_reconnect_cnt_ = 0;
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
    if (ws_client_reconnect_cnt_ >= 5) {
        LOG_WARNING(logger_, "ws client reconnect failed for max (5) times");
        return;
    }
    ws_client_reconnect_timer_ = std::make_unique<boost::asio::steady_timer>(*io_context_);
    ws_client_reconnect_timer_->expires_after(std::chrono::seconds(
        (1 << (ws_client_reconnect_cnt_++)) - 1
    ));
    ws_client_reconnect_timer_->async_wait([this](const boost::system::error_code ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        } else if (!ec && running_.load()) {
            LOG_INFO(logger_, "Trying {}th ws client reconnect", ws_client_reconnect_cnt_);
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
                const auto& code = market_data[data_offset];
                Omni::OrderbookMsg parsed_msg;
                data_offset += code_manager_->parse_orderbook_data(
                    code, data_offset, market_data, parsed_msg
                );
                event_queue_->enqueue(parsed_msg);
                break;
            }
            case CM::TrIdType::TradeTrId: {
                const auto& code = market_data[data_offset];
                Omni::TradeMsg parsed_msg;
                data_offset += code_manager_->parse_trade_data(
                    code, data_offset, market_data, parsed_msg
                );
                event_queue_->enqueue(parsed_msg);
                break;
            }
            case CM::TrIdType::ExecutionTrId: {
                Omni::ExecutionMsg parsed_msg;
                data_offset += code_manager_->parse_execution_data(
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
