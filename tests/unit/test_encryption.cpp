#include <gtest/gtest.h>
#include "hypershare/crypto/encryption.hpp"
#include "hypershare/crypto/random.hpp"
#include "hypershare/network/secure_message.hpp"
#include <memory>

namespace hypershare::crypto::test {

class EncryptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        encryption_engine_ = std::make_unique<EncryptionEngine>();
        nonce_manager_ = std::make_unique<NonceManager>();
        
        // Generate test key and nonce
        test_key_ = SecureRandom::generate_chacha20_key();
        test_nonce_ = SecureRandom::generate_chacha20_nonce();
    }
    
    std::unique_ptr<EncryptionEngine> encryption_engine_;
    std::unique_ptr<NonceManager> nonce_manager_;
    ChaCha20Key test_key_;
    ChaCha20Nonce test_nonce_;
};

TEST_F(EncryptionTest, BasicEncryptionDecryption) {
    std::string plaintext = "Hello, secure world!";
    std::vector<std::uint8_t> aad = {0x01, 0x02, 0x03, 0x04};
    
    // Encrypt
    EncryptedMessage encrypted;
    auto result = encryption_engine_->encrypt_string(
        plaintext, aad, test_key_, test_nonce_, encrypted
    );
    ASSERT_TRUE(result.success());
    
    // Verify structure
    ASSERT_EQ(encrypted.nonce, test_nonce_);
    ASSERT_FALSE(encrypted.ciphertext.empty());
    ASSERT_EQ(encrypted.tag.size(), AEAD_TAG_SIZE);
    
    // Decrypt
    std::string decrypted;
    result = encryption_engine_->decrypt_to_string(
        encrypted, aad, test_key_, decrypted
    );
    ASSERT_TRUE(result.success());
    ASSERT_EQ(plaintext, decrypted);
}

TEST_F(EncryptionTest, AuthenticationFailsWithWrongKey) {
    std::string plaintext = "Secret message";
    std::vector<std::uint8_t> aad = {0x01, 0x02, 0x03, 0x04};
    
    // Encrypt with original key
    EncryptedMessage encrypted;
    auto result = encryption_engine_->encrypt_string(
        plaintext, aad, test_key_, test_nonce_, encrypted
    );
    ASSERT_TRUE(result.success());
    
    // Try to decrypt with wrong key
    ChaCha20Key wrong_key = SecureRandom::generate_chacha20_key();
    std::string decrypted;
    result = encryption_engine_->decrypt_to_string(
        encrypted, aad, wrong_key, decrypted
    );
    ASSERT_FALSE(result.success());
    ASSERT_EQ(result.error, CryptoError::DECRYPTION_FAILED);
}

TEST_F(EncryptionTest, AuthenticationFailsWithWrongAAD) {
    std::string plaintext = "Secret message";
    std::vector<std::uint8_t> aad = {0x01, 0x02, 0x03, 0x04};
    std::vector<std::uint8_t> wrong_aad = {0x05, 0x06, 0x07, 0x08};
    
    // Encrypt with original AAD
    EncryptedMessage encrypted;
    auto result = encryption_engine_->encrypt_string(
        plaintext, aad, test_key_, test_nonce_, encrypted
    );
    ASSERT_TRUE(result.success());
    
    // Try to decrypt with wrong AAD
    std::string decrypted;
    result = encryption_engine_->decrypt_to_string(
        encrypted, wrong_aad, test_key_, decrypted
    );
    ASSERT_FALSE(result.success());
    ASSERT_EQ(result.error, CryptoError::DECRYPTION_FAILED);
}

TEST_F(EncryptionTest, CiphertextTampering) {
    std::string plaintext = "Important data";
    std::vector<std::uint8_t> aad = {0x01, 0x02, 0x03, 0x04};
    
    // Encrypt
    EncryptedMessage encrypted;
    auto result = encryption_engine_->encrypt_string(
        plaintext, aad, test_key_, test_nonce_, encrypted
    );
    ASSERT_TRUE(result.success());
    
    // Tamper with ciphertext
    if (!encrypted.ciphertext.empty()) {
        encrypted.ciphertext[0] ^= 0xFF;
    }
    
    // Decryption should fail
    std::string decrypted;
    result = encryption_engine_->decrypt_to_string(
        encrypted, aad, test_key_, decrypted
    );
    ASSERT_FALSE(result.success());
}

TEST_F(EncryptionTest, MessageSerialization) {
    std::string plaintext = "Serialization test";
    std::vector<std::uint8_t> aad = {0x01, 0x02, 0x03, 0x04};
    
    // Encrypt
    EncryptedMessage original;
    auto result = encryption_engine_->encrypt_string(
        plaintext, aad, test_key_, test_nonce_, original
    );
    ASSERT_TRUE(result.success());
    
    // Serialize and deserialize
    auto serialized = original.serialize();
    auto deserialized = EncryptedMessage::deserialize(serialized);
    
    // Verify fields match
    ASSERT_EQ(original.nonce, deserialized.nonce);
    ASSERT_EQ(original.ciphertext, deserialized.ciphertext);
    ASSERT_EQ(original.tag, deserialized.tag);
    
    // Should still decrypt correctly
    std::string decrypted;
    result = encryption_engine_->decrypt_to_string(
        deserialized, aad, test_key_, decrypted
    );
    ASSERT_TRUE(result.success());
    ASSERT_EQ(plaintext, decrypted);
}

TEST_F(EncryptionTest, NonceGeneration) {
    auto nonce1 = encryption_engine_->generate_nonce();
    auto nonce2 = encryption_engine_->generate_nonce();
    
    // Nonces should be different
    ASSERT_NE(nonce1, nonce2);
    ASSERT_EQ(nonce1.size(), CHACHA20_NONCE_SIZE);
    ASSERT_EQ(nonce2.size(), CHACHA20_NONCE_SIZE);
}

TEST_F(EncryptionTest, NonceManagerSequentialGeneration) {
    auto nonce1 = nonce_manager_->generate_outgoing_nonce();
    auto nonce2 = nonce_manager_->generate_outgoing_nonce();
    auto nonce3 = nonce_manager_->generate_outgoing_nonce();
    
    // Should be different and incrementing
    ASSERT_NE(nonce1, nonce2);
    ASSERT_NE(nonce2, nonce3);
    ASSERT_NE(nonce1, nonce3);
}

TEST_F(EncryptionTest, NonceManagerReplayProtection) {
    std::uint32_t peer_id = 123;
    
    auto nonce1 = nonce_manager_->generate_outgoing_nonce();
    auto nonce2 = nonce_manager_->generate_outgoing_nonce();
    
    // First time seeing these nonces should succeed
    ASSERT_TRUE(nonce_manager_->verify_incoming_nonce(nonce1, peer_id));
    ASSERT_TRUE(nonce_manager_->verify_incoming_nonce(nonce2, peer_id));
    
    // Replay attacks should fail
    ASSERT_FALSE(nonce_manager_->verify_incoming_nonce(nonce1, peer_id));
    ASSERT_FALSE(nonce_manager_->verify_incoming_nonce(nonce2, peer_id));
}

TEST_F(EncryptionTest, NonceManagerWindowProtection) {
    std::uint32_t peer_id = 456;
    nonce_manager_->set_window_size(5);  // Small window for testing
    
    // Generate and verify sequence of nonces
    std::vector<ChaCha20Nonce> nonces;
    for (int i = 0; i < 10; ++i) {
        nonces.push_back(nonce_manager_->generate_outgoing_nonce());
    }
    
    // Accept recent nonces
    for (int i = 5; i < 10; ++i) {
        ASSERT_TRUE(nonce_manager_->verify_incoming_nonce(nonces[i], peer_id));
    }
    
    // Old nonces outside window should be rejected
    for (int i = 0; i < 5; ++i) {
        ASSERT_FALSE(nonce_manager_->verify_incoming_nonce(nonces[i], peer_id));
    }
}

TEST_F(EncryptionTest, NonceManagerPeerIsolation) {
    std::uint32_t peer1 = 111;
    std::uint32_t peer2 = 222;
    
    auto nonce = nonce_manager_->generate_outgoing_nonce();
    
    // Same nonce can be accepted from different peers
    ASSERT_TRUE(nonce_manager_->verify_incoming_nonce(nonce, peer1));
    ASSERT_TRUE(nonce_manager_->verify_incoming_nonce(nonce, peer2));
    
    // But replay within same peer should fail
    ASSERT_FALSE(nonce_manager_->verify_incoming_nonce(nonce, peer1));
    ASSERT_FALSE(nonce_manager_->verify_incoming_nonce(nonce, peer2));
}

}

namespace hypershare::network::test {

class SecureMessageTest : public ::testing::Test {
protected:
    void SetUp() override {
        encryption_engine_ = std::make_shared<crypto::EncryptionEngine>();
        message_handler_ = std::make_unique<SecureMessageHandler>(encryption_engine_);
        
        // Create test session keys
        session_keys_.encryption_key = crypto::SecureRandom::generate_chacha20_key();
        session_keys_.mac_key = crypto::SecureRandom::generate_blake3_key();
        session_keys_.sequence_number = 1;
        session_keys_.created_at = std::chrono::steady_clock::now();
    }
    
    std::shared_ptr<crypto::EncryptionEngine> encryption_engine_;
    std::unique_ptr<SecureMessageHandler> message_handler_;
    crypto::KeyManager::SessionKeys session_keys_;
};

TEST_F(SecureMessageTest, SecureMessageEncryptionDecryption) {
    // Create a test heartbeat message
    HeartbeatMessage heartbeat;
    heartbeat.timestamp = 1234567890;
    heartbeat.active_connections = 5;
    heartbeat.available_files = 100;
    
    // Encrypt the message
    SecureMessage secure_msg;
    auto result = message_handler_->encrypt_message(
        MessageType::HEARTBEAT, heartbeat, session_keys_, secure_msg
    );
    ASSERT_TRUE(result.success());
    
    // Verify secure message structure
    ASSERT_EQ(secure_msg.original_type, MessageType::HEARTBEAT);
    ASSERT_EQ(secure_msg.sequence_number, session_keys_.sequence_number);
    ASSERT_FALSE(secure_msg.encrypted_payload.ciphertext.empty());
    
    // Decrypt the message
    HeartbeatMessage decrypted;
    result = message_handler_->decrypt_message(secure_msg, session_keys_, decrypted);
    ASSERT_TRUE(result.success());
    
    // Verify content matches
    ASSERT_EQ(heartbeat.timestamp, decrypted.timestamp);
    ASSERT_EQ(heartbeat.active_connections, decrypted.active_connections);
    ASSERT_EQ(heartbeat.available_files, decrypted.available_files);
}

TEST_F(SecureMessageTest, SecureMessageSerialization) {
    HeartbeatMessage heartbeat;
    heartbeat.timestamp = 9876543210ULL;
    heartbeat.active_connections = 3;
    heartbeat.available_files = 42;
    
    // Encrypt and serialize
    SecureMessage original;
    auto result = message_handler_->encrypt_message(
        MessageType::HEARTBEAT, heartbeat, session_keys_, original
    );
    ASSERT_TRUE(result.success());
    
    auto serialized = original.serialize();
    auto deserialized = SecureMessage::deserialize(serialized);
    
    // Verify fields match
    ASSERT_EQ(original.original_type, deserialized.original_type);
    ASSERT_EQ(original.sequence_number, deserialized.sequence_number);
    
    // Should still decrypt correctly
    HeartbeatMessage decrypted;
    result = message_handler_->decrypt_message(deserialized, session_keys_, decrypted);
    ASSERT_TRUE(result.success());
    ASSERT_EQ(heartbeat.timestamp, decrypted.timestamp);
}

TEST_F(SecureMessageTest, SequenceNumberReplayProtection) {
    HeartbeatMessage heartbeat;
    heartbeat.timestamp = 1111111111;
    heartbeat.active_connections = 1;
    heartbeat.available_files = 1;
    
    // Encrypt two messages
    SecureMessage msg1, msg2;
    session_keys_.sequence_number = 5;
    auto result = message_handler_->encrypt_message(
        MessageType::HEARTBEAT, heartbeat, session_keys_, msg1
    );
    ASSERT_TRUE(result.success());
    
    session_keys_.sequence_number = 6;
    result = message_handler_->encrypt_message(
        MessageType::HEARTBEAT, heartbeat, session_keys_, msg2
    );
    ASSERT_TRUE(result.success());
    
    // Decrypt in order should work
    HeartbeatMessage decrypted;
    session_keys_.sequence_number = 5;  // Reset for decryption
    result = message_handler_->decrypt_message(msg1, session_keys_, decrypted);
    ASSERT_TRUE(result.success());
    
    result = message_handler_->decrypt_message(msg2, session_keys_, decrypted);
    ASSERT_TRUE(result.success());
    
    // Replay should fail
    result = message_handler_->decrypt_message(msg1, session_keys_, decrypted);
    ASSERT_FALSE(result.success());
    ASSERT_EQ(result.error, crypto::CryptoError::VERIFICATION_FAILED);
}

}