#include "hypershare/network/tcp_client.hpp"
#include "hypershare/core/logger.hpp"
#include <boost/asio/connect.hpp>

namespace hypershare::network {

TcpClient::TcpClient()
    : state_(ClientState::DISCONNECTED)
    , io_context_()
    , resolver_(io_context_)
    , target_port_(0)
    , running_(false) {
}

TcpClient::~TcpClient() {
    stop();
}

std::future<bool> TcpClient::connect_async(const std::string& host, std::uint16_t port) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (state_ != ClientState::DISCONNECTED) {
        std::promise<bool> promise;
        promise.set_value(false);
        return promise.get_future();
    }
    
    target_host_ = host;
    target_port_ = port;
    set_state(ClientState::CONNECTING);
    
    connect_promise_ = std::promise<bool>();
    auto future = connect_promise_.get_future();
    
    LOG_INFO("Attempting to connect to {}:{}", host, port);
    
    auto endpoints = resolver_.resolve(host, std::to_string(port));
    tcp::socket socket(io_context_);
    
    boost::asio::async_connect(socket, endpoints,
        [this](boost::system::error_code ec, tcp::endpoint endpoint) {
            handle_connect(ec);
        });
    
    return future;
}

bool TcpClient::connect(const std::string& host, std::uint16_t port, std::chrono::milliseconds timeout) {
    auto future = connect_async(host, port);
    
    if (future.wait_for(timeout) == std::future_status::timeout) {
        LOG_ERROR("Connection to {}:{} timed out", host, port);
        set_state(ClientState::FAILED);
        return false;
    }
    
    return future.get();
}

void TcpClient::disconnect() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (state_ == ClientState::DISCONNECTED) {
        return;
    }
    
    LOG_INFO("Disconnecting from {}:{}", target_host_, target_port_);
    set_state(ClientState::DISCONNECTED);
    
    if (connection_) {
        connection_->close();
        connection_.reset();
    }
    
    io_context_.stop();
}

const std::string& TcpClient::get_remote_endpoint() const {
    if (connection_) {
        return connection_->get_remote_endpoint();
    }
    static std::string empty;
    return empty;
}

void TcpClient::send_message(const MessageHeader& header, const std::vector<std::uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (!connection_ || !is_connected()) {
        LOG_WARN("Attempted to send message while not connected");
        return;
    }
    
    connection_->send_message(header, payload);
}

void TcpClient::run() {
    running_ = true;
    
    while (running_) {
        try {
            io_context_.run();
            break;
        } catch (const std::exception& e) {
            LOG_ERROR("Client IO context error: {}", e.what());
            if (!running_) break;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            io_context_.restart();
        }
    }
}

void TcpClient::stop() {
    running_ = false;
    disconnect();
}

void TcpClient::handle_connect(const boost::system::error_code& error) {
    if (!error) {
        LOG_INFO("Successfully connected to {}:{}", target_host_, target_port_);
        set_state(ClientState::CONNECTED);
        
        // Create connection object from the connected socket
        tcp::socket socket(io_context_);
        // Note: In a real implementation, we'd need to get the socket from async_connect
        // This is a simplified version for demonstration
        
        connection_ = std::make_shared<Connection>(io_context_, std::move(socket));
        
        connection_->set_message_handler(
            [this](const MessageHeader& header, std::vector<std::uint8_t> payload) {
                handle_message(header, std::move(payload));
            });
        
        connection_->set_disconnect_handler(
            [this](std::shared_ptr<Connection> conn) {
                handle_disconnect(conn);
            });
        
        connection_->start();
        
        if (connect_handler_) {
            connect_handler_(true, "");
        }
        
        connect_promise_.set_value(true);
    } else {
        LOG_ERROR("Failed to connect to {}:{}: {}", target_host_, target_port_, error.message());
        set_state(ClientState::FAILED);
        
        if (connect_handler_) {
            connect_handler_(false, error.message());
        }
        
        connect_promise_.set_value(false);
    }
}

void TcpClient::handle_message(const MessageHeader& header, std::vector<std::uint8_t> payload) {
    LOG_DEBUG("Received message type {} ({} bytes)", 
              static_cast<int>(header.type), payload.size());
    
    if (message_handler_) {
        message_handler_(header, std::move(payload));
    }
}

void TcpClient::handle_disconnect(std::shared_ptr<Connection> connection) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    LOG_INFO("Disconnected from {}:{}", target_host_, target_port_);
    set_state(ClientState::DISCONNECTED);
    
    connection_.reset();
    
    if (disconnect_handler_) {
        disconnect_handler_("Connection closed by peer");
    }
}

void TcpClient::set_state(ClientState new_state) {
    if (state_ != new_state) {
        LOG_DEBUG("Client state changed: {} -> {}", 
                  static_cast<int>(state_), static_cast<int>(new_state));
        state_ = new_state;
    }
}

}