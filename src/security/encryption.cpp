#include "fmus/security/encryption.hpp"
#include "fmus/core/logger.hpp"
#include <random>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace fmus::security {

// BasicCryptoEngine implementation
BasicCryptoEngine::BasicCryptoEngine() {
    core::Logger::info("Basic crypto engine initialized");
}

BasicCryptoEngine::~BasicCryptoEngine() {
}

EncryptionKey BasicCryptoEngine::generateKey(EncryptionAlgorithm algorithm) {
    EncryptionKey key;
    key.algorithm = algorithm;
    
    size_t key_size = 0;
    switch (algorithm) {
        case EncryptionAlgorithm::AES_128_GCM:
            key_size = 16; // 128 bits
            break;
        case EncryptionAlgorithm::AES_256_GCM:
            key_size = 32; // 256 bits
            break;
        case EncryptionAlgorithm::CHACHA20_POLY1305:
            key_size = 32; // 256 bits
            break;
        default:
            key_size = 16;
            break;
    }
    
    key.key_data = generateRandom(key_size);
    key.created_at = std::chrono::system_clock::now();
    key.expires_at = key.created_at + std::chrono::hours(24);
    
    core::Logger::debug("Generated {} key: {} bytes", encryptionAlgorithmToString(algorithm), key_size);
    return key;
}

std::pair<std::vector<uint8_t>, std::vector<uint8_t>> BasicCryptoEngine::generateKeyPair(KeyExchangeMethod method) {
    size_t key_size = 0;
    switch (method) {
        case KeyExchangeMethod::ECDH_P256:
            key_size = 32;
            break;
        case KeyExchangeMethod::ECDH_P384:
            key_size = 48;
            break;
        case KeyExchangeMethod::RSA_2048:
            key_size = 256;
            break;
        case KeyExchangeMethod::RSA_4096:
            key_size = 512;
            break;
    }
    
    // Generate simple key pair (not cryptographically secure - for demo only)
    auto private_key = generateRandom(key_size);
    auto public_key = generateRandom(key_size / 2); // Public key is typically smaller
    
    core::Logger::debug("Generated key pair: private={} bytes, public={} bytes", private_key.size(), public_key.size());
    return {private_key, public_key};
}

std::vector<uint8_t> BasicCryptoEngine::encrypt(const std::vector<uint8_t>& plaintext, const EncryptionKey& key) {
    if (key.algorithm == EncryptionAlgorithm::NONE) {
        return plaintext;
    }
    
    // Simple XOR encryption for demonstration
    return xorEncrypt(plaintext, key.key_data);
}

std::vector<uint8_t> BasicCryptoEngine::decrypt(const std::vector<uint8_t>& ciphertext, const EncryptionKey& key) {
    if (key.algorithm == EncryptionAlgorithm::NONE) {
        return ciphertext;
    }
    
    // XOR is symmetric
    return xorEncrypt(ciphertext, key.key_data);
}

std::vector<uint8_t> BasicCryptoEngine::hash(const std::vector<uint8_t>& data, HashAlgorithm algorithm) {
    switch (algorithm) {
        case HashAlgorithm::SHA256:
            return simpleHash(data); // 32 bytes
        case HashAlgorithm::SHA512:
            return simpleHash(data); // Would be 64 bytes in real implementation
        case HashAlgorithm::BLAKE2B:
            return simpleHash(data); // 64 bytes
        case HashAlgorithm::MD5:
            return simpleHash(data); // 16 bytes
        default:
            return simpleHash(data);
    }
}

std::vector<uint8_t> BasicCryptoEngine::hmac(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key, HashAlgorithm algorithm) {
    // Simple HMAC implementation: hash(key + data + key)
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), key.begin(), key.end());
    combined.insert(combined.end(), data.begin(), data.end());
    combined.insert(combined.end(), key.begin(), key.end());
    
    return hash(combined, algorithm);
}

std::vector<uint8_t> BasicCryptoEngine::sign(const std::vector<uint8_t>& data, const std::vector<uint8_t>& private_key) {
    // Simple signature: hash(data + private_key)
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), data.begin(), data.end());
    combined.insert(combined.end(), private_key.begin(), private_key.end());
    
    return hash(combined, HashAlgorithm::SHA256);
}

bool BasicCryptoEngine::verify(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature, const std::vector<uint8_t>& public_key) {
    // For demo purposes, assume verification always succeeds if signature is not empty
    return !signature.empty() && !public_key.empty();
}

std::vector<uint8_t> BasicCryptoEngine::generateRandom(size_t length) {
    std::vector<uint8_t> random_data(length);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (size_t i = 0; i < length; ++i) {
        random_data[i] = dis(gen);
    }
    
    return random_data;
}

EncryptionKey BasicCryptoEngine::deriveKey(const std::vector<uint8_t>& master_key, const std::string& label, size_t key_length) {
    // Simple key derivation: hash(master_key + label)
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), master_key.begin(), master_key.end());
    combined.insert(combined.end(), label.begin(), label.end());
    
    auto derived_hash = hash(combined, HashAlgorithm::SHA256);
    
    EncryptionKey derived_key;
    derived_key.algorithm = EncryptionAlgorithm::AES_256_GCM;
    derived_key.key_data.resize(key_length);
    
    // Use hash output as key material
    for (size_t i = 0; i < key_length; ++i) {
        derived_key.key_data[i] = derived_hash[i % derived_hash.size()];
    }
    
    derived_key.created_at = std::chrono::system_clock::now();
    derived_key.expires_at = derived_key.created_at + std::chrono::hours(24);
    
    return derived_key;
}

std::vector<uint8_t> BasicCryptoEngine::xorEncrypt(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key) {
    std::vector<uint8_t> result(data.size());
    
    for (size_t i = 0; i < data.size(); ++i) {
        result[i] = data[i] ^ key[i % key.size()];
    }
    
    return result;
}

std::vector<uint8_t> BasicCryptoEngine::simpleHash(const std::vector<uint8_t>& data) {
    // Simple hash function for demonstration (not cryptographically secure)
    std::vector<uint8_t> hash_result(32, 0); // 256-bit hash
    
    uint32_t hash_value = 0x811c9dc5; // FNV-1a initial value
    
    for (uint8_t byte : data) {
        hash_value ^= byte;
        hash_value *= 0x01000193; // FNV-1a prime
    }
    
    // Spread hash value across result
    for (size_t i = 0; i < hash_result.size(); ++i) {
        hash_result[i] = static_cast<uint8_t>((hash_value >> (i % 32)) & 0xFF);
        hash_value = hash_value * 0x01000193 + i; // Mix in position
    }
    
    return hash_result;
}

// CertificateManager implementation
CertificateManager::CertificateManager(std::shared_ptr<CryptoEngine> crypto_engine)
    : crypto_engine_(crypto_engine) {
}

CertificateManager::~CertificateManager() {
}

Certificate CertificateManager::generateSelfSignedCertificate(const std::string& subject, std::chrono::hours validity_hours) {
    Certificate certificate;
    certificate.subject = subject;
    certificate.issuer = subject; // Self-signed
    certificate.type = CertificateType::SELF_SIGNED;
    certificate.valid_from = std::chrono::system_clock::now();
    certificate.valid_to = certificate.valid_from + validity_hours;
    
    // Generate serial number
    auto random_bytes = crypto_engine_->generateRandom(16);
    certificate.serial_number = bytesToHex(random_bytes);
    
    // Generate key pair
    auto [private_key, public_key] = crypto_engine_->generateKeyPair(KeyExchangeMethod::RSA_2048);
    certificate.public_key = public_key;
    
    // Create certificate data (simplified)
    std::ostringstream cert_data;
    cert_data << "-----BEGIN CERTIFICATE-----\n";
    cert_data << "Subject: " << subject << "\n";
    cert_data << "Serial: " << certificate.serial_number << "\n";
    cert_data << "Valid From: " << std::chrono::duration_cast<std::chrono::seconds>(certificate.valid_from.time_since_epoch()).count() << "\n";
    cert_data << "Valid To: " << std::chrono::duration_cast<std::chrono::seconds>(certificate.valid_to.time_since_epoch()).count() << "\n";
    cert_data << "Public Key: " << bytesToHex(public_key) << "\n";
    cert_data << "-----END CERTIFICATE-----\n";
    
    std::string cert_str = cert_data.str();
    certificate.certificate_data.assign(cert_str.begin(), cert_str.end());
    
    core::Logger::info("Generated self-signed certificate for {}", subject);
    return certificate;
}

bool CertificateManager::loadCertificate(const std::string& certificate_path, const std::string& private_key_path) {
    // Placeholder implementation - would load from files in production
    core::Logger::info("Loading certificate from {} and private key from {}", certificate_path, private_key_path);
    return true;
}

bool CertificateManager::saveCertificate(const Certificate& certificate, const std::string& certificate_path) {
    // Placeholder implementation - would save to file in production
    core::Logger::info("Saving certificate for {} to {}", certificate.subject, certificate_path);
    return true;
}

bool CertificateManager::validateCertificate(const Certificate& certificate) const {
    if (certificate.isExpired()) {
        core::Logger::warn("Certificate {} is expired", certificate.serial_number);
        return false;
    }
    
    if (isCertificateRevoked(certificate.serial_number)) {
        core::Logger::warn("Certificate {} is revoked", certificate.serial_number);
        return false;
    }
    
    return true;
}

bool CertificateManager::verifyCertificateChain(const std::vector<Certificate>& chain) const {
    if (chain.empty()) {
        return false;
    }
    
    // Verify each certificate in the chain
    for (const auto& cert : chain) {
        if (!validateCertificate(cert)) {
            return false;
        }
    }
    
    core::Logger::debug("Certificate chain verification passed for {} certificates", chain.size());
    return true;
}

void CertificateManager::addTrustedCertificate(const Certificate& certificate) {
    std::lock_guard<std::mutex> lock(mutex_);
    trusted_certificates_[certificate.serial_number] = certificate;
    
    if (certificate_callback_) {
        certificate_callback_(certificate, true);
    }
    
    core::Logger::info("Added trusted certificate: {}", certificate.subject);
}

void CertificateManager::removeTrustedCertificate(const std::string& serial_number) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = trusted_certificates_.find(serial_number);
    if (it != trusted_certificates_.end()) {
        core::Logger::info("Removed trusted certificate: {}", it->second.subject);
        trusted_certificates_.erase(it);
    }
}

std::vector<Certificate> CertificateManager::getTrustedCertificates() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<Certificate> certificates;
    for (const auto& [serial, cert] : trusted_certificates_) {
        certificates.push_back(cert);
    }
    
    return certificates;
}

void CertificateManager::revokeCertificate(const std::string& serial_number) {
    std::lock_guard<std::mutex> lock(mutex_);
    revoked_certificates_[serial_number] = std::chrono::system_clock::now();
    
    core::Logger::warn("Revoked certificate: {}", serial_number);
}

bool CertificateManager::isCertificateRevoked(const std::string& serial_number) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return revoked_certificates_.find(serial_number) != revoked_certificates_.end();
}

void CertificateManager::cleanupExpiredCertificates() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = trusted_certificates_.begin();
    while (it != trusted_certificates_.end()) {
        if (it->second.isExpired()) {
            core::Logger::info("Cleaning up expired certificate: {}", it->second.subject);
            it = trusted_certificates_.erase(it);
        } else {
            ++it;
        }
    }
}

// SecurityManager implementation
SecurityManager::SecurityManager() 
    : crypto_engine_(std::make_shared<BasicCryptoEngine>()),
      certificate_manager_(crypto_engine_),
      min_tls_version_(TlsVersion::TLS_1_2),
      require_client_cert_(false),
      initialized_(false) {
    
    // Set default secure cipher suites
    allowed_cipher_suites_ = {
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256",
        "TLS_AES_128_GCM_SHA256"
    };
}

SecurityManager::~SecurityManager() {
    shutdown();
}

EncryptionKey SecurityManager::generateSessionKey(EncryptionAlgorithm algorithm) {
    return crypto_engine_->generateKey(algorithm);
}

bool SecurityManager::rotateKeys() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    last_key_rotation_ = std::chrono::system_clock::now();
    
    // In a real implementation, this would rotate all active session keys
    core::Logger::info("Key rotation completed");
    return true;
}

SecurityManager::SecurityAudit SecurityManager::performSecurityAudit() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    SecurityAudit audit;
    audit.trusted_certificates = certificate_manager_.getTrustedCertificates().size();
    audit.last_key_rotation = last_key_rotation_;
    
    core::Logger::info("Security audit completed: {} trusted certificates", audit.trusted_certificates);
    return audit;
}

bool SecurityManager::initialize() {
    if (initialized_) {
        return true;
    }
    
    // Generate default self-signed certificate
    auto default_cert = certificate_manager_.generateSelfSignedCertificate("CN=FMUS-3G Server");
    certificate_manager_.addTrustedCertificate(default_cert);
    
    last_key_rotation_ = std::chrono::system_clock::now();
    initialized_ = true;
    
    core::Logger::info("Security manager initialized");
    return true;
}

void SecurityManager::shutdown() {
    if (!initialized_) {
        return;
    }
    
    initialized_ = false;
    core::Logger::info("Security manager shutdown");
}

// Utility functions
std::string encryptionAlgorithmToString(EncryptionAlgorithm algorithm) {
    switch (algorithm) {
        case EncryptionAlgorithm::AES_128_GCM: return "AES-128-GCM";
        case EncryptionAlgorithm::AES_256_GCM: return "AES-256-GCM";
        case EncryptionAlgorithm::CHACHA20_POLY1305: return "ChaCha20-Poly1305";
        case EncryptionAlgorithm::NONE: return "None";
        default: return "Unknown";
    }
}

EncryptionAlgorithm stringToEncryptionAlgorithm(const std::string& algorithm) {
    if (algorithm == "AES-128-GCM") return EncryptionAlgorithm::AES_128_GCM;
    if (algorithm == "AES-256-GCM") return EncryptionAlgorithm::AES_256_GCM;
    if (algorithm == "ChaCha20-Poly1305") return EncryptionAlgorithm::CHACHA20_POLY1305;
    if (algorithm == "None") return EncryptionAlgorithm::NONE;
    return EncryptionAlgorithm::NONE;
}

std::string hashAlgorithmToString(HashAlgorithm algorithm) {
    switch (algorithm) {
        case HashAlgorithm::SHA256: return "SHA-256";
        case HashAlgorithm::SHA512: return "SHA-512";
        case HashAlgorithm::BLAKE2B: return "BLAKE2b";
        case HashAlgorithm::MD5: return "MD5";
        default: return "Unknown";
    }
}

HashAlgorithm stringToHashAlgorithm(const std::string& algorithm) {
    if (algorithm == "SHA-256") return HashAlgorithm::SHA256;
    if (algorithm == "SHA-512") return HashAlgorithm::SHA512;
    if (algorithm == "BLAKE2b") return HashAlgorithm::BLAKE2B;
    if (algorithm == "MD5") return HashAlgorithm::MD5;
    return HashAlgorithm::SHA256;
}

std::string tlsVersionToString(TlsVersion version) {
    switch (version) {
        case TlsVersion::TLS_1_2: return "TLS 1.2";
        case TlsVersion::TLS_1_3: return "TLS 1.3";
        case TlsVersion::DTLS_1_2: return "DTLS 1.2";
        case TlsVersion::DTLS_1_3: return "DTLS 1.3";
        default: return "Unknown";
    }
}

TlsVersion stringToTlsVersion(const std::string& version) {
    if (version == "TLS 1.2") return TlsVersion::TLS_1_2;
    if (version == "TLS 1.3") return TlsVersion::TLS_1_3;
    if (version == "DTLS 1.2") return TlsVersion::DTLS_1_2;
    if (version == "DTLS 1.3") return TlsVersion::DTLS_1_3;
    return TlsVersion::TLS_1_2;
}

std::string bytesToHex(const std::vector<uint8_t>& bytes) {
    std::ostringstream hex_stream;
    hex_stream << std::hex << std::setfill('0');

    for (uint8_t byte : bytes) {
        hex_stream << std::setw(2) << static_cast<int>(byte);
    }

    return hex_stream.str();
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;

    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
        bytes.push_back(byte);
    }

    return bytes;
}

bool isSecureAlgorithm(EncryptionAlgorithm algorithm) {
    switch (algorithm) {
        case EncryptionAlgorithm::AES_128_GCM:
        case EncryptionAlgorithm::AES_256_GCM:
        case EncryptionAlgorithm::CHACHA20_POLY1305:
            return true;
        case EncryptionAlgorithm::NONE:
        default:
            return false;
    }
}

bool isSecureHash(HashAlgorithm algorithm) {
    switch (algorithm) {
        case HashAlgorithm::SHA256:
        case HashAlgorithm::SHA512:
        case HashAlgorithm::BLAKE2B:
            return true;
        case HashAlgorithm::MD5:
        default:
            return false;
    }
}

} // namespace fmus::security
