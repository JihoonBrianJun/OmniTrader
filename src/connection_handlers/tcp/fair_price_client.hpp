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

// Client for the pricer's fair-price server. Its tasks are distinct types from the
// listener client's, so a consumer holding both links dispatches them by type rather
// than by testing a tag on every message.
//
// The link is deliberately independent of the market-data one: a pricer outage
// degrades the trader to its own mid (the value ages out) rather than cutting the feed
// it runs on.
template <typename TaskT>
class FairPriceClient : public TcpClientBase {
    public:
        FairPriceClient(
            quill::Logger* logger,
            moodycamel::BlockingConcurrentQueue<TaskT>* task_queue,
            std::string address,
            unsigned short port
        )
        :   TcpClientBase(logger, "pricer", std::move(address), port),
            task_queue_(task_queue)
        {
        }

        // Stop before the subclass is gone: the base's hooks are virtual.
        ~FairPriceClient() override { stop(); }

    protected:
        void on_status(bool connecting, bool connected) override {
            task_queue_->enqueue(PricerStatusUpdate{
                .connecting = connecting, .connected = connected
            });
        }

        void on_subscribe_response(
            bool subscribe, bool success, const std::string& product
        ) override {
            task_queue_->enqueue(PricerSubscribeUpdate{
                .subscribe = subscribe, .success = success, .product = product
            });
        }

        void on_data_line(std::string_view feed, const std::string& line) override {
            if (feed != "fair_price") {
                LOG_WARNING(logger_, "Unhandled feed '{}' on the pricer link", feed);
                return;
            }

            FairPriceMsg fair_price_msg;
            auto ec = glz::read_json(fair_price_msg, line);
            if (ec) {
                LOG_WARNING(logger_, "Failed to parse fair_price msg ({})", line);
                return;
            }
            task_queue_->enqueue(FairPriceResponse{
                .product = fair_price_msg.product,
                .data = fair_price_msg.fair_price_data
            });
        }

    private:
        moodycamel::BlockingConcurrentQueue<TaskT>* task_queue_;
};

} // namespace Omni::Connection
