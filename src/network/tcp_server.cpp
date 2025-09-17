#include "hypershare/network/tcp_server.hpp"
#include "hypershare/core/logger.hpp"
#include <mutex>

namespace hypershare::network {

TcpServer::TcpServer(std::uint16_t port)
    : port_(port)
    , running_(false)
    , io_context_()
    , acceptor_(io_context_, tcp::endpoint(tcp::v4(), port))
    , last_cleanup_(std::chrono::steady_clock::now()) {
    
    LOG_INFO("TCP server initialized on port {}", port_);
}

TcpServer::~TcpServer() {
    stop();
}

bool TcpServer::start() {
    if (running_) {
        LOG_WARN("TCP server already running");
        return false;
    }
    
    try {
        acceptor_.listen();
        running_ = true;
        
        do_accept();
        
        server_thread_ = std::thread([this]() {
            LOG_INFO("TCP server started on port {}", port_);
            
            while (running_) {
                try {
                    io_context_.run();
                    break;
                } catch (const std::exception& e) {
                    LOG_ERROR("IO context error: {}", e.what());
                    if (!running_) break;
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    io_context_.restart();
                }
            }
            
            LOG_INFO("TCP server stopped");
        });
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start TCP server: {}", e.what());
        running_ = false;
        return false;
    }
}

void TcpServer::stop() {
    if (!running_) {
        return;
    }
    
    LOG_INFO("Stopping TCP server on port {}", port_);
    running_ = false;
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& [connection, _] : connections_) {
            connection->close();
        }
        connections_.clear();
    }
    
    boost::system::error_code ec;
    acceptor_.close(ec);
    io_context_.stop();
    
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

std::vector<std::shared_ptr<Connection>> TcpServer::get_connections() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    std::vector<std::shared_ptr<Connection>> result;
    result.reserve(connections_.size());
    
    for (const auto& [connection, _] : connections_) {
        if (connection->get_state() == ConnectionState::CONNECTED ||
            connection->get_state() == ConnectionState::AUTHENTICATED) {
            result.push_back(connection);
        }
    }
    
    return result;
}

void TcpServer::broadcast_message(const MessageHeader& header, const std::vector<std::uint8_t>& payload) {
    auto connections = get_connections();
    
    LOG_DEBUG("Broadcasting message type {} to {} connections", 
              static_cast<int>(header.type), connections.size());
    
    for (auto& connection : connections) {
        connection->send_message(header, payload);
    }
}

void TcpServer::do_accept() {
    if (!running_) {
        return;
    }
    
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec && running_) {
                auto connection = std::make_shared<Connection>(io_context_, std::move(socket));
                handle_new_connection(connection);
                
                do_accept();
            } else if (ec != boost::asio::error::operation_aborted) {
                LOG_ERROR("Accept error: {}", ec.message());
                
                if (running_) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    do_accept();
                }
            }
        });
}

void TcpServer::handle_new_connection(std::shared_ptr<Connection> connection) {
    LOG_INFO("New connection accepted from {}", connection->get_remote_endpoint());
    
    connection->set_message_handler(
        [this, connection](const MessageHeader& header, std::vector<std::uint8_t> payload) {
            handle_message(connection, header, std::move(payload));
        });
    
    connection->set_disconnect_handler(
        [this](std::shared_ptr<Connection> conn) {
            handle_connection_closed(conn);
        });
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_[connection] = true;
    }
    
    connection->start();
    
    if (connection_handler_) {
        connection_handler_(connection);
    }
    
    auto now = std::chrono::steady_clock::now();
    if (now - last_cleanup_ > std::chrono::minutes(1)) {
        cleanup_connections();
        last_cleanup_ = now;
    }
}

void TcpServer::handle_connection_closed(std::shared_ptr<Connection> connection) {
    LOG_INFO("Connection closed: {}", connection->get_remote_endpoint());
    
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.erase(connection);
}

void TcpServer::handle_message(std::shared_ptr<Connection> connection, const MessageHeader& header, std::vector<std::uint8_t> payload) {
    if (message_handler_) {
        message_handler_(connection, header, std::move(payload));
    }
}

void TcpServer::cleanup_connections() {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.begin();
    while (it != connections_.end()) {
        if (it->first->get_state() == ConnectionState::DISCONNECTED) {
            it = connections_.erase(it);
        } else {
            ++it;
        }
    }
}

}