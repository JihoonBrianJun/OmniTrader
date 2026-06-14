#pragma once

#include <memory>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <array>

#include <quill/Logger.h>
#include <moodycamel/blockingconcurrentqueue.h>
#include <boost/asio.hpp>

#include "trader/trader_dtypes.hpp"


namespace Omni::Connection {

class TcpClient {
    public:
        TcpClient(
            boost::asio::io_context& io_context,
            quill::Logger* logger,
            moodycamel::BlockingConcurrentQueue<Omni::Trader::Task>* task_queue
        );
        ~TcpClient();

        bool connect(const std::string& address, unsigned short port);
        void disconnect();

        void subscribe(const std::string& product, Category category);
        void unsubscribe(const std::string& product, Category category);
        void send_message(const std::string& message);

    private:
        void do_read();
        void do_write();

        void notify_tcp_status();
        void handle_message(const std::string& message);
        void handle_error(const std::string& error_msg);

        boost::asio::io_context& io_context_;
        boost::asio::ip::tcp::socket socket_;

        quill::Logger* logger_;
        moodycamel::BlockingConcurrentQueue<Omni::Trader::Task>* task_queue_;

        std::array<char, 1024> read_buffer_;
        std::string read_message_;

        moodycamel::BlockingConcurrentQueue<std::string> write_queue_;
        std::atomic<bool> writing_, connecting_, connected_;
};

} // namespace Omni::Connection
