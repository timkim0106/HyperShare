#pragma once

#include <array>
#include <vector>
#include <span>
#include <cstdint>

namespace hypershare::crypto {

// Key sizes for different algorithms
constexpr size_t ED25519_PUBLIC_KEY_SIZE = 32;
constexpr size_t ED25519_SECRET_KEY_SIZE = 32;
constexpr size_t ED25519_SIGNATURE_SIZE = 64;

constexpr size_t X25519_PUBLIC_KEY_SIZE = 32;
constexpr size_t X25519_SECRET_KEY_SIZE = 32;

constexpr size_t CHACHA20_KEY_SIZE = 32;
constexpr size_t CHACHA20_NONCE_SIZE = 12;

constexpr size_t POLY1305_TAG_SIZE = 16;
constexpr size_t AEAD_TAG_SIZE = POLY1305_TAG_SIZE;

constexpr size_t BLAKE3_HASH_SIZE = 32;
constexpr size_t BLAKE3_KEY_SIZE = 32;

constexpr size_t RANDOM_SEED_SIZE = 32;

// Type aliases for different key types
using Ed25519PublicKey = std::array<std::uint8_t, ED25519_PUBLIC_KEY_SIZE>;
using Ed25519SecretKey = std::array<std::uint8_t, ED25519_SECRET_KEY_SIZE>;
using Ed25519Signature = std::array<std::uint8_t, ED25519_SIGNATURE_SIZE>;

using X25519PublicKey = std::array<std::uint8_t, X25519_PUBLIC_KEY_SIZE>;
using X25519SecretKey = std::array<std::uint8_t, X25519_SECRET_KEY_SIZE>;

using ChaCha20Key = std::array<std::uint8_t, CHACHA20_KEY_SIZE>;
using ChaCha20Nonce = std::array<std::uint8_t, CHACHA20_NONCE_SIZE>;

using Blake3Hash = std::array<std::uint8_t, BLAKE3_HASH_SIZE>;
using Blake3Key = std::array<std::uint8_t, BLAKE3_KEY_SIZE>;

using AeadTag = std::array<std::uint8_t, AEAD_TAG_SIZE>;

// Secure memory utilities
struct SecureBytes {
    std::vector<std::uint8_t> data;
    
    SecureBytes() = default;
    explicit SecureBytes(size_t size);
    SecureBytes(const std::vector<std::uint8_t>& bytes);
    SecureBytes(std::span<const std::uint8_t> bytes);
    
    ~SecureBytes();
    
    // Disable copy to prevent key material leakage
    SecureBytes(const SecureBytes&) = delete;
    SecureBytes& operator=(const SecureBytes&) = delete;
    
    // Allow move
    SecureBytes(SecureBytes&& other) noexcept;
    SecureBytes& operator=(SecureBytes&& other) noexcept;
    
    std::uint8_t* data_ptr() { return data.data(); }
    const std::uint8_t* data_ptr() const { return data.data(); }
    size_t size() const { return data.size(); }
    bool empty() const { return data.empty(); }
    
    std::span<std::uint8_t> span() { return std::span(data); }
    std::span<const std::uint8_t> span() const { return std::span(data); }
    
    void clear();
    void resize(size_t new_size);
};

// Error types for crypto operations
enum class CryptoError {
    SUCCESS = 0,
    INVALID_KEY,
    INVALID_SIGNATURE,
    ENCRYPTION_FAILED,
    DECRYPTION_FAILED,
    KEY_GENERATION_FAILED,
    INVALID_NONCE,
    BUFFER_TOO_SMALL,
    VERIFICATION_FAILED,
    RANDOM_GENERATION_FAILED,
    INVALID_STATE
};

struct CryptoResult {
    CryptoError error;
    std::string message;
    
    CryptoResult(CryptoError err = CryptoError::SUCCESS, std::string msg = "")
        : error(err), message(std::move(msg)) {}
    
    bool success() const { return error == CryptoError::SUCCESS; }
    operator bool() const { return success(); }
};

}