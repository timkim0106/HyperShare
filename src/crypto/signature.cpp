#include "hypershare/crypto/signature.hpp"
#include <sodium.h>
#include <stdexcept>

namespace hypershare::crypto {

struct SignatureEngine::Impl {
    bool initialized = false;
    
    Impl() {
        if (sodium_init() < 0) {
            throw std::runtime_error("Failed to initialize libsodium");
        }
        initialized = true;
    }
};

SignatureEngine::SignatureEngine() 
    : impl_(std::make_unique<Impl>()) {
}

SignatureEngine::~SignatureEngine() = default;

CryptoResult SignatureEngine::sign(
    std::span<const std::uint8_t> message,
    const Ed25519SecretKey& secret_key,
    Ed25519Signature& out_signature) const {
    
    if (!impl_->initialized) {
        return CryptoResult(CryptoError::KEY_GENERATION_FAILED, "Signature engine not initialized");
    }
    
    if (message.empty()) {
        return CryptoResult(CryptoError::INVALID_KEY, "Message cannot be empty");
    }
    
    unsigned long long signature_len;
    
    int result = crypto_sign_detached(
        out_signature.data(),
        &signature_len,
        message.data(),
        message.size(),
        secret_key.data()
    );
    
    if (result != 0 || signature_len != ED25519_SIGNATURE_SIZE) {
        return CryptoResult(CryptoError::ENCRYPTION_FAILED, "Failed to create signature");
    }
    
    return CryptoResult();
}

CryptoResult SignatureEngine::verify(
    std::span<const std::uint8_t> message,
    const Ed25519Signature& signature,
    const Ed25519PublicKey& public_key) const {
    
    if (!impl_->initialized) {
        return CryptoResult(CryptoError::KEY_GENERATION_FAILED, "Signature engine not initialized");
    }
    
    if (message.empty()) {
        return CryptoResult(CryptoError::INVALID_KEY, "Message cannot be empty");
    }
    
    int result = crypto_sign_verify_detached(
        signature.data(),
        message.data(),
        message.size(),
        public_key.data()
    );
    
    if (result != 0) {
        return CryptoResult(CryptoError::VERIFICATION_FAILED, "Signature verification failed");
    }
    
    return CryptoResult();
}

CryptoResult SignatureEngine::sign_string(
    const std::string& message,
    const Ed25519SecretKey& secret_key,
    Ed25519Signature& out_signature) const {
    
    return sign(
        std::span(reinterpret_cast<const std::uint8_t*>(message.data()), message.size()),
        secret_key,
        out_signature
    );
}

CryptoResult SignatureEngine::verify_string(
    const std::string& message,
    const Ed25519Signature& signature,
    const Ed25519PublicKey& public_key) const {
    
    return verify(
        std::span(reinterpret_cast<const std::uint8_t*>(message.data()), message.size()),
        signature,
        public_key
    );
}

CryptoResult SignatureEngine::sign_combined(
    std::span<const std::uint8_t> message,
    std::span<const std::uint8_t> context,
    const Ed25519SecretKey& secret_key,
    Ed25519Signature& out_signature) const {
    
    // Combine message and context
    std::vector<std::uint8_t> combined;
    combined.reserve(message.size() + context.size());
    combined.insert(combined.end(), message.begin(), message.end());
    combined.insert(combined.end(), context.begin(), context.end());
    
    return sign(combined, secret_key, out_signature);
}

CryptoResult SignatureEngine::verify_combined(
    std::span<const std::uint8_t> message,
    std::span<const std::uint8_t> context,
    const Ed25519Signature& signature,
    const Ed25519PublicKey& public_key) const {
    
    // Combine message and context
    std::vector<std::uint8_t> combined;
    combined.reserve(message.size() + context.size());
    combined.insert(combined.end(), message.begin(), message.end());
    combined.insert(combined.end(), context.begin(), context.end());
    
    return verify(combined, signature, public_key);
}

}