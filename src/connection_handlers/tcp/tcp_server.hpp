#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <variant>

#include <quill/Logger.h>
#include <moodycamel/blockingconcurrentqueue.h>
#include <boost/asio.hpp>


namespace Omni::Connection {

class TcpSession : public std::enable_shared_from_this<TcpSession> {
    public:
        TcpSession(
            boost::asio::ip::tcp::socket socket,
            quill::Logger* logger,
            class TcpServer* server
        );

        void start();
        void send_message(const std::string& message);
        void close();

        boost::asio::ip::tcp::socket& socket() { return socket_; }

    private:
        void do_read();
        void do_write();
        void handle_message(const std::string& message);
        void subscribe_to_product(const std::string& product);
        void unsubscribe_from_product(const std::string& product);

        boost::asio::ip::tcp::socket socket_;
        quill::Logger* logger_;
        TcpServer* server_;

        std::array<char, 1024> read_buffer_;
        std::string read_message_;

        moodycamel::BlockingConcurrentQueue<std::string> write_queue_;
        std::atomic<bool> writing_, active_;
};

struct SessionCommand {
    enum Type {
        ADD_SESSION,
        REMOVE_SESSION
    } type;
    std::shared_ptr<TcpSession> session;
};

struct SubscriptionCommand {
    enum Type {
        SUBSCRIBE_PRODUCT,
        UNSUBSCRIBE_PRODUCT
    } type;
    std::shared_ptr<TcpSession> session;
    std::string product;
};

struct BroadcastMessage {
    std::string product;
    std::string json_data;
};

// A retained (last-value) message per product, replayed to each new subscriber
// (used for product info so late-joining traders learn tick/lot immediately).
struct RetainMessage {
    std::string product;
    std::string json_data;
};

using ServerTask = std::variant<
    SessionCommand, SubscriptionCommand, BroadcastMessage, RetainMessage
>;

class TcpServer {
    public:
        TcpServer(
            boost::asio::io_context& io_context,
            quill::Logger* logger,
            const std::string& address,
            unsigned short port
        );
        ~TcpServer();

        void start();
        void stop();
        void broadcast_to_subscribers(
            const std::string& product, const std::string& json_data
        );
        // Set the retained message for a product (replayed to new subscribers).
        void set_retained(const std::string& product, const std::string& json_data);

        void add_session(std::shared_ptr<TcpSession> session);
        void remove_session(std::shared_ptr<TcpSession> session);
        void notify_subscribe(std::shared_ptr<TcpSession> session, const std::string& product);
        void notify_unsubscribe(std::shared_ptr<TcpSession> session, const std::string& product);

    private:
        void do_accept();
        void broadcast_worker();

        boost::asio::io_context& io_context_;
        boost::asio::ip::tcp::acceptor acceptor_;

        quill::Logger* logger_;

        std::unordered_map<std::shared_ptr<TcpSession>, std::unordered_set<std::string>> session_to_products_;
        std::unordered_map<std::string, std::unordered_set<std::shared_ptr<TcpSession>>> product_to_sessions_;
        std::unordered_map<std::string, std::string> retained_;

        moodycamel::BlockingConcurrentQueue<ServerTask> task_queue_;
        std::thread task_thread_;
        std::atomic<bool> running_;
};

} // namespace Omni::Connection
