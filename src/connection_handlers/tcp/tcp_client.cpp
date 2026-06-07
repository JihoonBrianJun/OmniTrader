#include <iostream>
#include <glaze/glaze.hpp>
#include <quill/LogMacros.h>

#include "common/market_msg_types.hpp"
#include "tcp_client.hpp"


namespace Omni::Connection {

TcpClient::TcpClient(
    boost::asio::io_context& io_context,
    quill::Logger* logger,
    moodycamel::BlockingConcurrentQueue<Omni::Trader::Task>* task_queue
)
:   io_context_(io_context),
    socket_(io_context),
    logger_(logger),
    task_queue_(task_queue),
    writing_(false),
    connecting_(false),
    connected_(false)
{
}


TcpClient::~TcpClient() {
    disconnect();
}


bool TcpClient::connect(const std::string& address, unsigned short port) {
    if (connected_.load()) {
        return true;
    }

    connecting_.store(true);
    notify_tcp_status();

    try {
        boost::asio::ip::tcp::endpoint endpoint(
            boost::asio::ip::address::from_string(address), port
        );

        boost::system::error_code ec;
        socket_.connect(endpoint, ec);

        if (!ec) {
            connecting_.store(false);
            connected_.store(true);
            notify_tcp_status();

            do_read();
            LOG_INFO(logger_, "Connected to server {}:{}", address, port);
            return true;
        } else {
            handle_error("Failed to connect: " + ec.message());
            return false;
        }
    } catch (const std::exception& e) {
        handle_error("Connection error: " + std::string(e.what()));
        return false;
    }
}


void TcpClient::disconnect() {
    if (!connected_.load()) {
        return;
    }

    connecting_.store(false);
    connected_.store(false);

    boost::system::error_code ec;
    socket_.close(ec);

    LOG_INFO(logger_, "Disconnected from server");
}


void TcpClient::subscribe(const std::string& product) {
    if (!connected_.load()) {
        handle_error("Cannot subscribe: not connected to server");
        return;
    }

    auto subscribe_msg = SubscribeRequestMsg{
        .subscribe = true, .product = product
    };

    std::string json_buffer;
    auto ec = glz::write_json(subscribe_msg, json_buffer);
    if (ec) {
        LOG_WARNING(logger_, "Failed write_json for the subscribe msg of product {}", product);
    } else {
        send_message(json_buffer);
    }
}


void TcpClient::unsubscribe(const std::string& product) {
    if (!connected_.load()) {
        handle_error("Cannot unsubscribe: not connected to server");
        return;
    }

    auto unsubscribe_msg = SubscribeRequestMsg{
        .subscribe = false, .product = product
    };

    std::string json_buffer;
    auto ec = glz::write_json(unsubscribe_msg, json_buffer);
    if (ec) {
        LOG_WARNING(logger_, "Failed write_json for the unsubscribe msg of product {}", product);
    } else {
        send_message(json_buffer);
    }
}


void TcpClient::send_message(const std::string& message) {
    if (!connected_.load()) {
        handle_error("Cannot send message: not connected to server");
        return;
    }

    write_queue_.enqueue(message);

    // Serialize writes on the socket's executor: send_message runs on the caller
    // thread (e.g. the order handler) while do_write continuations run on the io
    // thread, so driving do_write directly here would race on the same socket.
    boost::asio::post(socket_.get_executor(), [this]() {
        bool expected = false;
        if (writing_.compare_exchange_strong(expected, true)) {
            do_write();
        }
    });
}


void TcpClient::do_read() {
    if (!connected_.load()) {
        return;
    }

    socket_.async_read_some(
        boost::asio::buffer(read_buffer_),
        [this](boost::system::error_code ec, std::size_t length) {
            if (!ec && connected_.load()) {
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
                if (connected_.load()) {
                    handle_error("Read error: " + ec.message());
                    disconnect();
                }
            }
        }
    );
}


void TcpClient::do_write() {
    if (!connected_.load()) {
        writing_.store(false);
        return;
    }

    std::string message;
    if (write_queue_.try_dequeue(message)) {
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(message + "\n"),
            [this](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec && connected_.load()) {
                    std::string next_message;
                    if (write_queue_.try_dequeue(next_message)) {
                        do_write();
                    } else {
                        writing_.store(false);
                    }
                } else {
                    writing_.store(false);
                    if (connected_.load()) {
                        handle_error("Write error: " + ec.message());
                        disconnect();
                    }
                }
            }
        );
    } else {
        writing_.store(false);
    }
}


void TcpClient::notify_tcp_status() {
    task_queue_->enqueue(Omni::Trader::TcpStatusUpdate{
        .connecting = connecting_.load(),
        .connected = connected_.load()
    });
}


void TcpClient::handle_message(const std::string& message) {
    try {
        FeedClassifier feed_classifier;
        auto ec = glz::read<glz::opts{.format = glz::JSON, .partial_read = true}>(
            feed_classifier, message
        );
        if (ec) {
            LOG_WARNING(logger_, "Failed to get feed from the message ({})", message);
            return;
        }

        if (feed_classifier.feed == "subscribe") {
            SubscribeResponseMsg subscribe_response;
            auto ec = glz::read_json(subscribe_response, message);
            if (ec) {
                LOG_WARNING(logger_, "Failed to parse subscribe response ({})", message);
                return;
            }
            task_queue_->enqueue(Omni::Trader::TcpSubscribeUpdate{
                .subscribe = subscribe_response.subscribe,
                .success = subscribe_response.success,
                .product = subscribe_response.product
            });
        } else if (feed_classifier.feed == "orderbook") {
            OrderbookMsg orderbook_msg;
            auto ec = glz::read_json(orderbook_msg, message);
            if (ec) {
                LOG_WARNING(logger_, "Failed to parse orderbook msg ({})", message);
                return;
            }
            task_queue_->enqueue(Omni::Trader::TcpMarketDataResponse{
                .feed = Omni::Trader::TcpMarketDataResponse::Orderbook,
                .product = orderbook_msg.product,
                .data = orderbook_msg.orderbook_data
            });
        } else if (feed_classifier.feed == "trade") {
            TradeMsg trade_msg;
            auto ec = glz::read_json(trade_msg, message);
            if (ec) {
                LOG_WARNING(logger_, "Failed to parse trade msg ({})", message);
                return;
            }
            task_queue_->enqueue(Omni::Trader::TcpMarketDataResponse{
                .feed = Omni::Trader::TcpMarketDataResponse::Trade,
                .product = trade_msg.product,
                .data = trade_msg.trade_data
            });
        } else if (feed_classifier.feed == "execution") {
            ExecutionMsg execution_msg;
            auto ec = glz::read_json(execution_msg, message);
            if (ec) {
                LOG_WARNING(logger_, "Failed to parse execution msg ({})", message);
                return;
            }
            task_queue_->enqueue(Omni::Trader::TcpMarketDataResponse{
                .feed = Omni::Trader::TcpMarketDataResponse::Execution,
                .product = execution_msg.product,
                .data = execution_msg.execution_data
            });
        } else if (feed_classifier.feed == "position") {
            PositionMsg position_msg;
            auto ec = glz::read_json(position_msg, message);
            if (ec) {
                LOG_WARNING(logger_, "Failed to parse position msg ({})", message);
                return;
            }
            task_queue_->enqueue(Omni::Trader::TcpMarketDataResponse{
                .feed = Omni::Trader::TcpMarketDataResponse::Position,
                .product = position_msg.product,
                .data = position_msg.position_data
            });
        } else if (feed_classifier.feed == "balance") {
            BalanceMsg balance_msg;
            auto ec = glz::read_json(balance_msg, message);
            if (ec) {
                LOG_WARNING(logger_, "Failed to parse balance msg ({})", message);
                return;
            }
            task_queue_->enqueue(Omni::Trader::TcpMarketDataResponse{
                .feed = Omni::Trader::TcpMarketDataResponse::Balance,
                .product = balance_msg.asset,
                .data = balance_msg.balance_data
            });
        } else if (feed_classifier.feed == "product_info") {
            ProductInfoMsg product_info_msg;
            auto ec = glz::read_json(product_info_msg, message);
            if (ec) {
                LOG_WARNING(logger_, "Failed to parse product_info msg ({})", message);
                return;
            }
            task_queue_->enqueue(Omni::Trader::TcpMarketDataResponse{
                .feed = Omni::Trader::TcpMarketDataResponse::ProductInfo,
                .product = product_info_msg.product,
                .data = product_info_msg.product_info_data
            });
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Error parsing server message: {}", e.what());
    }
}


void TcpClient::handle_error(const std::string& error_msg) {
    LOG_ERROR(logger_, "{}", error_msg);
}

} // namespace Omni::Connection
