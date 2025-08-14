#pragma once

#include "message.hpp"
#include "dialog.hpp"
#include "../network/socket.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <mutex>

namespace fmus::sip {

// Registration State
enum class RegistrationState {
    UNREGISTERED,
    REGISTERING,
    REGISTERED,
    UNREGISTERING,
    FAILED
};

// User Account Information
struct UserAccount {
    std::string username;
    std::string password;
    std::string realm;
    std::string display_name;
    bool enabled = true;
    
    // Contact information
    std::string contact_uri;
    std::chrono::system_clock::time_point expires;
    std::string user_agent;
    
    // Authentication
    std::string nonce;
    std::chrono::system_clock::time_point nonce_expires;
    uint32_t nonce_count = 0;
    
    bool isExpired() const {
        return std::chrono::system_clock::now() > expires;
    }
    
    bool isNonceExpired() const {
        return std::chrono::system_clock::now() > nonce_expires;
    }
};

// Authentication Challenge
struct AuthChallenge {
    std::string realm;
    std::string nonce;
    std::string opaque;
    std::string algorithm = "MD5";
    std::string qop = "auth";
    
    std::string toString() const;
    static AuthChallenge fromString(const std::string& auth_header);
};

// Authentication Response
struct AuthResponse {
    std::string username;
    std::string realm;
    std::string nonce;
    std::string uri;
    std::string response;
    std::string algorithm = "MD5";
    std::string cnonce;
    std::string nc;
    std::string qop;
    std::string opaque;
    
    static AuthResponse fromString(const std::string& auth_header);
    bool verify(const std::string& password, const std::string& method) const;
};

// SIP Registrar (Server-side registration handling)
class SipRegistrar {
public:
    using AuthenticationCallback = std::function<bool(const std::string&, const std::string&)>; // username, password
    using RegistrationCallback = std::function<void(const UserAccount&, RegistrationState)>;

    SipRegistrar(const std::string& realm);
    ~SipRegistrar();
    
    // Configuration
    void setRealm(const std::string& realm) { realm_ = realm; }
    const std::string& getRealm() const { return realm_; }
    
    void setDefaultExpires(uint32_t seconds) { default_expires_ = seconds; }
    uint32_t getDefaultExpires() const { return default_expires_; }
    
    void setMaxExpires(uint32_t seconds) { max_expires_ = seconds; }
    uint32_t getMaxExpires() const { return max_expires_; }
    
    // Callbacks
    void setAuthenticationCallback(AuthenticationCallback callback) { auth_callback_ = callback; }
    void setRegistrationCallback(RegistrationCallback callback) { registration_callback_ = callback; }
    
    // User management
    bool addUser(const std::string& username, const std::string& password, 
                 const std::string& display_name = "");
    bool removeUser(const std::string& username);
    bool updateUserPassword(const std::string& username, const std::string& password);
    
    UserAccount* findUser(const std::string& username);
    const UserAccount* findUser(const std::string& username) const;
    
    // Registration handling
    SipMessage processRegister(const SipMessage& request);
    bool isRegistered(const std::string& username) const;
    std::vector<std::string> getRegisteredUsers() const;
    
    // Authentication
    std::string generateNonce() const;
    bool validateNonce(const std::string& nonce) const;
    AuthChallenge createChallenge() const;
    bool authenticateRequest(const SipMessage& request, const UserAccount& user);
    
    // Cleanup
    void cleanupExpiredRegistrations();
    void cleanupExpiredNonces();

private:
    void notifyRegistration(const UserAccount& user, RegistrationState state);
    std::string calculateMD5(const std::string& data) const;
    std::string calculateResponse(const std::string& username, const std::string& realm,
                                 const std::string& password, const std::string& method,
                                 const std::string& uri, const std::string& nonce,
                                 const std::string& nc = "", const std::string& cnonce = "",
                                 const std::string& qop = "") const;
    
    std::string realm_;
    uint32_t default_expires_ = 3600; // 1 hour
    uint32_t max_expires_ = 86400;    // 24 hours
    
    std::unordered_map<std::string, UserAccount> users_;
    
    AuthenticationCallback auth_callback_;
    RegistrationCallback registration_callback_;
    
    mutable std::mutex mutex_;
};

// SIP Registration Client (Client-side registration)
class SipRegistrationClient {
public:
    using StateCallback = std::function<void(RegistrationState, RegistrationState)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    SipRegistrationClient();
    ~SipRegistrationClient();
    
    // Configuration
    void setRegistrar(const network::SocketAddress& registrar) { registrar_ = registrar; }
    void setCredentials(const std::string& username, const std::string& password) {
        username_ = username;
        password_ = password;
    }
    void setUserUri(const std::string& uri) { user_uri_ = uri; }
    void setContactUri(const std::string& uri) { contact_uri_ = uri; }
    void setExpires(uint32_t seconds) { expires_ = seconds; }
    
    // Callbacks
    void setStateCallback(StateCallback callback) { state_callback_ = callback; }
    void setErrorCallback(ErrorCallback callback) { error_callback_ = callback; }
    
    // Registration operations
    bool registerUser();
    bool unregisterUser();
    bool refreshRegistration();
    
    // State
    RegistrationState getState() const { return state_; }
    bool isRegistered() const { return state_ == RegistrationState::REGISTERED; }
    
    // Message handling
    bool processResponse(const SipMessage& response);

private:
    void setState(RegistrationState new_state);
    void notifyError(const std::string& error);
    
    SipMessage createRegisterRequest(uint32_t expires = 0) const;
    SipMessage createAuthenticatedRequest(const SipMessage& challenge_response) const;
    
    std::string calculateAuthResponse(const AuthChallenge& challenge, 
                                    const std::string& method, const std::string& uri) const;
    
    network::SocketAddress registrar_;
    std::string username_;
    std::string password_;
    std::string user_uri_;
    std::string contact_uri_;
    uint32_t expires_ = 3600;
    
    std::atomic<RegistrationState> state_;
    
    // Authentication state
    std::string current_nonce_;
    std::string current_realm_;
    uint32_t nonce_count_ = 0;
    
    StateCallback state_callback_;
    ErrorCallback error_callback_;
    
    mutable std::mutex mutex_;
};

// Registration Manager (coordinates client and server functionality)
class RegistrationManager {
public:
    RegistrationManager();
    ~RegistrationManager();
    
    // Server functionality
    SipRegistrar& getRegistrar() { return registrar_; }
    const SipRegistrar& getRegistrar() const { return registrar_; }
    
    // Client functionality
    SipRegistrationClient& getClient() { return client_; }
    const SipRegistrationClient& getClient() const { return client_; }
    
    // Message routing
    bool routeMessage(const SipMessage& message);
    
    // Maintenance
    void performMaintenance(); // Cleanup expired registrations/nonces

private:
    SipRegistrar registrar_;
    SipRegistrationClient client_;
};

// Utility functions for digest authentication
namespace auth {

std::string generateNonce();
std::string calculateMD5Hash(const std::string& data);
std::string calculateDigestResponse(const std::string& username, const std::string& realm,
                                   const std::string& password, const std::string& method,
                                   const std::string& uri, const std::string& nonce,
                                   const std::string& nc = "", const std::string& cnonce = "",
                                   const std::string& qop = "");

bool verifyDigestResponse(const std::string& username, const std::string& realm,
                         const std::string& password, const std::string& method,
                         const std::string& uri, const std::string& nonce,
                         const std::string& response, const std::string& nc = "",
                         const std::string& cnonce = "", const std::string& qop = "");

std::string formatAuthHeader(const AuthChallenge& challenge);
std::string formatAuthResponse(const AuthResponse& response);

} // namespace auth

} // namespace fmus::sip
