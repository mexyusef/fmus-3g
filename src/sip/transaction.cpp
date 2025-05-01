#include <fmus/sip/sip.hpp>
#include <fmus/core/logger.hpp>

#include <sstream>
#include <algorithm>
#include <chrono>

namespace fmus::sip {

// SIP Transaction implementation

SipTransaction::SipTransaction(SipTransactionType type, const std::string& branch, SipMethod method)
    : type_(type),
      branch_(branch),
      method_(method),
      state_(type == SipTransactionType::Client ?
            SipTransactionState::Trying :
            SipTransactionState::Proceeding),
      created_at_(std::chrono::steady_clock::now()),
      last_activity_(created_at_) {}

SipTransactionType SipTransaction::getType() const {
    return type_;
}

std::string SipTransaction::getBranch() const {
    return branch_;
}

SipMethod SipTransaction::getMethod() const {
    return method_;
}

SipTransactionState SipTransaction::getState() const {
    return state_;
}

void SipTransaction::setState(SipTransactionState state) {
    // Menyimpan state sebelumnya untuk acara logging
    SipTransactionState old_state = state_;
    state_ = state;

    // Update timestamp last_activity_ saat state berubah
    last_activity_ = std::chrono::steady_clock::now();

    // Log state change
    core::Logger::debug("SIP Transaction {} state changed: {} -> {}",
                     branch_,
                     stateToString(old_state),
                     stateToString(state_));

    // Emit state change event
    events_.emit("stateChanged", state_);
}

std::chrono::steady_clock::time_point SipTransaction::getCreatedAt() const {
    return created_at_;
}

std::chrono::steady_clock::time_point SipTransaction::getLastActivity() const {
    return last_activity_;
}

core::EventEmitter<SipTransactionEvents>& SipTransaction::events() {
    return events_;
}

const SipMessage& SipTransaction::getRequest() const {
    return request_;
}

const SipMessage& SipTransaction::getLastResponse() const {
    return last_response_;
}

bool SipTransaction::isCompleted() const {
    return state_ == SipTransactionState::Completed ||
           state_ == SipTransactionState::Terminated;
}

bool SipTransaction::isTerminated() const {
    return state_ == SipTransactionState::Terminated;
}

bool SipTransaction::isInvite() const {
    return method_ == SipMethod::INVITE;
}

bool SipTransaction::isClientTransaction() const {
    return type_ == SipTransactionType::Client;
}

bool SipTransaction::isServerTransaction() const {
    return type_ == SipTransactionType::Server;
}

void SipTransaction::processRequest(const SipMessage& request) {
    // Menyimpan referensi ke request
    request_ = request;

    // Update timestamp last_activity_
    last_activity_ = std::chrono::steady_clock::now();

    if (type_ == SipTransactionType::Server) {
        if (state_ == SipTransactionState::Trying) {
            // Ini adalah request pertama - transisi ke Proceeding
            setState(SipTransactionState::Proceeding);
        } else if (state_ == SipTransactionState::Proceeding) {
            // Ini adalah retransmission - tetap di Proceeding
            // Tidak perlu mengubah state
        } else {
            // Tidak valid untuk menerima request di state lain untuk server transaction
            core::Logger::warn("Server transaction {} received request in invalid state {}",
                           branch_, stateToString(state_));
        }
    } else {
        // Client transaction seharusnya tidak menerima request
        core::Logger::error("Client transaction {} received request - ignoring", branch_);
    }

    // Emit request event
    events_.emit("request", request);
}

void SipTransaction::processResponse(const SipMessage& response) {
    // Menyimpan referensi ke response terakhir
    last_response_ = response;

    // Update timestamp last_activity_
    last_activity_ = std::chrono::steady_clock::now();

    // Get response code
    SipResponseCode code = response.getResponseCode();
    int code_value = static_cast<int>(code);

    if (type_ == SipTransactionType::Client) {
        if (isInvite()) {
            // Client INVITE transaction
            if (state_ == SipTransactionState::Trying ||
                state_ == SipTransactionState::Proceeding) {

                if (code_value >= 100 && code_value < 200) {
                    // Provisional response - transisi ke Proceeding
                    setState(SipTransactionState::Proceeding);
                } else if (code_value >= 200 && code_value < 300) {
                    // 2xx response - transisi ke Terminated
                    setState(SipTransactionState::Terminated);
                } else if (code_value >= 300) {
                    // Final non-2xx response - transisi ke Completed
                    setState(SipTransactionState::Completed);

                    // Start timer K (untuk RFC3261 compliance) - seharusnya 64*T1 = 32 detik
                    // Setelah timer K, kita transisi ke Terminated
                    // Implementasi sederhana: langsung set ke Terminated
                    setState(SipTransactionState::Terminated);
                }
            } else {
                // Ignore responses in other states
                core::Logger::debug("Client INVITE transaction {} ignoring response in state {}",
                               branch_, stateToString(state_));
            }
        } else {
            // Client non-INVITE transaction
            if (state_ == SipTransactionState::Trying ||
                state_ == SipTransactionState::Proceeding) {

                if (code_value >= 100 && code_value < 200) {
                    // Provisional response - transisi ke Proceeding
                    setState(SipTransactionState::Proceeding);
                } else if (code_value >= 200) {
                    // Final response - transisi ke Completed
                    setState(SipTransactionState::Completed);

                    // Start timer K (untuk RFC3261 compliance) - seharusnya 64*T1 = 32 detik
                    // Setelah timer K, kita transisi ke Terminated
                    // Implementasi sederhana: langsung set ke Terminated
                    setState(SipTransactionState::Terminated);
                }
            } else {
                // Ignore responses in other states
                core::Logger::debug("Client non-INVITE transaction {} ignoring response in state {}",
                               branch_, stateToString(state_));
            }
        }
    } else {
        // Server transactions should not receive responses
        core::Logger::error("Server transaction {} received response - ignoring", branch_);
    }

    // Emit response event
    events_.emit("response", response);
}

void SipTransaction::sendRequest(const SipMessage& request) {
    if (type_ == SipTransactionType::Client) {
        // Menyimpan referensi ke request
        request_ = request;

        // Update timestamp last_activity_
        last_activity_ = std::chrono::steady_clock::now();

        if (state_ == SipTransactionState::Trying) {
            // Ini adalah request yang pertama kali dikirim
            // Emit send event
            events_.emit("send", request);
        } else {
            // Retransmission or invalid state
            core::Logger::warn("Client transaction {} sending request in state {}",
                          branch_, stateToString(state_));
            events_.emit("send", request);
        }
    } else {
        // Server transactions don't send requests
        core::Logger::error("Server transaction {} attempted to send request - ignoring", branch_);
    }
}

void SipTransaction::sendResponse(const SipMessage& response) {
    if (type_ == SipTransactionType::Server) {
        // Menyimpan referensi ke response terakhir
        last_response_ = response;

        // Update timestamp last_activity_
        last_activity_ = std::chrono::steady_clock::now();

        // Get response code
        SipResponseCode code = response.getResponseCode();
        int code_value = static_cast<int>(code);

        if (isInvite()) {
            // Server INVITE transaction
            if (state_ == SipTransactionState::Proceeding) {
                if (code_value >= 100 && code_value < 200) {
                    // Tetap di state Proceeding untuk provisional response
                } else if (code_value >= 200 && code_value < 300) {
                    // 2xx responses are handled by the TU (Transaction User) layer
                    // Transaction terminates
                    setState(SipTransactionState::Terminated);
                } else if (code_value >= 300) {
                    // Final non-2xx response - transition to Completed
                    setState(SipTransactionState::Completed);

                    // Start timer H (for RFC3261 compliance) - typically 64*T1 = 32s
                    // After timer H, transition to Terminated
                    // Simple implementation: directly set to Terminated after some delay
                    // In a real implementation, we would use a timer
                    setState(SipTransactionState::Terminated);
                }
            } else if (state_ == SipTransactionState::Completed) {
                // Retransmission of the final response
                // In a real implementation, we would keep the last response
                // and retransmit it automatically
            } else {
                // Invalid state
                core::Logger::warn("Server INVITE transaction {} sending response in invalid state {}",
                              branch_, stateToString(state_));
            }
        } else {
            // Server non-INVITE transaction
            if (state_ == SipTransactionState::Proceeding) {
                if (code_value >= 100 && code_value < 200) {
                    // Tetap di state Proceeding untuk provisional response
                } else if (code_value >= 200) {
                    // Final response - transition to Completed
                    setState(SipTransactionState::Completed);

                    // Start timer J (for RFC3261 compliance) - typically 64*T1 = 32s
                    // After timer J, transition to Terminated
                    // Simple implementation: directly set to Terminated
                    setState(SipTransactionState::Terminated);
                }
            } else if (state_ == SipTransactionState::Completed) {
                // Retransmission of the final response
                // In a real implementation, we would keep the last response
                // and retransmit it automatically
            } else {
                // Invalid state
                core::Logger::warn("Server non-INVITE transaction {} sending response in invalid state {}",
                              branch_, stateToString(state_));
            }
        }

        // Emit send event
        events_.emit("send", response);
    } else {
        // Client transactions don't send responses
        core::Logger::error("Client transaction {} attempted to send response - ignoring", branch_);
    }
}

bool SipTransaction::matches(const SipMessage& message) const {
    if (message.getType() == SipMessageType::Request) {
        // Request - check branch parameter in Via header
        std::string via = message.getHeaders().getVia();
        return via.find("branch=" + branch_) != std::string::npos &&
               message.getMethod() == method_;
    } else {
        // Response - check branch parameter in Via header
        std::string via = message.getHeaders().getVia();
        return via.find("branch=" + branch_) != std::string::npos;
    }
}

std::string SipTransaction::stateToString(SipTransactionState state) {
    switch (state) {
        case SipTransactionState::Trying:
            return "Trying";
        case SipTransactionState::Proceeding:
            return "Proceeding";
        case SipTransactionState::Completed:
            return "Completed";
        case SipTransactionState::Terminated:
            return "Terminated";
        default:
            return "Unknown";
    }
}

// SIP Transaction Manager implementation
SipTransactionManager::SipTransactionManager() {}

std::shared_ptr<SipTransaction> SipTransactionManager::createClientTransaction(const SipMessage& request) {
    // Ekstrak branch dari Via header
    std::string via = request.getHeaders().getVia();
    std::string branch;

    // Mencari parameter branch
    size_t branch_pos = via.find("branch=");
    if (branch_pos != std::string::npos) {
        // Skip "branch="
        branch_pos += 7;

        // Find end of branch value (next semicolon or end of string)
        size_t branch_end = via.find(';', branch_pos);
        if (branch_end == std::string::npos) {
            branch_end = via.length();
        }

        branch = via.substr(branch_pos, branch_end - branch_pos);
    } else {
        // Tidak ada branch parameter - tidak valid untuk RFC 3261
        core::Logger::error("Request has no branch parameter in Via header");
        throw SipError(SipErrorCode::InvalidMessage, "Missing branch parameter in Via header");
    }

    // Buat transaction baru
    auto transaction = std::make_shared<SipTransaction>(
        SipTransactionType::Client,
        branch,
        request.getMethod()
    );

    // Simpan transaction
    std::lock_guard<std::mutex> lock(mutex_);
    transactions_[branch] = transaction;

    // Send request
    transaction->sendRequest(request);

    return transaction;
}

std::shared_ptr<SipTransaction> SipTransactionManager::createServerTransaction(const SipMessage& request) {
    // Ekstrak branch dari Via header
    std::string via = request.getHeaders().getVia();
    std::string branch;

    // Mencari parameter branch
    size_t branch_pos = via.find("branch=");
    if (branch_pos != std::string::npos) {
        // Skip "branch="
        branch_pos += 7;

        // Find end of branch value (next semicolon or end of string)
        size_t branch_end = via.find(';', branch_pos);
        if (branch_end == std::string::npos) {
            branch_end = via.length();
        }

        branch = via.substr(branch_pos, branch_end - branch_pos);
    } else {
        // Tidak ada branch parameter - tidak valid untuk RFC 3261
        core::Logger::error("Request has no branch parameter in Via header");
        throw SipError(SipErrorCode::InvalidMessage, "Missing branch parameter in Via header");
    }

    // Buat transaction baru
    auto transaction = std::make_shared<SipTransaction>(
        SipTransactionType::Server,
        branch,
        request.getMethod()
    );

    // Simpan transaction
    std::lock_guard<std::mutex> lock(mutex_);
    transactions_[branch] = transaction;

    // Process request
    transaction->processRequest(request);

    return transaction;
}

std::shared_ptr<SipTransaction> SipTransactionManager::findTransaction(const std::string& branch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = transactions_.find(branch);
    if (it != transactions_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<SipTransaction> SipTransactionManager::findTransaction(const SipMessage& message) {
    std::string via = message.getHeaders().getVia();
    std::string branch;

    // Mencari parameter branch
    size_t branch_pos = via.find("branch=");
    if (branch_pos != std::string::npos) {
        // Skip "branch="
        branch_pos += 7;

        // Find end of branch value (next semicolon or end of string)
        size_t branch_end = via.find(';', branch_pos);
        if (branch_end == std::string::npos) {
            branch_end = via.length();
        }

        branch = via.substr(branch_pos, branch_end - branch_pos);
    } else {
        // Tidak ada branch parameter
        return nullptr;
    }

    return findTransaction(branch);
}

void SipTransactionManager::processMessage(const SipMessage& message) {
    auto transaction = findTransaction(message);

    if (transaction) {
        if (message.getType() == SipMessageType::Request) {
            transaction->processRequest(message);
        } else {
            transaction->processResponse(message);
        }
    } else if (message.getType() == SipMessageType::Request) {
        // Ini adalah request yang tidak ada transaksinya - buat transaction baru
        createServerTransaction(message);
    } else {
        // Ini adalah response yang tidak ada transaksinya - diabaikan
        core::Logger::warn("Received response without matching transaction");
    }
}

void SipTransactionManager::removeTransaction(const std::string& branch) {
    std::lock_guard<std::mutex> lock(mutex_);
    transactions_.erase(branch);
}

void SipTransactionManager::cleanupTransactions() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> to_remove;

    for (const auto& [branch, transaction] : transactions_) {
        if (transaction->isTerminated()) {
            // Jika transaksi sudah terminated, hapus setelah 5 detik
            auto timeout = std::chrono::seconds(5);
            if (now - transaction->getLastActivity() > timeout) {
                to_remove.push_back(branch);
            }
        } else if (!transaction->isInvite()) {
            // Non-INVITE transactions timeout after 64*T1 = 32s
            auto timeout = std::chrono::seconds(32);
            if (now - transaction->getCreatedAt() > timeout) {
                core::Logger::debug("Non-INVITE transaction {} timed out", branch);
                transaction->setState(SipTransactionState::Terminated);
                to_remove.push_back(branch);
            }
        } else if (transaction->isInvite() && transaction->getState() == SipTransactionState::Completed) {
            // INVITE transactions in Completed state timeout after 64*T1 = 32s
            auto timeout = std::chrono::seconds(32);
            if (now - transaction->getLastActivity() > timeout) {
                core::Logger::debug("INVITE transaction {} timed out in Completed state", branch);
                transaction->setState(SipTransactionState::Terminated);
                to_remove.push_back(branch);
            }
        }
    }

    for (const auto& branch : to_remove) {
        transactions_.erase(branch);
    }
}

size_t SipTransactionManager::getTransactionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return transactions_.size();
}

} // namespace fmus::sip