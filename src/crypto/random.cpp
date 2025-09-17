#include "hypershare/crypto/random.hpp"
#include "hypershare/core/logger.hpp"
#include <sodium.h>

namespace hypershare::crypto {

bool SecureRandom::initialized_ = false;

bool SecureRandom::initialize() {
    if (initialized_) {
        return true;
    }
    
    if (sodium_init() < 0) {
        LOG_ERROR("Failed to initialize libsodium");
        return false;
    }
    
    initialized_ = true;
    LOG_INFO("Cryptographic random number generator initialized");
    return true;
}

void SecureRandom::cleanup() {
    // libsodium doesn't require explicit cleanup
    initialized_ = false;
}

CryptoResult SecureRandom::generate_bytes(std::span<std::uint8_t> output) {
    if (!initialized_) {
        return CryptoResult(CryptoError::RANDOM_GENERATION_FAILED, "Random generator not initialized");
    }
    
    if (output.empty()) {
        return CryptoResult(CryptoError::BUFFER_TOO_SMALL, "Output buffer is empty");
    }
    
    randombytes_buf(output.data(), output.size());
    return CryptoResult();
}

CryptoResult SecureRandom::generate_bytes(std::vector<std::uint8_t>& output) {
    return generate_bytes(std::span(output));
}

SecureBytes SecureRandom::generate_bytes(size_t count) {
    SecureBytes result(count);
    auto crypto_result = generate_bytes(result.span());
    if (!crypto_result.success()) {
        throw std::runtime_error("Failed to generate random bytes: " + crypto_result.message);
    }
    return result;
}

std::uint32_t SecureRandom::generate_uint32() {
    return randombytes_random();
}

std::uint64_t SecureRandom::generate_uint64() {
    std::uint64_t high = randombytes_random();
    std::uint64_t low = randombytes_random();
    return (high << 32) | low;
}

std::uint32_t SecureRandom::generate_uniform(std::uint32_t upper_bound) {
    return randombytes_uniform(upper_bound);
}

}