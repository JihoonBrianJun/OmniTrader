#pragma once
#include <functional>

#include <memory>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <variant>

#include <quill/Logger.h>
#include <moodycamel/blockingconcurrentqueue.h>
#include <boost/asio.hpp>

#include "common/market_msg_types.hpp"


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
        void subscribe_to_product(const std::string& product, Category category);
        void unsubscribe_from_product(const std::string& product, Category category);

        boost::asio::ip::tcp::socket socket_;
        quill::Logger* logger_;
        TcpServer* server_;

        std::array<char, 1024> read_buffer_;
        std::string read_message_;

        moodycamel::BlockingConcurrentQueue<std::string> write_queue_;
        // The in-flight write's bytes. async_write only refers to the buffer it is
        // given, so it has to outlive the call; held here and only ever touched on
        // the socket's executor.
        std::string write_buffer_;
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

// Updates the server's retained last-value product info for a product. Applied on
// the worker thread (like every other task) so product_infos_ needs no lock.
struct ProductInfoUpdate {
    std::string product;
    ProductInfoData product_info_data;
};

using ServerTask = std::variant<
    SessionCommand, SubscriptionCommand, BroadcastMessage, ProductInfoUpdate
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
        // Update the retained last-value product info for a product (enqueued and
        // applied on the worker thread) so a session that subscribes later receives
        // it (serialized on subscribe). Live updates to current subscribers go via
        // broadcast_to_subscribers like any feed.
        void set_product_info(const std::string& product, const ProductInfoData& data);

        // Called on the broadcast worker thread each time a session subscribes to a
        // product. The owner uses it to publish state that is snapshot-based rather
        // than streamed (positions, resting orders), which a subscriber joining
        // mid-session would otherwise never see. Must not block: it runs in front of
        // every pending broadcast. Set before start().
        using SubscribeHook = std::function<void(const std::string& product)>;
        void set_subscribe_hook(SubscribeHook hook) { subscribe_hook_ = std::move(hook); }

        void add_session(std::shared_ptr<TcpSession> session);
        void remove_session(std::shared_ptr<TcpSession> session);
        void notify_subscribe(std::shared_ptr<TcpSession> session, const std::string& product);
        void notify_unsubscribe(std::shared_ptr<TcpSession> session, const std::string& product);

    private:
        void do_accept();
        void broadcast_worker();

        SubscribeHook subscribe_hook_;

        boost::asio::io_context& io_context_;
        boost::asio::ip::tcp::acceptor acceptor_;

        quill::Logger* logger_;

        std::unordered_map<std::shared_ptr<TcpSession>, std::unordered_set<std::string>> session_to_products_;
        std::unordered_map<std::string, std::unordered_set<std::shared_ptr<TcpSession>>> product_to_sessions_;
        std::map<std::string, ProductInfoData> product_infos_;

        moodycamel::BlockingConcurrentQueue<ServerTask> task_queue_;
        std::thread task_thread_;
        std::atomic<bool> running_;
};

} // namespace Omni::Connection
