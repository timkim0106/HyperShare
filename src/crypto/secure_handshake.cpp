#include "hypershare/crypto/secure_handshake.hpp"
#include "hypershare/crypto/signature.hpp"
#include "hypershare/crypto/random.hpp"
#include "hypershare/crypto/hash.hpp"
#include <sodium.h>
#include <stdexcept>
#include <cstring>
#include <map>
#include <chrono>

namespace {
    class RandomGenerator {
    public:
        std::uint64_t generate_uint64() {
            return hypershare::crypto::SecureRandom::generate_uint64();
        }
    };
}

namespace hypershare::crypto {

namespace {
    void write_uint32(std::vector<std::uint8_t>& buffer, std::uint32_t value) {
        buffer.push_back((value >> 24) & 0xFF);
        buffer.push_back((value >> 16) & 0xFF);
        buffer.push_back((value >> 8) & 0xFF);
        buffer.push_back(value & 0xFF);
    }
    
    void write_uint64(std::vector<std::uint8_t>& buffer, std::uint64_t value) {
        buffer.push_back((value >> 56) & 0xFF);
        buffer.push_back((value >> 48) & 0xFF);
        buffer.push_back((value >> 40) & 0xFF);
        buffer.push_back((value >> 32) & 0xFF);
        buffer.push_back((value >> 24) & 0xFF);
        buffer.push_back((value >> 16) & 0xFF);
        buffer.push_back((value >> 8) & 0xFF);
        buffer.push_back(value & 0xFF);
    }
    
    void write_uint16(std::vector<std::uint8_t>& buffer, std::uint16_t value) {
        buffer.push_back((value >> 8) & 0xFF);
        buffer.push_back(value & 0xFF);
    }
    
    void write_string(std::vector<std::uint8_t>& buffer, const std::string& str) {
        write_uint32(buffer, static_cast<std::uint32_t>(str.size()));
        buffer.insert(buffer.end(), str.begin(), str.end());
    }
    
    void write_array(std::vector<std::uint8_t>& buffer, std::span<const std::uint8_t> data) {
        buffer.insert(buffer.end(), data.begin(), data.end());
    }
    
    std::uint32_t read_uint32(std::span<const std::uint8_t>& data) {
        if (data.size() < 4) throw std::runtime_error("Insufficient data for uint32");
        std::uint32_t value = (static_cast<std::uint32_t>(data[0]) << 24) |
                             (static_cast<std::uint32_t>(data[1]) << 16) |
                             (static_cast<std::uint32_t>(data[2]) << 8) |
                             static_cast<std::uint32_t>(data[3]);
        data = data.subspan(4);
        return value;
    }
    
    std::uint64_t read_uint64(std::span<const std::uint8_t>& data) {
        if (data.size() < 8) throw std::runtime_error("Insufficient data for uint64");
        std::uint64_t value = (static_cast<std::uint64_t>(data[0]) << 56) |
                             (static_cast<std::uint64_t>(data[1]) << 48) |
                             (static_cast<std::uint64_t>(data[2]) << 40) |
                             (static_cast<std::uint64_t>(data[3]) << 32) |
                             (static_cast<std::uint64_t>(data[4]) << 24) |
                             (static_cast<std::uint64_t>(data[5]) << 16) |
                             (static_cast<std::uint64_t>(data[6]) << 8) |
                             static_cast<std::uint64_t>(data[7]);
        data = data.subspan(8);
        return value;
    }
    
    std::uint16_t read_uint16(std::span<const std::uint8_t>& data) {
        if (data.size() < 2) throw std::runtime_error("Insufficient data for uint16");
        std::uint16_t value = (static_cast<std::uint16_t>(data[0]) << 8) |
                             static_cast<std::uint16_t>(data[1]);
        data = data.subspan(2);
        return value;
    }
    
    std::string read_string(std::span<const std::uint8_t>& data) {
        auto length = read_uint32(data);
        if (data.size() < length) throw std::runtime_error("Insufficient data for string");
        std::string str(reinterpret_cast<const char*>(data.data()), length);
        data = data.subspan(length);
        return str;
    }
    
    template<size_t N>
    std::array<std::uint8_t, N> read_array(std::span<const std::uint8_t>& data) {
        if (data.size() < N) throw std::runtime_error("Insufficient data for array");
        std::array<std::uint8_t, N> arr;
        std::copy(data.begin(), data.begin() + N, arr.begin());
        data = data.subspan(N);
        return arr;
    }
}

struct SecureHandshake::Impl {
    SignatureEngine signature_engine;
    Blake3Hasher hash_engine;
    ::RandomGenerator random_generator;
    
    // Trusted peers database (in production this would be persistent)
    std::map<Ed25519PublicKey, std::string> trusted_peers;
    
    Impl() {
        // Engines are initialized in their constructors
    }
};

SecureHandshake::SecureHandshake(std::shared_ptr<KeyManager> key_manager)
    : impl_(std::make_unique<Impl>())
    , key_manager_(std::move(key_manager))
    , phase_(HandshakePhase::INITIATE) {
    
    if (!key_manager_) {
        throw std::invalid_argument("KeyManager cannot be null");
    }
}

SecureHandshake::~SecureHandshake() = default;

CryptoResult SecureHandshake::initiate_handshake(
    std::uint32_t peer_id,
    std::uint16_t listen_port,
    const std::string& peer_name,
    std::uint32_t capabilities,
    SecureHandshakeMessage& out_message) {
    
    if (phase_ != HandshakePhase::INITIATE) {
        return CryptoResult(CryptoError::INVALID_STATE, "Handshake already in progress");
    }
    
    if (!key_manager_->has_identity_keys()) {
        return CryptoResult(CryptoError::INVALID_KEY, "No identity keys available");
    }
    
    // Generate ephemeral keys for this handshake
    our_ephemeral_keys_ = key_manager_->generate_ephemeral_keys();
    
    // Generate nonce
    our_nonce_ = impl_->random_generator.generate_uint64();
    
    // Fill message
    out_message.peer_id = peer_id;
    out_message.listen_port = listen_port;
    out_message.peer_name = peer_name;
    out_message.capabilities = capabilities;
    out_message.identity_public_key = key_manager_->get_identity_keys().public_key;
    out_message.ephemeral_public_key = our_ephemeral_keys_.public_key;
    out_message.nonce = our_nonce_;
    
    // Create signature data (message data without signature)
    auto signature_data = create_signature_data("HANDSHAKE_INITIATE", out_message.serialize_for_signature());
    
    // Sign the handshake data
    auto sign_result = impl_->signature_engine.sign(
        signature_data,
        key_manager_->get_identity_keys().secret_key,
        out_message.signature
    );
    
    if (!sign_result) {
        return sign_result;
    }
    
    handshake_start_time_ = std::chrono::steady_clock::now();
    phase_ = HandshakePhase::RESPOND;
    
    return CryptoResult();
}

CryptoResult SecureHandshake::respond_to_handshake(
    const SecureHandshakeMessage& incoming_message,
    std::uint32_t our_peer_id,
    SecureHandshakeAckMessage& out_ack_message) {
    
    // Verify the incoming handshake signature
    auto verify_result = verify_handshake_signature(incoming_message);
    if (!verify_result) {
        phase_ = HandshakePhase::FAILED;
        return verify_result;
    }
    
    // Store peer information
    peer_identity_public_key_ = incoming_message.identity_public_key;
    peer_ephemeral_public_key_ = incoming_message.ephemeral_public_key;
    peer_nonce_ = incoming_message.nonce;
    
    // Generate our ephemeral keys
    our_ephemeral_keys_ = key_manager_->generate_ephemeral_keys();
    our_nonce_ = impl_->random_generator.generate_uint64();
    
    // Fill ack message
    out_ack_message.peer_id = our_peer_id;
    out_ack_message.ephemeral_public_key = our_ephemeral_keys_.public_key;
    out_ack_message.nonce = our_nonce_;
    out_ack_message.response_nonce = peer_nonce_; // Echo back their nonce
    
    // Create handshake context for signature
    auto context = create_handshake_context(
        peer_identity_public_key_,
        key_manager_->get_identity_keys().public_key,
        peer_ephemeral_public_key_,
        our_ephemeral_keys_.public_key
    );
    
    auto signature_data = create_signature_data("HANDSHAKE_RESPOND", out_ack_message.serialize_for_signature());
    signature_data.insert(signature_data.end(), context.begin(), context.end());
    
    // Sign the ack data
    auto sign_result = impl_->signature_engine.sign(
        signature_data,
        key_manager_->get_identity_keys().secret_key,
        out_ack_message.signature
    );
    
    if (!sign_result) {
        phase_ = HandshakePhase::FAILED;
        return sign_result;
    }
    
    phase_ = HandshakePhase::COMPLETE;
    return CryptoResult();
}

CryptoResult SecureHandshake::complete_handshake(
    const SecureHandshakeAckMessage& ack_message,
    KeyManager::SessionKeys& out_session_keys) {
    
    if (phase_ != HandshakePhase::RESPOND) {
        return CryptoResult(CryptoError::INVALID_STATE, "Not in respond phase");
    }
    
    // Verify nonce echo
    if (ack_message.response_nonce != our_nonce_) {
        phase_ = HandshakePhase::FAILED;
        return CryptoResult(CryptoError::VERIFICATION_FAILED, "Nonce mismatch");
    }
    
    // Store peer ephemeral key
    peer_ephemeral_public_key_ = ack_message.ephemeral_public_key;
    peer_nonce_ = ack_message.nonce;
    
    // Verify ack signature
    auto verify_result = verify_ack_signature(ack_message, peer_identity_public_key_);
    if (!verify_result) {
        phase_ = HandshakePhase::FAILED;
        return verify_result;
    }
    
    // Derive session keys
    auto context = create_handshake_context(
        key_manager_->get_identity_keys().public_key,
        peer_identity_public_key_,
        our_ephemeral_keys_.public_key,
        peer_ephemeral_public_key_
    );
    
    out_session_keys = key_manager_->derive_session_keys(
        our_ephemeral_keys_.secret_key,
        peer_ephemeral_public_key_,
        context
    );
    
    phase_ = HandshakePhase::COMPLETE;
    return CryptoResult();
}

CryptoResult SecureHandshake::derive_server_session_keys(
    const SecureHandshakeMessage& handshake_message,
    KeyManager::SessionKeys& out_session_keys) {
    
    if (phase_ != HandshakePhase::COMPLETE) {
        return CryptoResult(CryptoError::INVALID_STATE, "Handshake not complete");
    }
    
    auto context = create_handshake_context(
        peer_identity_public_key_,
        key_manager_->get_identity_keys().public_key,
        peer_ephemeral_public_key_,
        our_ephemeral_keys_.public_key
    );
    
    out_session_keys = key_manager_->derive_session_keys(
        our_ephemeral_keys_.secret_key,
        peer_ephemeral_public_key_,
        context
    );
    
    return CryptoResult();
}

CryptoResult SecureHandshake::verify_handshake_signature(
    const SecureHandshakeMessage& message) const {
    
    auto signature_data = create_signature_data("HANDSHAKE_INITIATE", message.serialize_for_signature());
    
    return impl_->signature_engine.verify(
        signature_data,
        message.signature,
        message.identity_public_key
    );
}

CryptoResult SecureHandshake::verify_ack_signature(
    const SecureHandshakeAckMessage& ack_message,
    const Ed25519PublicKey& peer_identity_key) const {
    
    auto context = create_handshake_context(
        peer_identity_public_key_,
        key_manager_->get_identity_keys().public_key,
        peer_ephemeral_public_key_,
        our_ephemeral_keys_.public_key
    );
    
    auto signature_data = create_signature_data("HANDSHAKE_RESPOND", ack_message.serialize_for_signature());
    signature_data.insert(signature_data.end(), context.begin(), context.end());
    
    return impl_->signature_engine.verify(
        signature_data,
        ack_message.signature,
        peer_identity_key
    );
}

HandshakePhase SecureHandshake::get_phase() const {
    return phase_;
}

void SecureHandshake::reset() {
    phase_ = HandshakePhase::INITIATE;
    our_ephemeral_keys_ = {};
    peer_ephemeral_public_key_ = {};
    peer_identity_public_key_ = {};
    our_nonce_ = 0;
    peer_nonce_ = 0;
}

std::string SecureHandshake::get_peer_fingerprint(const Ed25519PublicKey& public_key) const {
    auto hash = Blake3Hasher::hash(std::span(public_key));
    
    std::string fingerprint;
    for (size_t i = 0; i < 8; ++i) { // First 8 bytes for readable fingerprint
        if (i > 0) fingerprint += ":";
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", hash[i]);
        fingerprint += buf;
    }
    return fingerprint;
}

bool SecureHandshake::is_trusted_peer(const Ed25519PublicKey& public_key) const {
    return impl_->trusted_peers.find(public_key) != impl_->trusted_peers.end();
}

void SecureHandshake::add_trusted_peer(const Ed25519PublicKey& public_key, const std::string& name) {
    impl_->trusted_peers[public_key] = name;
}

// Message serialization implementations
std::vector<std::uint8_t> SecureHandshakeMessage::serialize() const {
    std::vector<std::uint8_t> buffer;
    write_uint32(buffer, peer_id);
    write_uint16(buffer, listen_port);
    write_string(buffer, peer_name);
    write_uint32(buffer, capabilities);
    write_array(buffer, std::span(identity_public_key));
    write_array(buffer, std::span(ephemeral_public_key));
    write_uint64(buffer, nonce);
    write_array(buffer, std::span(signature));
    return buffer;
}

std::vector<std::uint8_t> SecureHandshakeMessage::serialize_for_signature() const {
    std::vector<std::uint8_t> buffer;
    write_uint32(buffer, peer_id);
    write_uint16(buffer, listen_port);
    write_string(buffer, peer_name);
    write_uint32(buffer, capabilities);
    write_array(buffer, std::span(identity_public_key));
    write_array(buffer, std::span(ephemeral_public_key));
    write_uint64(buffer, nonce);
    // Note: signature is NOT included in signature data
    return buffer;
}

SecureHandshakeMessage SecureHandshakeMessage::deserialize(std::span<const std::uint8_t> data) {
    SecureHandshakeMessage msg;
    auto span = data;
    msg.peer_id = read_uint32(span);
    msg.listen_port = read_uint16(span);
    msg.peer_name = read_string(span);
    msg.capabilities = read_uint32(span);
    msg.identity_public_key = read_array<ED25519_PUBLIC_KEY_SIZE>(span);
    msg.ephemeral_public_key = read_array<X25519_PUBLIC_KEY_SIZE>(span);
    msg.nonce = read_uint64(span);
    msg.signature = read_array<ED25519_SIGNATURE_SIZE>(span);
    return msg;
}

std::vector<std::uint8_t> SecureHandshakeAckMessage::serialize() const {
    std::vector<std::uint8_t> buffer;
    write_uint32(buffer, peer_id);
    write_array(buffer, std::span(ephemeral_public_key));
    write_uint64(buffer, nonce);
    write_uint64(buffer, response_nonce);
    write_array(buffer, std::span(signature));
    return buffer;
}

std::vector<std::uint8_t> SecureHandshakeAckMessage::serialize_for_signature() const {
    std::vector<std::uint8_t> buffer;
    write_uint32(buffer, peer_id);
    write_array(buffer, std::span(ephemeral_public_key));
    write_uint64(buffer, nonce);
    write_uint64(buffer, response_nonce);
    // Note: signature is NOT included in signature data
    return buffer;
}

SecureHandshakeAckMessage SecureHandshakeAckMessage::deserialize(std::span<const std::uint8_t> data) {
    SecureHandshakeAckMessage msg;
    auto span = data;
    msg.peer_id = read_uint32(span);
    msg.ephemeral_public_key = read_array<X25519_PUBLIC_KEY_SIZE>(span);
    msg.nonce = read_uint64(span);
    msg.response_nonce = read_uint64(span);
    msg.signature = read_array<ED25519_SIGNATURE_SIZE>(span);
    return msg;
}

// Helper function implementations
std::vector<std::uint8_t> create_signature_data(
    const std::string& context,
    std::span<const std::uint8_t> handshake_data) {
    
    std::vector<std::uint8_t> signature_data;
    write_string(signature_data, context);
    signature_data.insert(signature_data.end(), handshake_data.begin(), handshake_data.end());
    return signature_data;
}

std::vector<std::uint8_t> create_handshake_context(
    const Ed25519PublicKey& initiator_identity,
    const Ed25519PublicKey& responder_identity,
    const X25519PublicKey& initiator_ephemeral,
    const X25519PublicKey& responder_ephemeral) {
    
    std::vector<std::uint8_t> context;
    write_string(context, "HYPERSHARE_HANDSHAKE_V1");
    write_array(context, std::span(initiator_identity));
    write_array(context, std::span(responder_identity));
    write_array(context, std::span(initiator_ephemeral));
    write_array(context, std::span(responder_ephemeral));
    return context;
}

}