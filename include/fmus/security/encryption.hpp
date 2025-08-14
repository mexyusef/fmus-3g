#pragma once

#include "../network/socket.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <mutex>

namespace fmus::security {

// Encryption Algorithms
enum class EncryptionAlgorithm {
    AES_128_GCM,
    AES_256_GCM,
    CHACHA20_POLY1305,
    NONE
};

// Hash Algorithms
enum class HashAlgorithm {
    SHA256,
    SHA512,
    BLAKE2B,
    MD5  // For compatibility only
};

// Key Exchange Methods
enum class KeyExchangeMethod {
    ECDH_P256,
    ECDH_P384,
    RSA_2048,
    RSA_4096
};

// Certificate Types
enum class CertificateType {
    X509_PEM,
    X509_DER,
    SELF_SIGNED
};

// TLS/DTLS Versions
enum class TlsVersion {
    TLS_1_2,
    TLS_1_3,
    DTLS_1_2,
    DTLS_1_3
};

// Encryption Key
struct EncryptionKey {
    std::vector<uint8_t> key_data;
    EncryptionAlgorithm algorithm;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
    
    EncryptionKey() : algorithm(EncryptionAlgorithm::NONE), 
                     created_at(std::chrono::system_clock::now()),
                     expires_at(std::chrono::system_clock::now() + std::chrono::hours(24)) {}
    
    bool isExpired() const {
        return std::chrono::system_clock::now() > expires_at;
    }
    
    size_t getKeySize() const { return key_data.size(); }
};

// Digital Certificate
struct Certificate {
    std::string subject;
    std::string issuer;
    std::string serial_number;
    std::chrono::system_clock::time_point valid_from;
    std::chrono::system_clock::time_point valid_to;
    std::vector<uint8_t> public_key;
    std::vector<uint8_t> certificate_data;
    CertificateType type;
    
    Certificate() : type(CertificateType::SELF_SIGNED),
                   valid_from(std::chrono::system_clock::now()),
                   valid_to(std::chrono::system_clock::now() + std::chrono::hours(24 * 365)) {}
    
    bool isValid() const {
        auto now = std::chrono::system_clock::now();
        return now >= valid_from && now <= valid_to;
    }
    
    bool isExpired() const {
        return std::chrono::system_clock::now() > valid_to;
    }
};

// Cryptographic Engine Interface
class CryptoEngine {
public:
    virtual ~CryptoEngine() = default;
    
    // Key generation
    virtual EncryptionKey generateKey(EncryptionAlgorithm algorithm) = 0;
    virtual std::pair<std::vector<uint8_t>, std::vector<uint8_t>> generateKeyPair(KeyExchangeMethod method) = 0;
    
    // Symmetric encryption/decryption
    virtual std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, const EncryptionKey& key) = 0;
    virtual std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext, const EncryptionKey& key) = 0;
    
    // Hashing
    virtual std::vector<uint8_t> hash(const std::vector<uint8_t>& data, HashAlgorithm algorithm) = 0;
    virtual std::vector<uint8_t> hmac(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key, HashAlgorithm algorithm) = 0;
    
    // Digital signatures
    virtual std::vector<uint8_t> sign(const std::vector<uint8_t>& data, const std::vector<uint8_t>& private_key) = 0;
    virtual bool verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature, const std::vector<uint8_t>& public_key) = 0;
    
    // Random number generation
    virtual std::vector<uint8_t> generateRandom(size_t length) = 0;
    
    // Key derivation
    virtual EncryptionKey deriveKey(const std::vector<uint8_t>& master_key, const std::string& label, size_t key_length) = 0;
};

// Basic Crypto Engine Implementation
class BasicCryptoEngine : public CryptoEngine {
public:
    BasicCryptoEngine();
    ~BasicCryptoEngine() override;
    
    // CryptoEngine implementation
    EncryptionKey generateKey(EncryptionAlgorithm algorithm) override;
    std::pair<std::vector<uint8_t>, std::vector<uint8_t>> generateKeyPair(KeyExchangeMethod method) override;
    
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, const EncryptionKey& key) override;
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext, const EncryptionKey& key) override;
    
    std::vector<uint8_t> hash(const std::vector<uint8_t>& data, HashAlgorithm algorithm) override;
    std::vector<uint8_t> hmac(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key, HashAlgorithm algorithm) override;
    
    std::vector<uint8_t> sign(const std::vector<uint8_t>& data, const std::vector<uint8_t>& private_key) override;
    bool verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature, const std::vector<uint8_t>& public_key) override;
    
    std::vector<uint8_t> generateRandom(size_t length) override;
    EncryptionKey deriveKey(const std::vector<uint8_t>& master_key, const std::string& label, size_t key_length) override;

private:
    // Simple XOR encryption for demonstration
    std::vector<uint8_t> xorEncrypt(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key);
    std::vector<uint8_t> simpleHash(const std::vector<uint8_t>& data);
};

// Certificate Manager
class CertificateManager {
public:
    using CertificateCallback = std::function<void(const Certificate&, bool)>; // certificate, valid

    CertificateManager(std::shared_ptr<CryptoEngine> crypto_engine);
    ~CertificateManager();
    
    // Certificate operations
    Certificate generateSelfSignedCertificate(const std::string& subject, std::chrono::hours validity_hours = std::chrono::hours(24 * 365));
    bool loadCertificate(const std::string& certificate_path, const std::string& private_key_path);
    bool saveCertificate(const Certificate& certificate, const std::string& certificate_path);
    
    // Certificate validation
    bool validateCertificate(const Certificate& certificate) const;
    bool verifyCertificateChain(const std::vector<Certificate>& chain) const;
    
    // Certificate storage
    void addTrustedCertificate(const Certificate& certificate);
    void removeTrustedCertificate(const std::string& serial_number);
    std::vector<Certificate> getTrustedCertificates() const;
    
    // Certificate revocation
    void revokeCertificate(const std::string& serial_number);
    bool isCertificateRevoked(const std::string& serial_number) const;
    
    // Callbacks
    void setCertificateCallback(CertificateCallback callback) { certificate_callback_ = callback; }
    
    // Maintenance
    void cleanupExpiredCertificates();

private:
    std::shared_ptr<CryptoEngine> crypto_engine_;
    std::unordered_map<std::string, Certificate> trusted_certificates_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> revoked_certificates_;
    
    CertificateCallback certificate_callback_;
    
    mutable std::mutex mutex_;
};

// TLS/DTLS Context
class TlsContext {
public:
    using HandshakeCallback = std::function<void(bool)>; // success

    TlsContext(TlsVersion version, std::shared_ptr<CryptoEngine> crypto_engine);
    ~TlsContext();
    
    // Configuration
    void setCertificate(const Certificate& certificate, const std::vector<uint8_t>& private_key);
    void addTrustedCertificate(const Certificate& certificate);
    void setCipherSuites(const std::vector<std::string>& cipher_suites);
    
    // TLS operations
    bool performHandshake(std::shared_ptr<network::Socket> socket, bool is_server);
    bool sendEncrypted(const std::vector<uint8_t>& data);
    std::vector<uint8_t> receiveDecrypted();
    
    // State
    bool isHandshakeComplete() const { return handshake_complete_; }
    bool isSecure() const { return secure_; }
    EncryptionAlgorithm getCurrentCipher() const { return current_cipher_; }
    
    // Callbacks
    void setHandshakeCallback(HandshakeCallback callback) { handshake_callback_ = callback; }

private:
    TlsVersion version_;
    std::shared_ptr<CryptoEngine> crypto_engine_;
    std::shared_ptr<network::Socket> socket_;
    
    Certificate local_certificate_;
    std::vector<uint8_t> local_private_key_;
    std::vector<Certificate> trusted_certificates_;
    std::vector<std::string> cipher_suites_;
    
    std::atomic<bool> handshake_complete_;
    std::atomic<bool> secure_;
    EncryptionAlgorithm current_cipher_;
    EncryptionKey session_key_;
    
    HandshakeCallback handshake_callback_;
    
    mutable std::mutex mutex_;
};

// SRTP (Secure RTP) Implementation
class SrtpContext {
public:
    SrtpContext(std::shared_ptr<CryptoEngine> crypto_engine);
    ~SrtpContext();
    
    // Key management
    bool setMasterKey(const std::vector<uint8_t>& master_key, const std::vector<uint8_t>& master_salt);
    bool deriveSessionKeys();
    
    // SRTP operations
    std::vector<uint8_t> encryptRtp(const std::vector<uint8_t>& rtp_packet);
    std::vector<uint8_t> decryptSrtp(const std::vector<uint8_t>& srtp_packet);
    
    // SRTCP operations
    std::vector<uint8_t> encryptRtcp(const std::vector<uint8_t>& rtcp_packet);
    std::vector<uint8_t> decryptSrtcp(const std::vector<uint8_t>& srtcp_packet);
    
    // Configuration
    void setEncryptionAlgorithm(EncryptionAlgorithm algorithm) { encryption_algorithm_ = algorithm; }
    void setAuthenticationAlgorithm(HashAlgorithm algorithm) { auth_algorithm_ = algorithm; }

private:
    std::shared_ptr<CryptoEngine> crypto_engine_;
    
    std::vector<uint8_t> master_key_;
    std::vector<uint8_t> master_salt_;
    
    EncryptionKey rtp_encryption_key_;
    EncryptionKey rtp_auth_key_;
    EncryptionKey rtcp_encryption_key_;
    EncryptionKey rtcp_auth_key_;
    
    EncryptionAlgorithm encryption_algorithm_;
    HashAlgorithm auth_algorithm_;
    
    std::atomic<uint32_t> rtp_sequence_;
    std::atomic<uint32_t> rtcp_sequence_;
    
    mutable std::mutex mutex_;
};

// Security Manager (coordinates all security components)
class SecurityManager {
public:
    SecurityManager();
    ~SecurityManager();
    
    // Component access
    std::shared_ptr<CryptoEngine> getCryptoEngine() { return crypto_engine_; }
    CertificateManager& getCertificateManager() { return certificate_manager_; }
    
    // TLS/DTLS context creation
    std::unique_ptr<TlsContext> createTlsContext(TlsVersion version);
    std::unique_ptr<SrtpContext> createSrtpContext();
    
    // Security policy
    void setMinimumTlsVersion(TlsVersion version) { min_tls_version_ = version; }
    void setAllowedCipherSuites(const std::vector<std::string>& cipher_suites) { allowed_cipher_suites_ = cipher_suites; }
    void setRequireClientCertificate(bool require) { require_client_cert_ = require; }
    
    // Key management
    EncryptionKey generateSessionKey(EncryptionAlgorithm algorithm);
    bool rotateKeys();
    
    // Security audit
    struct SecurityAudit {
        size_t active_tls_connections = 0;
        size_t active_srtp_sessions = 0;
        size_t trusted_certificates = 0;
        size_t revoked_certificates = 0;
        std::chrono::system_clock::time_point last_key_rotation;
    };
    
    SecurityAudit performSecurityAudit() const;
    
    // Initialization
    bool initialize();
    void shutdown();

private:
    std::shared_ptr<CryptoEngine> crypto_engine_;
    CertificateManager certificate_manager_;
    
    TlsVersion min_tls_version_;
    std::vector<std::string> allowed_cipher_suites_;
    bool require_client_cert_;
    
    std::chrono::system_clock::time_point last_key_rotation_;
    
    std::atomic<bool> initialized_;
    mutable std::mutex mutex_;
};

// Utility functions
std::string encryptionAlgorithmToString(EncryptionAlgorithm algorithm);
EncryptionAlgorithm stringToEncryptionAlgorithm(const std::string& algorithm);

std::string hashAlgorithmToString(HashAlgorithm algorithm);
HashAlgorithm stringToHashAlgorithm(const std::string& algorithm);

std::string tlsVersionToString(TlsVersion version);
TlsVersion stringToTlsVersion(const std::string& version);

std::string bytesToHex(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> hexToBytes(const std::string& hex);

bool isSecureAlgorithm(EncryptionAlgorithm algorithm);
bool isSecureHash(HashAlgorithm algorithm);

} // namespace fmus::security
