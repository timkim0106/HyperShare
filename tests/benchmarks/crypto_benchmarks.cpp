#include <benchmark/benchmark.h>
#include "hypershare/crypto/secure_handshake.hpp"
#include "hypershare/crypto/key_manager.hpp"
#include "hypershare/crypto/encryption.hpp"
#include "hypershare/crypto/signature.hpp"
#include "hypershare/crypto/hash.hpp"
#include "hypershare/crypto/random.hpp"
#include <memory>
#include <vector>
#include <string>

using namespace hypershare::crypto;

// Global setup for benchmarks
class CryptoBenchmarkFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State& state) override {
        key_manager1_ = std::make_shared<KeyManager>();
        key_manager2_ = std::make_shared<KeyManager>();
        
        // Generate identity keys
        key_manager1_->generate_identity_keys();
        key_manager2_->generate_identity_keys();
        
        handshake1_ = std::make_unique<SecureHandshake>(key_manager1_);
        handshake2_ = std::make_unique<SecureHandshake>(key_manager2_);
        
        // Setup session keys for encryption benchmarks
        setup_session_keys();
    }
    
    void TearDown(const ::benchmark::State& state) override {
        handshake1_.reset();
        handshake2_.reset();
    }
    
protected:
    void setup_session_keys() {
        SecureHandshakeMessage init_msg;
        handshake1_->initiate_handshake(1001, 8080, "peer1", 0x01, init_msg);
        
        SecureHandshakeAckMessage ack_msg;
        handshake2_->respond_to_handshake(init_msg, 1002, ack_msg);
        
        handshake1_->complete_handshake(ack_msg, session_keys1_);
        handshake2_->derive_server_session_keys(init_msg, session_keys2_);
    }
    
    std::shared_ptr<KeyManager> key_manager1_;
    std::shared_ptr<KeyManager> key_manager2_;
    std::unique_ptr<SecureHandshake> handshake1_;
    std::unique_ptr<SecureHandshake> handshake2_;
    KeyManager::SessionKeys session_keys1_;
    KeyManager::SessionKeys session_keys2_;
};

// Benchmark key generation
BENCHMARK_F(CryptoBenchmarkFixture, KeyGeneration_Identity)(benchmark::State& state) {
    for (auto _ : state) {
        auto km = std::make_shared<KeyManager>();
        benchmark::DoNotOptimize(km->generate_identity_keys());
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(CryptoBenchmarkFixture, KeyGeneration_Ephemeral)(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(key_manager1_->generate_ephemeral_keys());
    }
    state.SetItemsProcessed(state.iterations());
}

// Benchmark complete handshake
BENCHMARK_F(CryptoBenchmarkFixture, CompleteHandshake)(benchmark::State& state) {
    for (auto _ : state) {
        handshake1_->reset();
        handshake2_->reset();
        
        SecureHandshakeMessage init_msg;
        handshake1_->initiate_handshake(1001, 8080, "peer1", 0x01, init_msg);
        
        SecureHandshakeAckMessage ack_msg;
        handshake2_->respond_to_handshake(init_msg, 1002, ack_msg);
        
        KeyManager::SessionKeys keys1, keys2;
        handshake1_->complete_handshake(ack_msg, keys1);
        handshake2_->derive_server_session_keys(init_msg, keys2);
    }
    state.SetItemsProcessed(state.iterations());
}

// Benchmark key rotation
BENCHMARK_F(CryptoBenchmarkFixture, KeyRotation)(benchmark::State& state) {
    for (auto _ : state) {
        KeyManager::SessionKeys old_keys = session_keys1_;
        KeyManager::SessionKeys new_keys1, new_keys2;
        
        benchmark::DoNotOptimize(handshake1_->initiate_key_rotation(old_keys, new_keys1));
    }
    state.SetItemsProcessed(state.iterations());
}

// Benchmark signature operations
BENCHMARK_F(CryptoBenchmarkFixture, Signature_Sign)(benchmark::State& state) {
    SignatureEngine sig_engine;
    auto identity_keys = key_manager1_->get_identity_keys();
    
    std::string test_data = "This is a test message for signature benchmarking";
    std::vector<uint8_t> data_bytes(test_data.begin(), test_data.end());
    
    for (auto _ : state) {
        Ed25519Signature signature;
        benchmark::DoNotOptimize(sig_engine.sign(data_bytes, identity_keys.secret_key, signature));
    }
    state.SetBytesProcessed(state.iterations() * test_data.size());
}

BENCHMARK_F(CryptoBenchmarkFixture, Signature_Verify)(benchmark::State& state) {
    SignatureEngine sig_engine;
    auto identity_keys = key_manager1_->get_identity_keys();
    
    std::string test_data = "This is a test message for signature benchmarking";
    std::vector<uint8_t> data_bytes(test_data.begin(), test_data.end());
    
    Ed25519Signature signature;
    sig_engine.sign(data_bytes, identity_keys.secret_key, signature);
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(sig_engine.verify(data_bytes, signature, identity_keys.public_key));
    }
    state.SetBytesProcessed(state.iterations() * test_data.size());
}

// Benchmark hashing
BENCHMARK_F(CryptoBenchmarkFixture, Hash_BLAKE3_Small)(benchmark::State& state) {
    std::string test_data = "Small test data for hashing";
    std::vector<uint8_t> data_bytes(test_data.begin(), test_data.end());
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(Blake3Hasher::hash(data_bytes));
    }
    state.SetBytesProcessed(state.iterations() * test_data.size());
}

BENCHMARK_F(CryptoBenchmarkFixture, Hash_BLAKE3_1KB)(benchmark::State& state) {
    std::vector<uint8_t> data_bytes(1024, 0x42);
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(Blake3Hasher::hash(data_bytes));
    }
    state.SetBytesProcessed(state.iterations() * 1024);
}

BENCHMARK_F(CryptoBenchmarkFixture, Hash_BLAKE3_64KB)(benchmark::State& state) {
    std::vector<uint8_t> data_bytes(65536, 0x42);
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(Blake3Hasher::hash(data_bytes));
    }
    state.SetBytesProcessed(state.iterations() * 65536);
}

// Benchmark encryption
BENCHMARK_F(CryptoBenchmarkFixture, Encryption_ChaCha20Poly1305_1KB)(benchmark::State& state) {
    EncryptionEngine encryption;
    std::vector<uint8_t> plaintext(1024, 0x42);
    std::vector<uint8_t> empty_ad;
    EncryptedMessage encrypted_msg;
    
    for (auto _ : state) {
        auto nonce = encryption.generate_nonce();
        benchmark::DoNotOptimize(encryption.encrypt(plaintext, empty_ad, session_keys1_.encryption_key, nonce, encrypted_msg));
    }
    state.SetBytesProcessed(state.iterations() * 1024);
}

BENCHMARK_F(CryptoBenchmarkFixture, Decryption_ChaCha20Poly1305_1KB)(benchmark::State& state) {
    EncryptionEngine encryption;
    std::vector<uint8_t> plaintext(1024, 0x42);
    std::vector<uint8_t> empty_ad, decrypted;
    EncryptedMessage encrypted_msg;
    
    // Pre-encrypt the data
    auto nonce = encryption.generate_nonce();
    encryption.encrypt(plaintext, empty_ad, session_keys1_.encryption_key, nonce, encrypted_msg);
    
    for (auto _ : state) {
        decrypted.clear();
        benchmark::DoNotOptimize(encryption.decrypt(encrypted_msg, empty_ad, session_keys1_.encryption_key, decrypted));
    }
    state.SetBytesProcessed(state.iterations() * 1024);
}

BENCHMARK_F(CryptoBenchmarkFixture, Encryption_ChaCha20Poly1305_64KB)(benchmark::State& state) {
    EncryptionEngine encryption;
    std::vector<uint8_t> plaintext(65536, 0x42);
    std::vector<uint8_t> empty_ad;
    EncryptedMessage encrypted_msg;
    
    for (auto _ : state) {
        auto nonce = encryption.generate_nonce();
        benchmark::DoNotOptimize(encryption.encrypt(plaintext, empty_ad, session_keys1_.encryption_key, nonce, encrypted_msg));
    }
    state.SetBytesProcessed(state.iterations() * 65536);
}

BENCHMARK_F(CryptoBenchmarkFixture, Decryption_ChaCha20Poly1305_64KB)(benchmark::State& state) {
    EncryptionEngine encryption;
    std::vector<uint8_t> plaintext(65536, 0x42);
    std::vector<uint8_t> empty_ad, decrypted;
    EncryptedMessage encrypted_msg;
    
    // Pre-encrypt the data
    auto nonce = encryption.generate_nonce();
    encryption.encrypt(plaintext, empty_ad, session_keys1_.encryption_key, nonce, encrypted_msg);
    
    for (auto _ : state) {
        decrypted.clear();
        benchmark::DoNotOptimize(encryption.decrypt(encrypted_msg, empty_ad, session_keys1_.encryption_key, decrypted));
    }
    state.SetBytesProcessed(state.iterations() * 65536);
}

// Benchmark random number generation
static void RandomGeneration_Uint64(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(SecureRandom::generate_uint64());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(RandomGeneration_Uint64);

static void RandomGeneration_Bytes_32(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<uint8_t> random_bytes(32);
        benchmark::DoNotOptimize(SecureRandom::generate_bytes(random_bytes));
    }
    state.SetBytesProcessed(state.iterations() * 32);
}
BENCHMARK(RandomGeneration_Bytes_32);

static void RandomGeneration_Bytes_1KB(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<uint8_t> random_bytes(1024);
        benchmark::DoNotOptimize(SecureRandom::generate_bytes(random_bytes));
    }
    state.SetBytesProcessed(state.iterations() * 1024);
}
BENCHMARK(RandomGeneration_Bytes_1KB);

// Benchmark message serialization
BENCHMARK_F(CryptoBenchmarkFixture, MessageSerialization_HandshakeMessage)(benchmark::State& state) {
    SecureHandshakeMessage msg;
    msg.peer_id = 12345;
    msg.listen_port = 8080;
    msg.peer_name = "BenchmarkPeer";
    msg.capabilities = 0xFF;
    msg.identity_public_key = key_manager1_->get_identity_keys().public_key;
    msg.ephemeral_public_key = key_manager1_->generate_ephemeral_keys().public_key;
    msg.nonce = 0x123456789ABCDEF0;
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(msg.serialize());
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(CryptoBenchmarkFixture, MessageDeserialization_HandshakeMessage)(benchmark::State& state) {
    SecureHandshakeMessage msg;
    msg.peer_id = 12345;
    msg.listen_port = 8080;
    msg.peer_name = "BenchmarkPeer";
    msg.capabilities = 0xFF;
    msg.identity_public_key = key_manager1_->get_identity_keys().public_key;
    msg.ephemeral_public_key = key_manager1_->generate_ephemeral_keys().public_key;
    msg.nonce = 0x123456789ABCDEF0;
    
    auto serialized = msg.serialize();
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(SecureHandshakeMessage::deserialize(serialized));
    }
    state.SetItemsProcessed(state.iterations());
}

// Benchmark key derivation
BENCHMARK_F(CryptoBenchmarkFixture, KeyDerivation_SessionKeys)(benchmark::State& state) {
    auto ephemeral_keys1 = key_manager1_->generate_ephemeral_keys();
    auto ephemeral_keys2 = key_manager2_->generate_ephemeral_keys();
    
    std::string context = "BENCHMARK_CONTEXT";
    std::vector<uint8_t> context_bytes(context.begin(), context.end());
    
    for (auto _ : state) {
        benchmark::DoNotOptimize(key_manager1_->derive_session_keys(
            ephemeral_keys1.secret_key,
            ephemeral_keys2.public_key,
            context_bytes
        ));
    }
    state.SetItemsProcessed(state.iterations());
}

// Performance targets and assertions
BENCHMARK_F(CryptoBenchmarkFixture, PerformanceTargets)(benchmark::State& state) {
    // This benchmark documents our performance targets
    auto start = std::chrono::high_resolution_clock::now();
    
    for (auto _ : state) {
        // Simulate a complete secure operation: handshake + key rotation + encryption
        handshake1_->reset();
        handshake2_->reset();
        
        // Handshake
        SecureHandshakeMessage init_msg;
        handshake1_->initiate_handshake(1001, 8080, "peer1", 0x01, init_msg);
        
        SecureHandshakeAckMessage ack_msg;
        handshake2_->respond_to_handshake(init_msg, 1002, ack_msg);
        
        KeyManager::SessionKeys keys1, keys2;
        handshake1_->complete_handshake(ack_msg, keys1);
        handshake2_->derive_server_session_keys(init_msg, keys2);
        
        // Key rotation
        KeyManager::SessionKeys new_keys1, new_keys2;
        handshake1_->initiate_key_rotation(keys1, new_keys1);
        
        // Encrypt 64KB (typical chunk size)
        EncryptionEngine encryption;
        std::vector<uint8_t> chunk_data(65536, 0x42);
        std::vector<uint8_t> empty_ad;
        EncryptedMessage encrypted_chunk;
        auto nonce = encryption.generate_nonce();
        encryption.encrypt(chunk_data, empty_ad, new_keys1.encryption_key, nonce, encrypted_chunk);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    state.SetItemsProcessed(state.iterations());
    state.counters["latency_us"] = benchmark::Counter(
        static_cast<double>(duration.count()) / state.iterations(),
        benchmark::Counter::kAvgIterations
    );
    
    // Target: Complete operation should take less than 10ms
    if (state.iterations() > 100) {  // Only check after warmup
        auto avg_latency_us = static_cast<double>(duration.count()) / state.iterations();
        if (avg_latency_us > 10000) {  // 10ms in microseconds
            state.SkipWithError("Performance target missed: operation took too long");
        }
    }
}

BENCHMARK_MAIN();