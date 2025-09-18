#include "hypershare/crypto/encryption.hpp"
#include "hypershare/crypto/random.hpp"
#include "hypershare/crypto/hash.hpp"
#include <sodium.h>
#include <stdexcept>
#include <unordered_map>
#include <set>
#include <mutex>
#include <cstring>

namespace hypershare::crypto {

namespace {
    void write_uint32(std::vector<std::uint8_t>& buffer, std::uint32_t value) {
        buffer.push_back((value >> 24) & 0xFF);
        buffer.push_back((value >> 16) & 0xFF);
        buffer.push_back((value >> 8) & 0xFF);
        buffer.push_back(value & 0xFF);
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
    
    void write_array(std::vector<std::uint8_t>& buffer, std::span<const std::uint8_t> data) {
        buffer.insert(buffer.end(), data.begin(), data.end());
    }
    
    template<size_t N>
    std::array<std::uint8_t, N> read_array(std::span<const std::uint8_t>& data) {
        if (data.size() < N) throw std::runtime_error("Insufficient data for array");
        std::array<std::uint8_t, N> arr;
        std::copy(data.begin(), data.begin() + N, arr.begin());
        data = data.subspan(N);
        return arr;
    }
    
    std::uint64_t nonce_to_uint64(const ChaCha20Nonce& nonce) {
        std::uint64_t result = 0;
        for (size_t i = 0; i < 8 && i < nonce.size(); ++i) {
            result |= (static_cast<std::uint64_t>(nonce[i]) << (i * 8));
        }
        return result;
    }
    
    ChaCha20Nonce uint64_to_nonce(std::uint64_t value) {
        ChaCha20Nonce nonce = {};
        for (size_t i = 0; i < 8 && i < nonce.size(); ++i) {
            nonce[i] = (value >> (i * 8)) & 0xFF;
        }
        // Fill remaining bytes with secure random
        if (nonce.size() > 8) {
            auto random_bytes = SecureRandom::generate_bytes(nonce.size() - 8);
            std::copy(random_bytes.data.begin(), random_bytes.data.end(), nonce.begin() + 8);
        }
        return nonce;
    }
}

// EncryptedMessage implementation
std::vector<std::uint8_t> EncryptedMessage::serialize() const {
    std::vector<std::uint8_t> buffer;
    buffer.reserve(total_size());
    
    write_array(buffer, std::span(nonce));
    write_uint32(buffer, static_cast<std::uint32_t>(ciphertext.size()));
    write_array(buffer, ciphertext);
    write_array(buffer, std::span(tag));
    
    return buffer;
}

EncryptedMessage EncryptedMessage::deserialize(std::span<const std::uint8_t> data) {
    EncryptedMessage msg;
    auto span = data;
    
    msg.nonce = read_array<CHACHA20_NONCE_SIZE>(span);
    auto ciphertext_size = read_uint32(span);
    
    if (span.size() < ciphertext_size + AEAD_TAG_SIZE) {
        throw std::runtime_error("Insufficient data for encrypted message");
    }
    
    msg.ciphertext.assign(span.begin(), span.begin() + ciphertext_size);
    span = span.subspan(ciphertext_size);
    msg.tag = read_array<AEAD_TAG_SIZE>(span);
    
    return msg;
}

// EncryptionEngine implementation
struct EncryptionEngine::Impl {
    bool initialized = false;
    HashEngine hash_engine;
    
    Impl() {
        if (sodium_init() < 0) {
            throw std::runtime_error("Failed to initialize libsodium for encryption");
        }
        initialized = true;
    }
};

EncryptionEngine::EncryptionEngine() 
    : impl_(std::make_unique<Impl>()) {
}

EncryptionEngine::~EncryptionEngine() = default;

CryptoResult EncryptionEngine::encrypt(
    std::span<const std::uint8_t> plaintext,
    std::span<const std::uint8_t> additional_data,
    const ChaCha20Key& key,
    const ChaCha20Nonce& nonce,
    EncryptedMessage& out_encrypted) const {
    
    if (!impl_->initialized) {
        return CryptoResult(CryptoError::ENCRYPTION_FAILED, "Encryption engine not initialized");
    }
    
    if (plaintext.empty()) {
        return CryptoResult(CryptoError::INVALID_KEY, "Plaintext cannot be empty");
    }
    
    // Prepare output buffers
    out_encrypted.nonce = nonce;
    out_encrypted.ciphertext.resize(plaintext.size() + AEAD_TAG_SIZE);
    
    unsigned long long ciphertext_len;
    
    int result = crypto_aead_chacha20poly1305_ietf_encrypt(
        out_encrypted.ciphertext.data(),
        &ciphertext_len,
        plaintext.data(),
        plaintext.size(),
        additional_data.data(),
        additional_data.size(),
        nullptr,  // nsec (not used)
        nonce.data(),
        key.data()
    );
    
    if (result != 0) {
        return CryptoResult(CryptoError::ENCRYPTION_FAILED, "ChaCha20-Poly1305 encryption failed");
    }
    
    // Split ciphertext and tag
    if (ciphertext_len < AEAD_TAG_SIZE) {
        return CryptoResult(CryptoError::ENCRYPTION_FAILED, "Invalid ciphertext length");
    }
    
    auto actual_ciphertext_len = ciphertext_len - AEAD_TAG_SIZE;
    
    // Copy the tag (last 16 bytes)
    std::copy(
        out_encrypted.ciphertext.begin() + actual_ciphertext_len,
        out_encrypted.ciphertext.begin() + ciphertext_len,
        out_encrypted.tag.begin()
    );
    
    // Resize ciphertext to exclude tag
    out_encrypted.ciphertext.resize(actual_ciphertext_len);
    
    return CryptoResult();
}

CryptoResult EncryptionEngine::decrypt(
    const EncryptedMessage& encrypted,
    std::span<const std::uint8_t> additional_data,
    const ChaCha20Key& key,
    std::vector<std::uint8_t>& out_plaintext) const {
    
    if (!impl_->initialized) {
        return CryptoResult(CryptoError::DECRYPTION_FAILED, "Encryption engine not initialized");
    }
    
    if (encrypted.ciphertext.empty()) {
        return CryptoResult(CryptoError::INVALID_KEY, "Ciphertext cannot be empty");
    }
    
    // Prepare combined ciphertext + tag for libsodium
    std::vector<std::uint8_t> combined_ciphertext;
    combined_ciphertext.reserve(encrypted.ciphertext.size() + AEAD_TAG_SIZE);
    combined_ciphertext.insert(combined_ciphertext.end(), 
                              encrypted.ciphertext.begin(), 
                              encrypted.ciphertext.end());
    combined_ciphertext.insert(combined_ciphertext.end(),
                              encrypted.tag.begin(),
                              encrypted.tag.end());
    
    out_plaintext.resize(encrypted.ciphertext.size());
    unsigned long long plaintext_len;
    
    int result = crypto_aead_chacha20poly1305_ietf_decrypt(
        out_plaintext.data(),
        &plaintext_len,
        nullptr,  // nsec (not used)
        combined_ciphertext.data(),
        combined_ciphertext.size(),
        additional_data.data(),
        additional_data.size(),
        encrypted.nonce.data(),
        key.data()
    );
    
    if (result != 0) {
        return CryptoResult(CryptoError::DECRYPTION_FAILED, "ChaCha20-Poly1305 decryption failed");
    }
    
    out_plaintext.resize(plaintext_len);
    return CryptoResult();
}

CryptoResult EncryptionEngine::encrypt_string(
    const std::string& plaintext,
    std::span<const std::uint8_t> additional_data,
    const ChaCha20Key& key,
    const ChaCha20Nonce& nonce,
    EncryptedMessage& out_encrypted) const {
    
    return encrypt(
        std::span(reinterpret_cast<const std::uint8_t*>(plaintext.data()), plaintext.size()),
        additional_data,
        key,
        nonce,
        out_encrypted
    );
}

CryptoResult EncryptionEngine::decrypt_to_string(
    const EncryptedMessage& encrypted,
    std::span<const std::uint8_t> additional_data,
    const ChaCha20Key& key,
    std::string& out_plaintext) const {
    
    std::vector<std::uint8_t> plaintext_bytes;
    auto result = decrypt(encrypted, additional_data, key, plaintext_bytes);
    if (!result) {
        return result;
    }
    
    out_plaintext.assign(
        reinterpret_cast<const char*>(plaintext_bytes.data()),
        plaintext_bytes.size()
    );
    
    return CryptoResult();
}

ChaCha20Nonce EncryptionEngine::generate_nonce() const {
    return SecureRandom::generate_chacha20_nonce();
}

ChaCha20Key EncryptionEngine::derive_key_from_secret(
    std::span<const std::uint8_t> shared_secret,
    std::span<const std::uint8_t> context) const {
    
    // Use BLAKE3 to derive key from shared secret
    std::vector<std::uint8_t> input;
    input.insert(input.end(), shared_secret.begin(), shared_secret.end());
    input.insert(input.end(), context.begin(), context.end());
    
    Blake3Hash hash;
    impl_->hash_engine.hash(input, std::span(hash));
    
    ChaCha20Key key;
    std::copy(hash.begin(), hash.begin() + CHACHA20_KEY_SIZE, key.begin());
    
    return key;
}

// NonceManager implementation
struct NonceManager::Impl {
    std::mutex mutex;
    
    // Per-peer nonce tracking for replay protection
    struct PeerNonceState {
        std::uint64_t highest_seen = 0;
        std::set<std::uint64_t> recent_nonces;
    };
    
    std::unordered_map<std::uint32_t, PeerNonceState> peer_states;
};

NonceManager::NonceManager() 
    : impl_(std::make_unique<Impl>())
    , outgoing_counter_(1)  // Start from 1 to avoid zero nonce
    , window_size_(1000) {  // Default window size
}

NonceManager::~NonceManager() = default;

ChaCha20Nonce NonceManager::generate_outgoing_nonce() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto nonce = uint64_to_nonce(outgoing_counter_++);
    return nonce;
}

bool NonceManager::verify_incoming_nonce(const ChaCha20Nonce& nonce, std::uint32_t peer_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    auto nonce_value = nonce_to_uint64(nonce);
    auto& peer_state = impl_->peer_states[peer_id];
    
    // Check if this nonce has been seen before (replay attack)
    if (peer_state.recent_nonces.count(nonce_value)) {
        return false;  // Replay detected
    }
    
    // Check if nonce is too old
    if (nonce_value + window_size_ < peer_state.highest_seen) {
        return false;  // Too old
    }
    
    // Accept the nonce
    peer_state.recent_nonces.insert(nonce_value);
    peer_state.highest_seen = std::max(peer_state.highest_seen, nonce_value);
    
    // Cleanup old nonces outside the window
    auto cutoff = peer_state.highest_seen > window_size_ ? 
                  peer_state.highest_seen - window_size_ : 0;
    
    auto it = peer_state.recent_nonces.begin();
    while (it != peer_state.recent_nonces.end() && *it < cutoff) {
        it = peer_state.recent_nonces.erase(it);
    }
    
    return true;
}

void NonceManager::reset_peer_nonces(std::uint32_t peer_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->peer_states.erase(peer_id);
}

}