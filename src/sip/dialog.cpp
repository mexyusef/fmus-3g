#include "fmus/sip/dialog.hpp"
#include "fmus/core/logger.hpp"
#include <sstream>
#include <algorithm>
#include <random>

namespace fmus::sip {

// Dialog implementation
Dialog::Dialog(const std::string& dialog_id, const SipMessage& initial_request)
    : dialog_id_(dialog_id), state_(DialogState::EARLY), local_cseq_(1), remote_cseq_(0) {
    
    extractDialogInfo(initial_request);
    
    core::Logger::debug("Created dialog {}", dialog_id_);
}

Dialog::~Dialog() {
    core::Logger::debug("Destroyed dialog {}", dialog_id_);
}

void Dialog::setState(DialogState new_state) {
    DialogState old_state = state_.exchange(new_state);
    if (old_state != new_state) {
        notifyStateChange(old_state);
        core::Logger::debug("Dialog {} state changed: {} -> {}", 
                           dialog_id_, static_cast<int>(old_state), static_cast<int>(new_state));
    }
}

bool Dialog::processMessage(const SipMessage& message) {
    if (!validateMessage(message)) {
        return false;
    }
    
    // Update remote CSeq if this is a request
    if (message.isRequest()) {
        uint32_t cseq = extractCSeq(message.getHeaders().getCSeq());
        updateRemoteCSeq(cseq);
    }
    
    // Update dialog state based on message
    if (message.isResponse()) {
        SipResponseCode code = message.getResponseCode();
        if (code >= SipResponseCode::OK && code < SipResponseCode::MultipleChoices) {
            if (state_ == DialogState::EARLY) {
                confirmDialog();
            }
        }
    }
    
    notifyMessage(message);
    return true;
}

SipMessage Dialog::createRequest(SipMethod method) const {
    SipUri target_uri(remote_target_);
    SipMessage request(method, target_uri);
    
    // Set required headers
    request.getHeaders().setCallId(call_id_);
    request.getHeaders().setFrom(local_uri_ + ";tag=" + local_tag_);
    request.getHeaders().setTo(remote_uri_ + (remote_tag_.empty() ? "" : ";tag=" + remote_tag_));
    request.getHeaders().setCSeq(std::to_string(local_cseq_) + " " + methodToString(method));
    
    // Add route set if present
    if (!route_set_.empty()) {
        for (const auto& route : route_set_) {
            request.getHeaders().set("Route", route);
        }
    }
    
    return request;
}

SipMessage Dialog::createResponse(const SipMessage& request, SipResponseCode code, 
                                 const std::string& reason) const {
    SipMessage response(code, reason);
    
    // Copy headers from request
    response.getHeaders().setCallId(request.getHeaders().getCallId());
    response.getHeaders().setFrom(request.getHeaders().getFrom());
    response.getHeaders().setTo(request.getHeaders().getTo());
    response.getHeaders().setCSeq(request.getHeaders().getCSeq());
    response.getHeaders().setVia(request.getHeaders().getVia());
    
    // Add To tag if not present and this is a final response
    if (code >= SipResponseCode::OK && response.getHeaders().getTo().find("tag=") == std::string::npos) {
        response.getHeaders().setTo(response.getHeaders().getTo() + ";tag=" + local_tag_);
    }
    
    return response;
}

bool Dialog::establishDialog(const SipMessage& response) {
    if (!response.isResponse() || response.getResponseCode() < SipResponseCode::OK) {
        return false;
    }
    
    extractDialogInfo(response);
    
    if (response.getResponseCode() < SipResponseCode::MultipleChoices) {
        confirmDialog();
    }
    
    return true;
}

void Dialog::confirmDialog() {
    if (state_ == DialogState::EARLY) {
        setState(DialogState::CONFIRMED);
    }
}

void Dialog::terminateDialog() {
    setState(DialogState::TERMINATED);
    
    // Clean up transactions
    std::lock_guard<std::mutex> lock(mutex_);
    transactions_.clear();
}

void Dialog::addTransaction(std::shared_ptr<Transaction> transaction) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Remove expired weak_ptrs
    transactions_.erase(
        std::remove_if(transactions_.begin(), transactions_.end(),
                      [](const std::weak_ptr<Transaction>& weak_tx) { return weak_tx.expired(); }),
        transactions_.end());
    
    transactions_.push_back(transaction);
    transaction->setDialog(shared_from_this());
}

void Dialog::removeTransaction(const std::string& transaction_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    transactions_.erase(
        std::remove_if(transactions_.begin(), transactions_.end(),
                      [&transaction_id](const std::weak_ptr<Transaction>& weak_tx) {
                          auto tx = weak_tx.lock();
                          return !tx || tx->getId() == transaction_id;
                      }),
        transactions_.end());
}

std::shared_ptr<Transaction> Dialog::findTransaction(const std::string& transaction_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& weak_tx : transactions_) {
        auto tx = weak_tx.lock();
        if (tx && tx->getId() == transaction_id) {
            return tx;
        }
    }
    
    return nullptr;
}

bool Dialog::validateMessage(const SipMessage& message) const {
    // Check Call-ID
    if (message.getHeaders().getCallId() != call_id_) {
        return false;
    }
    
    // For requests, check that remote CSeq is not decreasing
    if (message.isRequest()) {
        uint32_t cseq = extractCSeq(message.getHeaders().getCSeq());
        if (cseq < remote_cseq_) {
            core::Logger::warn("Dialog {}: Received request with decreasing CSeq {} < {}", 
                              dialog_id_, cseq, remote_cseq_.load());
            return false;
        }
    }
    
    return true;
}

std::string Dialog::generateDialogId(const SipMessage& message) {
    std::ostringstream oss;
    oss << message.getHeaders().getCallId();
    
    // Add tags if present
    std::string from = message.getHeaders().getFrom();
    std::string to = message.getHeaders().getTo();
    
    size_t from_tag_pos = from.find("tag=");
    if (from_tag_pos != std::string::npos) {
        oss << ":" << from.substr(from_tag_pos + 4);
    }
    
    size_t to_tag_pos = to.find("tag=");
    if (to_tag_pos != std::string::npos) {
        oss << ":" << to.substr(to_tag_pos + 4);
    }
    
    return oss.str();
}

void Dialog::notifyStateChange(DialogState old_state) {
    if (state_callback_) {
        state_callback_(old_state, state_);
    }
}

void Dialog::notifyMessage(const SipMessage& message) {
    if (message_callback_) {
        message_callback_(message);
    }
}

void Dialog::extractDialogInfo(const SipMessage& message) {
    call_id_ = message.getHeaders().getCallId();
    
    // Extract tags from From and To headers
    std::string from = message.getHeaders().getFrom();
    std::string to = message.getHeaders().getTo();
    
    size_t from_tag_pos = from.find("tag=");
    if (from_tag_pos != std::string::npos) {
        size_t end_pos = from.find(';', from_tag_pos + 4);
        if (end_pos == std::string::npos) end_pos = from.length();
        
        if (message.isRequest()) {
            remote_tag_ = from.substr(from_tag_pos + 4, end_pos - from_tag_pos - 4);
        } else {
            local_tag_ = from.substr(from_tag_pos + 4, end_pos - from_tag_pos - 4);
        }
    }
    
    size_t to_tag_pos = to.find("tag=");
    if (to_tag_pos != std::string::npos) {
        size_t end_pos = to.find(';', to_tag_pos + 4);
        if (end_pos == std::string::npos) end_pos = to.length();
        
        if (message.isRequest()) {
            local_tag_ = to.substr(to_tag_pos + 4, end_pos - to_tag_pos - 4);
        } else {
            remote_tag_ = to.substr(to_tag_pos + 4, end_pos - to_tag_pos - 4);
        }
    }
    
    // Generate local tag if not present
    if (local_tag_.empty()) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        
        std::ostringstream oss;
        for (int i = 0; i < 8; ++i) {
            oss << std::hex << dis(gen);
        }
        local_tag_ = oss.str();
    }
    
    // Extract URIs
    if (message.isRequest()) {
        remote_uri_ = from.substr(0, from.find(';'));
        local_uri_ = to.substr(0, to.find(';'));
        
        // Remote target is the Contact header or Request-URI
        std::string contact = message.getHeaders().get("Contact");
        if (!contact.empty()) {
            remote_target_ = contact;
        } else {
            remote_target_ = message.getRequestUri().toString();
        }
    } else {
        local_uri_ = from.substr(0, from.find(';'));
        remote_uri_ = to.substr(0, to.find(';'));
        
        // Remote target from Contact header
        std::string contact = message.getHeaders().get("Contact");
        if (!contact.empty()) {
            remote_target_ = contact;
        }
    }
}

uint32_t Dialog::extractCSeq(const std::string& cseq_header) const {
    std::istringstream iss(cseq_header);
    uint32_t cseq;
    iss >> cseq;
    return cseq;
}

// DialogManager implementation
DialogManager::DialogManager() {
}

DialogManager::~DialogManager() {
    cleanup();
}

std::shared_ptr<Dialog> DialogManager::createDialog(const SipMessage& initial_request) {
    std::string dialog_id = Dialog::generateDialogId(initial_request);

    // Check if dialog already exists
    auto existing = findDialog(dialog_id);
    if (existing) {
        return existing;
    }

    // Create new dialog
    auto dialog = std::make_shared<Dialog>(dialog_id, initial_request);

    // Set up callbacks
    dialog->setStateCallback([this, dialog](DialogState old_state, DialogState new_state) {
        onDialogStateChanged(dialog, old_state, new_state);
    });

    std::lock_guard<std::mutex> lock(mutex_);
    dialogs_.push_back(dialog);

    if (dialog_created_callback_) {
        dialog_created_callback_(dialog);
    }

    core::Logger::info("Created dialog {}", dialog_id);
    return dialog;
}

std::shared_ptr<Dialog> DialogManager::findDialog(const std::string& dialog_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& dialog : dialogs_) {
        if (dialog->getId() == dialog_id) {
            return dialog;
        }
    }

    return nullptr;
}

std::shared_ptr<Dialog> DialogManager::findDialogByMessage(const SipMessage& message) const {
    std::string dialog_id = Dialog::generateDialogId(message);
    return findDialog(dialog_id);
}

void DialogManager::removeDialog(const std::string& dialog_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find_if(dialogs_.begin(), dialogs_.end(),
                          [&dialog_id](const std::shared_ptr<Dialog>& dialog) {
                              return dialog->getId() == dialog_id;
                          });

    if (it != dialogs_.end()) {
        if (dialog_terminated_callback_) {
            dialog_terminated_callback_(*it);
        }
        dialogs_.erase(it);
        core::Logger::info("Removed dialog {}", dialog_id);
    }
}

void DialogManager::removeDialog(std::shared_ptr<Dialog> dialog) {
    if (dialog) {
        removeDialog(dialog->getId());
    }
}

bool DialogManager::routeMessage(const SipMessage& message) {
    auto dialog = findDialogByMessage(message);

    if (dialog) {
        bool processed = dialog->processMessage(message);

        if (message_callback_) {
            message_callback_(message, dialog);
        }

        return processed;
    }

    // No existing dialog found
    if (message.isRequest()) {
        // Create new dialog for dialog-creating requests
        SipMethod method = message.getMethod();
        if (method == SipMethod::INVITE || method == SipMethod::SUBSCRIBE) {
            dialog = createDialog(message);
            if (dialog) {
                dialog->processMessage(message);

                if (message_callback_) {
                    message_callback_(message, dialog);
                }

                return true;
            }
        }
    }

    return false;
}

size_t DialogManager::getDialogCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dialogs_.size();
}

std::vector<std::shared_ptr<Dialog>> DialogManager::getAllDialogs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dialogs_;
}

void DialogManager::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Remove terminated dialogs
    auto it = std::remove_if(dialogs_.begin(), dialogs_.end(),
                            [](const std::shared_ptr<Dialog>& dialog) {
                                return dialog->isTerminated();
                            });

    size_t removed = std::distance(it, dialogs_.end());
    dialogs_.erase(it, dialogs_.end());

    if (removed > 0) {
        core::Logger::debug("Cleaned up {} terminated dialogs", removed);
    }
}

void DialogManager::onDialogStateChanged(std::shared_ptr<Dialog> dialog,
                                        DialogState /* old_state */, DialogState new_state) {
    if (new_state == DialogState::TERMINATED) {
        // Schedule for removal (don't remove immediately to avoid iterator issues)
        core::Logger::debug("Dialog {} terminated, will be cleaned up", dialog->getId());
    }
}

} // namespace fmus::sip
