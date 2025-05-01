#include <fmus/sip/sip.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/core/task.hpp>

#include <chrono>
#include <regex>

namespace fmus::sip {

// SIP Registration implementation
SipRegistration::SipRegistration(std::shared_ptr<SipAgent> agent, const SipUri& registrar_uri)
    : agent_(agent),
      registrar_uri_(registrar_uri),
      state_(SipRegistrationState::Unregistered),
      retry_count_(0),
      max_retries_(3),
      expires_(3600) {}

SipRegistrationState SipRegistration::getState() const {
    return state_;
}

void SipRegistration::setState(SipRegistrationState state) {
    // Menyimpan state sebelumnya untuk acara logging
    SipRegistrationState old_state = state_;
    state_ = state;

    // Log state change
    core::Logger::debug("SIP Registration state changed: {} -> {}",
                     stateToString(old_state),
                     stateToString(state_));

    // Emit state change event
    events_.emit("stateChanged", state_);
}

core::EventEmitter<SipRegistrationEvents>& SipRegistration::events() {
    return events_;
}

std::string SipRegistration::stateToString(SipRegistrationState state) {
    switch (state) {
        case SipRegistrationState::Unregistered:
            return "Unregistered";
        case SipRegistrationState::Registering:
            return "Registering";
        case SipRegistrationState::Registered:
            return "Registered";
        case SipRegistrationState::RegistrationFailed:
            return "RegistrationFailed";
        case SipRegistrationState::Refreshing:
            return "Refreshing";
        case SipRegistrationState::Unregistering:
            return "Unregistering";
        default:
            return "Unknown";
    }
}

void SipRegistration::setCredentials(const std::string& username, const std::string& password) {
    username_ = username;
    password_ = password;
    has_credentials_ = true;
}

void SipRegistration::setExpires(int expires) {
    expires_ = expires;
}

int SipRegistration::getExpires() const {
    return expires_;
}

core::Task<void> SipRegistration::register_() {
    if (state_ == SipRegistrationState::Registering ||
        state_ == SipRegistrationState::Registered ||
        state_ == SipRegistrationState::Refreshing) {
        throw SipError(SipErrorCode::InvalidState, "Already registered or in process");
    }

    // Reset retry count
    retry_count_ = 0;

    // Transition to Registering state
    setState(SipRegistrationState::Registering);

    // Prepare REGISTER request
    auto register_msg = createRegisterRequest();

    // Create transaction
    auto transaction = agent_->getTransactionManager()->createClientTransaction(register_msg);

    // Store transaction for future use
    current_transaction_ = transaction;

    // Set up response handler
    transaction->events().on("response", [this](const SipMessage& response) {
        handleRegisterResponse(response);
    });

    // Return a task that completes when registration is complete or failed
    core::TaskPromise<void> promise;
    auto future = promise.getFuture();

    // Set up a handler for state changes
    auto handler = events_.on("stateChanged", [this, promise](SipRegistrationState state) mutable {
        if (state == SipRegistrationState::Registered) {
            // Registration successful - resolve promise
            promise.setReady();
        } else if (state == SipRegistrationState::RegistrationFailed) {
            // Registration failed - reject promise
            promise.setError(SipError(SipErrorCode::RegistrationFailed, "Registration failed"));
        }
    });

    // Wait for completion
    co_await future;

    // Cleanup listener
    events_.off("stateChanged", handler);

    co_return;
}

core::Task<void> SipRegistration::unregister() {
    if (state_ != SipRegistrationState::Registered) {
        throw SipError(SipErrorCode::InvalidState, "Not registered");
    }

    // Transition to Unregistering state
    setState(SipRegistrationState::Unregistering);

    // Prepare REGISTER request with expires=0
    SipMessage register_msg = createRegisterRequest(0);

    // Create transaction
    auto transaction = agent_->getTransactionManager()->createClientTransaction(register_msg);

    // Store transaction for future use
    current_transaction_ = transaction;

    // Set up response handler
    transaction->events().on("response", [this](const SipMessage& response) {
        handleUnregisterResponse(response);
    });

    // Return a task that completes when unregistration is complete
    core::TaskPromise<void> promise;
    auto future = promise.getFuture();

    // Set up a handler for state changes
    auto handler = events_.on("stateChanged", [this, promise](SipRegistrationState state) mutable {
        if (state == SipRegistrationState::Unregistered) {
            // Unregistration successful - resolve promise
            promise.setReady();
        } else if (state == SipRegistrationState::RegistrationFailed) {
            // Unregistration failed - still resolve but with a warning
            core::Logger::warn("Unregistration failed, but continuing anyway");
            promise.setReady();
        }
    });

    // Wait for completion
    co_await future;

    // Cleanup listener
    events_.off("stateChanged", handler);

    co_return;
}

void SipRegistration::refresh() {
    if (state_ != SipRegistrationState::Registered) {
        core::Logger::warn("Cannot refresh registration when not registered");
        return;
    }

    // Transition to Refreshing state
    setState(SipRegistrationState::Refreshing);

    // Prepare REGISTER request
    SipMessage register_msg = createRegisterRequest();

    // Create transaction
    auto transaction = agent_->getTransactionManager()->createClientTransaction(register_msg);

    // Store transaction for future use
    current_transaction_ = transaction;

    // Set up response handler
    transaction->events().on("response", [this](const SipMessage& response) {
        handleRegisterResponse(response);
    });
}

SipMessage SipRegistration::createRegisterRequest(int custom_expires) {
    // Create REGISTER request
    SipMessage register_msg = SipMessage::createRequest(SipMethod::REGISTER, registrar_uri_);

    // Set From header
    std::string from_uri = agent_->getContactUri()->toString();
    std::string from_tag = SipMessage::generateTag();
    register_msg.getHeaders().setFrom(from_uri + ";tag=" + from_tag);

    // Set To header (same as From but without tag)
    register_msg.getHeaders().setTo(from_uri);

    // Set Call-ID header
    if (call_id_.empty()) {
        call_id_ = SipMessage::generateCallId();
    }
    register_msg.getHeaders().setCallId(call_id_);

    // Set CSeq header
    cseq_++;
    register_msg.getHeaders().setCSeq(std::to_string(cseq_) + " REGISTER");

    // Set Via header
    std::string via = "SIP/2.0/UDP " + agent_->getContactUri()->getHost();
    if (agent_->getContactUri()->getPort() > 0) {
        via += ":" + std::to_string(agent_->getContactUri()->getPort());
    }
    via += ";branch=" + SipMessage::generateBranch();
    register_msg.getHeaders().setVia(via);

    // Set Max-Forwards header
    register_msg.getHeaders().setMaxForwards("70");

    // Set Contact header
    register_msg.getHeaders().setContact("<" + agent_->getContactUri()->toString() + ">");

    // Set Expires header
    int expires_value = (custom_expires >= 0) ? custom_expires : expires_;
    register_msg.getHeaders().setExpires(std::to_string(expires_value));

    // Set User-Agent header
    register_msg.getHeaders().setUserAgent("FMUS SIP Client");

    // If we have an authorization from a previous challenge, add it
    if (!auth_header_.empty()) {
        register_msg.getHeaders().setHeader("Authorization", auth_header_);
    }

    return register_msg;
}

void SipRegistration::handleRegisterResponse(const SipMessage& response) {
    SipResponseCode code = response.getResponseCode();
    int code_value = static_cast<int>(code);

    if (code_value >= 100 && code_value < 200) {
        // Provisional response - ignore
        return;
    } else if (code_value == 200) {
        // Registration successful

        // Extract registration info from headers
        parseRegistrationInfo(response);

        // Transition to Registered state
        setState(SipRegistrationState::Registered);

        // Schedule refresh before expiration
        // In a real implementation, we would set a timer to refresh before expires_
        // For now, just log that we should refresh
        core::Logger::debug("Registration successful, should refresh in {} seconds",
                         expires_ > 30 ? expires_ - 30 : expires_ / 2);
    } else if (code_value == 401 || code_value == 407) {
        // Authentication required
        if (!has_credentials_) {
            // No credentials available
            core::Logger::error("Authentication required but no credentials available");
            setState(SipRegistrationState::RegistrationFailed);
            events_.emit("error", SipError(SipErrorCode::AuthenticationFailed,
                                         "Authentication required but no credentials available"));
            return;
        }

        if (retry_count_ >= max_retries_) {
            // Too many retries
            core::Logger::error("Authentication failed after {} retries", retry_count_);
            setState(SipRegistrationState::RegistrationFailed);
            events_.emit("error", SipError(SipErrorCode::AuthenticationFailed,
                                         "Authentication failed after " +
                                         std::to_string(retry_count_) + " retries"));
            return;
        }

        // Extract authentication info
        std::string auth_header = (code_value == 401) ?
            response.getHeaders().getHeader("www-authenticate") :
            response.getHeaders().getHeader("proxy-authenticate");

        if (auth_header.empty()) {
            core::Logger::error("Authentication required but no authentication header found");
            setState(SipRegistrationState::RegistrationFailed);
            events_.emit("error", SipError(SipErrorCode::AuthenticationFailed,
                                         "Authentication required but no authentication header found"));
            return;
        }

        // Parse authentication header
        std::string realm;
        std::string nonce;

        std::regex realm_regex("realm=\"([^\"]+)\"");
        std::regex nonce_regex("nonce=\"([^\"]+)\"");

        std::smatch match;
        if (std::regex_search(auth_header, match, realm_regex)) {
            realm = match[1].str();
        }

        if (std::regex_search(auth_header, match, nonce_regex)) {
            nonce = match[1].str();
        }

        if (realm.empty() || nonce.empty()) {
            core::Logger::error("Authentication header missing realm or nonce");
            setState(SipRegistrationState::RegistrationFailed);
            events_.emit("error", SipError(SipErrorCode::AuthenticationFailed,
                                         "Authentication header missing realm or nonce"));
            return;
        }

        // Calculate digest response (simplified implementation)
        // In a real implementation, we would calculate MD5 hashes according to RFC 2617
        std::string ha1 = username_ + ":" + realm + ":" + password_;  // should be MD5
        std::string ha2 = "REGISTER:" + registrar_uri_.toString();    // should be MD5
        std::string response_value = ha1 + ":" + nonce + ":" + ha2;   // should be MD5

        // Build authorization header
        auth_header_ = "Digest username=\"" + username_ + "\", "
                      "realm=\"" + realm + "\", "
                      "nonce=\"" + nonce + "\", "
                      "uri=\"" + registrar_uri_.toString() + "\", "
                      "response=\"" + response_value + "\", "
                      "algorithm=MD5";

        // Increment retry count
        retry_count_++;

        // Retry registration with authentication
        SipMessage register_msg = createRegisterRequest();

        // Create transaction
        auto transaction = agent_->getTransactionManager()->createClientTransaction(register_msg);

        // Store transaction for future use
        current_transaction_ = transaction;

        // Set up response handler
        transaction->events().on("response", [this](const SipMessage& response) {
            handleRegisterResponse(response);
        });
    } else {
        // Registration failed
        core::Logger::error("Registration failed with code {}: {}",
                         code_value, response.getReasonPhrase());

        setState(SipRegistrationState::RegistrationFailed);
        events_.emit("error", SipError(SipErrorCode::RegistrationFailed,
                                     "Registration failed with code " +
                                     std::to_string(code_value) + ": " +
                                     response.getReasonPhrase()));
    }
}

void SipRegistration::handleUnregisterResponse(const SipMessage& response) {
    SipResponseCode code = response.getResponseCode();
    int code_value = static_cast<int>(code);

    if (code_value >= 100 && code_value < 200) {
        // Provisional response - ignore
        return;
    } else if (code_value == 200 || code_value == 481) {
        // 200 OK or 481 Call/Transaction Does Not Exist are both acceptable
        // for unregistration

        // Transition to Unregistered state
        setState(SipRegistrationState::Unregistered);

        // Clear registration info
        clearRegistrationInfo();
    } else {
        // Unregistration failed, but we still consider ourselves unregistered
        core::Logger::warn("Unregistration failed with code {}: {}, but still unregistering",
                       code_value, response.getReasonPhrase());

        setState(SipRegistrationState::Unregistered);

        // Clear registration info
        clearRegistrationInfo();

        events_.emit("error", SipError(SipErrorCode::UnregistrationFailed,
                                     "Unregistration failed with code " +
                                     std::to_string(code_value) + ": " +
                                     response.getReasonPhrase()));
    }
}

void SipRegistration::parseRegistrationInfo(const SipMessage& response) {
    // Extract registration expiration time from response
    std::string expires_header = response.getHeaders().getExpires();
    if (!expires_header.empty()) {
        try {
            expires_ = std::stoi(expires_header);
        } catch (const std::exception& e) {
            core::Logger::warn("Failed to parse Expires header: {}", e.what());
        }
    }

    // Extract Contact header with expires parameter
    std::string contact = response.getHeaders().getContact();
    if (!contact.empty()) {
        std::regex expires_regex("expires=([0-9]+)");
        std::smatch match;
        if (std::regex_search(contact, match, expires_regex)) {
            try {
                int contact_expires = std::stoi(match[1].str());
                if (contact_expires > 0) {
                    // Override expires_ with the value from Contact header
                    expires_ = contact_expires;
                }
            } catch (const std::exception& e) {
                core::Logger::warn("Failed to parse Contact expires parameter: {}", e.what());
            }
        }
    }

    // Log registration info
    core::Logger::info("Registered successfully for {} seconds", expires_);
}

void SipRegistration::clearRegistrationInfo() {
    // Clear authentication header
    auth_header_.clear();

    // Reset CSeq
    cseq_ = 0;

    // Log
    core::Logger::info("Registration cleared");
}

} // namespace fmus::sip