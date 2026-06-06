#pragma once
#include <string>
#include <vector>
#include <memory>

#include <moodycamel/blockingconcurrentqueue.h>

#include "connection_handlers/websocket/websocket_client.hpp"
#include "market_listener/exchange/kis/code_manager_base.hpp"
#include "market_listener/exchange/kis/kis_listener_dtypes.hpp"


namespace Omni::Listener::KIS {

// KIS market socket: plain ws, pipe-delimited text frames, AES-CBC encrypted
// execution frames. Parses inbound frames into the WsResponse queue.
class KisWebsocketClient : public Omni::Connection::BaseWebsocketClient<Omni::Connection::WsPlainConfig> {
    public:
        using Base = Omni::Connection::BaseWebsocketClient<Omni::Connection::WsPlainConfig>;

        KisWebsocketClient(
            const std::string& uri, quill::Logger* logger,
            moodycamel::BlockingConcurrentQueue<WsResponse>* queue,
            std::shared_ptr<Omni::KIS::CodeManager::ICodeManager> code_manager
        );

    private:
        moodycamel::BlockingConcurrentQueue<WsResponse>* queue_;
        std::shared_ptr<Omni::KIS::CodeManager::ICodeManager> code_manager_;

        std::string ws_iv_, ws_key_;

        void notify_websocket_status() override;
        void on_stream_message(Base::message_ptr msg) override;

        std::string base64_decode(const std::string& cipher_b64);
        std::string aes_cbc_decrypt(const std::string& cipher_b64);
        void split_msg(
            const std::string_view& msg, char sep, std::vector<std::string>& msg_segments
        );
};

} // namespace Omni::Listener::KIS
