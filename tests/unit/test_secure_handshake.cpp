#include <gtest/gtest.h>
#include "hypershare/crypto/secure_handshake.hpp"
#include "hypershare/crypto/key_manager.hpp"
#include <memory>

namespace hypershare::crypto::test {

class SecureHandshakeTest : public ::testing::Test {
protected:
    void SetUp() override {
        client_key_manager_ = std::make_shared<KeyManager>();
        server_key_manager_ = std::make_shared<KeyManager>();
        
        ASSERT_TRUE(client_key_manager_->initialize());
        ASSERT_TRUE(server_key_manager_->initialize());
        
        ASSERT_TRUE(client_key_manager_->generate_identity_keys().success());
        ASSERT_TRUE(server_key_manager_->generate_identity_keys().success());
        
        client_handshake_ = std::make_unique<SecureHandshake>(client_key_manager_);
        server_handshake_ = std::make_unique<SecureHandshake>(server_key_manager_);
    }
    
    std::shared_ptr<KeyManager> client_key_manager_;
    std::shared_ptr<KeyManager> server_key_manager_;
    std::unique_ptr<SecureHandshake> client_handshake_;
    std::unique_ptr<SecureHandshake> server_handshake_;
};

TEST_F(SecureHandshakeTest, BasicHandshakeFlow) {
    // Client initiates handshake
    SecureHandshakeMessage handshake_msg;
    auto result = client_handshake_->initiate_handshake(
        12345,      // peer_id
        8080,       // listen_port
        "test_client", // peer_name
        0x01,       // capabilities
        handshake_msg
    );
    ASSERT_TRUE(result.success());
    ASSERT_EQ(client_handshake_->get_phase(), HandshakePhase::RESPOND);
    
    // Server responds to handshake
    SecureHandshakeAckMessage ack_msg;
    result = server_handshake_->respond_to_handshake(
        handshake_msg,
        67890,      // our_peer_id
        ack_msg
    );
    ASSERT_TRUE(result.success());
    ASSERT_EQ(server_handshake_->get_phase(), HandshakePhase::COMPLETE);
    
    // Client completes handshake
    KeyManager::SessionKeys client_session_keys;
    result = client_handshake_->complete_handshake(ack_msg, client_session_keys);
    ASSERT_TRUE(result.success());
    ASSERT_EQ(client_handshake_->get_phase(), HandshakePhase::COMPLETE);
    
    // Server derives session keys
    KeyManager::SessionKeys server_session_keys;
    result = server_handshake_->derive_server_session_keys(handshake_msg, server_session_keys);
    ASSERT_TRUE(result.success());
    
    // Both sides should have the same session keys
    ASSERT_EQ(client_session_keys.encryption_key, server_session_keys.encryption_key);
    ASSERT_EQ(client_session_keys.mac_key, server_session_keys.mac_key);
}

TEST_F(SecureHandshakeTest, SignatureVerification) {
    SecureHandshakeMessage handshake_msg;
    auto result = client_handshake_->initiate_handshake(
        12345, 8080, "test_client", 0x01, handshake_msg
    );
    ASSERT_TRUE(result.success());
    
    // Verify signature is valid
    result = server_handshake_->verify_handshake_signature(handshake_msg);
    ASSERT_TRUE(result.success());
    
    // Corrupt the signature and verify it fails
    handshake_msg.signature[0] ^= 0xFF;
    result = server_handshake_->verify_handshake_signature(handshake_msg);
    ASSERT_FALSE(result.success());
    ASSERT_EQ(result.error, CryptoError::VERIFICATION_FAILED);
}

TEST_F(SecureHandshakeTest, MessageSerialization) {
    SecureHandshakeMessage original;
    original.peer_id = 12345;
    original.listen_port = 8080;
    original.peer_name = "test_peer";
    original.capabilities = 0x0F;
    
    // Fill with test data
    std::fill(original.identity_public_key.begin(), original.identity_public_key.end(), 0xAA);
    std::fill(original.ephemeral_public_key.begin(), original.ephemeral_public_key.end(), 0xBB);
    original.nonce = 0x123456789ABCDEF0ULL;
    std::fill(original.signature.begin(), original.signature.end(), 0xCC);
    
    // Serialize and deserialize
    auto serialized = original.serialize();
    auto deserialized = SecureHandshakeMessage::deserialize(serialized);
    
    // Verify all fields match
    ASSERT_EQ(original.peer_id, deserialized.peer_id);
    ASSERT_EQ(original.listen_port, deserialized.listen_port);
    ASSERT_EQ(original.peer_name, deserialized.peer_name);
    ASSERT_EQ(original.capabilities, deserialized.capabilities);
    ASSERT_EQ(original.identity_public_key, deserialized.identity_public_key);
    ASSERT_EQ(original.ephemeral_public_key, deserialized.ephemeral_public_key);
    ASSERT_EQ(original.nonce, deserialized.nonce);
    ASSERT_EQ(original.signature, deserialized.signature);
}

TEST_F(SecureHandshakeTest, PeerFingerprinting) {
    Ed25519PublicKey test_key;
    std::fill(test_key.begin(), test_key.end(), 0x42);
    
    auto fingerprint = client_handshake_->get_peer_fingerprint(test_key);
    ASSERT_FALSE(fingerprint.empty());
    ASSERT_NE(fingerprint.find(':'), std::string::npos);  // Should contain colons
    
    // Same key should produce same fingerprint
    auto fingerprint2 = client_handshake_->get_peer_fingerprint(test_key);
    ASSERT_EQ(fingerprint, fingerprint2);
}

TEST_F(SecureHandshakeTest, TrustedPeerManagement) {
    Ed25519PublicKey test_key;
    std::fill(test_key.begin(), test_key.end(), 0x42);
    
    ASSERT_FALSE(client_handshake_->is_trusted_peer(test_key));
    
    client_handshake_->add_trusted_peer(test_key, "test_peer");
    ASSERT_TRUE(client_handshake_->is_trusted_peer(test_key));
}

TEST_F(SecureHandshakeTest, InvalidStateTransitions) {
    // Try to complete handshake without initiating
    SecureHandshakeAckMessage ack_msg;
    KeyManager::SessionKeys session_keys;
    auto result = client_handshake_->complete_handshake(ack_msg, session_keys);
    ASSERT_FALSE(result.success());
    ASSERT_EQ(result.error, CryptoError::INVALID_STATE);
}

TEST_F(SecureHandshakeTest, HandshakeReset) {
    SecureHandshakeMessage handshake_msg;
    auto result = client_handshake_->initiate_handshake(
        12345, 8080, "test_client", 0x01, handshake_msg
    );
    ASSERT_TRUE(result.success());
    ASSERT_EQ(client_handshake_->get_phase(), HandshakePhase::RESPOND);
    
    client_handshake_->reset();
    ASSERT_EQ(client_handshake_->get_phase(), HandshakePhase::INITIATE);
}

}