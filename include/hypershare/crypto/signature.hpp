#pragma once

#include "hypershare/crypto/crypto_types.hpp"
#include <span>
#include <vector>

namespace hypershare::crypto {

class SignatureEngine {
public:
    SignatureEngine();
    ~SignatureEngine();
    
    // Ed25519 signing
    CryptoResult sign(
        std::span<const std::uint8_t> message,
        const Ed25519SecretKey& secret_key,
        Ed25519Signature& out_signature
    ) const;
    
    // Ed25519 verification
    CryptoResult verify(
        std::span<const std::uint8_t> message,
        const Ed25519Signature& signature,
        const Ed25519PublicKey& public_key
    ) const;
    
    // Convenience methods for string messages
    CryptoResult sign_string(
        const std::string& message,
        const Ed25519SecretKey& secret_key,
        Ed25519Signature& out_signature
    ) const;
    
    CryptoResult verify_string(
        const std::string& message,
        const Ed25519Signature& signature,
        const Ed25519PublicKey& public_key
    ) const;
    
    // Combined message signing (for protocols that need message + context)
    CryptoResult sign_combined(
        std::span<const std::uint8_t> message,
        std::span<const std::uint8_t> context,
        const Ed25519SecretKey& secret_key,
        Ed25519Signature& out_signature
    ) const;
    
    CryptoResult verify_combined(
        std::span<const std::uint8_t> message,
        std::span<const std::uint8_t> context,
        const Ed25519Signature& signature,
        const Ed25519PublicKey& public_key
    ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}