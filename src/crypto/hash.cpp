#include "hypershare/crypto/hash.hpp"
#include "hypershare/core/logger.hpp"
#include <sodium.h>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace hypershare::crypto {

struct Blake3Hasher::Impl {
    crypto_generichash_state state;
    bool has_key;
    Blake3Key key;
};

Blake3Hasher::Blake3Hasher() 
    : impl_(std::make_unique<Impl>())
    , initialized_(false) {
    impl_->has_key = false;
}

Blake3Hasher::~Blake3Hasher() = default;

CryptoResult Blake3Hasher::initialize(const Blake3Key* key) {
    if (key) {
        impl_->key = *key;
        impl_->has_key = true;
        
        if (crypto_generichash_init(&impl_->state, impl_->key.data(), impl_->key.size(), BLAKE3_HASH_SIZE) != 0) {
            return CryptoResult(CryptoError::KEY_GENERATION_FAILED, "Failed to initialize keyed Blake3 hasher");
        }
    } else {
        impl_->has_key = false;
        
        if (crypto_generichash_init(&impl_->state, nullptr, 0, BLAKE3_HASH_SIZE) != 0) {
            return CryptoResult(CryptoError::KEY_GENERATION_FAILED, "Failed to initialize Blake3 hasher");
        }
    }
    
    initialized_ = true;
    return CryptoResult();
}

CryptoResult Blake3Hasher::update(std::span<const std::uint8_t> data) {
    if (!initialized_) {
        return CryptoResult(CryptoError::INVALID_KEY, "Hasher not initialized");
    }
    
    if (crypto_generichash_update(&impl_->state, data.data(), data.size()) != 0) {
        return CryptoResult(CryptoError::VERIFICATION_FAILED, "Failed to update hash");
    }
    
    return CryptoResult();
}

CryptoResult Blake3Hasher::finalize(std::span<std::uint8_t> output) {
    if (!initialized_) {
        return CryptoResult(CryptoError::INVALID_KEY, "Hasher not initialized");
    }
    
    if (output.size() < BLAKE3_HASH_SIZE) {
        return CryptoResult(CryptoError::BUFFER_TOO_SMALL, "Output buffer too small");
    }
    
    if (crypto_generichash_final(&impl_->state, output.data(), BLAKE3_HASH_SIZE) != 0) {
        return CryptoResult(CryptoError::VERIFICATION_FAILED, "Failed to finalize hash");
    }
    
    initialized_ = false; // Hasher is consumed
    return CryptoResult();
}

Blake3Hash Blake3Hasher::finalize() {
    Blake3Hash result;
    auto crypto_result = finalize(std::span(result));
    if (!crypto_result.success()) {
        throw std::runtime_error("Failed to finalize hash: " + crypto_result.message);
    }
    return result;
}

void Blake3Hasher::reset() {
    initialized_ = false;
    auto result = initialize(impl_->has_key ? &impl_->key : nullptr);
    if (!result.success()) {
        throw std::runtime_error("Failed to reset hasher: " + result.message);
    }
}

Blake3Hash Blake3Hasher::hash(std::span<const std::uint8_t> data) {
    Blake3Hash result;
    crypto_generichash(result.data(), result.size(), data.data(), data.size(), nullptr, 0);
    return result;
}

Blake3Hash Blake3Hasher::hash_keyed(const Blake3Key& key, std::span<const std::uint8_t> data) {
    Blake3Hash result;
    crypto_generichash(result.data(), result.size(), data.data(), data.size(), key.data(), key.size());
    return result;
}

Blake3Hash Blake3Hasher::hash_multiple(const std::vector<std::span<const std::uint8_t>>& data_spans) {
    Blake3Hasher hasher;
    hasher.initialize();
    
    for (const auto& span : data_spans) {
        auto result = hasher.update(span);
        if (!result.success()) {
            throw std::runtime_error("Failed to hash multiple spans: " + result.message);
        }
    }
    
    return hasher.finalize();
}

CryptoResult Blake3Hasher::hash_file(const std::filesystem::path& file_path, Blake3Hash& output) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return CryptoResult(CryptoError::VERIFICATION_FAILED, "Cannot open file for hashing");
    }
    
    Blake3Hasher hasher;
    auto result = hasher.initialize();
    if (!result.success()) {
        return result;
    }
    
    constexpr size_t buffer_size = 65536; // 64KB buffer
    std::vector<std::uint8_t> buffer(buffer_size);
    
    while (file.good()) {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer_size);
        size_t bytes_read = static_cast<size_t>(file.gcount());
        
        if (bytes_read > 0) {
            result = hasher.update(std::span(buffer.data(), bytes_read));
            if (!result.success()) {
                return result;
            }
        }
    }
    
    result = hasher.finalize(std::span(output));
    return result;
}

namespace hash_utils {

Blake3Hash hash_string(const std::string& str) {
    std::span<const std::uint8_t> data(reinterpret_cast<const std::uint8_t*>(str.data()), str.size());
    return Blake3Hasher::hash(data);
}

Blake3Hash hash_with_context(std::span<const std::uint8_t> data, const std::string& context) {
    Blake3Hasher hasher;
    hasher.initialize();
    
    // Hash context first
    std::span<const std::uint8_t> context_data(reinterpret_cast<const std::uint8_t*>(context.data()), context.size());
    hasher.update(context_data);
    
    // Then hash the actual data
    hasher.update(data);
    
    return hasher.finalize();
}

bool verify_hash(std::span<const std::uint8_t> data, const Blake3Hash& expected_hash) {
    auto computed_hash = Blake3Hasher::hash(data);
    return computed_hash == expected_hash;
}

std::string hash_to_hex(const Blake3Hash& hash) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto byte : hash) {
        oss << std::setw(2) << static_cast<unsigned>(byte);
    }
    return oss.str();
}

std::optional<Blake3Hash> hash_from_hex(const std::string& hex_string) {
    if (hex_string.length() != BLAKE3_HASH_SIZE * 2) {
        return std::nullopt;
    }
    
    Blake3Hash hash;
    for (size_t i = 0; i < BLAKE3_HASH_SIZE; ++i) {
        std::string byte_str = hex_string.substr(i * 2, 2);
        try {
            hash[i] = static_cast<std::uint8_t>(std::stoul(byte_str, nullptr, 16));
        } catch (...) {
            return std::nullopt;
        }
    }
    
    return hash;
}

Blake3Hash derive_hash(const Blake3Hash& parent_hash, std::span<const std::uint8_t> context) {
    Blake3Hasher hasher;
    hasher.initialize();
    
    // Hash parent first, then context
    hasher.update(std::span(parent_hash));
    hasher.update(context);
    
    return hasher.finalize();
}

}

}