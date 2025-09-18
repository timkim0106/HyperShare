#pragma once

#include "hypershare/network/connection_manager.hpp"
#include "hypershare/crypto/secure_handshake.hpp"
#include "hypershare/crypto/key_manager.hpp"
#include <memory>
#include <unordered_map>

namespace hypershare::network {

enum class SecureHandshakeState {
    NONE,
    INITIATED,
    RESPONDING,
    COMPLETED,
    FAILED
};

struct SecureConnectionInfo : public ConnectionInfo {
    SecureHandshakeState secure_handshake_state;
    crypto::KeyManager::SessionKeys session_keys;
    crypto::Ed25519PublicKey peer_identity_key;
    std::chrono::steady_clock::time_point last_key_rotation;
    bool peer_authenticated;
    std::string peer_fingerprint;
};

class SecureConnectionManager : public ConnectionManager {
public:
    explicit SecureConnectionManager(std::shared_ptr<crypto::KeyManager> key_manager);
    ~SecureConnectionManager();
    
    // Override base class methods to include security
    bool start(std::uint16_t tcp_port, std::uint16_t udp_port = 8081) override;
    void stop() override;
    
    bool connect_to_peer_secure(const std::string& host, std::uint16_t port);
    
    // Security-specific methods
    bool is_peer_authenticated(std::uint32_t peer_id) const;
    std::string get_peer_fingerprint(std::uint32_t peer_id) const;
    bool add_trusted_peer(const crypto::Ed25519PublicKey& public_key, const std::string& name);
    
    // Secure messaging
    template<MessagePayload T>
    bool send_encrypted_message(std::uint32_t peer_id, MessageType type, const T& message) {
        auto conn_info = get_secure_connection_info(peer_id);
        if (!conn_info || !conn_info->peer_authenticated) {
            return false;
        }
        // TODO: Implement encryption in next commit
        return send_to_peer(peer_id, type, message);
    }
    
    // Key rotation management
    void check_key_rotation();
    void set_key_rotation_interval(std::chrono::milliseconds interval) { key_rotation_interval_ = interval; }
    
    std::vector<SecureConnectionInfo> get_secure_connections() const;
    std::optional<SecureConnectionInfo> get_secure_connection_info(std::uint32_t peer_id) const;

protected:
    // Override message handlers for secure handshake
    void handle_secure_handshake(std::shared_ptr<Connection> connection, 
                                const crypto::SecureHandshakeMessage& msg);
    void handle_secure_handshake_ack(std::shared_ptr<Connection> connection, 
                                    const crypto::SecureHandshakeAckMessage& msg);
    
    void initiate_secure_handshake(std::shared_ptr<Connection> connection);
    void send_secure_handshake(std::shared_ptr<Connection> connection);

private:
    std::shared_ptr<crypto::KeyManager> key_manager_;
    std::unique_ptr<crypto::SecureHandshake> secure_handshake_;
    
    // Secure connection tracking
    std::unordered_map<std::shared_ptr<Connection>, SecureConnectionInfo> secure_connections_;
    std::unordered_map<std::uint32_t, SecureConnectionInfo> peer_secure_info_;
    mutable std::mutex secure_connections_mutex_;
    
    // Key rotation
    std::chrono::milliseconds key_rotation_interval_;
    std::thread key_rotation_thread_;
    
    // Security configuration
    bool require_authentication_;
    bool allow_anonymous_peers_;
    std::chrono::milliseconds handshake_timeout_;
};

}