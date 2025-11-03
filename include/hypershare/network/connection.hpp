#pragma once

#include "hypershare/network/protocol.hpp"
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <functional>
#include <chrono>
#include <queue>

namespace hypershare::network {

using boost::asio::ip::tcp;

enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    AUTHENTICATED,
    CLOSING
};

class Connection : public std::enable_shared_from_this<Connection> {
public:
    using MessageHandler = std::function<void(const MessageHeader&, std::vector<std::uint8_t>)>;
    using DisconnectHandler = std::function<void(std::shared_ptr<Connection>)>;
    
    Connection(boost::asio::io_context& io_context, tcp::socket socket);
    ~Connection();
    
    void start();
    void close();
    
    void send_message(const MessageHeader& header, const std::vector<std::uint8_t>& payload);
    void send_raw(MessageType type, const std::vector<std::uint8_t>& payload);
    
    template<MessagePayload T>
    void send_message(MessageType type, const T& payload) {
        auto payload_data = payload.serialize();
        MessageHeader header(type, static_cast<std::uint32_t>(payload_data.size()));
        header.calculate_checksum(payload_data);
        send_message(header, payload_data);
    }
    
    void set_message_handler(MessageHandler handler) { message_handler_ = std::move(handler); }
    void set_disconnect_handler(DisconnectHandler handler) { disconnect_handler_ = std::move(handler); }
    
    ConnectionState get_state() const { return state_; }
    const std::string& get_remote_endpoint() const { return remote_endpoint_; }
    std::string get_remote_address() const;
    std::uint16_t get_remote_port() const;
    std::chrono::steady_clock::time_point get_last_activity() const { return last_activity_; }
    
    std::uint32_t get_peer_id() const { return peer_id_; }
    void set_peer_id(std::uint32_t id) { peer_id_ = id; }

private:
    void do_read_header();
    void do_read_payload(std::uint32_t payload_size);
    void do_write();
    void handle_message(const MessageHeader& header, std::vector<std::uint8_t> payload);
    void handle_error(const boost::system::error_code& error);
    
    boost::asio::io_context& io_context_;
    tcp::socket socket_;
    ConnectionState state_;
    std::string remote_endpoint_;
    std::chrono::steady_clock::time_point last_activity_;
    std::uint32_t peer_id_;
    
    MessageHandler message_handler_;
    DisconnectHandler disconnect_handler_;
    
    std::array<std::uint8_t, MESSAGE_HEADER_SIZE> read_header_buffer_;
    std::vector<std::uint8_t> read_payload_buffer_;
    
    std::queue<std::vector<std::uint8_t>> write_queue_;
    bool write_in_progress_;
};

}