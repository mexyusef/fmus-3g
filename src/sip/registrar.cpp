#include "fmus/sip/registrar.hpp"
#include "fmus/core/logger.hpp"
#include <sstream>
#include <random>
#include <iomanip>
#include <algorithm>

namespace fmus::sip {

// AuthChallenge implementation
std::string AuthChallenge::toString() const {
    std::ostringstream oss;
    oss << "Digest realm=\"" << realm << "\", nonce=\"" << nonce << "\"";
    if (!opaque.empty()) {
        oss << ", opaque=\"" << opaque << "\"";
    }
    oss << ", algorithm=" << algorithm;
    if (!qop.empty()) {
        oss << ", qop=\"" << qop << "\"";
    }
    return oss.str();
}

AuthChallenge AuthChallenge::fromString(const std::string& auth_header) {
    AuthChallenge challenge;
    
    // Simple parsing - would need more robust implementation for production
    size_t realm_pos = auth_header.find("realm=\"");
    if (realm_pos != std::string::npos) {
        realm_pos += 7; // Length of "realm=\""
        size_t end_pos = auth_header.find("\"", realm_pos);
        if (end_pos != std::string::npos) {
            challenge.realm = auth_header.substr(realm_pos, end_pos - realm_pos);
        }
    }
    
    size_t nonce_pos = auth_header.find("nonce=\"");
    if (nonce_pos != std::string::npos) {
        nonce_pos += 7; // Length of "nonce=\""
        size_t end_pos = auth_header.find("\"", nonce_pos);
        if (end_pos != std::string::npos) {
            challenge.nonce = auth_header.substr(nonce_pos, end_pos - nonce_pos);
        }
    }
    
    size_t opaque_pos = auth_header.find("opaque=\"");
    if (opaque_pos != std::string::npos) {
        opaque_pos += 8; // Length of "opaque=\""
        size_t end_pos = auth_header.find("\"", opaque_pos);
        if (end_pos != std::string::npos) {
            challenge.opaque = auth_header.substr(opaque_pos, end_pos - opaque_pos);
        }
    }
    
    return challenge;
}

// AuthResponse implementation
AuthResponse AuthResponse::fromString(const std::string& auth_header) {
    AuthResponse response;
    
    // Simple parsing - would need more robust implementation for production
    std::istringstream iss(auth_header);
    std::string token;
    
    while (std::getline(iss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        
        size_t eq_pos = token.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = token.substr(0, eq_pos);
            std::string value = token.substr(eq_pos + 1);
            
            // Remove quotes
            if (value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.length() - 2);
            }
            
            if (key == "username") response.username = value;
            else if (key == "realm") response.realm = value;
            else if (key == "nonce") response.nonce = value;
            else if (key == "uri") response.uri = value;
            else if (key == "response") response.response = value;
            else if (key == "algorithm") response.algorithm = value;
            else if (key == "cnonce") response.cnonce = value;
            else if (key == "nc") response.nc = value;
            else if (key == "qop") response.qop = value;
            else if (key == "opaque") response.opaque = value;
        }
    }
    
    return response;
}

bool AuthResponse::verify(const std::string& password, const std::string& method) const {
    return auth::verifyDigestResponse(username, realm, password, method, uri, nonce,
                                     response, nc, cnonce, qop);
}

// SipRegistrar implementation
SipRegistrar::SipRegistrar(const std::string& realm) : realm_(realm) {
}

SipRegistrar::~SipRegistrar() {
}

bool SipRegistrar::addUser(const std::string& username, const std::string& password, 
                          const std::string& display_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    UserAccount user;
    user.username = username;
    user.password = password;
    user.realm = realm_;
    user.display_name = display_name.empty() ? username : display_name;
    user.enabled = true;
    
    users_[username] = user;
    
    core::Logger::info("Added user: {} ({})", username, user.display_name);
    return true;
}

bool SipRegistrar::removeUser(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = users_.find(username);
    if (it != users_.end()) {
        users_.erase(it);
        core::Logger::info("Removed user: {}", username);
        return true;
    }
    
    return false;
}

bool SipRegistrar::updateUserPassword(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = users_.find(username);
    if (it != users_.end()) {
        it->second.password = password;
        core::Logger::info("Updated password for user: {}", username);
        return true;
    }
    
    return false;
}

UserAccount* SipRegistrar::findUser(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = users_.find(username);
    return (it != users_.end()) ? &it->second : nullptr;
}

const UserAccount* SipRegistrar::findUser(const std::string& username) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = users_.find(username);
    return (it != users_.end()) ? &it->second : nullptr;
}

SipMessage SipRegistrar::processRegister(const SipMessage& request) {
    if (request.getMethod() != SipMethod::REGISTER) {
        return SipMessage(SipResponseCode::BadRequest, "Method not allowed");
    }
    
    // Extract username from To header
    std::string to_header = request.getHeaders().getTo();
    size_t uri_start = to_header.find("sip:");
    if (uri_start == std::string::npos) {
        return SipMessage(SipResponseCode::BadRequest, "Invalid To header");
    }
    
    size_t uri_end = to_header.find("@", uri_start);
    if (uri_end == std::string::npos) {
        return SipMessage(SipResponseCode::BadRequest, "Invalid To header");
    }
    
    std::string username = to_header.substr(uri_start + 4, uri_end - uri_start - 4);
    
    // Find user
    UserAccount* user = findUser(username);
    if (!user) {
        return SipMessage(SipResponseCode::NotFound, "User not found");
    }
    
    if (!user->enabled) {
        return SipMessage(SipResponseCode::Forbidden, "User disabled");
    }
    
    // Check for authentication
    if (!request.getHeaders().has("Authorization")) {
        // Send challenge
        AuthChallenge challenge = createChallenge();
        user->nonce = challenge.nonce;
        user->nonce_expires = std::chrono::system_clock::now() + std::chrono::minutes(5);
        
        SipMessage response(SipResponseCode::Unauthorized, "Authentication Required");
        response.getHeaders().set("WWW-Authenticate", challenge.toString());
        return response;
    }
    
    // Verify authentication
    if (!authenticateRequest(request, *user)) {
        return SipMessage(SipResponseCode::Forbidden, "Authentication failed");
    }
    
    // Process registration
    std::string contact = request.getHeaders().get("Contact");
    uint32_t expires = default_expires_;
    
    // Parse Expires header
    if (request.getHeaders().has("Expires")) {
        try {
            expires = std::stoul(request.getHeaders().get("Expires"));
        } catch (...) {
            expires = default_expires_;
        }
    }
    
    // Limit expires to maximum
    if (expires > max_expires_) {
        expires = max_expires_;
    }
    
    if (expires == 0) {
        // Unregister
        user->contact_uri.clear();
        user->expires = std::chrono::system_clock::now();
        notifyRegistration(*user, RegistrationState::UNREGISTERED);
        
        core::Logger::info("User {} unregistered", username);
    } else {
        // Register/refresh
        user->contact_uri = contact;
        user->expires = std::chrono::system_clock::now() + std::chrono::seconds(expires);
        user->user_agent = request.getHeaders().get("User-Agent");
        notifyRegistration(*user, RegistrationState::REGISTERED);
        
        core::Logger::info("User {} registered for {} seconds", username, expires);
    }
    
    // Create success response
    SipMessage response(SipResponseCode::OK, "OK");
    response.getHeaders().setFrom(request.getHeaders().getFrom());
    response.getHeaders().setTo(request.getHeaders().getTo());
    response.getHeaders().setCallId(request.getHeaders().getCallId());
    response.getHeaders().setCSeq(request.getHeaders().getCSeq());
    response.getHeaders().setVia(request.getHeaders().getVia());
    
    if (!contact.empty()) {
        response.getHeaders().set("Contact", contact + ";expires=" + std::to_string(expires));
    }
    
    return response;
}

bool SipRegistrar::isRegistered(const std::string& username) const {
    const UserAccount* user = findUser(username);
    return user && !user->contact_uri.empty() && !user->isExpired();
}

std::vector<std::string> SipRegistrar::getRegisteredUsers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> registered;
    for (const auto& [username, user] : users_) {
        if (!user.contact_uri.empty() && !user.isExpired()) {
            registered.push_back(username);
        }
    }
    
    return registered;
}

std::string SipRegistrar::generateNonce() const {
    return auth::generateNonce();
}

bool SipRegistrar::validateNonce(const std::string& nonce) const {
    // Simple validation - check if nonce exists in any user
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& [username, user] : users_) {
        if (user.nonce == nonce && !user.isNonceExpired()) {
            return true;
        }
    }
    
    return false;
}

AuthChallenge SipRegistrar::createChallenge() const {
    AuthChallenge challenge;
    challenge.realm = realm_;
    challenge.nonce = generateNonce();
    challenge.algorithm = "MD5";
    challenge.qop = "auth";
    return challenge;
}

bool SipRegistrar::authenticateRequest(const SipMessage& request, const UserAccount& user) {
    std::string auth_header = request.getHeaders().get("Authorization");
    if (auth_header.empty()) {
        return false;
    }
    
    // Remove "Digest " prefix
    if (auth_header.substr(0, 7) == "Digest ") {
        auth_header = auth_header.substr(7);
    }
    
    AuthResponse auth_response = AuthResponse::fromString(auth_header);
    
    // Verify nonce
    if (auth_response.nonce != user.nonce || user.isNonceExpired()) {
        return false;
    }
    
    // Verify response
    return auth_response.verify(user.password, methodToString(request.getMethod()));
}

void SipRegistrar::cleanupExpiredRegistrations() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::now();
    for (auto& [username, user] : users_) {
        if (!user.contact_uri.empty() && user.expires < now) {
            user.contact_uri.clear();
            notifyRegistration(user, RegistrationState::UNREGISTERED);
            core::Logger::debug("Expired registration for user: {}", username);
        }
    }
}

void SipRegistrar::cleanupExpiredNonces() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::now();
    for (auto& [username, user] : users_) {
        if (user.nonce_expires < now) {
            user.nonce.clear();
            user.nonce_count = 0;
        }
    }
}

void SipRegistrar::notifyRegistration(const UserAccount& user, RegistrationState state) {
    if (registration_callback_) {
        registration_callback_(user, state);
    }
}

std::string SipRegistrar::calculateMD5(const std::string& data) const {
    return auth::calculateMD5Hash(data);
}

std::string SipRegistrar::calculateResponse(const std::string& username, const std::string& realm,
                                           const std::string& password, const std::string& method,
                                           const std::string& uri, const std::string& nonce,
                                           const std::string& nc, const std::string& cnonce,
                                           const std::string& qop) const {
    return auth::calculateDigestResponse(username, realm, password, method, uri, nonce, nc, cnonce, qop);
}

// SipRegistrationClient implementation
SipRegistrationClient::SipRegistrationClient() : state_(RegistrationState::UNREGISTERED), nonce_count_(0) {
}

SipRegistrationClient::~SipRegistrationClient() {
}

bool SipRegistrationClient::registerUser() {
    if (username_.empty() || user_uri_.empty()) {
        notifyError("Username and user URI must be set");
        return false;
    }

    setState(RegistrationState::REGISTERING);

    SipMessage request = createRegisterRequest(expires_);

    // TODO: Send request via transport layer
    core::Logger::info("Sending REGISTER request for user: {}", username_);
    core::Logger::debug("REGISTER request:\n{}", request.toString());

    return true;
}

bool SipRegistrationClient::unregisterUser() {
    if (state_ != RegistrationState::REGISTERED) {
        notifyError("User is not registered");
        return false;
    }

    setState(RegistrationState::UNREGISTERING);

    SipMessage request = createRegisterRequest(0); // Expires = 0 means unregister

    // TODO: Send request via transport layer
    core::Logger::info("Sending unregister request for user: {}", username_);

    return true;
}

bool SipRegistrationClient::refreshRegistration() {
    if (state_ != RegistrationState::REGISTERED) {
        notifyError("User is not registered");
        return false;
    }

    return registerUser(); // Same as register
}

bool SipRegistrationClient::processResponse(const SipMessage& response) {
    if (!response.isResponse()) {
        return false;
    }

    SipResponseCode code = response.getResponseCode();

    switch (code) {
        case SipResponseCode::OK:
            if (state_ == RegistrationState::REGISTERING) {
                setState(RegistrationState::REGISTERED);
                core::Logger::info("Registration successful for user: {}", username_);
            } else if (state_ == RegistrationState::UNREGISTERING) {
                setState(RegistrationState::UNREGISTERED);
                core::Logger::info("Unregistration successful for user: {}", username_);
            }
            return true;

        case SipResponseCode::Unauthorized:
            if (response.getHeaders().has("WWW-Authenticate")) {
                std::string auth_header = response.getHeaders().get("WWW-Authenticate");

                // Remove "Digest " prefix
                if (auth_header.substr(0, 7) == "Digest ") {
                    auth_header = auth_header.substr(7);
                }

                AuthChallenge challenge = AuthChallenge::fromString(auth_header);
                current_realm_ = challenge.realm;
                current_nonce_ = challenge.nonce;
                nonce_count_++;

                // Create authenticated request
                SipMessage auth_request = createAuthenticatedRequest(response);

                // TODO: Send authenticated request via transport layer
                core::Logger::info("Sending authenticated REGISTER request for user: {}", username_);
                core::Logger::debug("Authenticated request:\n{}", auth_request.toString());

                return true;
            }
            break;

        default:
            setState(RegistrationState::FAILED);
            notifyError("Registration failed with code: " + std::to_string(static_cast<int>(code)));
            return false;
    }

    return false;
}

void SipRegistrationClient::setState(RegistrationState new_state) {
    RegistrationState old_state = state_.exchange(new_state);
    if (old_state != new_state && state_callback_) {
        state_callback_(old_state, new_state);
    }
}

void SipRegistrationClient::notifyError(const std::string& error) {
    core::Logger::error("Registration client error: {}", error);
    if (error_callback_) {
        error_callback_(error);
    }
}

SipMessage SipRegistrationClient::createRegisterRequest(uint32_t expires) const {
    SipUri request_uri(user_uri_);
    SipMessage request(SipMethod::REGISTER, request_uri);

    // Set headers
    request.getHeaders().setFrom(user_uri_);
    request.getHeaders().setTo(user_uri_);
    request.getHeaders().setCallId(auth::generateNonce() + "@" + registrar_.ip);
    request.getHeaders().setCSeq("1 REGISTER");
    request.getHeaders().setVia("SIP/2.0/UDP " + registrar_.toString());

    if (!contact_uri_.empty()) {
        request.getHeaders().set("Contact", contact_uri_);
    }

    if (expires > 0) {
        request.getHeaders().set("Expires", std::to_string(expires));
    } else {
        request.getHeaders().set("Expires", "0");
    }

    request.getHeaders().set("User-Agent", "FMUS-3G");

    return request;
}

SipMessage SipRegistrationClient::createAuthenticatedRequest(const SipMessage& /* challenge_response */) const {
    SipMessage request = createRegisterRequest(expires_);

    // Add Authorization header
    if (!current_nonce_.empty()) {
        std::string auth_response = calculateAuthResponse(
            AuthChallenge{current_realm_, current_nonce_, "", "MD5", "auth"},
            "REGISTER", user_uri_);

        request.getHeaders().set("Authorization", "Digest " + auth_response);
    }

    return request;
}

std::string SipRegistrationClient::calculateAuthResponse(const AuthChallenge& challenge,
                                                        const std::string& method,
                                                        const std::string& uri) const {
    std::string cnonce = auth::generateNonce().substr(0, 8);
    std::string nc = std::to_string(nonce_count_);

    // Pad nc to 8 digits
    while (nc.length() < 8) {
        nc = "0" + nc;
    }

    std::string response = auth::calculateDigestResponse(
        username_, challenge.realm, password_, method, uri, challenge.nonce, nc, cnonce, challenge.qop);

    std::ostringstream oss;
    oss << "username=\"" << username_ << "\", "
        << "realm=\"" << challenge.realm << "\", "
        << "nonce=\"" << challenge.nonce << "\", "
        << "uri=\"" << uri << "\", "
        << "response=\"" << response << "\", "
        << "algorithm=" << challenge.algorithm;

    if (!challenge.qop.empty()) {
        oss << ", qop=" << challenge.qop
            << ", nc=" << nc
            << ", cnonce=\"" << cnonce << "\"";
    }

    if (!challenge.opaque.empty()) {
        oss << ", opaque=\"" << challenge.opaque << "\"";
    }

    return oss.str();
}

// RegistrationManager implementation
RegistrationManager::RegistrationManager() : registrar_("fmus.local") {
}

RegistrationManager::~RegistrationManager() {
}

bool RegistrationManager::routeMessage(const SipMessage& message) {
    if (message.getMethod() == SipMethod::REGISTER) {
        // Route to registrar
        SipMessage response = registrar_.processRegister(message);
        // TODO: Send response via transport layer
        core::Logger::debug("Generated registration response:\n{}", response.toString());
        return true;
    } else if (message.isResponse()) {
        // Route to client
        return client_.processResponse(message);
    }

    return false;
}

void RegistrationManager::performMaintenance() {
    registrar_.cleanupExpiredRegistrations();
    registrar_.cleanupExpiredNonces();
}

// Authentication utility functions
namespace auth {

std::string generateNonce() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::ostringstream oss;

    // Add timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    oss << std::hex << timestamp;

    // Add random part
    for (int i = 0; i < 16; ++i) {
        oss << std::hex << dis(gen);
    }

    return oss.str();
}

std::string calculateMD5Hash(const std::string& data) {
    // Placeholder implementation - would use proper MD5 library in production
    std::hash<std::string> hasher;
    size_t hash_value = hasher(data);

    std::ostringstream oss;
    oss << std::hex << hash_value;
    return oss.str();
}

std::string calculateDigestResponse(const std::string& username, const std::string& realm,
                                   const std::string& password, const std::string& method,
                                   const std::string& uri, const std::string& nonce,
                                   const std::string& nc, const std::string& cnonce,
                                   const std::string& qop) {
    // Calculate HA1 = MD5(username:realm:password)
    std::string ha1_input = username + ":" + realm + ":" + password;
    std::string ha1 = calculateMD5Hash(ha1_input);

    // Calculate HA2 = MD5(method:uri)
    std::string ha2_input = method + ":" + uri;
    std::string ha2 = calculateMD5Hash(ha2_input);

    // Calculate response
    std::string response_input;
    if (qop.empty()) {
        // RFC 2069 compatibility
        response_input = ha1 + ":" + nonce + ":" + ha2;
    } else {
        // RFC 2617
        response_input = ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2;
    }

    return calculateMD5Hash(response_input);
}

bool verifyDigestResponse(const std::string& username, const std::string& realm,
                         const std::string& password, const std::string& method,
                         const std::string& uri, const std::string& nonce,
                         const std::string& response, const std::string& nc,
                         const std::string& cnonce, const std::string& qop) {
    std::string expected = calculateDigestResponse(username, realm, password, method, uri, nonce, nc, cnonce, qop);
    return expected == response;
}

std::string formatAuthHeader(const AuthChallenge& challenge) {
    return "Digest " + challenge.toString();
}

std::string formatAuthResponse(const AuthResponse& response) {
    std::ostringstream oss;
    oss << "Digest username=\"" << response.username << "\", "
        << "realm=\"" << response.realm << "\", "
        << "nonce=\"" << response.nonce << "\", "
        << "uri=\"" << response.uri << "\", "
        << "response=\"" << response.response << "\", "
        << "algorithm=" << response.algorithm;

    if (!response.qop.empty()) {
        oss << ", qop=" << response.qop
            << ", nc=" << response.nc
            << ", cnonce=\"" << response.cnonce << "\"";
    }

    if (!response.opaque.empty()) {
        oss << ", opaque=\"" << response.opaque << "\"";
    }

    return oss.str();
}

} // namespace auth

} // namespace fmus::sip
