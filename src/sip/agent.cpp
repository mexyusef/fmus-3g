#include <fmus/sip/sip.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/core/task.hpp>

#include <chrono>
#include <thread>
#include <regex>
#include <algorithm>

namespace fmus::sip {

// SIP Agent implementation
SipAgent::SipAgent() :
    transaction_manager_(std::make_shared<SipTransactionManager>()),
    is_running_(false) {
    // Initialize contact URI with default values
    contact_uri_ = SipUri("sip:user@localhost:5060");
}

SipAgent::~SipAgent() {
    stop();
}

std::shared_ptr<SipTransactionManager> SipAgent::getTransactionManager() const {
    return transaction_manager_;
}

std::shared_ptr<SipUri> SipAgent::getContactUri() const {
    return std::make_shared<SipUri>(contact_uri_);
}

void SipAgent::setContactUri(const SipUri& uri) {
    contact_uri_ = uri;
}

core::EventEmitter<SipAgentEvents>& SipAgent::events() {
    return events_;
}

void SipAgent::start() {
    if (is_running_) {
        return;
    }

    is_running_ = true;

    // Start the worker thread for transaction cleanup and other periodic tasks
    worker_thread_ = std::thread([this]() {
        while (is_running_) {
            // Menjalankan cleanup pada TransactionManager
            transaction_manager_->cleanupTransactions();

            // Sleep for 1 second
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    core::Logger::info("SIP Agent started");
}

void SipAgent::stop() {
    if (!is_running_) {
        return;
    }

    is_running_ = false;

    // Unregister from all registrars
    for (auto& [uri, registration] : registrations_) {
        if (registration->getState() == SipRegistrationState::Registered) {
            try {
                // Unregister synchronously
                registration->unregister().get();
            } catch (const std::exception& e) {
                core::Logger::warn("Failed to unregister from {}: {}", uri, e.what());
            }
        }
    }

    // Hangup all active calls
    for (auto& [call_id, call] : calls_) {
        if (call->getState() != SipCallState::Disconnected) {
            try {
                // Hangup synchronously
                call->hangup().get();
            } catch (const std::exception& e) {
                core::Logger::warn("Failed to hangup call {}: {}", call_id, e.what());
            }
        }
    }

    // Wait for worker thread to finish
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    // Clear all collections
    registrations_.clear();
    calls_.clear();

    core::Logger::info("SIP Agent stopped");
}

void SipAgent::processMessage(const SipMessage& message) {
    // Kirim semua pesan ke TransactionManager
    transaction_manager_->processMessage(message);

    // Proses pesan berdasarkan jenis dan method
    if (message.getType() == SipMessageType::Request) {
        handleRequest(message);
    } else {
        // Responses are handled by the transactions
    }
}

void SipAgent::handleRequest(const SipMessage& request) {
    // Mendapatkan Call-ID dari request
    std::string call_id = request.getHeaders().getCallId();

    // Logika khusus untuk setiap method
    switch (request.getMethod()) {
        case SipMethod::INVITE:
            handleInvite(request);
            break;

        case SipMethod::BYE:
            handleBye(request);
            break;

        case SipMethod::CANCEL:
            handleCancel(request);
            break;

        case SipMethod::ACK:
            // ACKs are handled by the transactions
            break;

        case SipMethod::REGISTER:
        case SipMethod::OPTIONS:
        case SipMethod::INFO:
        case SipMethod::UPDATE:
        case SipMethod::REFER:
        case SipMethod::SUBSCRIBE:
        case SipMethod::NOTIFY:
        case SipMethod::MESSAGE:
        case SipMethod::PUBLISH:
        case SipMethod::PRACK:
            // Not supported as requests in this implementation
            // Send 405 Method Not Allowed
            sendMethodNotAllowed(request);
            break;
    }
}

void SipAgent::handleInvite(const SipMessage& invite) {
    // Mendapatkan Call-ID dari INVITE
    std::string call_id = invite.getHeaders().getCallId();

    // Mendapatkan atau membuat transaction untuk INVITE
    auto transaction = transaction_manager_->findTransaction(invite);
    if (!transaction) {
        // Buat transaction baru
        transaction = transaction_manager_->createServerTransaction(invite);
    }

    // Periksa apakah call dengan ID ini sudah ada
    auto call_it = calls_.find(call_id);
    if (call_it != calls_.end()) {
        // Call sudah ada - re-INVITE case
        core::Logger::debug("Received re-INVITE for existing call {}", call_id);

        // Re-INVITEs not implemented in this simplified version
        // Send 488 Not Acceptable Here
        SipMessage response = SipMessage::createResponse(
            SipResponseCode::NotAcceptableHere, invite);

        // Ensure To header has tag
        std::string to = response.getHeaders().getTo();
        std::regex tag_regex("tag=([^;]+)");
        if (!std::regex_search(to, tag_regex)) {
            // Generate a tag if not present
            std::string tag = SipMessage::generateTag();
            if (to.find(';') != std::string::npos) {
                response.getHeaders().setTo(to + ";tag=" + tag);
            } else {
                response.getHeaders().setTo(to + ";tag=" + tag);
            }
        }

        // Send response
        transaction->sendResponse(response);
    } else {
        // Buat call baru
        auto call = std::make_shared<SipCall>(shared_from_this(), call_id);

        // Simpan call
        calls_[call_id] = call;

        // Proses INVITE untuk call baru
        call->processIncomingInvite(transaction, invite);

        // Set up event handler for call state changes
        call->events().on("stateChanged", [this, call_id](SipCallState state) {
            if (state == SipCallState::Disconnected) {
                // Call is disconnected - remove it
                std::lock_guard<std::mutex> lock(mutex_);
                calls_.erase(call_id);
            }
        });

        // Emit incoming call event
        events_.emit("incomingCall", call);
    }
}

void SipAgent::handleBye(const SipMessage& bye) {
    // Mendapatkan Call-ID dari BYE
    std::string call_id = bye.getHeaders().getCallId();

    // Mendapatkan transaction untuk BYE
    auto transaction = transaction_manager_->findTransaction(bye);
    if (!transaction) {
        transaction = transaction_manager_->createServerTransaction(bye);
    }

    // Periksa apakah call dengan ID ini ada
    auto call_it = calls_.find(call_id);
    if (call_it != calls_.end()) {
        // Call ditemukan - proses BYE
        call_it->second->processBye(transaction, bye);
    } else {
        // Call tidak ditemukan - kirim 481 Call Leg Does Not Exist
        SipMessage response = SipMessage::createResponse(
            SipResponseCode::CallLegTransactionDoesNotExist, bye);
        transaction->sendResponse(response);
    }
}

void SipAgent::handleCancel(const SipMessage& cancel) {
    // Mendapatkan Call-ID dari CANCEL
    std::string call_id = cancel.getHeaders().getCallId();

    // Mendapatkan transaction untuk CANCEL
    auto transaction = transaction_manager_->findTransaction(cancel);
    if (!transaction) {
        transaction = transaction_manager_->createServerTransaction(cancel);
    }

    // Periksa apakah call dengan ID ini ada
    auto call_it = calls_.find(call_id);
    if (call_it != calls_.end()) {
        // Call ditemukan - proses CANCEL
        call_it->second->processCancel(transaction, cancel);
    } else {
        // Call tidak ditemukan - kirim 481 Call Leg Does Not Exist
        SipMessage response = SipMessage::createResponse(
            SipResponseCode::CallLegTransactionDoesNotExist, cancel);
        transaction->sendResponse(response);
    }
}

void SipAgent::sendMethodNotAllowed(const SipMessage& request) {
    // Create a 405 Method Not Allowed response
    SipMessage response = SipMessage::createResponse(
        SipResponseCode::MethodNotAllowed, request);

    // Add Allow header
    response.getHeaders().setHeader("Allow", "INVITE, ACK, CANCEL, BYE, OPTIONS");

    // Ensure To header has tag
    std::string to = response.getHeaders().getTo();
    std::regex tag_regex("tag=([^;]+)");
    if (!std::regex_search(to, tag_regex)) {
        // Generate a tag if not present
        std::string tag = SipMessage::generateTag();
        if (to.find(';') != std::string::npos) {
            response.getHeaders().setTo(to + ";tag=" + tag);
        } else {
            response.getHeaders().setTo(to + ";tag=" + tag);
        }
    }

    // Get or create transaction
    auto transaction = transaction_manager_->findTransaction(request);
    if (!transaction) {
        transaction = transaction_manager_->createServerTransaction(request);
    }

    // Send response
    transaction->sendResponse(response);
}

core::Task<std::shared_ptr<SipCall>> SipAgent::createCall(const SipUri& target) {
    // Buat call baru
    std::string call_id = SipMessage::generateCallId();
    auto call = std::make_shared<SipCall>(shared_from_this(), call_id);

    // Simpan call
    {
        std::lock_guard<std::mutex> lock(mutex_);
        calls_[call_id] = call;
    }

    // Set up event handler for call state changes
    call->events().on("stateChanged", [this, call_id](SipCallState state) {
        if (state == SipCallState::Disconnected) {
            // Call is disconnected - remove it after a delay
            // In a real implementation, we would use a timer
            std::lock_guard<std::mutex> lock(mutex_);
            calls_.erase(call_id);
        }
    });

    co_return call;
}

std::shared_ptr<SipRegistration> SipAgent::createRegistration(const SipUri& registrar) {
    // Normalize the registrar URI for use as a key
    std::string uri_str = registrar.toString();

    // Check if a registration with this registrar already exists
    auto it = registrations_.find(uri_str);
    if (it != registrations_.end()) {
        return it->second;
    }

    // Create a new registration
    auto registration = std::make_shared<SipRegistration>(shared_from_this(), registrar);

    // Store the registration
    registrations_[uri_str] = registration;

    // Set up event handler for registration state changes
    registration->events().on("stateChanged", [this, uri_str](SipRegistrationState state) {
        if (state == SipRegistrationState::Unregistered) {
            // Registration is unregistered - remove it after a delay
            // In a real implementation, we would use a timer
            std::lock_guard<std::mutex> lock(mutex_);
            registrations_.erase(uri_str);
        }
    });

    return registration;
}

std::shared_ptr<SipCall> SipAgent::findCall(const std::string& call_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = calls_.find(call_id);
    if (it != calls_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<SipRegistration> SipAgent::findRegistration(const SipUri& registrar) {
    std::string uri_str = registrar.toString();
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(uri_str);
    if (it != registrations_.end()) {
        return it->second;
    }
    return nullptr;
}

size_t SipAgent::getCallCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return calls_.size();
}

size_t SipAgent::getRegistrationCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return registrations_.size();
}

} // namespace fmus::sip