#include "fmus/sip/transaction.hpp"
#include "fmus/sip/dialog.hpp"
#include "fmus/core/logger.hpp"
#include <sstream>
#include <random>
#include <iomanip>
#include <algorithm>

namespace fmus::sip {

// Transaction base class implementation
Transaction::Transaction(TransactionType type, const std::string& transaction_id)
    : type_(type), transaction_id_(transaction_id), state_(TransactionState::TRYING) {
}

Transaction::~Transaction() {
    stopAllTimers();
}

void Transaction::setState(TransactionState new_state) {
    TransactionState old_state = state_.exchange(new_state);
    if (old_state != new_state) {
        notifyStateChange(old_state);
        core::Logger::debug("Transaction {} state changed: {} -> {}", 
                           transaction_id_, static_cast<int>(old_state), static_cast<int>(new_state));
    }
}

void Transaction::startTimer(const std::string& timer_name, std::chrono::milliseconds duration) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Stop existing timer with same name
    stopTimer(timer_name);
    
    Timer timer;
    timer.name = timer_name;
    timer.duration = duration;
    timer.expiry = std::chrono::steady_clock::now() + duration;
    timer.active = true;
    
    timers_.push_back(timer);
    
    core::Logger::debug("Started timer {} for transaction {} ({}ms)", 
                       timer_name, transaction_id_, duration.count());
}

void Transaction::stopTimer(const std::string& timer_name) {
    auto it = std::find_if(timers_.begin(), timers_.end(),
                          [&timer_name](const Timer& t) { return t.name == timer_name; });
    if (it != timers_.end()) {
        it->active = false;
        core::Logger::debug("Stopped timer {} for transaction {}", timer_name, transaction_id_);
    }
}

void Transaction::stopAllTimers() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& timer : timers_) {
        timer.active = false;
    }
    timers_.clear();
}

void Transaction::checkTimers() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    
    for (auto& timer : timers_) {
        if (timer.active && now >= timer.expiry) {
            timer.active = false;
            core::Logger::debug("Timer {} expired for transaction {}", timer.name, transaction_id_);
            notifyTimeout();
        }
    }
    
    // Remove inactive timers
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                                [](const Timer& t) { return !t.active; }),
                 timers_.end());
}

void Transaction::notifyStateChange(TransactionState old_state) {
    if (state_callback_) {
        state_callback_(old_state, state_);
    }
}

void Transaction::notifyTimeout() {
    if (timeout_callback_) {
        timeout_callback_();
    }
}

void Transaction::notifyMessage(const SipMessage& message) {
    if (message_callback_) {
        message_callback_(message);
    }
}

// ClientInviteTransaction implementation
ClientInviteTransaction::ClientInviteTransaction(const std::string& transaction_id, const SipMessage& invite)
    : Transaction(TransactionType::CLIENT_INVITE, transaction_id), invite_(invite) {
    setState(TransactionState::CALLING);
}

bool ClientInviteTransaction::processMessage(const SipMessage& message) {
    if (!canAcceptMessage(message)) {
        return false;
    }
    
    if (message.isResponse()) {
        SipResponseCode code = message.getResponseCode();
        
        if (code >= SipResponseCode::Trying && code < SipResponseCode::OK) {
            handleProvisionalResponse(message);
        } else {
            handleFinalResponse(message);
        }
        
        return true;
    }
    
    return false;
}

bool ClientInviteTransaction::sendMessage(const SipMessage& message) {
    // Client transactions typically don't send messages except INVITE, ACK, CANCEL
    if (message.getMethod() == SipMethod::INVITE) {
        return sendInvite();
    } else if (message.getMethod() == SipMethod::ACK) {
        return sendAck(last_response_);
    } else if (message.getMethod() == SipMethod::CANCEL) {
        return sendCancel();
    }
    
    return false;
}

bool ClientInviteTransaction::canAcceptMessage(const SipMessage& message) const {
    if (!message.isResponse()) {
        return false;
    }
    
    // Check if response matches our INVITE
    return message.getHeaders().getCallId() == invite_.getHeaders().getCallId() &&
           message.getHeaders().getCSeq().find("INVITE") != std::string::npos;
}

bool ClientInviteTransaction::sendInvite() {
    if (state_ != TransactionState::CALLING) {
        return false;
    }
    
    startTimerA(); // Retransmission timer
    startTimerB(); // Transaction timeout
    
    notifyMessage(invite_);
    return true;
}

bool ClientInviteTransaction::sendAck(const SipMessage& final_response) {
    if (ack_sent_) {
        return true; // Already sent
    }
    
    // Create ACK message
    SipMessage ack(SipMethod::ACK, invite_.getRequestUri());
    ack.getHeaders().setCallId(invite_.getHeaders().getCallId());
    ack.getHeaders().setFrom(invite_.getHeaders().getFrom());
    ack.getHeaders().setTo(final_response.getHeaders().getTo()); // Include To tag from response
    ack.getHeaders().setCSeq(invite_.getHeaders().getCSeq().substr(0, invite_.getHeaders().getCSeq().find(' ')) + " ACK");
    ack.getHeaders().setVia(invite_.getHeaders().getVia());
    
    ack_sent_ = true;
    notifyMessage(ack);
    return true;
}

bool ClientInviteTransaction::sendCancel() {
    if (state_ != TransactionState::PROCEEDING) {
        return false; // Can only cancel after receiving provisional response
    }
    
    // Create CANCEL message
    SipMessage cancel(SipMethod::CANCEL, invite_.getRequestUri());
    cancel.getHeaders().setCallId(invite_.getHeaders().getCallId());
    cancel.getHeaders().setFrom(invite_.getHeaders().getFrom());
    cancel.getHeaders().setTo(invite_.getHeaders().getTo());
    cancel.getHeaders().setCSeq(invite_.getHeaders().getCSeq().substr(0, invite_.getHeaders().getCSeq().find(' ')) + " CANCEL");
    cancel.getHeaders().setVia(invite_.getHeaders().getVia());
    
    notifyMessage(cancel);
    return true;
}

void ClientInviteTransaction::handleProvisionalResponse(const SipMessage& response) {
    if (state_ == TransactionState::CALLING) {
        setState(TransactionState::PROCEEDING);
        stopTimer("TimerA"); // Stop retransmissions
    }
    
    last_response_ = response;
    notifyMessage(response);
}

void ClientInviteTransaction::handleFinalResponse(const SipMessage& response) {
    setState(TransactionState::COMPLETED);
    stopTimer("TimerA");
    stopTimer("TimerB");
    
    last_response_ = response;
    
    SipResponseCode code = response.getResponseCode();
    if (code >= SipResponseCode::OK && code < SipResponseCode::MultipleChoices) {
        // 2xx response - send ACK and terminate immediately
        sendAck(response);
        setState(TransactionState::TERMINATED);
    } else {
        // Error response - send ACK and start Timer D
        sendAck(response);
        startTimerD();
    }
    
    notifyMessage(response);
}

void ClientInviteTransaction::startTimerA() {
    startTimer("TimerA", std::chrono::milliseconds(500)); // T1 = 500ms
}

void ClientInviteTransaction::startTimerB() {
    startTimer("TimerB", std::chrono::seconds(32)); // 64*T1 = 32s
}

void ClientInviteTransaction::startTimerD() {
    startTimer("TimerD", std::chrono::seconds(32)); // 32s for UDP, 0 for TCP
}

// ClientNonInviteTransaction implementation
ClientNonInviteTransaction::ClientNonInviteTransaction(const std::string& transaction_id, const SipMessage& request)
    : Transaction(TransactionType::CLIENT_NON_INVITE, transaction_id), request_(request) {
    setState(TransactionState::TRYING_NON_INVITE);
}

bool ClientNonInviteTransaction::processMessage(const SipMessage& message) {
    if (!canAcceptMessage(message)) {
        return false;
    }
    
    if (message.isResponse()) {
        SipResponseCode code = message.getResponseCode();
        
        if (code >= SipResponseCode::Trying && code < SipResponseCode::OK) {
            handleProvisionalResponse(message);
        } else {
            handleFinalResponse(message);
        }
        
        return true;
    }
    
    return false;
}

bool ClientNonInviteTransaction::sendMessage(const SipMessage& message) {
    if (message.getMethod() == request_.getMethod()) {
        startTimerE(); // Retransmission timer
        startTimerF(); // Transaction timeout
        notifyMessage(request_);
        return true;
    }
    
    return false;
}

bool ClientNonInviteTransaction::canAcceptMessage(const SipMessage& message) const {
    if (!message.isResponse()) {
        return false;
    }
    
    // Check if response matches our request
    return message.getHeaders().getCallId() == request_.getHeaders().getCallId() &&
           message.getHeaders().getCSeq() == request_.getHeaders().getCSeq();
}

void ClientNonInviteTransaction::handleProvisionalResponse(const SipMessage& response) {
    if (state_ == TransactionState::TRYING_NON_INVITE) {
        setState(TransactionState::PROCEEDING_NON_INVITE);
        stopTimer("TimerE"); // Stop retransmissions
    }
    
    notifyMessage(response);
}

void ClientNonInviteTransaction::handleFinalResponse(const SipMessage& response) {
    setState(TransactionState::COMPLETED_NON_INVITE);
    stopTimer("TimerE");
    stopTimer("TimerF");
    
    startTimerK(); // Wait for retransmissions
    
    notifyMessage(response);
}

void ClientNonInviteTransaction::startTimerE() {
    startTimer("TimerE", std::chrono::milliseconds(500)); // T1 = 500ms
}

void ClientNonInviteTransaction::startTimerF() {
    startTimer("TimerF", std::chrono::seconds(32)); // 64*T1 = 32s
}

void ClientNonInviteTransaction::startTimerK() {
    startTimer("TimerK", std::chrono::seconds(5)); // T4 = 5s
}

// ServerInviteTransaction implementation
ServerInviteTransaction::ServerInviteTransaction(const std::string& transaction_id, const SipMessage& invite)
    : Transaction(TransactionType::SERVER_INVITE, transaction_id), invite_(invite) {
    setState(TransactionState::TRYING);
}

bool ServerInviteTransaction::processMessage(const SipMessage& message) {
    if (!canAcceptMessage(message)) {
        return false;
    }

    if (message.isRequest()) {
        if (message.getMethod() == SipMethod::ACK) {
            handleAck(message);
        } else if (message.getMethod() == SipMethod::CANCEL) {
            handleCancel(message);
        } else if (message.getMethod() == SipMethod::INVITE) {
            // Retransmitted INVITE - resend last response
            if (!last_response_.getHeaders().getCallId().empty()) {
                notifyMessage(last_response_);
            }
        }
        return true;
    }

    return false;
}

bool ServerInviteTransaction::sendMessage(const SipMessage& message) {
    if (message.isResponse()) {
        return sendResponse(message);
    }
    return false;
}

bool ServerInviteTransaction::canAcceptMessage(const SipMessage& message) const {
    if (!message.isRequest()) {
        return false;
    }

    // Check if request matches our INVITE transaction
    return message.getHeaders().getCallId() == invite_.getHeaders().getCallId() &&
           (message.getMethod() == SipMethod::INVITE ||
            message.getMethod() == SipMethod::ACK ||
            message.getMethod() == SipMethod::CANCEL);
}

bool ServerInviteTransaction::sendProvisionalResponse(SipResponseCode code, const std::string& reason) {
    if (final_response_sent_) {
        return false;
    }

    SipMessage response(code, reason.empty() ? "" : reason);
    response.getHeaders().setCallId(invite_.getHeaders().getCallId());
    response.getHeaders().setFrom(invite_.getHeaders().getFrom());
    response.getHeaders().setTo(invite_.getHeaders().getTo());
    response.getHeaders().setCSeq(invite_.getHeaders().getCSeq());
    response.getHeaders().setVia(invite_.getHeaders().getVia());

    return sendResponse(response);
}

bool ServerInviteTransaction::sendFinalResponse(SipResponseCode code, const std::string& reason) {
    SipMessage response(code, reason.empty() ? "" : reason);
    response.getHeaders().setCallId(invite_.getHeaders().getCallId());
    response.getHeaders().setFrom(invite_.getHeaders().getFrom());
    response.getHeaders().setTo(invite_.getHeaders().getTo());
    response.getHeaders().setCSeq(invite_.getHeaders().getCSeq());
    response.getHeaders().setVia(invite_.getHeaders().getVia());

    final_response_sent_ = true;
    return sendResponse(response);
}

bool ServerInviteTransaction::sendResponse(const SipMessage& response) {
    if (state_ == TransactionState::TRYING) {
        setState(TransactionState::PROCEEDING);
    }

    last_response_ = response;

    SipResponseCode code = response.getResponseCode();
    if (code >= SipResponseCode::OK) {
        // Final response
        final_response_sent_ = true;
        setState(TransactionState::COMPLETED);

        if (code >= SipResponseCode::MultipleChoices) {
            // Error response - start retransmission timer
            startTimerG();
            startTimerH(); // Wait for ACK
        }
        // For 2xx responses, the transaction terminates immediately
        // The dialog layer handles ACK for 2xx responses
    }

    notifyMessage(response);
    return true;
}

void ServerInviteTransaction::handleAck(const SipMessage& ack) {
    if (state_ == TransactionState::COMPLETED) {
        setState(TransactionState::CONFIRMED);
        stopTimer("TimerG");
        stopTimer("TimerH");
        startTimerI(); // Wait for ACK retransmissions
    }

    notifyMessage(ack);
}

void ServerInviteTransaction::handleCancel(const SipMessage& cancel) {
    // CANCEL creates its own transaction, but we should notify the application
    notifyMessage(cancel);
}

void ServerInviteTransaction::startTimerG() {
    startTimer("TimerG", std::chrono::milliseconds(500)); // T1 = 500ms
}

void ServerInviteTransaction::startTimerH() {
    startTimer("TimerH", std::chrono::seconds(64)); // 64*T1 = 64s
}

void ServerInviteTransaction::startTimerI() {
    startTimer("TimerI", std::chrono::seconds(5)); // T4 = 5s
}

// ServerNonInviteTransaction implementation
ServerNonInviteTransaction::ServerNonInviteTransaction(const std::string& transaction_id, const SipMessage& request)
    : Transaction(TransactionType::SERVER_NON_INVITE, transaction_id), request_(request) {
    setState(TransactionState::TRYING_NON_INVITE);
}

bool ServerNonInviteTransaction::processMessage(const SipMessage& message) {
    if (!canAcceptMessage(message)) {
        return false;
    }

    if (message.isRequest() && message.getMethod() == request_.getMethod()) {
        // Retransmitted request - resend last response
        if (!last_response_.getHeaders().getCallId().empty()) {
            notifyMessage(last_response_);
        }
        return true;
    }

    return false;
}

bool ServerNonInviteTransaction::sendMessage(const SipMessage& message) {
    if (message.isResponse()) {
        return sendResponse(message);
    }
    return false;
}

bool ServerNonInviteTransaction::canAcceptMessage(const SipMessage& message) const {
    if (!message.isRequest()) {
        return false;
    }

    // Check if request matches our transaction
    return message.getHeaders().getCallId() == request_.getHeaders().getCallId() &&
           message.getHeaders().getCSeq() == request_.getHeaders().getCSeq();
}

bool ServerNonInviteTransaction::sendProvisionalResponse(SipResponseCode code, const std::string& reason) {
    if (final_response_sent_) {
        return false;
    }

    SipMessage response(code, reason.empty() ? "" : reason);
    response.getHeaders().setCallId(request_.getHeaders().getCallId());
    response.getHeaders().setFrom(request_.getHeaders().getFrom());
    response.getHeaders().setTo(request_.getHeaders().getTo());
    response.getHeaders().setCSeq(request_.getHeaders().getCSeq());
    response.getHeaders().setVia(request_.getHeaders().getVia());

    return sendResponse(response);
}

bool ServerNonInviteTransaction::sendFinalResponse(SipResponseCode code, const std::string& reason) {
    SipMessage response(code, reason.empty() ? "" : reason);
    response.getHeaders().setCallId(request_.getHeaders().getCallId());
    response.getHeaders().setFrom(request_.getHeaders().getFrom());
    response.getHeaders().setTo(request_.getHeaders().getTo());
    response.getHeaders().setCSeq(request_.getHeaders().getCSeq());
    response.getHeaders().setVia(request_.getHeaders().getVia());

    final_response_sent_ = true;
    return sendResponse(response);
}

bool ServerNonInviteTransaction::sendResponse(const SipMessage& response) {
    if (state_ == TransactionState::TRYING_NON_INVITE) {
        setState(TransactionState::PROCEEDING_NON_INVITE);
    }

    last_response_ = response;

    SipResponseCode code = response.getResponseCode();
    if (code >= SipResponseCode::OK) {
        // Final response
        final_response_sent_ = true;
        setState(TransactionState::COMPLETED_NON_INVITE);
        startTimerJ(); // Wait for retransmissions
    }

    notifyMessage(response);
    return true;
}

void ServerNonInviteTransaction::startTimerJ() {
    startTimer("TimerJ", std::chrono::seconds(32)); // 64*T1 = 32s for UDP, 0 for TCP
}

// TransactionIdGenerator implementation
std::string TransactionIdGenerator::generateClientId(const SipMessage& request) {
    // Client transaction ID = hash(Request-URI + Via + CSeq + Call-ID + From + To)
    return hashMessage(request);
}

std::string TransactionIdGenerator::generateServerId(const SipMessage& request) {
    // Server transaction ID = hash(Via + CSeq + Call-ID + From + To)
    // Note: Request-URI is not included for server transactions
    std::ostringstream oss;
    oss << request.getHeaders().getVia()
        << request.getHeaders().getCSeq()
        << request.getHeaders().getCallId()
        << request.getHeaders().getFrom()
        << request.getHeaders().getTo();

    return std::to_string(std::hash<std::string>{}(oss.str()));
}

std::string TransactionIdGenerator::generateBranch() {
    // Generate RFC 3261 compliant branch parameter
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::ostringstream oss;
    oss << "z9hG4bK"; // Magic cookie for RFC 3261 compliance

    // Add 8 random hex characters
    for (int i = 0; i < 8; ++i) {
        oss << std::hex << dis(gen);
    }

    return oss.str();
}

std::string TransactionIdGenerator::hashMessage(const SipMessage& message) {
    std::ostringstream oss;

    if (message.isRequest()) {
        oss << message.getRequestUri().toString();
    }

    oss << message.getHeaders().getVia()
        << message.getHeaders().getCSeq()
        << message.getHeaders().getCallId()
        << message.getHeaders().getFrom()
        << message.getHeaders().getTo();

    return std::to_string(std::hash<std::string>{}(oss.str()));
}

} // namespace fmus::sip
