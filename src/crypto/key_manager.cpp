#include "hypershare/crypto/key_manager.hpp"
#include "hypershare/crypto/random.hpp"
#include "hypershare/core/logger.hpp"
#include <sodium.h>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace hypershare::crypto {

struct KeyManager::Impl {
    // HKDF implementation using BLAKE3
    static CryptoResult hkdf_blake3(
        std::span<const std::uint8_t> input_key_material,
        std::span<const std::uint8_t> salt,
        std::span<const std::uint8_t> info,
        std::span<std::uint8_t> output_key
    ) {
        // HKDF-Extract: PRK = HMAC-Hash(salt, IKM)
        crypto_auth_hmacsha256_state extract_state;
        std::array<std::uint8_t, crypto_auth_hmacsha256_BYTES> prk;
        
        crypto_auth_hmacsha256_init(&extract_state, salt.data(), salt.size());
        crypto_auth_hmacsha256_update(&extract_state, input_key_material.data(), input_key_material.size());
        crypto_auth_hmacsha256_final(&extract_state, prk.data());
        
        // HKDF-Expand: OKM = HMAC-Hash(PRK, info || 0x01)
        crypto_auth_hmacsha256_state expand_state;
        crypto_auth_hmacsha256_init(&expand_state, prk.data(), prk.size());
        crypto_auth_hmacsha256_update(&expand_state, info.data(), info.size());
        
        std::uint8_t counter = 0x01;
        crypto_auth_hmacsha256_update(&expand_state, &counter, 1);
        
        std::array<std::uint8_t, crypto_auth_hmacsha256_BYTES> expanded;
        crypto_auth_hmacsha256_final(&expand_state, expanded.data());
        
        // Copy required bytes to output
        size_t copy_size = std::min(output_key.size(), expanded.size());
        std::copy(expanded.begin(), expanded.begin() + copy_size, output_key.begin());
        
        // Clear sensitive data
        sodium_memzero(prk.data(), prk.size());
        sodium_memzero(expanded.data(), expanded.size());
        
        return CryptoResult();
    }
};

KeyManager::KeyManager() 
    : impl_(std::make_unique<Impl>())
    , initialized_(false)
    , has_identity_keys_(false) {
}

KeyManager::~KeyManager() {
    cleanup();
}

bool KeyManager::initialize(const std::optional<std::filesystem::path>& key_storage_path) {
    if (initialized_) {
        return true;
    }
    
    if (!SecureRandom::initialize()) {
        LOG_ERROR("Failed to initialize secure random generator");
        return false;
    }
    
    storage_path_ = key_storage_path;
    
    // Try to load existing identity keys if storage path is provided
    if (storage_path_) {
        auto key_file = *storage_path_ / "identity.key";
        if (std::filesystem::exists(key_file)) {
            auto result = load_identity_keys(key_file);
            if (result.success()) {
                LOG_INFO("Loaded existing identity keys from {}", key_file.string());
            } else {
                LOG_WARN("Failed to load identity keys: {}", result.message);
            }
        }
    }
    
    // Generate new identity keys if we don't have them
    if (!has_identity_keys_) {
        auto result = generate_identity_keys();
        if (!result.success()) {
            LOG_ERROR("Failed to generate identity keys: {}", result.message);
            return false;
        }
        
        // Save keys if storage path is provided
        if (storage_path_) {
            std::filesystem::create_directories(*storage_path_);
            auto key_file = *storage_path_ / "identity.key";
            auto save_result = save_identity_keys(key_file);
            if (save_result.success()) {
                LOG_INFO("Saved identity keys to {}", key_file.string());
            } else {
                LOG_WARN("Failed to save identity keys: {}", save_result.message);
            }
        }
    }
    
    initialized_ = true;
    LOG_INFO("Key manager initialized with peer ID: {}", get_peer_id());
    return true;
}

void KeyManager::cleanup() {
    if (initialized_) {
        // Clear sensitive key material
        sodium_memzero(identity_keys_.secret_key.data(), identity_keys_.secret_key.size());
        has_identity_keys_ = false;
        initialized_ = false;
    }
}

CryptoResult KeyManager::generate_identity_keys() {
    crypto_sign_keypair(identity_keys_.public_key.data(), identity_keys_.secret_key.data());
    has_identity_keys_ = true;
    
    LOG_DEBUG("Generated new Ed25519 identity key pair");
    return CryptoResult();
}

bool KeyManager::has_identity_keys() const {
    return has_identity_keys_;
}

const Ed25519KeyPair& KeyManager::get_identity_keys() const {
    if (!has_identity_keys_) {
        throw std::runtime_error("No identity keys available");
    }
    return identity_keys_;
}

CryptoResult KeyManager::load_identity_keys(const std::filesystem::path& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return CryptoResult(CryptoError::INVALID_KEY, "Cannot open key file");
    }
    
    // Read public key
    file.read(reinterpret_cast<char*>(identity_keys_.public_key.data()), 
              identity_keys_.public_key.size());
    
    // Read secret key  
    file.read(reinterpret_cast<char*>(identity_keys_.secret_key.data()),
              identity_keys_.secret_key.size());
    
    if (!file.good()) {
        return CryptoResult(CryptoError::INVALID_KEY, "Invalid key file format");
    }
    
    // Verify key pair consistency
    Ed25519PublicKey derived_public;
    crypto_sign_seed_keypair(derived_public.data(), nullptr, identity_keys_.secret_key.data());
    
    if (derived_public != identity_keys_.public_key) {
        return CryptoResult(CryptoError::INVALID_KEY, "Key pair consistency check failed");
    }
    
    has_identity_keys_ = true;
    return CryptoResult();
}

CryptoResult KeyManager::save_identity_keys(const std::filesystem::path& file_path) const {
    if (!has_identity_keys_) {
        return CryptoResult(CryptoError::INVALID_KEY, "No identity keys to save");
    }
    
    std::ofstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return CryptoResult(CryptoError::INVALID_KEY, "Cannot create key file");
    }
    
    // Write public key
    file.write(reinterpret_cast<const char*>(identity_keys_.public_key.data()),
               identity_keys_.public_key.size());
    
    // Write secret key
    file.write(reinterpret_cast<const char*>(identity_keys_.secret_key.data()),
               identity_keys_.secret_key.size());
    
    if (!file.good()) {
        return CryptoResult(CryptoError::INVALID_KEY, "Failed to write key file");
    }
    
    return CryptoResult();
}

X25519KeyPair KeyManager::generate_ephemeral_keys() const {
    X25519KeyPair keypair;
    crypto_box_keypair(keypair.public_key.data(), keypair.secret_key.data());
    return keypair;
}

CryptoResult KeyManager::derive_key(
    std::span<const std::uint8_t> input_key_material,
    std::span<const std::uint8_t> salt,
    std::span<const std::uint8_t> info,
    std::span<std::uint8_t> output_key
) const {
    return Impl::hkdf_blake3(input_key_material, salt, info, output_key);
}

ChaCha20Key KeyManager::derive_encryption_key(
    std::span<const std::uint8_t> shared_secret,
    std::span<const std::uint8_t> context
) const {
    ChaCha20Key key;
    std::string salt_str = "hypershare_encrypt";
    std::span<const std::uint8_t> salt(reinterpret_cast<const std::uint8_t*>(salt_str.data()), salt_str.size());
    
    auto result = derive_key(shared_secret, salt, context, std::span(key));
    if (!result.success()) {
        throw std::runtime_error("Failed to derive encryption key: " + result.message);
    }
    
    return key;
}

Blake3Key KeyManager::derive_mac_key(
    std::span<const std::uint8_t> shared_secret,
    std::span<const std::uint8_t> context
) const {
    Blake3Key key;
    std::string salt_str = "hypershare_mac";
    std::span<const std::uint8_t> salt(reinterpret_cast<const std::uint8_t*>(salt_str.data()), salt_str.size());
    
    auto result = derive_key(shared_secret, salt, context, std::span(key));
    if (!result.success()) {
        throw std::runtime_error("Failed to derive MAC key: " + result.message);
    }
    
    return key;
}

KeyManager::SessionKeys KeyManager::derive_session_keys(
    const X25519SecretKey& our_secret,
    const X25519PublicKey& their_public,
    std::span<const std::uint8_t> context
) const {
    // Perform X25519 key exchange
    std::array<std::uint8_t, crypto_box_BEFORENMBYTES> shared_secret;
    crypto_box_beforenm(shared_secret.data(), their_public.data(), our_secret.data());
    
    // Derive session keys
    SessionKeys keys;
    keys.encryption_key = derive_encryption_key(shared_secret, context);
    keys.mac_key = derive_mac_key(shared_secret, context);
    keys.sequence_number = 0;
    keys.created_at = std::chrono::steady_clock::now();
    
    // Clear shared secret
    sodium_memzero(shared_secret.data(), shared_secret.size());
    
    return keys;
}

bool KeyManager::should_rotate_keys(const SessionKeys& keys, std::chrono::milliseconds max_age) const {
    auto age = std::chrono::steady_clock::now() - keys.created_at;
    return age >= max_age;
}

std::string KeyManager::public_key_to_string(const Ed25519PublicKey& key) const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto byte : key) {
        oss << std::setw(2) << static_cast<unsigned>(byte);
    }
    return oss.str();
}

std::optional<Ed25519PublicKey> KeyManager::public_key_from_string(const std::string& str) const {
    if (str.length() != ED25519_PUBLIC_KEY_SIZE * 2) {
        return std::nullopt;
    }
    
    Ed25519PublicKey key;
    for (size_t i = 0; i < ED25519_PUBLIC_KEY_SIZE; ++i) {
        std::string byte_str = str.substr(i * 2, 2);
        try {
            key[i] = static_cast<std::uint8_t>(std::stoul(byte_str, nullptr, 16));
        } catch (...) {
            return std::nullopt;
        }
    }
    
    return key;
}

std::string KeyManager::get_peer_id() const {
    if (!has_identity_keys_) {
        return "unknown";
    }
    
    // Use first 8 bytes of public key as peer ID
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < 8; ++i) {
        oss << std::setw(2) << static_cast<unsigned>(identity_keys_.public_key[i]);
    }
    
    return oss.str();
}

}