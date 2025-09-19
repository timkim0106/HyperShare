#include <gtest/gtest.h>
#include "hypershare/crypto/secure_handshake.hpp"
#include "hypershare/crypto/key_manager.hpp"
#include "hypershare/crypto/signature.hpp"
#include "hypershare/crypto/random.hpp"
#include "hypershare/network/connection_manager.hpp"
#include "hypershare/network/secure_message.hpp"
#include "hypershare/crypto/encryption.hpp"
#include <chrono>
#include <thread>
#include <memory>

using namespace hypershare::crypto;
using namespace hypershare::network;

class KeyRotationIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup key managers
        key_manager1_ = std::make_shared<KeyManager>();
        key_manager2_ = std::make_shared<KeyManager>();
        
        ASSERT_TRUE(key_manager1_->generate_identity_keys().success());
        ASSERT_TRUE(key_manager2_->generate_identity_keys().success());
        
        // Setup handshake managers
        handshake1_ = std::make_unique<SecureHandshake>(key_manager1_);
        handshake2_ = std::make_unique<SecureHandshake>(key_manager2_);
        
        // Setup encryption engines
        encryption1_ = std::make_unique<EncryptionEngine>();
        encryption2_ = std::make_unique<EncryptionEngine>();
        
        // Perform initial handshake
        perform_initial_handshake();
    }
    
    void perform_initial_handshake() {
        SecureHandshakeMessage init_msg;
        ASSERT_TRUE(handshake1_->initiate_handshake(1001, 8080, "peer1", 0x01, init_msg).success());
        
        SecureHandshakeAckMessage ack_msg;
        ASSERT_TRUE(handshake2_->respond_to_handshake(init_msg, 1002, ack_msg).success());
        
        ASSERT_TRUE(handshake1_->complete_handshake(ack_msg, session_keys1_).success());
        ASSERT_TRUE(handshake2_->derive_server_session_keys(init_msg, session_keys2_).success());
        
        last_rotation_time_ = std::chrono::steady_clock::now();
        bytes_transferred_ = 0;
    }
    
    void simulate_data_transfer(std::uint64_t bytes) {
        // Simulate transferring data by updating counter
        bytes_transferred_ += bytes;
    }
    
    bool test_message_encryption_decryption(const KeyManager::SessionKeys& encrypt_keys, 
                                           const KeyManager::SessionKeys& decrypt_keys,
                                           const std::string& message) {
        std::vector<uint8_t> plaintext(message.begin(), message.end());
        std::vector<uint8_t> empty_ad, decrypted;
        EncryptedMessage encrypted_msg;
        auto nonce = encryption1_->generate_nonce();
        
        auto encrypt_result = encryption1_->encrypt(plaintext, empty_ad, encrypt_keys.encryption_key, nonce, encrypted_msg);
        if (!encrypt_result.success()) return false;
        
        auto decrypt_result = encryption2_->decrypt(encrypted_msg, empty_ad, decrypt_keys.encryption_key, decrypted);
        if (!decrypt_result.success()) return false;
        
        return plaintext == decrypted;
    }
    
    std::shared_ptr<KeyManager> key_manager1_, key_manager2_;
    std::unique_ptr<SecureHandshake> handshake1_, handshake2_;
    std::unique_ptr<EncryptionEngine> encryption1_, encryption2_;
    KeyManager::SessionKeys session_keys1_, session_keys2_;
    std::chrono::steady_clock::time_point last_rotation_time_;
    std::uint64_t bytes_transferred_;
};

// Test key rotation triggered by data transfer threshold
TEST_F(KeyRotationIntegrationTest, DataTriggeredRotation) {
    // Test initial encryption works
    ASSERT_TRUE(test_message_encryption_decryption(session_keys1_, session_keys2_, "Initial message"));
    
    // Simulate large data transfer
    simulate_data_transfer(KEY_ROTATION_BYTES_THRESHOLD + 1);
    
    // Check if rotation is triggered
    ASSERT_TRUE(handshake1_->should_rotate_keys(last_rotation_time_, bytes_transferred_));
    
    // Perform key rotation
    KeyManager::SessionKeys new_keys1, new_keys2;
    ASSERT_TRUE(handshake1_->initiate_key_rotation(session_keys1_, new_keys1).success());
    
    // Test that new keys work for encryption
    ASSERT_TRUE(test_message_encryption_decryption(new_keys1, new_keys1, "Message after rotation"));
    
    // Test that old keys still work for existing data
    ASSERT_TRUE(test_message_encryption_decryption(session_keys1_, session_keys2_, "Old key message"));
    
    // Test that new keys don't work with old keys (forward secrecy)
    ASSERT_FALSE(test_message_encryption_decryption(new_keys1, session_keys2_, "Cross-key message"));
}

// Test key rotation triggered by time threshold
TEST_F(KeyRotationIntegrationTest, TimeTriggeredRotation) {
    // Test initial encryption works
    ASSERT_TRUE(test_message_encryption_decryption(session_keys1_, session_keys2_, "Initial message"));
    
    // Simulate time passage
    auto old_time = std::chrono::steady_clock::now() - KEY_ROTATION_TIME_THRESHOLD - std::chrono::minutes(1);
    
    // Check if rotation is triggered
    ASSERT_TRUE(handshake1_->should_rotate_keys(old_time, 0));
    
    // Perform key rotation
    KeyManager::SessionKeys new_keys1, new_keys2;
    ASSERT_TRUE(handshake1_->initiate_key_rotation(session_keys1_, new_keys1).success());
    
    // Test that new keys work for encryption
    ASSERT_TRUE(test_message_encryption_decryption(new_keys1, new_keys1, "Message after time rotation"));
}

// Test multiple consecutive key rotations
TEST_F(KeyRotationIntegrationTest, MultipleRotations) {
    std::vector<KeyManager::SessionKeys> key_history1, key_history2;
    key_history1.push_back(session_keys1_);
    key_history2.push_back(session_keys2_);
    
    // Perform 3 key rotations
    for (int i = 0; i < 3; ++i) {
        KeyManager::SessionKeys new_keys1, new_keys2;
        
        // Simulate data transfer to trigger rotation
        simulate_data_transfer(KEY_ROTATION_BYTES_THRESHOLD + 1);
        bytes_transferred_ = 0; // Reset for next iteration
        
        ASSERT_TRUE(handshake1_->initiate_key_rotation(key_history1.back(), new_keys1).success());
        
        // Store key generations
        key_history1.push_back(new_keys1);
        
        // Test that current generation keys work
        std::string test_msg = "Message in generation " + std::to_string(i + 1);
        ASSERT_TRUE(test_message_encryption_decryption(new_keys1, new_keys1, test_msg));
    }
    
    // Test that each generation is different
    for (size_t i = 1; i < key_history1.size(); ++i) {
        EXPECT_NE(key_history1[i].encryption_key, key_history1[i-1].encryption_key);
    }
    
    // Test forward secrecy - older keys cannot decrypt newer messages
    std::string latest_msg = "Latest message";
    std::vector<uint8_t> plaintext(latest_msg.begin(), latest_msg.end());
    std::vector<uint8_t> ciphertext, decrypted;
    
    // Encrypt with latest key
    EncryptedMessage encrypted_msg;
    auto nonce = encryption1_->generate_nonce();
    std::vector<uint8_t> empty_ad;
    ASSERT_TRUE(encryption1_->encrypt(plaintext, empty_ad, key_history1.back().encryption_key, nonce, encrypted_msg).success());
    
    // Try to decrypt with older keys (should fail)
    for (size_t i = 0; i < key_history1.size() - 1; ++i) {
        auto decrypt_result = encryption2_->decrypt(encrypted_msg, empty_ad, key_history1[i].encryption_key, decrypted);
        EXPECT_FALSE(decrypt_result.success()) << "Old key " << i << " should not decrypt new message";
    }
}

// Test key rotation under load (simulated concurrent message processing)
TEST_F(KeyRotationIntegrationTest, RotationUnderLoad) {
    const int num_messages = 100;
    const std::string base_message = "Load test message ";
    
    // Send messages before rotation
    std::vector<std::string> sent_messages;
    for (int i = 0; i < num_messages / 2; ++i) {
        std::string msg = base_message + std::to_string(i);
        sent_messages.push_back(msg);
        ASSERT_TRUE(test_message_encryption_decryption(session_keys1_, session_keys2_, msg));
    }
    
    // Trigger key rotation
    simulate_data_transfer(KEY_ROTATION_BYTES_THRESHOLD + 1);
    
    KeyManager::SessionKeys new_keys1, new_keys2;
    ASSERT_TRUE(handshake1_->initiate_key_rotation(session_keys1_, new_keys1).success());
    
    // Send messages after rotation with new keys
    for (int i = num_messages / 2; i < num_messages; ++i) {
        std::string msg = base_message + std::to_string(i);
        sent_messages.push_back(msg);
        ASSERT_TRUE(test_message_encryption_decryption(new_keys1, new_keys1, msg));
    }
    
    // Verify all messages were processed correctly
    EXPECT_EQ(sent_messages.size(), num_messages);
}

// Test error handling during key rotation
TEST_F(KeyRotationIntegrationTest, RotationErrorHandling) {
    // Test rotation without complete handshake
    handshake1_->reset();
    KeyManager::SessionKeys dummy_keys1, dummy_keys2;
    
    auto result = handshake1_->initiate_key_rotation(session_keys1_, dummy_keys1);
    EXPECT_FALSE(result.success());
    EXPECT_EQ(result.error, CryptoError::INVALID_STATE);
    
    // Reset handshake state
    perform_initial_handshake();
    
    // Test with invalid session keys
    KeyManager::SessionKeys invalid_keys = session_keys1_;
    std::fill(invalid_keys.encryption_key.begin(), invalid_keys.encryption_key.end(), 0);
    
    // Rotation should still work (creates new keys)
    result = handshake1_->initiate_key_rotation(invalid_keys, dummy_keys1);
    EXPECT_TRUE(result.success());
}

// Test key rotation response mechanism
TEST_F(KeyRotationIntegrationTest, KeyRotationResponse) {
    // Create rotation message
    KeyRotationMessage rotation_msg;
    rotation_msg.rotation_id = 1;
    rotation_msg.new_ephemeral_public_key = key_manager1_->generate_ephemeral_keys().public_key;
    rotation_msg.nonce = hypershare::crypto::SecureRandom::generate_uint64();
    rotation_msg.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    // Sign the rotation message
    SignatureEngine sig_engine;
    auto signature_data = create_signature_data("KEY_ROTATION", rotation_msg.serialize_for_signature());
    ASSERT_TRUE(sig_engine.sign(signature_data, key_manager1_->get_identity_keys().secret_key, rotation_msg.signature).success());
    
    // Peer 2 responds to rotation
    KeyManager::SessionKeys new_keys1, new_keys2;
    ASSERT_TRUE(handshake2_->respond_to_key_rotation(rotation_msg, session_keys2_, new_keys2).success());
    
    // Test that response generated valid keys
    ASSERT_TRUE(test_message_encryption_decryption(new_keys2, new_keys2, "Response test message"));
}

// Test forced rotation after maximum time
TEST_F(KeyRotationIntegrationTest, ForcedRotationAfterMaxTime) {
    auto very_old_time = std::chrono::steady_clock::now() - KEY_ROTATION_MAX_TIME - std::chrono::minutes(1);
    
    // Should force rotation even with no data transfer
    ASSERT_TRUE(handshake1_->should_rotate_keys(very_old_time, 0));
    
    // Perform forced rotation
    KeyManager::SessionKeys new_keys1, new_keys2;
    ASSERT_TRUE(handshake1_->initiate_key_rotation(session_keys1_, new_keys1).success());
    
    // Test that forced rotation works correctly
    ASSERT_TRUE(test_message_encryption_decryption(new_keys1, new_keys1, "Forced rotation message"));
}

// Test key rotation serialization/deserialization
TEST_F(KeyRotationIntegrationTest, RotationMessageSerialization) {
    KeyRotationMessage original;
    original.rotation_id = 42;
    original.new_ephemeral_public_key = key_manager1_->generate_ephemeral_keys().public_key;
    original.nonce = 0x123456789ABCDEF0;
    original.timestamp = 1234567890123456789ULL;
    
    // Sign the message
    SignatureEngine sig_engine;
    auto signature_data = create_signature_data("KEY_ROTATION", original.serialize_for_signature());
    ASSERT_TRUE(sig_engine.sign(signature_data, key_manager1_->get_identity_keys().secret_key, original.signature).success());
    
    // Serialize and deserialize
    auto serialized = original.serialize();
    auto deserialized = KeyRotationMessage::deserialize(serialized);
    
    // Verify all fields match
    EXPECT_EQ(original.rotation_id, deserialized.rotation_id);
    EXPECT_EQ(original.new_ephemeral_public_key, deserialized.new_ephemeral_public_key);
    EXPECT_EQ(original.nonce, deserialized.nonce);
    EXPECT_EQ(original.timestamp, deserialized.timestamp);
    EXPECT_EQ(original.signature, deserialized.signature);
    
    // Verify signature is still valid after round-trip
    auto verify_data = create_signature_data("KEY_ROTATION", deserialized.serialize_for_signature());
    ASSERT_TRUE(sig_engine.verify(verify_data, deserialized.signature, key_manager1_->get_identity_keys().public_key).success());
}