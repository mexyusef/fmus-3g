#pragma once

#include "message.hpp"
#include <string>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>
#include <mutex>

namespace fmus::sip {

// Transaction States
enum class TransactionState {
    // Client Transaction States
    CALLING,        // Initial INVITE sent
    PROCEEDING,     // 1xx response received
    COMPLETED,      // Final response received
    TERMINATED,     // Transaction finished
    
    // Server Transaction States  
    TRYING,         // Request received, processing
    CONFIRMED,      // ACK received (INVITE server only)
    
    // Non-INVITE states
    TRYING_NON_INVITE,      // Non-INVITE request received
    PROCEEDING_NON_INVITE,  // Provisional response sent
    COMPLETED_NON_INVITE    // Final response sent
};

// Transaction Types
enum class TransactionType {
    CLIENT_INVITE,
    CLIENT_NON_INVITE,
    SERVER_INVITE,
    SERVER_NON_INVITE
};

// Forward declarations
class TransactionManager;
class Dialog;

// Base Transaction class
class Transaction {
public:
    using StateCallback = std::function<void(TransactionState, TransactionState)>;
    using TimeoutCallback = std::function<void()>;
    using MessageCallback = std::function<void(const SipMessage&)>;

    Transaction(TransactionType type, const std::string& transaction_id);
    virtual ~Transaction();
    
    // Basic properties
    const std::string& getId() const { return transaction_id_; }
    TransactionType getType() const { return type_; }
    TransactionState getState() const { return state_; }
    
    // Message handling
    virtual bool processMessage(const SipMessage& message) = 0;
    virtual bool sendMessage(const SipMessage& message) = 0;
    
    // State management
    void setState(TransactionState new_state);
    
    // Callbacks
    void setStateCallback(StateCallback callback) { state_callback_ = callback; }
    void setTimeoutCallback(TimeoutCallback callback) { timeout_callback_ = callback; }
    void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    
    // Timing
    void startTimer(const std::string& timer_name, std::chrono::milliseconds duration);
    void stopTimer(const std::string& timer_name);
    void stopAllTimers();
    
    // Validation
    bool isTerminated() const { return state_ == TransactionState::TERMINATED; }
    virtual bool canAcceptMessage(const SipMessage& message) const = 0;
    
    // Dialog association
    void setDialog(std::shared_ptr<Dialog> dialog) { dialog_ = dialog; }
    std::shared_ptr<Dialog> getDialog() const { return dialog_.lock(); }

protected:
    void notifyStateChange(TransactionState old_state);
    void notifyTimeout();
    void notifyMessage(const SipMessage& message);
    
    TransactionType type_;
    std::string transaction_id_;
    std::atomic<TransactionState> state_;
    
    // Callbacks
    StateCallback state_callback_;
    TimeoutCallback timeout_callback_;
    MessageCallback message_callback_;
    
    // Dialog reference (weak to avoid circular dependencies)
    std::weak_ptr<Dialog> dialog_;
    
    // Synchronization
    mutable std::mutex mutex_;
    
    // Timer management
    struct Timer {
        std::string name;
        std::chrono::steady_clock::time_point expiry;
        std::chrono::milliseconds duration;
        bool active = false;
    };
    
    std::vector<Timer> timers_;
    void checkTimers();
};

// Client INVITE Transaction
class ClientInviteTransaction : public Transaction {
public:
    ClientInviteTransaction(const std::string& transaction_id, const SipMessage& invite);
    
    bool processMessage(const SipMessage& message) override;
    bool sendMessage(const SipMessage& message) override;
    bool canAcceptMessage(const SipMessage& message) const override;
    
    // INVITE-specific methods
    bool sendInvite();
    bool sendAck(const SipMessage& final_response);
    bool sendCancel();
    
    const SipMessage& getInvite() const { return invite_; }

private:
    void handleProvisionalResponse(const SipMessage& response);
    void handleFinalResponse(const SipMessage& response);
    void startTimerA(); // Retransmission timer
    void startTimerB(); // Transaction timeout
    void startTimerD(); // Wait time for response retransmissions
    
    SipMessage invite_;
    SipMessage last_response_;
    bool ack_sent_ = false;
    int retransmission_count_ = 0;
};

// Client Non-INVITE Transaction  
class ClientNonInviteTransaction : public Transaction {
public:
    ClientNonInviteTransaction(const std::string& transaction_id, const SipMessage& request);
    
    bool processMessage(const SipMessage& message) override;
    bool sendMessage(const SipMessage& message) override;
    bool canAcceptMessage(const SipMessage& message) const override;
    
    const SipMessage& getRequest() const { return request_; }

private:
    void handleProvisionalResponse(const SipMessage& response);
    void handleFinalResponse(const SipMessage& response);
    void startTimerE(); // Retransmission timer
    void startTimerF(); // Transaction timeout
    void startTimerK(); // Wait time for response retransmissions
    
    SipMessage request_;
    int retransmission_count_ = 0;
};

// Server INVITE Transaction
class ServerInviteTransaction : public Transaction {
public:
    ServerInviteTransaction(const std::string& transaction_id, const SipMessage& invite);
    
    bool processMessage(const SipMessage& message) override;
    bool sendMessage(const SipMessage& message) override;
    bool canAcceptMessage(const SipMessage& message) const override;
    
    // Server-specific methods
    bool sendProvisionalResponse(SipResponseCode code, const std::string& reason = "");
    bool sendFinalResponse(SipResponseCode code, const std::string& reason = "");
    bool sendResponse(const SipMessage& response);
    
    const SipMessage& getInvite() const { return invite_; }

private:
    void handleAck(const SipMessage& ack);
    void handleCancel(const SipMessage& cancel);
    void startTimerG(); // Response retransmission timer
    void startTimerH(); // Wait time for ACK
    void startTimerI(); // Wait time for ACK retransmissions
    
    SipMessage invite_;
    SipMessage last_response_;
    bool final_response_sent_ = false;
    int retransmission_count_ = 0;
};

// Server Non-INVITE Transaction
class ServerNonInviteTransaction : public Transaction {
public:
    ServerNonInviteTransaction(const std::string& transaction_id, const SipMessage& request);
    
    bool processMessage(const SipMessage& message) override;
    bool sendMessage(const SipMessage& message) override;
    bool canAcceptMessage(const SipMessage& message) const override;
    
    // Server-specific methods
    bool sendProvisionalResponse(SipResponseCode code, const std::string& reason = "");
    bool sendFinalResponse(SipResponseCode code, const std::string& reason = "");
    bool sendResponse(const SipMessage& response);
    
    const SipMessage& getRequest() const { return request_; }

private:
    void startTimerJ(); // Wait time for retransmissions
    
    SipMessage request_;
    SipMessage last_response_;
    bool final_response_sent_ = false;
};

// Transaction ID generation utilities
class TransactionIdGenerator {
public:
    // Generate transaction ID from SIP message (RFC 3261 Section 17.1.3)
    static std::string generateClientId(const SipMessage& request);
    static std::string generateServerId(const SipMessage& request);
    
    // Generate branch parameter for Via header
    static std::string generateBranch();

private:
    static std::string hashMessage(const SipMessage& message);
};

} // namespace fmus::sip
