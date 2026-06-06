#include "binance_ws_client.hpp"

namespace Omni::Binance {

BinanceWebsocketClient::BinanceWebsocketClient(
    const std::string& uri, quill::Logger* logger,
    StatusCb status_cb, MessageCb message_cb,
    int ping_interval_sec
)
:   Base(uri, logger, ping_interval_sec),
    status_cb_(std::move(status_cb)),
    message_cb_(std::move(message_cb))
{
    set_handlers();
    init_connection(uri);
}


void BinanceWebsocketClient::notify_websocket_status() {
    if (status_cb_) status_cb_(connecting_.load(), opened_.load());
}


void BinanceWebsocketClient::on_stream_message(Base::message_ptr msg) {
    if (message_cb_) message_cb_(msg->get_payload());
}

} // namespace Omni::Binance
