#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <glaze/glaze.hpp>
#include <quill/Logger.h>
#include <quill/LogMacros.h>
#include <moodycamel/blockingconcurrentqueue.h>

#include "common/market_msg_types.hpp"
#include "common/feed_msg_types.hpp"
#include "connection_handlers/tcp/tcp_client_base.hpp"


namespace Omni::Connection {

// Client for the listener's broadcast server: the normalized market and user feed.
//
// Templated on the consumer's task type -- any variant that can hold
// ListenerStatusUpdate / ListenerSubscribeUpdate / MarketDataResponse works, so the
// trader and the pricer share one implementation without this layer having to know
// about either of them.
template <typename TaskT>
class MarketFeedClient : public TcpClientBase {
    public:
        MarketFeedClient(
            quill::Logger* logger,
            moodycamel::BlockingConcurrentQueue<TaskT>* task_queue,
            std::string address,
            unsigned short port
        )
        :   TcpClientBase(logger, "listener", std::move(address), port),
            task_queue_(task_queue)
        {
        }

        // Stop before the subclass is gone: the base's hooks are virtual.
        ~MarketFeedClient() override { stop(); }

    protected:
        void on_status(bool connecting, bool connected) override {
            task_queue_->enqueue(ListenerStatusUpdate{
                .connecting = connecting, .connected = connected
            });
        }

        void on_subscribe_response(
            bool subscribe, bool success, const std::string& product
        ) override {
            task_queue_->enqueue(ListenerSubscribeUpdate{
                .subscribe = subscribe, .success = success, .product = product
            });
        }

        void on_data_line(std::string_view feed, const std::string& line) override {
            if (feed == "orderbook") {
                emit<OrderbookMsg>(line, feed, MarketDataResponse::Orderbook,
                    [](const OrderbookMsg& msg) { return msg.orderbook_data; });
            } else if (feed == "trade") {
                emit<TradeMsg>(line, feed, MarketDataResponse::Trade,
                    [](const TradeMsg& msg) { return msg.trade_data; });
            } else if (feed == "execution") {
                emit<ExecutionMsg>(line, feed, MarketDataResponse::Execution,
                    [](const ExecutionMsg& msg) { return msg.execution_data; });
            } else if (feed == "position") {
                emit<PositionMsg>(line, feed, MarketDataResponse::Position,
                    [](const PositionMsg& msg) { return msg.position_data; });
            } else if (feed == "product_info") {
                emit<ProductInfoMsg>(line, feed, MarketDataResponse::ProductInfo,
                    [](const ProductInfoMsg& msg) { return msg.product_info_data; });
            } else {
                LOG_WARNING(logger_, "Unhandled feed '{}' on the listener link", feed);
            }
        }

    private:
        // Parse one feed message and hand it to the consumer as a MarketDataResponse.
        template <typename MsgT, typename ExtractT>
        void emit(
            const std::string& line,
            std::string_view feed,
            MarketDataResponse::Feed kind,
            ExtractT extract
        ) {
            MsgT msg;
            auto ec = glz::read_json(msg, line);
            if (ec) {
                LOG_WARNING(logger_, "Failed to parse {} msg ({})", feed, line);
                return;
            }
            task_queue_->enqueue(MarketDataResponse{
                .feed = kind, .product = msg.product, .data = extract(msg)
            });
        }

        moodycamel::BlockingConcurrentQueue<TaskT>* task_queue_;
};

} // namespace Omni::Connection
