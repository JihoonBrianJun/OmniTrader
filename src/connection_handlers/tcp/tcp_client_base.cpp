#include <utility>

#include <glaze/glaze.hpp>
#include <quill/LogMacros.h>

#include "connection_handlers/tcp/tcp_client_base.hpp"


namespace Omni::Connection {

TcpClientBase::TcpClientBase(
    quill::Logger* logger,
    std::string name,
    std::string address,
    unsigned short port,
    long reconnect_interval_ms
)
:   logger_(logger),
    name_(std::move(name)),
    address_(std::move(address)),
    port_(port),
    reconnect_interval_(reconnect_interval_ms),
    work_guard_(boost::asio::make_work_guard(io_context_)),
    socket_(io_context_),
    reconnect_timer_(io_context_)
{
}


TcpClientBase::~TcpClientBase() {
    stop();
}


void TcpClientBase::start() {
    if (running_.exchange(true)) return;

    boost::asio::post(io_context_, [this]() { do_connect(); });

    io_thread_ = std::thread([this]() {
        try {
            io_context_.run();
        } catch (const std::exception& e) {
            LOG_WARNING(logger_, "Exception within {} client thread: {}", name_, e.what());
        }
    });
}


void TcpClientBase::stop() {
    if (!running_.exchange(false)) return;

    // Tear down on the io thread rather than here: the socket belongs to that thread,
    // and dropping the work guard from inside lets run() return once this has run.
    boost::asio::post(io_context_, [this]() {
        reconnect_timer_.cancel();
        close_socket();
        work_guard_.reset();
    });

    if (io_thread_.joinable()) io_thread_.join();
}


void TcpClientBase::do_connect() {
    if (!running_.load()) return;

    boost::system::error_code addr_ec;
    auto addr = boost::asio::ip::make_address(address_, addr_ec);
    if (addr_ec) {
        // Not retryable -- a bad address will not become good. Leave the link down;
        // the consumer degrades the same way it does for an absent peer.
        LOG_ERROR(
            logger_, "Invalid {} server address {}: {}", name_, address_, addr_ec.message()
        );
        return;
    }

    // Start each attempt from clean buffers. A link that dropped mid-line must not
    // prefix the next connection's first message with the leftover fragment, and a
    // request queued against the dead link is not worth replaying -- the consumer
    // re-subscribes from scratch on the status update anyway.
    writing_ = false;
    write_queue_.clear();
    read_message_.clear();

    // Move-assign a fresh socket for each attempt: a failed connect leaves the
    // descriptor open, and connecting the same socket again fails outright with
    // EINVAL, so reusing it would mean only the first retry could ever succeed.
    socket_ = boost::asio::ip::tcp::socket(io_context_);
    on_status(true, false);

    socket_.async_connect(
        boost::asio::ip::tcp::endpoint(addr, port_),
        [this](boost::system::error_code ec) {
            if (!running_.load()) return;

            if (ec) {
                LOG_ERROR(
                    logger_, "Failed to connect to {} server {}:{}: {}",
                    name_, address_, port_, ec.message()
                );
                boost::system::error_code close_ec;
                socket_.close(close_ec);
                on_status(false, false);
                arm_reconnect_timer();
                return;
            }

            connected_.store(true, std::memory_order_relaxed);
            LOG_INFO(logger_, "Connected to {} server {}:{}", name_, address_, port_);
            // Drives the consumer's (re)subscribe: every reconnect replays the
            // subscriptions, so a peer restart does not silently end the feed.
            on_status(false, true);
            do_read();
        }
    );
}


void TcpClientBase::arm_reconnect_timer() {
    if (!running_.load()) return;

    reconnect_timer_.expires_after(reconnect_interval_);
    reconnect_timer_.async_wait([this](boost::system::error_code ec) {
        if (ec == boost::asio::error::operation_aborted || !running_.load()) return;
        do_connect();
    });
}


void TcpClientBase::close_socket() {
    boost::system::error_code ec;
    socket_.close(ec);

    // Deliberately not clearing write_queue_ here: an async_write may still be
    // registered against the string at its front, and close() only *starts* the
    // cancellation. do_connect resets both buffers instead, by which point every
    // handler has drained.

    if (connected_.exchange(false, std::memory_order_relaxed)) {
        LOG_INFO(logger_, "Disconnected from {} server", name_);
        // Tell the consumer the link is down. Without this a dropped feed is invisible
        // to it: it would go on believing it is connected, never re-subscribe, and
        // silently receive nothing for the rest of the session.
        //
        // Not while stopping, though: stop() clears running_ before it gets here, and
        // this call is virtual. If the object is being destroyed, the subclass is
        // already gone and there is nobody left to tell anyway.
        if (running_.load()) on_status(false, false);
    }
}


void TcpClientBase::do_read() {
    socket_.async_read_some(
        boost::asio::buffer(read_buffer_),
        [this](boost::system::error_code ec, std::size_t length) {
            // on_data_line below is virtual; never dispatch it once stop() has begun,
            // since the subclass may already be gone.
            if (!running_.load()) return;

            if (ec) {
                if (connected_.load(std::memory_order_relaxed)) {
                    LOG_ERROR(logger_, "{} read error: {}", name_, ec.message());
                    close_socket();
                    arm_reconnect_timer();
                }
                return;
            }

            read_message_.append(read_buffer_.data(), length);

            size_t pos = 0;
            while ((pos = read_message_.find('\n')) != std::string::npos) {
                std::string line = read_message_.substr(0, pos);
                read_message_.erase(0, pos + 1);
                if (!line.empty()) handle_line(line);
            }

            do_read();
        }
    );
}


void TcpClientBase::enqueue_write(std::string message) {
    write_queue_.push_back(std::move(message));
    if (!writing_) {
        writing_ = true;
        do_write();
    }
}


void TcpClientBase::do_write() {
    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }

    // The buffer has to outlive the async operation, so write straight out of the
    // queued string and pop it only once the write has completed.
    boost::asio::async_write(
        socket_,
        boost::asio::buffer(write_queue_.front()),
        [this](boost::system::error_code ec, std::size_t /*length*/) {
            if (ec) {
                writing_ = false;
                if (connected_.load(std::memory_order_relaxed)) {
                    LOG_ERROR(logger_, "{} write error: {}", name_, ec.message());
                    close_socket();
                    arm_reconnect_timer();
                }
                return;
            }
            write_queue_.pop_front();
            do_write();
        }
    );
}


void TcpClientBase::subscribe(const std::string& product, Category category) {
    send_subscribe_request(product, category, true);
}


void TcpClientBase::unsubscribe(const std::string& product, Category category) {
    send_subscribe_request(product, category, false);
}


void TcpClientBase::send_subscribe_request(
    const std::string& product, Category category, bool subscribe
) {
    auto request = SubscribeRequestMsg{
        .subscribe = subscribe, .product = product, .category = category
    };

    std::string json_buffer;
    auto ec = glz::write_json(request, json_buffer);
    if (ec) {
        LOG_WARNING(
            logger_, "Failed write_json for the {} msg of product {}",
            subscribe ? "subscribe" : "unsubscribe", product
        );
        return;
    }
    json_buffer += "\n";

    // Called from the consumer's thread, so hand the write to the io thread rather
    // than touching the socket here.
    boost::asio::post(
        io_context_,
        [this, product, subscribe, json_buffer = std::move(json_buffer)]() mutable {
            if (!connected_.load(std::memory_order_relaxed)) {
                LOG_WARNING(
                    logger_, "Cannot {} product {}: not connected to {} server",
                    subscribe ? "subscribe" : "unsubscribe", product, name_
                );
                return;
            }
            enqueue_write(std::move(json_buffer));
        }
    );
}


void TcpClientBase::handle_line(const std::string& line) {
    try {
        FeedClassifier feed_classifier;
        auto ec = glz::read<glz::opts{.format = glz::JSON, .partial_read = true}>(
            feed_classifier, line
        );
        if (ec) {
            LOG_WARNING(logger_, "Failed to get feed from the message ({})", line);
            return;
        }

        // The subscribe handshake is identical on both links, so it is answered here;
        // everything else is feed data that only the subclass knows how to read.
        if (feed_classifier.feed == "subscribe") {
            SubscribeResponseMsg response;
            auto response_ec = glz::read_json(response, line);
            if (response_ec) {
                LOG_WARNING(logger_, "Failed to parse subscribe response ({})", line);
                return;
            }
            on_subscribe_response(response.subscribe, response.success, response.product);
            return;
        }

        on_data_line(feed_classifier.feed, line);
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Error parsing {} server message: {}", name_, e.what());
    }
}

} // namespace Omni::Connection
