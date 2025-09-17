#include "hypershare/crypto/crypto_types.hpp"
#include <algorithm>
#include <cstring>

// Include libsodium for secure memory management
#include <sodium.h>

namespace hypershare::crypto {

SecureBytes::SecureBytes(size_t size) : data(size) {
    // Zero-initialize for security
    sodium_memzero(data.data(), data.size());
}

SecureBytes::SecureBytes(const std::vector<std::uint8_t>& bytes) : data(bytes) {}

SecureBytes::SecureBytes(std::span<const std::uint8_t> bytes) 
    : data(bytes.begin(), bytes.end()) {}

SecureBytes::~SecureBytes() {
    clear();
}

SecureBytes::SecureBytes(SecureBytes&& other) noexcept 
    : data(std::move(other.data)) {
    // Other object is now empty, no need to clear
}

SecureBytes& SecureBytes::operator=(SecureBytes&& other) noexcept {
    if (this != &other) {
        clear(); // Clear our current data
        data = std::move(other.data);
    }
    return *this;
}

void SecureBytes::clear() {
    if (!data.empty()) {
        sodium_memzero(data.data(), data.size());
        data.clear();
    }
}

void SecureBytes::resize(size_t new_size) {
    size_t old_size = data.size();
    data.resize(new_size);
    
    // Zero-initialize new bytes
    if (new_size > old_size) {
        sodium_memzero(data.data() + old_size, new_size - old_size);
    }
}

}