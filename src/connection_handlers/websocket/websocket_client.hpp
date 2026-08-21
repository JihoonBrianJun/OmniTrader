#pragma once

#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>
#include <fstream>
#include <cstdlib>

#include <quill/Logger.h>
#include <quill/LogMacros.h>

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <openssl/ssl.h>


namespace Omni::Connection {

using WsPlainConfig = websocketpp::config::asio_client;       // ws://  (KIS)
using WsTlsConfig   = websocketpp::config::asio_tls_client;   // wss:// (Binance)

// Generic websocketpp client base. Templated on the websocketpp config so the
// same lifecycle (connect / ping-pong / fail handling) serves both plain `ws`
// (KIS) and TLS `wss` (Binance) endpoints. Subclasses implement the message
// parsing and the status-notification sink.
template<typename Config>
class BaseWebsocketClient : public websocketpp::client<Config> {
    public:
        using ClientType  = websocketpp::client<Config>;
        using message_ptr = typename ClientType::message_ptr;

        static constexpr bool is_tls = std::is_same_v<Config, WsTlsConfig>;

        BaseWebsocketClient(
            const std::string& uri, quill::Logger* logger,
            int ping_interval_sec = 24 * 60 * 60
        )
        :   ClientType(),
            logger_(logger),
            connecting_(false),
            opened_(false),
            awaiting_pong_(false),
            destroying_(false),
            ping_interval_sec_(ping_interval_sec),
            sni_host_(extract_host(uri))
        {
            this->set_access_channels(websocketpp::log::alevel::all);
            this->clear_access_channels(websocketpp::log::alevel::frame_payload);
            this->set_error_channels(websocketpp::log::elevel::all);

            this->init_asio();

            if constexpr (is_tls) {
                this->set_tls_init_handler(
                    [this](websocketpp::connection_hdl) {
                        auto ctx = websocketpp::lib::make_shared<boost::asio::ssl::context>(
                            boost::asio::ssl::context::sslv23
                        );
                        ctx->set_options(
                            boost::asio::ssl::context::default_workarounds |
                            boost::asio::ssl::context::no_sslv2 |
                            boost::asio::ssl::context::no_sslv3 |
                            boost::asio::ssl::context::single_dh_use
                        );
                        load_ca_trust(*ctx);
                        ctx->set_verify_mode(boost::asio::ssl::verify_peer);
                        return ctx;
                    }
                );
                // Server Name Indication is per-connection, so it must be set on
                // the SSL object (not the shared context) before the handshake.
                this->set_socket_init_handler(
                    [this](
                        websocketpp::connection_hdl,
                        boost::asio::ssl::stream<boost::asio::ip::tcp::socket>& socket
                    ) {
                        if (!SSL_set_tlsext_host_name(socket.native_handle(), sni_host_.c_str())) {
                            LOG_WARNING(logger_, "Failed to set TLS SNI host: {}", sni_host_);
                        }
                    }
                );
            }
        }

        virtual ~BaseWebsocketClient() {
            destroying_.store(true);
            this->stop();
            if (client_thread_.joinable()) {
                client_thread_.join();
            } else {
                LOG_WARNING(logger_, "Unable to join the client thread");
            }
            LOG_INFO(logger_, "WebsocketClient object destroyed!");
        }

        websocketpp::connection_hdl get_connection_hdl() const {
            return connection_hdl_;
        }

    protected:
        quill::Logger* logger_;

        websocketpp::connection_hdl connection_hdl_;
        std::thread client_thread_;

        std::atomic<bool> connecting_, opened_, awaiting_pong_, destroying_;
        int ping_interval_sec_;
        std::string sni_host_;
        std::shared_ptr<boost::asio::steady_timer> ping_timer_;

        virtual void notify_websocket_status() = 0;
        virtual void on_stream_message(message_ptr msg) = 0;

        void set_handlers() {
            this->set_open_handler([this](websocketpp::connection_hdl hdl) {
                if (destroying_.load()) return;
                connecting_.store(false);
                opened_.store(true);

                this->get_con_from_hdl(hdl)->ping("");
                awaiting_pong_.store(true);
                send_periodic_ping(hdl);

                notify_websocket_status();
            });

            this->set_close_handler([this](websocketpp::connection_hdl hdl) {
                if (destroying_.load()) return;
                connecting_.store(false);
                opened_.store(false);

                auto conn = this->get_con_from_hdl(hdl);
                LOG_WARNING(
                    logger_,
                    "Connection closed with product: {} ({})",
                    conn->get_remote_close_code(),
                    conn->get_remote_close_reason()
                );

                notify_websocket_status();
            });

            this->set_fail_handler([this](websocketpp::connection_hdl hdl) {
                if (destroying_.load()) return;
                connecting_.store(false);
                opened_.store(false);

                auto conn = this->get_con_from_hdl(hdl);
                LOG_WARNING(
                    logger_,
                    "Connection failed: {} (ws_state={}, http_status={})",
                    conn->get_ec().message(),
                    static_cast<int>(conn->get_state()),
                    static_cast<int>(conn->get_response_code())
                );

                notify_websocket_status();
            });

            // Pongs arrive here and nowhere else. websocketpp routes control frames
            // (ping/pong/close) to process_control_frame, which dispatches a pong to
            // the pong handler; the message handler is reached only for data frames
            // (it is guarded by !is_control(opcode)). Clearing awaiting_pong_ from
            // the message handler therefore never fires, which left the flag stuck
            // true and made send_periodic_ping declare every socket dead one ping
            // interval after it opened.
            this->set_pong_handler([this](
                websocketpp::connection_hdl, std::string
            ) {
                if (destroying_.load()) return;
                awaiting_pong_.store(false);
            });

            this->set_message_handler([this](
                websocketpp::connection_hdl /*hdl*/, message_ptr msg
            ) {
                if (destroying_.load()) return;
                on_stream_message(msg);
            });
        }

        void init_connection(const std::string& uri) {
            connecting_.store(true);
            notify_websocket_status();

            websocketpp::lib::error_code ec;
            auto conn = this->get_connection(uri, ec);
            if (ec) {
                LOG_WARNING(logger_, "Could not create connection_ptr: {}", ec.message());
            }

            try {
                connection_hdl_ = conn->get_handle();
                this->connect(conn);
            } catch (...) {
                LOG_WARNING(logger_, "Unexpected failed to init connection");
                opened_.store(false);
                notify_websocket_status();
                return;
            }

            client_thread_ = std::thread([this]() {
                try {
                    this->run();
                } catch (const std::exception& e) {
                    LOG_WARNING(
                        logger_,
                        "Exception in run thread: {} (opened: {}, connecting: {}, destroying: {})",
                        e.what(), opened_.load(), connecting_.load(), destroying_.load()
                    );
                }
            });
        }

        void send_periodic_ping(websocketpp::connection_hdl hdl) {
            ping_timer_ = std::make_shared<boost::asio::steady_timer>(this->get_io_service());
            ping_timer_->expires_after(std::chrono::seconds(ping_interval_sec_));

            ping_timer_->async_wait([this, hdl](const boost::system::error_code ec) {
                if (ec == boost::asio::error::operation_aborted) {
                    return;
                } else if (!ec) {
                    if (awaiting_pong_.load()) {
                        LOG_WARNING(logger_, "Pong not received in time; closing socket");
                        opened_.store(false);
                        // Close it rather than only marking it down. The owner is
                        // told next and will drop this client, but a socket left
                        // half-open until then keeps a connection slot on the venue
                        // and can still deliver frames into a dying object.
                        try {
                            websocketpp::lib::error_code ec;
                            this->close(hdl, websocketpp::close::status::going_away, "pong timeout", ec);
                        } catch (const std::exception& e) {
                            LOG_WARNING(logger_, "Close after pong timeout failed: {}", e.what());
                        }
                        notify_websocket_status();
                        return;
                    }

                    if (opened_.load()) {
                        this->get_con_from_hdl(hdl)->ping("");
                        awaiting_pong_.store(true);
                    } else {
                        awaiting_pong_.store(false);
                    }

                    send_periodic_ping(hdl);
                } else if (opened_.load()) {
                    LOG_WARNING(logger_, "Ping Timer Error: {}", ec.message());
                }
            });
        }

        // conan's OpenSSL has no usable default CA store on macOS, so seed the
        // verify store from common bundle locations (and SSL_CERT_FILE) in
        // addition to the built-in default paths.
        void load_ca_trust(boost::asio::ssl::context& ctx) {
            boost::system::error_code ec;
            ctx.set_default_verify_paths(ec);

            std::vector<std::string> candidates;
            if (const char* env = std::getenv("SSL_CERT_FILE")) candidates.emplace_back(env);
            candidates.insert(candidates.end(), {
                "/etc/ssl/cert.pem",                          // macOS / LibreSSL
                "/opt/homebrew/etc/openssl@3/cert.pem",       // Homebrew (arm64)
                "/opt/homebrew/etc/ca-certificates/cert.pem",
                "/usr/local/etc/openssl@3/cert.pem",          // Homebrew (x86_64)
                "/etc/ssl/certs/ca-certificates.crt",         // Debian/Ubuntu
                "/etc/pki/tls/certs/ca-bundle.crt"            // RHEL/Fedora
            });

            for (const auto& path : candidates) {
                std::ifstream f(path);
                if (!f.good()) continue;
                boost::system::error_code load_ec;
                ctx.load_verify_file(path, load_ec);
                if (!load_ec) {
                    LOG_INFO(logger_, "Loaded CA trust bundle: {}", path);
                    return;
                }
            }
            LOG_WARNING(logger_, "No CA trust bundle found; TLS verification may fail");
        }

        static std::string extract_host(const std::string& uri) {
            auto scheme_pos = uri.find("://");
            size_t start = (scheme_pos == std::string::npos) ? 0 : scheme_pos + 3;
            size_t end = uri.find_first_of(":/", start);
            return uri.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
        }
};

} // namespace Omni::Connection
