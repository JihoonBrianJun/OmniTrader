#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <string_view>
#include <thread>

#include <quill/Logger.h>
#include <boost/asio.hpp>

#include "common/market_msg_types.hpp"


namespace Omni::Connection {

// Feed-agnostic client for an internal feed server (the listener's broadcast server,
// or the pricer's fair-price server).
//
// Everything the two links have in common lives here: dialling, the reconnect timer,
// line framing, the write queue, and the subscribe handshake -- which is the same
// protocol on both. A subclass supplies only what differs: how to read the feeds its
// own link carries, and what to put on its consumer's queue.
//
// Each client owns its io_context and the thread running it, so the links are
// independent -- a burst of orderbook messages on one cannot delay reads on another,
// and reconnection is driven here rather than by the consumer's decision loop.
//
// Threading: every socket operation runs on this client's io thread. The public
// methods are safe to call from any thread and post their work onto it; the protected
// hooks are always invoked on it.
class TcpClientBase {
    public:
        TcpClientBase(
            quill::Logger* logger,
            std::string name,               // "listener" / "pricer", for log lines
            std::string address,
            unsigned short port,
            long reconnect_interval_ms = 5000
        );
        virtual ~TcpClientBase();

        TcpClientBase(const TcpClientBase&) = delete;
        TcpClientBase& operator=(const TcpClientBase&) = delete;

        // Starts the io thread and dials. Retried every reconnect_interval_ms for as
        // long as the peer is away, so any process can be restarted independently and
        // the others pick it back up.
        void start();
        // Tears the link down and joins the io thread. Idempotent.
        //
        // A subclass must call this from its own destructor: the hooks below are
        // virtual, and by the time ~TcpClientBase runs the subclass is already gone.
        // The io thread is guarded against dispatching them once stop() has begun, so
        // forgetting is not fatal, but stopping first is the contract.
        void stop();

        void subscribe(const std::string& product, Category category);
        void unsubscribe(const std::string& product, Category category);

        bool connected() const { return connected_.load(std::memory_order_relaxed); }

    protected:
        // Hooks, all invoked on this client's io thread.

        // One received line whose feed is not part of the shared subscribe handshake.
        virtual void on_data_line(std::string_view feed, const std::string& line) = 0;
        virtual void on_status(bool connecting, bool connected) = 0;
        virtual void on_subscribe_response(
            bool subscribe, bool success, const std::string& product
        ) = 0;

        quill::Logger* logger_;

    private:
        void do_connect();
        void do_read();
        void do_write();
        void arm_reconnect_timer();
        void close_socket();
        void handle_line(const std::string& line);
        void send_subscribe_request(const std::string& product, Category category, bool subscribe);
        void enqueue_write(std::string message);

        std::string name_;
        std::string address_;
        unsigned short port_;
        std::chrono::milliseconds reconnect_interval_;

        boost::asio::io_context io_context_;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
        boost::asio::ip::tcp::socket socket_;
        boost::asio::steady_timer reconnect_timer_;
        std::thread io_thread_;

        std::array<char, 8192> read_buffer_;
        std::string read_message_;

        // io thread only, so neither needs synchronizing.
        std::deque<std::string> write_queue_;
        bool writing_ = false;

        // Read by the consumer thread via connected().
        std::atomic<bool> connected_{false};
        std::atomic<bool> running_{false};
};

} // namespace Omni::Connection
