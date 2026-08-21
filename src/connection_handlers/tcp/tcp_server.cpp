#include <iostream>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <glaze/glaze.hpp>
#include <quill/LogMacros.h>

#include "common/market_msg_types.hpp"
#include "tcp_server.hpp"

namespace Omni::Connection {

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;


TcpSession::TcpSession(
    boost::asio::ip::tcp::socket socket, quill::Logger* logger, TcpServer* server
)
:   socket_(std::move(socket)),
    logger_(logger),
    server_(server),
    writing_(false),
    active_(true)
{
}


void TcpSession::start() {
    server_->add_session(shared_from_this());
    do_read();
}


void TcpSession::send_message(
    const std::string& message
) {
    if (!active_.load()) return;

    write_queue_.enqueue(message);

    // send_message is called from multiple threads (the io_context thread for the
    // subscribe ack, and the broadcast_worker thread for broadcasts/retained
    // replays). Drive do_write strictly on the socket's executor so all writes
    // (and the writing_ flag) are serialized on one thread; otherwise the ack and
    // a retained replay can race and corrupt/drop a message on the same socket.
    auto self = shared_from_this();
    boost::asio::post(socket_.get_executor(), [this, self]() {
        bool expected = false;
        if (writing_.compare_exchange_strong(expected, true)) {
            do_write();
        }
    });
}


void TcpSession::close() {
    active_.store(false);
    boost::system::error_code ec;
    socket_.close(ec);
    server_->remove_session(shared_from_this());
}


void TcpSession::do_read() {
    auto self(shared_from_this());
    socket_.async_read_some(
        boost::asio::buffer(read_buffer_),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec && active_.load()) {
                read_message_.append(read_buffer_.data(), length);

                size_t pos = 0;
                while ((pos = read_message_.find('\n')) != std::string::npos) {
                    std::string line = read_message_.substr(0, pos);
                    read_message_.erase(0, pos + 1);

                    if (!line.empty()) {
                        handle_message(line);
                    }
                }

                do_read();
            } else {
                close();
            }
        }
    );
}


void TcpSession::do_write() {
    if (!active_.load()) {
        writing_.store(false);
        return;
    }

    if (!write_queue_.try_dequeue(write_buffer_)) {
        writing_.store(false);
        return;
    }
    write_buffer_ += '\n';

    // Write out of write_buffer_, a member, rather than a temporary: async_write
    // returns immediately and only refers to the buffer while the operation is in
    // flight, so a `message + "\n"` temporary would be destroyed before the bytes
    // were ever sent. Only ever touched on the socket's executor (do_write is
    // reached from the post in send_message and from this completion handler,
    // both of which run there), so it needs no synchronization.
    auto self(shared_from_this());
    boost::asio::async_write(
        socket_,
        boost::asio::buffer(write_buffer_),
        [this, self](boost::system::error_code ec, std::size_t /*length*/) {
            if (!ec && active_.load()) {
                // Chain straight into the next write. The previous version
                // dequeued here to *test* for a next message and then called
                // do_write(), which dequeued again -- so every second message on
                // a session was silently discarded. A subscriber that was sent
                // both a subscribe ack and the retained product_info received
                // only one of the two, at random.
                do_write();
            } else {
                writing_.store(false);
                close();
            }
        }
    );
}


void TcpSession::handle_message(const std::string& message) {
    try {
        SubscribeRequestMsg subscribe_request;
        auto ec = glz::read_json(subscribe_request, message);
        if (ec) {
            LOG_WARNING(logger_, "Failed to parse subscribe request ({})", message);
            return;
        }

        if (subscribe_request.subscribe) {
            subscribe_to_product(subscribe_request.product, subscribe_request.category);
        } else {
            unsubscribe_from_product(subscribe_request.product, subscribe_request.category);
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Error parsing client message: {}", e.what());
    }
}


void TcpSession::subscribe_to_product(const std::string& product, Category category) {
    server_->notify_subscribe(shared_from_this(), product);

    auto subscribe_response = SubscribeResponseMsg{
        .subscribe = true, .success = true, .product = product, .category = category
    };

    std::string json_buffer;
    auto ec = glz::write_json(subscribe_response, json_buffer);
    if (ec) {
        LOG_WARNING(logger_, "Failed write_json for the subscribe response of product {}", product);
    } else {
        send_message(json_buffer);
    }
}


void TcpSession::unsubscribe_from_product(const std::string& product, Category category) {
    server_->notify_unsubscribe(shared_from_this(), product);

    auto unsubscribe_response = SubscribeResponseMsg{
        .subscribe = false, .success = true, .product = product, .category = category
    };

    std::string json_buffer;
    auto ec = glz::write_json(unsubscribe_response, json_buffer);
    if (ec) {
        LOG_WARNING(logger_, "Failed write_json for the unsubscribe response of product {}", product);
    } else {
        send_message(json_buffer);
    }
}


TcpServer::TcpServer(
    boost::asio::io_context& io_context,
    quill::Logger* logger,
    const std::string& address,
    unsigned short port
)
:   io_context_(io_context),
    acceptor_(
        io_context,
        boost::asio::ip::tcp::endpoint(
            boost::asio::ip::address::from_string(address), port
        )
    ),
    logger_(logger),
    running_(false)
{
}


TcpServer::~TcpServer() {
    stop();
}


void TcpServer::start() {
    running_.store(true);

    task_thread_ = std::thread([this]() {
        broadcast_worker();
    });

    do_accept();
}


void TcpServer::stop() {
    // Idempotent: the destructor calls stop() too, and so may the owner.
    if (!running_.exchange(false)) return;

    boost::system::error_code ec;
    acceptor_.close(ec);

    // Join the worker *before* touching the session maps. They belong to the
    // worker thread alone -- every mutation of them goes through a task, which is
    // why they carry no lock -- and stop() runs on another thread. The previous
    // order walked and cleared them while the worker could still be inside a task
    // mutating the same maps, which corrupted them and surfaced as a bad_weak_ptr
    // out of TcpSession::close(). Once the worker is joined this is single
    // threaded and safe.
    if (task_thread_.joinable()) {
        task_thread_.join();
    }

    for (auto& [session, products] : session_to_products_) {
        session->close();
    }
    session_to_products_.clear();
    product_to_sessions_.clear();
}


void TcpServer::broadcast_to_subscribers(
    const std::string& product, const std::string& json_data
) {
    if (!running_.load()) return;
    task_queue_.enqueue(BroadcastMessage{product, json_data});
}


void TcpServer::set_product_info(
    const std::string& product, const ProductInfoData& data
) {
    if (!running_.load()) return;
    task_queue_.enqueue(ProductInfoUpdate{product, data});
}


void TcpServer::add_session(std::shared_ptr<TcpSession> session) {
    task_queue_.enqueue(SessionCommand{
        SessionCommand::Type::ADD_SESSION, session
    });
}


void TcpServer::remove_session(std::shared_ptr<TcpSession> session) {
    task_queue_.enqueue(SessionCommand{
        SessionCommand::Type::REMOVE_SESSION, session
    });
}


void TcpServer::notify_subscribe(std::shared_ptr<TcpSession> session, const std::string& product) {
    task_queue_.enqueue(SubscriptionCommand{
        SubscriptionCommand::Type::SUBSCRIBE_PRODUCT, session, product
    });
}


void TcpServer::notify_unsubscribe(std::shared_ptr<TcpSession> session, const std::string& product) {
    task_queue_.enqueue(SubscriptionCommand{
        SubscriptionCommand::Type::UNSUBSCRIBE_PRODUCT, session, product
    });
}


void TcpServer::do_accept() {
    auto new_session = std::make_shared<TcpSession>(
        boost::asio::ip::tcp::socket(io_context_), logger_, this);

    acceptor_.async_accept(
        new_session->socket(),
        [this, new_session](boost::system::error_code ec) {
            if (!ec) {
                new_session->start();
            }

            if (running_.load()) {
                do_accept();
            }
        }
    );
}


void TcpServer::broadcast_worker() {
    while (running_.load()) {
        ServerTask task;
        // Timed, so shutdown does not depend on another task happening to arrive
        // to wake this thread up: stop() clears running_ and then joins.
        if (!task_queue_.wait_dequeue_timed(task, std::chrono::milliseconds(100))) {
            continue;
        }

        std::visit(overloaded{
            [&](const SessionCommand& cmd) {
                switch (cmd.type) {
                    case SessionCommand::Type::ADD_SESSION: {
                        session_to_products_[cmd.session] = std::unordered_set<std::string>();
                        break;
                    }
                    case SessionCommand::Type::REMOVE_SESSION: {
                        auto it = session_to_products_.find(cmd.session);
                        if (it != session_to_products_.end()) {
                            for (const auto& product : it->second) {
                                auto product_it = product_to_sessions_.find(product);
                                if (product_it != product_to_sessions_.end()) {
                                    product_it->second.erase(cmd.session);
                                    if (product_it->second.empty()) {
                                        product_to_sessions_.erase(product_it);
                                    }
                                }
                            }
                            session_to_products_.erase(it);
                        }
                        break;
                    }
                }
            },
            [&](const SubscriptionCommand& cmd) {
                switch (cmd.type) {
                    case SubscriptionCommand::Type::SUBSCRIBE_PRODUCT: {
                        session_to_products_[cmd.session].insert(cmd.product);
                        product_to_sessions_[cmd.product].insert(cmd.session);
                        // Serialize and send the retained product info (if any)
                        auto info_it = product_infos_.find(cmd.product);
                        if (info_it != product_infos_.end()) {
                            auto product_info_msg = ProductInfoMsg{
                                .product = cmd.product,
                                .product_info_data = info_it->second
                            };
                            std::string json_buffer;
                            if (!glz::write_json(product_info_msg, json_buffer)) {
                                cmd.session->send_message(json_buffer);
                            }
                        }
                        // Snapshot-based state (positions, resting orders) has no
                        // retained value here -- it is the adapter's to fetch -- so
                        // the owner is told a subscriber arrived and republishes it.
                        if (subscribe_hook_) subscribe_hook_(cmd.product);
                        break;
                    }
                    case SubscriptionCommand::Type::UNSUBSCRIBE_PRODUCT: {
                        auto session_it = session_to_products_.find(cmd.session);
                        if (session_it != session_to_products_.end()) {
                            session_it->second.erase(cmd.product);
                        }

                        auto product_it = product_to_sessions_.find(cmd.product);
                        if (product_it != product_to_sessions_.end()) {
                            product_it->second.erase(cmd.session);
                            if (product_it->second.empty()) {
                                product_to_sessions_.erase(product_it);
                            }
                        }
                        break;
                    }
                }
            },
            [&](const BroadcastMessage& msg) {
                auto it = product_to_sessions_.find(msg.product);
                if (it != product_to_sessions_.end()) {
                    for (auto& session : it->second) {
                        session->send_message(msg.json_data);
                    }
                }
            },
            [&](const ProductInfoUpdate& msg) {
                product_infos_[msg.product] = msg.product_info_data;
            }
        }, task);
    }
}

} // namespace Omni::Connection
