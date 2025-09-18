#pragma once

#include "hypershare/crypto/crypto_types.hpp"
#include "hypershare/crypto/key_manager.hpp"
#include "hypershare/network/protocol.hpp"
#include <memory>
#include <chrono>
#include <span>
#include <string>
#include <vector>
#include <cstdint>

namespace hypershare::crypto {

enum class HandshakePhase {
    INITIATE,
    RESPOND,
    COMPLETE,
    FAILED
};

struct SecureHandshakeMessage {
    std::uint32_t peer_id;
    std::uint16_t listen_port;
    std::string peer_name;
    std::uint32_t capabilities;
    
    // Cryptographic fields
    Ed25519PublicKey identity_public_key;
    X25519PublicKey ephemeral_public_key;
    std::uint64_t nonce;
    Ed25519Signature signature;
    
    std::vector<std::uint8_t> serialize() const;
    std::vector<std::uint8_t> serialize_for_signature() const;
    static SecureHandshakeMessage deserialize(std::span<const std::uint8_t> data);
};

struct SecureHandshakeAckMessage {
    std::uint32_t peer_id;
    X25519PublicKey ephemeral_public_key;
    std::uint64_t nonce;
    std::uint64_t response_nonce;
    Ed25519Signature signature;
    
    std::vector<std::uint8_t> serialize() const;
    std::vector<std::uint8_t> serialize_for_signature() const;
    static SecureHandshakeAckMessage deserialize(std::span<const std::uint8_t> data);
};

class SecureHandshake {
public:
    explicit SecureHandshake(std::shared_ptr<KeyManager> key_manager);
    ~SecureHandshake();
    
    // Client-side handshake initiation
    CryptoResult initiate_handshake(
        std::uint32_t peer_id,
        std::uint16_t listen_port,
        const std::string& peer_name,
        std::uint32_t capabilities,
        SecureHandshakeMessage& out_message
    );
    
    // Server-side handshake response
    CryptoResult respond_to_handshake(
        const SecureHandshakeMessage& incoming_message,
        std::uint32_t our_peer_id,
        SecureHandshakeAckMessage& out_ack_message
    );
    
    // Client-side handshake completion
    CryptoResult complete_handshake(
        const SecureHandshakeAckMessage& ack_message,
        KeyManager::SessionKeys& out_session_keys
    );
    
    // Server-side session key derivation
    CryptoResult derive_server_session_keys(
        const SecureHandshakeMessage& handshake_message,
        KeyManager::SessionKeys& out_session_keys
    );
    
    // Verification functions
    CryptoResult verify_handshake_signature(
        const SecureHandshakeMessage& message
    ) const;
    
    CryptoResult verify_ack_signature(
        const SecureHandshakeAckMessage& ack_message,
        const Ed25519PublicKey& peer_identity_key
    ) const;
    
    // State management
    HandshakePhase get_phase() const;
    void reset();
    
    // Utility functions
    std::string get_peer_fingerprint(const Ed25519PublicKey& public_key) const;
    bool is_trusted_peer(const Ed25519PublicKey& public_key) const;
    void add_trusted_peer(const Ed25519PublicKey& public_key, const std::string& name);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    std::shared_ptr<KeyManager> key_manager_;
    HandshakePhase phase_;
    
    // Session state for multi-round handshake
    X25519KeyPair our_ephemeral_keys_;
    X25519PublicKey peer_ephemeral_public_key_;
    Ed25519PublicKey peer_identity_public_key_;
    std::uint64_t our_nonce_;
    std::uint64_t peer_nonce_;
    std::chrono::steady_clock::time_point handshake_start_time_;
};

// Helper functions for message construction
std::vector<std::uint8_t> create_signature_data(
    const std::string& context,
    std::span<const std::uint8_t> handshake_data
);

std::vector<std::uint8_t> create_handshake_context(
    const Ed25519PublicKey& initiator_identity,
    const Ed25519PublicKey& responder_identity,
    const X25519PublicKey& initiator_ephemeral,
    const X25519PublicKey& responder_ephemeral
);

}

// Ensure SecureHandshakeMessage satisfies MessagePayload concept
static_assert(hypershare::network::MessagePayload<hypershare::crypto::SecureHandshakeMessage>);
static_assert(hypershare::network::MessagePayload<hypershare::crypto::SecureHandshakeAckMessage>);