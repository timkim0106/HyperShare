#include "hypershare/network/secure_message.hpp"
#include "hypershare/crypto/hash.hpp"
#include <stdexcept>
#include <cstring>

namespace hypershare::network {

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
    
    void write_uint8(std::vector<std::uint8_t>& buffer, std::uint8_t value) {
        buffer.push_back(value);
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
    
    std::uint8_t read_uint8(std::span<const std::uint8_t>& data) {
        if (data.empty()) throw std::runtime_error("Insufficient data for uint8");
        std::uint8_t value = data[0];
        data = data.subspan(1);
        return value;
    }
}

// SecureMessage implementation
std::vector<std::uint8_t> SecureMessage::serialize() const {
    std::vector<std::uint8_t> buffer;
    
    write_uint8(buffer, static_cast<std::uint8_t>(original_type));
    write_uint64(buffer, sequence_number);
    
    auto encrypted_data = encrypted_payload.serialize();
    write_uint32(buffer, static_cast<std::uint32_t>(encrypted_data.size()));
    buffer.insert(buffer.end(), encrypted_data.begin(), encrypted_data.end());
    
    return buffer;
}

SecureMessage SecureMessage::deserialize(std::span<const std::uint8_t> data) {
    SecureMessage msg;
    auto span = data;
    
    msg.original_type = static_cast<MessageType>(read_uint8(span));
    msg.sequence_number = read_uint64(span);
    
    auto encrypted_size = read_uint32(span);
    if (span.size() < encrypted_size) {
        throw std::runtime_error("Insufficient data for encrypted payload");
    }
    
    auto encrypted_span = span.subspan(0, encrypted_size);
    msg.encrypted_payload = crypto::EncryptedMessage::deserialize(encrypted_span);
    
    return msg;
}

// SecureMessageHandler implementation
SecureMessageHandler::SecureMessageHandler(std::shared_ptr<crypto::EncryptionEngine> encryption_engine)
    : encryption_engine_(std::move(encryption_engine))
    , last_decrypted_sequence_(0) {
    
    if (!encryption_engine_) {
        throw std::invalid_argument("EncryptionEngine cannot be null");
    }
}

SecureMessageHandler::~SecureMessageHandler() = default;

bool SecureMessageHandler::verify_message_integrity(
    const SecureMessage& secure_message,
    const crypto::KeyManager::SessionKeys& session_keys) const {
    
    // Create AAD for verification
    auto aad = create_aad(secure_message.original_type, secure_message.sequence_number);
    
    // Try to decrypt with zero-length output to verify integrity
    std::vector<std::uint8_t> dummy_output;
    auto result = encryption_engine_->decrypt(
        secure_message.encrypted_payload,
        aad,
        session_keys.encryption_key,
        dummy_output
    );
    
    return result.success();
}

void SecureMessageHandler::reset_sequence_numbers() {
    last_decrypted_sequence_ = 0;
}

std::vector<std::uint8_t> SecureMessageHandler::create_aad(
    MessageType type, 
    std::uint64_t sequence_number) const {
    
    std::vector<std::uint8_t> aad;
    aad.reserve(16);  // 1 + 8 + 7 bytes for padding
    
    write_uint8(aad, static_cast<std::uint8_t>(type));
    write_uint64(aad, sequence_number);
    
    // Add some protocol-specific context
    const char* context = "HYPER";
    aad.insert(aad.end(), context, context + 5);
    
    return aad;
}

// SecureConnection implementation
SecureConnection::SecureConnection(
    std::shared_ptr<Connection> base_connection,
    std::shared_ptr<SecureMessageHandler> message_handler,
    const crypto::KeyManager::SessionKeys& session_keys)
    : base_connection_(std::move(base_connection))
    , message_handler_(std::move(message_handler))
    , session_keys_(session_keys) {
    
    if (!base_connection_ || !message_handler_) {
        throw std::invalid_argument("Base connection and message handler cannot be null");
    }
}

SecureConnection::~SecureConnection() = default;

bool SecureConnection::is_connected() const {
    return base_connection_ && base_connection_->get_state() == ConnectionState::CONNECTED;
}

void SecureConnection::disconnect() {
    if (base_connection_) {
        base_connection_->close();
    }
}

std::string SecureConnection::get_remote_endpoint() const {
    if (base_connection_) {
        return base_connection_->get_remote_endpoint();
    }
    return "";
}

void SecureConnection::update_session_keys(const crypto::KeyManager::SessionKeys& new_keys) {
    session_keys_ = new_keys;
    message_handler_->reset_sequence_numbers();
}

}