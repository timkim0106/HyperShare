#include "hypershare/network/connection.hpp"
#include "hypershare/core/logger.hpp"
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

namespace hypershare::network {

Connection::Connection(boost::asio::io_context& io_context, tcp::socket socket)
    : io_context_(io_context)
    , socket_(std::move(socket))
    , state_(ConnectionState::CONNECTED)
    , last_activity_(std::chrono::steady_clock::now())
    , peer_id_(0)
    , write_in_progress_(false) {
    
    try {
        remote_endpoint_ = socket_.remote_endpoint().address().to_string() + ":" +
                          std::to_string(socket_.remote_endpoint().port());
    } catch (const std::exception& e) {
        remote_endpoint_ = "unknown";
        LOG_WARN("Failed to get remote endpoint: {}", e.what());
    }
    
    LOG_INFO("New connection from {}", remote_endpoint_);
}

Connection::~Connection() {
    LOG_DEBUG("Connection to {} destroyed", remote_endpoint_);
}

void Connection::start() {
    LOG_DEBUG("Starting connection to {}", remote_endpoint_);
    do_read_header();
}

void Connection::close() {
    if (state_ == ConnectionState::DISCONNECTED || state_ == ConnectionState::CLOSING) {
        return;
    }
    
    state_ = ConnectionState::CLOSING;
    LOG_INFO("Closing connection to {}", remote_endpoint_);
    
    boost::system::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_both, ec);
    socket_.close(ec);
    
    state_ = ConnectionState::DISCONNECTED;
    
    if (disconnect_handler_) {
        disconnect_handler_(shared_from_this());
    }
}

void Connection::send_message(const MessageHeader& header, const std::vector<std::uint8_t>& payload) {
    if (state_ != ConnectionState::CONNECTED && state_ != ConnectionState::AUTHENTICATED) {
        LOG_WARN("Attempted to send message on inactive connection to {}", remote_endpoint_);
        return;
    }
    
    auto header_data = header.serialize();
    std::vector<std::uint8_t> message;
    message.reserve(header_data.size() + payload.size());
    message.insert(message.end(), header_data.begin(), header_data.end());
    message.insert(message.end(), payload.begin(), payload.end());
    
    bool write_was_empty = write_queue_.empty();
    write_queue_.push(std::move(message));
    
    if (write_was_empty && !write_in_progress_) {
        do_write();
    }
    
    LOG_DEBUG("Queued message type {} ({} bytes) for {}", 
              static_cast<int>(header.type), payload.size(), remote_endpoint_);
}

void Connection::send_raw(MessageType type, const std::vector<std::uint8_t>& payload) {
    MessageHeader header(type, static_cast<std::uint32_t>(payload.size()));
    header.calculate_checksum(payload);
    send_message(header, payload);
}

std::string Connection::get_remote_address() const {
    try {
        return socket_.remote_endpoint().address().to_string();
    } catch (const std::exception& e) {
        LOG_WARN("Failed to get remote address: {}", e.what());
        return "unknown";
    }
}

std::uint16_t Connection::get_remote_port() const {
    try {
        return socket_.remote_endpoint().port();
    } catch (const std::exception& e) {
        LOG_WARN("Failed to get remote port: {}", e.what());
        return 0;
    }
}

void Connection::do_read_header() {
    if (state_ == ConnectionState::DISCONNECTED || state_ == ConnectionState::CLOSING) {
        return;
    }
    
    auto self = shared_from_this();
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_header_buffer_),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                last_activity_ = std::chrono::steady_clock::now();
                
                try {
                    std::span<const std::uint8_t> header_span(read_header_buffer_.data(), MESSAGE_HEADER_SIZE);
                    auto header = MessageHeader::deserialize(header_span);
                    
                    if (!header.is_valid()) {
                        LOG_ERROR("Invalid message header from {}", remote_endpoint_);
                        close();
                        return;
                    }
                    
                    if (header.payload_size > 0) {
                        do_read_payload(header.payload_size);
                    } else {
                        handle_message(header, {});
                        do_read_header();
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to parse message header from {}: {}", remote_endpoint_, e.what());
                    close();
                }
            } else {
                handle_error(ec);
            }
        });
}

void Connection::do_read_payload(std::uint32_t payload_size) {
    if (payload_size > 10 * 1024 * 1024) { // 10MB limit
        LOG_ERROR("Payload too large ({} bytes) from {}", payload_size, remote_endpoint_);
        close();
        return;
    }
    
    read_payload_buffer_.resize(payload_size);
    
    auto self = shared_from_this();
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_payload_buffer_),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                last_activity_ = std::chrono::steady_clock::now();
                
                try {
                    std::span<const std::uint8_t> header_span(read_header_buffer_.data(), MESSAGE_HEADER_SIZE);
                    auto header = MessageHeader::deserialize(header_span);
                    
                    if (!header.verify_checksum(read_payload_buffer_)) {
                        LOG_ERROR("Checksum mismatch for message from {}", remote_endpoint_);
                        close();
                        return;
                    }
                    
                    handle_message(header, std::move(read_payload_buffer_));
                    do_read_header();
                } catch (const std::exception& e) {
                    LOG_ERROR("Failed to process message payload from {}: {}", remote_endpoint_, e.what());
                    close();
                }
            } else {
                handle_error(ec);
            }
        });
}

void Connection::do_write() {
    if (write_queue_.empty() || write_in_progress_) {
        return;
    }
    
    write_in_progress_ = true;
    auto& message = write_queue_.front();
    
    auto self = shared_from_this();
    boost::asio::async_write(socket_,
        boost::asio::buffer(message),
        [this, self](boost::system::error_code ec, std::size_t length) {
            write_in_progress_ = false;
            
            if (!ec) {
                write_queue_.pop();
                
                if (!write_queue_.empty()) {
                    do_write();
                }
            } else {
                handle_error(ec);
            }
        });
}

void Connection::handle_message(const MessageHeader& header, std::vector<std::uint8_t> payload) {
    LOG_DEBUG("Received message type {} ({} bytes) from {}", 
              static_cast<int>(header.type), payload.size(), remote_endpoint_);
    
    if (message_handler_) {
        message_handler_(header, std::move(payload));
    }
}

void Connection::handle_error(const boost::system::error_code& error) {
    if (error == boost::asio::error::eof) {
        LOG_INFO("Connection to {} closed by peer", remote_endpoint_);
    } else if (error == boost::asio::error::operation_aborted) {
        LOG_DEBUG("Connection operation aborted for {}", remote_endpoint_);
    } else {
        LOG_ERROR("Connection error with {}: {}", remote_endpoint_, error.message());
    }
    
    close();
}

}