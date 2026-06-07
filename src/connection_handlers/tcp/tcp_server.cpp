#include <iostream>
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

    std::string message;
    if (write_queue_.try_dequeue(message)) {
        auto self(shared_from_this());
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(message + "\n"),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec && active_.load()) {
                    std::string next_message;
                    if (write_queue_.try_dequeue(next_message)) {
                        do_write();
                    } else {
                        writing_.store(false);
                    }
                } else {
                    writing_.store(false);
                    close();
                }
            }
        );
    } else {
        writing_.store(false);
    }
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
            subscribe_to_product(subscribe_request.product);
        } else {
            unsubscribe_from_product(subscribe_request.product);
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Error parsing client message: {}", e.what());
    }
}


void TcpSession::subscribe_to_product(const std::string& product) {
    server_->notify_subscribe(shared_from_this(), product);

    auto subscribe_response = SubscribeResponseMsg{
        .subscribe = true, .success = true, .product = product
    };

    std::string json_buffer;
    auto ec = glz::write_json(subscribe_response, json_buffer);
    if (ec) {
        LOG_WARNING(logger_, "Failed write_json for the subscribe response of product {}", product);
    } else {
        send_message(json_buffer);
    }
}


void TcpSession::unsubscribe_from_product(const std::string& product) {
    server_->notify_unsubscribe(shared_from_this(), product);

    auto unsubscribe_response = SubscribeResponseMsg{
        .subscribe = false, .success = true, .product = product
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
    running_.store(false);

    boost::system::error_code ec;
    acceptor_.close(ec);

    for (auto& [session, products] : session_to_products_) {
        session->close();
    }
    session_to_products_.clear();
    product_to_sessions_.clear();

    if (task_thread_.joinable()) {
        task_thread_.join();
    }
}


void TcpServer::broadcast_to_subscribers(
    const std::string& product, const std::string& json_data
) {
    if (!running_.load()) return;
    task_queue_.enqueue(BroadcastMessage{product, json_data});
}


void TcpServer::set_retained(
    const std::string& product, const std::string& json_data
) {
    if (!running_.load()) return;
    task_queue_.enqueue(RetainMessage{product, json_data});
}


void TcpServer::broadcast_to_all(
    const std::string& key, const std::string& json_data
) {
    if (!running_.load()) return;
    task_queue_.enqueue(BroadcastAllMessage{key, json_data});
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
        task_queue_.wait_dequeue(task);

        std::visit(overloaded{
            [&](const SessionCommand& cmd) {
                switch (cmd.type) {
                    case SessionCommand::Type::ADD_SESSION: {
                        session_to_products_[cmd.session] = std::unordered_set<std::string>();
                        // Replay account-level retained messages (e.g. balances) so
                        // a new session has them immediately, before any subscribe.
                        for (const auto& [key, json_data] : account_retained_) {
                            cmd.session->send_message(json_data);
                        }
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
                        // Replay the retained message (e.g. product info) so a
                        // late-joining subscriber gets it immediately.
                        auto retained_it = retained_.find(cmd.product);
                        LOG_INFO(
                            logger_, "[Retain] SUBSCRIBE {} retained_size={} found={}",
                            cmd.product, retained_.size(), retained_it != retained_.end()
                        );
                        if (retained_it != retained_.end()) {
                            cmd.session->send_message(retained_it->second);
                        }
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
            [&](const RetainMessage& msg) {
                retained_[msg.product] = msg.json_data;
                LOG_INFO(
                    logger_, "[Retain] STORE {} retained_size={}",
                    msg.product, retained_.size()
                );
            },
            [&](const BroadcastAllMessage& msg) {
                account_retained_[msg.key] = msg.json_data;
                for (auto& [session, products] : session_to_products_) {
                    session->send_message(msg.json_data);
                }
            }
        }, task);
    }
}

} // namespace Omni::Connection
