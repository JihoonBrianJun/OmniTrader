#pragma once
#include <string>
#include <functional>

#include "connection_handlers/websocket/websocket_client.hpp"


namespace Omni::Binance {

// TLS websocket client for Binance streams. Generic over market vs user streams:
// the owner supplies status/message callbacks. Binance pushes JSON text frames
// and server-side pings (websocketpp auto-replies with pong).
class BinanceWebsocketClient
    : public Omni::Connection::BaseWebsocketClient<Omni::Connection::WsTlsConfig> {
    public:
        using Base = Omni::Connection::BaseWebsocketClient<Omni::Connection::WsTlsConfig>;
        using StatusCb = std::function<void(bool connecting, bool opened)>;
        using MessageCb = std::function<void(const std::string& payload)>;

        BinanceWebsocketClient(
            const std::string& uri, quill::Logger* logger,
            StatusCb status_cb, MessageCb message_cb,
            int ping_interval_sec = 180
        );

    private:
        StatusCb status_cb_;
        MessageCb message_cb_;

        void notify_websocket_status() override;
        void on_stream_message(Base::message_ptr msg) override;
};

} // namespace Omni::Binance
