#include <gtest/gtest.h>
#include "hypershare/crypto/secure_handshake.hpp"
#include "hypershare/crypto/key_manager.hpp"
#include "hypershare/crypto/encryption.hpp"
#include "hypershare/crypto/signature.hpp"
#include "hypershare/crypto/hash.hpp"
#include "hypershare/crypto/random.hpp"
#include <chrono>
#include <thread>
#include <vector>
#include <memory>

using namespace hypershare::crypto;

class SecurityComprehensiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        key_manager1_ = std::make_shared<KeyManager>();
        key_manager2_ = std::make_shared<KeyManager>();
        
        // Generate identity keys for both peers
        ASSERT_TRUE(key_manager1_->generate_identity_keys().success());
        ASSERT_TRUE(key_manager2_->generate_identity_keys().success());
        
        handshake1_ = std::make_unique<SecureHandshake>(key_manager1_);
        handshake2_ = std::make_unique<SecureHandshake>(key_manager2_);
    }
    
    void TearDown() override {
        handshake1_.reset();
        handshake2_.reset();
    }
    
    // Helper function to perform complete handshake
    CryptoResult perform_handshake(KeyManager::SessionKeys& keys1, KeyManager::SessionKeys& keys2) {
        SecureHandshakeMessage init_msg;
        auto result = handshake1_->initiate_handshake(1001, 8080, "peer1", 0x01, init_msg);
        if (!result.success()) return result;
        
        SecureHandshakeAckMessage ack_msg;
        result = handshake2_->respond_to_handshake(init_msg, 1002, ack_msg);
        if (!result.success()) return result;
        
        result = handshake1_->complete_handshake(ack_msg, keys1);
        if (!result.success()) return result;
        
        return handshake2_->derive_server_session_keys(init_msg, keys2);
    }
    
    std::shared_ptr<KeyManager> key_manager1_;
    std::shared_ptr<KeyManager> key_manager2_;
    std::unique_ptr<SecureHandshake> handshake1_;
    std::unique_ptr<SecureHandshake> handshake2_;
};

// Test forward secrecy - old session keys cannot decrypt new messages
TEST_F(SecurityComprehensiveTest, ForwardSecrecy) {
    KeyManager::SessionKeys initial_keys1, initial_keys2;
    ASSERT_TRUE(perform_handshake(initial_keys1, initial_keys2).success());
    
    // Encrypt a message with initial keys
    std::string plaintext = "Secret message before rotation";
    std::vector<uint8_t> plaintext_bytes(plaintext.begin(), plaintext.end());
    
    EncryptionEngine encryption;
    EncryptedMessage encrypted_msg;
    auto nonce = encryption.generate_nonce();
    std::vector<uint8_t> empty_ad;
    auto encrypt_result = encryption.encrypt(plaintext_bytes, empty_ad, initial_keys1.encryption_key, nonce, encrypted_msg);
    ASSERT_TRUE(encrypt_result.success());
    
    // Perform key rotation
    KeyManager::SessionKeys new_keys1, new_keys2;
    auto rotation_result = handshake1_->initiate_key_rotation(initial_keys1, new_keys1);
    ASSERT_TRUE(rotation_result.success());
    
    // Try to decrypt with new keys (should fail - demonstrating forward secrecy)
    std::vector<uint8_t> decrypted;
    auto decrypt_result = encryption.decrypt(encrypted_msg, empty_ad, new_keys1.encryption_key, decrypted);
    EXPECT_FALSE(decrypt_result.success());
    
    // Decrypt with original keys should still work
    decrypt_result = encryption.decrypt(encrypted_msg, empty_ad, initial_keys1.encryption_key, decrypted);
    ASSERT_TRUE(decrypt_result.success());
    EXPECT_EQ(plaintext_bytes, decrypted);
}

// Test key rotation triggers
TEST_F(SecurityComprehensiveTest, KeyRotationTriggers) {
    auto now = std::chrono::steady_clock::now();
    
    // Should not rotate with no data and recent keys
    EXPECT_FALSE(handshake1_->should_rotate_keys(now, 0));
    
    // Should rotate with large data transfer
    EXPECT_TRUE(handshake1_->should_rotate_keys(now, KEY_ROTATION_BYTES_THRESHOLD + 1));
    
    // Should rotate after time threshold
    auto old_time = now - KEY_ROTATION_TIME_THRESHOLD - std::chrono::minutes(1);
    EXPECT_TRUE(handshake1_->should_rotate_keys(old_time, 0));
    
    // Should force rotate after max time
    auto very_old_time = now - KEY_ROTATION_MAX_TIME - std::chrono::minutes(1);
    EXPECT_TRUE(handshake1_->should_rotate_keys(very_old_time, 0));
}

// Test replay attack protection
TEST_F(SecurityComprehensiveTest, ReplayAttackProtection) {
    KeyManager::SessionKeys keys1, keys2;
    ASSERT_TRUE(perform_handshake(keys1, keys2).success());
    
    // Create a key rotation message
    KeyManager::SessionKeys new_keys1, new_keys2;
    auto rotation_result = handshake1_->initiate_key_rotation(keys1, new_keys1);
    ASSERT_TRUE(rotation_result.success());
    
    // Create a rotation message with old timestamp
    KeyRotationMessage old_msg;
    old_msg.rotation_id = 1;
    old_msg.new_ephemeral_public_key = key_manager1_->generate_ephemeral_keys().public_key;
    old_msg.nonce = SecureRandom::generate_uint64();
    
    // Set timestamp to 10 minutes ago (beyond 5-minute window)
    auto old_timestamp = std::chrono::steady_clock::now() - std::chrono::minutes(10);
    old_msg.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        old_timestamp.time_since_epoch()).count();
    
    SignatureEngine sig_engine;
    auto signature_data = create_signature_data("KEY_ROTATION", old_msg.serialize_for_signature());
    ASSERT_TRUE(sig_engine.sign(signature_data, key_manager1_->get_identity_keys().secret_key, old_msg.signature).success());
    
    // Should reject old message
    KeyManager::SessionKeys rejected_keys1, rejected_keys2;
    auto replay_result = handshake2_->respond_to_key_rotation(old_msg, keys2, rejected_keys2);
    EXPECT_FALSE(replay_result.success());
    EXPECT_EQ(replay_result.error, CryptoError::VERIFICATION_FAILED);
}

// Test cryptographic primitive security properties
TEST_F(SecurityComprehensiveTest, CryptographicPrimitives) {
    // Test signature determinism and verification
    SignatureEngine sig_engine;
    auto identity_keys = key_manager1_->get_identity_keys();
    
    std::string test_data = "Test signature data";
    std::vector<uint8_t> data_bytes(test_data.begin(), test_data.end());
    
    Ed25519Signature sig1, sig2;
    ASSERT_TRUE(sig_engine.sign(data_bytes, identity_keys.secret_key, sig1).success());
    ASSERT_TRUE(sig_engine.sign(data_bytes, identity_keys.secret_key, sig2).success());
    
    // Signatures might be different due to randomness, but both should verify
    ASSERT_TRUE(sig_engine.verify(data_bytes, sig1, identity_keys.public_key).success());
    ASSERT_TRUE(sig_engine.verify(data_bytes, sig2, identity_keys.public_key).success());
    
    // Test hash consistency
    Blake3Hasher hasher;
    auto hash1 = hasher.hash(data_bytes);
    auto hash2 = hasher.hash(data_bytes);
    EXPECT_EQ(hash1, hash2);
    
    // Test encryption/decryption round-trip
    EncryptionEngine encryption;
    auto session_keys = key_manager1_->derive_session_keys(
        key_manager1_->generate_ephemeral_keys().secret_key,
        key_manager2_->generate_ephemeral_keys().public_key,
        data_bytes
    );
    
    EncryptedMessage encrypted_msg;
    auto nonce = encryption.generate_nonce();
    std::vector<uint8_t> empty_ad, decrypted;
    ASSERT_TRUE(encryption.encrypt(data_bytes, empty_ad, session_keys.encryption_key, nonce, encrypted_msg).success());
    ASSERT_TRUE(encryption.decrypt(encrypted_msg, empty_ad, session_keys.encryption_key, decrypted).success());
    EXPECT_EQ(data_bytes, decrypted);
}

// Test peer fingerprint consistency and uniqueness
TEST_F(SecurityComprehensiveTest, PeerFingerprints) {
    auto fp1_a = handshake1_->get_peer_fingerprint(key_manager1_->get_identity_keys().public_key);
    auto fp1_b = handshake1_->get_peer_fingerprint(key_manager1_->get_identity_keys().public_key);
    auto fp2 = handshake1_->get_peer_fingerprint(key_manager2_->get_identity_keys().public_key);
    
    // Same key should produce same fingerprint
    EXPECT_EQ(fp1_a, fp1_b);
    
    // Different keys should produce different fingerprints
    EXPECT_NE(fp1_a, fp2);
    
    // Fingerprint should be in expected format (8 hex pairs separated by colons)
    EXPECT_EQ(fp1_a.length(), 23); // "xx:xx:xx:xx:xx:xx:xx:xx"
    EXPECT_EQ(std::count(fp1_a.begin(), fp1_a.end(), ':'), 7);
}

// Test trusted peer management
TEST_F(SecurityComprehensiveTest, TrustedPeerManagement) {
    auto peer_key = key_manager2_->get_identity_keys().public_key;
    
    // Initially not trusted
    EXPECT_FALSE(handshake1_->is_trusted_peer(peer_key));
    
    // Add to trusted peers
    handshake1_->add_trusted_peer(peer_key, "Test Peer");
    EXPECT_TRUE(handshake1_->is_trusted_peer(peer_key));
    
    // Different key should not be trusted
    auto other_key = key_manager1_->generate_ephemeral_keys().public_key;
    Ed25519PublicKey other_identity_key;
    std::copy(other_key.begin(), other_key.end(), other_identity_key.begin());
    EXPECT_FALSE(handshake1_->is_trusted_peer(other_identity_key));
}

// Test message serialization/deserialization
TEST_F(SecurityComprehensiveTest, MessageSerialization) {
    // Test SecureHandshakeMessage
    SecureHandshakeMessage original_msg;
    original_msg.peer_id = 12345;
    original_msg.listen_port = 8080;
    original_msg.peer_name = "TestPeer";
    original_msg.capabilities = 0xFF;
    original_msg.identity_public_key = key_manager1_->get_identity_keys().public_key;
    original_msg.ephemeral_public_key = key_manager1_->generate_ephemeral_keys().public_key;
    original_msg.nonce = 0x123456789ABCDEF0;
    
    // Fill signature with test data
    std::fill(original_msg.signature.begin(), original_msg.signature.end(), 0xAB);
    
    auto serialized = original_msg.serialize();
    auto deserialized = SecureHandshakeMessage::deserialize(serialized);
    
    EXPECT_EQ(original_msg.peer_id, deserialized.peer_id);
    EXPECT_EQ(original_msg.listen_port, deserialized.listen_port);
    EXPECT_EQ(original_msg.peer_name, deserialized.peer_name);
    EXPECT_EQ(original_msg.capabilities, deserialized.capabilities);
    EXPECT_EQ(original_msg.identity_public_key, deserialized.identity_public_key);
    EXPECT_EQ(original_msg.ephemeral_public_key, deserialized.ephemeral_public_key);
    EXPECT_EQ(original_msg.nonce, deserialized.nonce);
    EXPECT_EQ(original_msg.signature, deserialized.signature);
    
    // Test KeyRotationMessage
    KeyRotationMessage rotation_msg;
    rotation_msg.rotation_id = 42;
    rotation_msg.new_ephemeral_public_key = key_manager1_->generate_ephemeral_keys().public_key;
    rotation_msg.nonce = 0xFEDCBA9876543210;
    rotation_msg.timestamp = 1234567890123456789ULL;
    std::fill(rotation_msg.signature.begin(), rotation_msg.signature.end(), 0xCD);
    
    auto rot_serialized = rotation_msg.serialize();
    auto rot_deserialized = KeyRotationMessage::deserialize(rot_serialized);
    
    EXPECT_EQ(rotation_msg.rotation_id, rot_deserialized.rotation_id);
    EXPECT_EQ(rotation_msg.new_ephemeral_public_key, rot_deserialized.new_ephemeral_public_key);
    EXPECT_EQ(rotation_msg.nonce, rot_deserialized.nonce);
    EXPECT_EQ(rotation_msg.timestamp, rot_deserialized.timestamp);
    EXPECT_EQ(rotation_msg.signature, rot_deserialized.signature);
}

// Test handshake state management
TEST_F(SecurityComprehensiveTest, HandshakeStateMachine) {
    // Initial state
    EXPECT_EQ(handshake1_->get_phase(), HandshakePhase::INITIATE);
    EXPECT_EQ(handshake2_->get_phase(), HandshakePhase::INITIATE);
    
    // After initiation
    SecureHandshakeMessage init_msg;
    ASSERT_TRUE(handshake1_->initiate_handshake(1001, 8080, "peer1", 0x01, init_msg).success());
    EXPECT_EQ(handshake1_->get_phase(), HandshakePhase::RESPOND);
    
    // After response
    SecureHandshakeAckMessage ack_msg;
    ASSERT_TRUE(handshake2_->respond_to_handshake(init_msg, 1002, ack_msg).success());
    EXPECT_EQ(handshake2_->get_phase(), HandshakePhase::COMPLETE);
    
    // After completion
    KeyManager::SessionKeys keys1;
    ASSERT_TRUE(handshake1_->complete_handshake(ack_msg, keys1).success());
    EXPECT_EQ(handshake1_->get_phase(), HandshakePhase::COMPLETE);
    
    // Reset should return to initial state
    handshake1_->reset();
    EXPECT_EQ(handshake1_->get_phase(), HandshakePhase::INITIATE);
}

// Test error conditions and edge cases
TEST_F(SecurityComprehensiveTest, ErrorConditions) {
    // Test invalid handshake sequence
    SecureHandshakeAckMessage invalid_ack;
    KeyManager::SessionKeys dummy_keys;
    
    auto result = handshake1_->complete_handshake(invalid_ack, dummy_keys);
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, CryptoError::INVALID_STATE);
    
    // Test key rotation without complete handshake
    result = handshake1_->initiate_key_rotation(dummy_keys, dummy_keys);
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, CryptoError::INVALID_STATE);
    
    // Test null key manager
    EXPECT_THROW(SecureHandshake(nullptr), std::invalid_argument);
}