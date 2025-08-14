#pragma once

#include "message.hpp"
#include "transaction.hpp"
#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>

namespace fmus::sip {

// Dialog States
enum class DialogState {
    EARLY,      // Dialog created, not confirmed yet
    CONFIRMED,  // Dialog confirmed (2xx response received/sent)
    TERMINATED  // Dialog terminated
};

// Dialog class manages SIP dialogs (RFC 3261 Section 12)
class Dialog : public std::enable_shared_from_this<Dialog> {
public:
    using StateCallback = std::function<void(DialogState, DialogState)>;
    using MessageCallback = std::function<void(const SipMessage&)>;

    Dialog(const std::string& dialog_id, const SipMessage& initial_request);
    ~Dialog();
    
    // Basic properties
    const std::string& getId() const { return dialog_id_; }
    DialogState getState() const { return state_; }
    
    // Dialog identification (RFC 3261 Section 12.1.1)
    const std::string& getCallId() const { return call_id_; }
    const std::string& getLocalTag() const { return local_tag_; }
    const std::string& getRemoteTag() const { return remote_tag_; }
    
    // Route set management
    const std::vector<std::string>& getRouteSet() const { return route_set_; }
    void setRouteSet(const std::vector<std::string>& routes) { route_set_ = routes; }
    
    // Contact and target URI
    const std::string& getLocalUri() const { return local_uri_; }
    const std::string& getRemoteUri() const { return remote_uri_; }
    const std::string& getRemoteTarget() const { return remote_target_; }
    
    void setLocalUri(const std::string& uri) { local_uri_ = uri; }
    void setRemoteUri(const std::string& uri) { remote_uri_ = uri; }
    void setRemoteTarget(const std::string& target) { remote_target_ = target; }
    
    // Sequence numbers (CSeq)
    uint32_t getLocalCSeq() const { return local_cseq_; }
    uint32_t getRemoteCSeq() const { return remote_cseq_; }
    
    uint32_t getNextLocalCSeq() { return ++local_cseq_; }
    void updateRemoteCSeq(uint32_t cseq) { 
        if (cseq > remote_cseq_) remote_cseq_ = cseq; 
    }
    
    // State management
    void setState(DialogState new_state);
    bool isEarly() const { return state_ == DialogState::EARLY; }
    bool isConfirmed() const { return state_ == DialogState::CONFIRMED; }
    bool isTerminated() const { return state_ == DialogState::TERMINATED; }
    
    // Message processing
    bool processMessage(const SipMessage& message);
    SipMessage createRequest(SipMethod method) const;
    SipMessage createResponse(const SipMessage& request, SipResponseCode code, 
                             const std::string& reason = "") const;
    
    // Dialog establishment
    bool establishDialog(const SipMessage& response);
    void confirmDialog();
    void terminateDialog();
    
    // Transaction management
    void addTransaction(std::shared_ptr<Transaction> transaction);
    void removeTransaction(const std::string& transaction_id);
    std::shared_ptr<Transaction> findTransaction(const std::string& transaction_id) const;
    
    // Callbacks
    void setStateCallback(StateCallback callback) { state_callback_ = callback; }
    void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    
    // Validation
    bool validateMessage(const SipMessage& message) const;
    static std::string generateDialogId(const SipMessage& message);

private:
    void notifyStateChange(DialogState old_state);
    void notifyMessage(const SipMessage& message);
    void extractDialogInfo(const SipMessage& message);
    uint32_t extractCSeq(const std::string& cseq_header) const;
    
    std::string dialog_id_;
    std::atomic<DialogState> state_;
    
    // Dialog identification
    std::string call_id_;
    std::string local_tag_;
    std::string remote_tag_;
    
    // URIs and routing
    std::string local_uri_;
    std::string remote_uri_;
    std::string remote_target_;
    std::vector<std::string> route_set_;
    
    // Sequence numbers
    std::atomic<uint32_t> local_cseq_;
    std::atomic<uint32_t> remote_cseq_;
    
    // Associated transactions
    std::vector<std::weak_ptr<Transaction>> transactions_;
    
    // Callbacks
    StateCallback state_callback_;
    MessageCallback message_callback_;
    
    // Synchronization
    mutable std::mutex mutex_;
};

// Dialog Manager
class DialogManager {
public:
    using DialogCallback = std::function<void(std::shared_ptr<Dialog>)>;
    using MessageCallback = std::function<void(const SipMessage&, std::shared_ptr<Dialog>)>;

    DialogManager();
    ~DialogManager();
    
    // Dialog management
    std::shared_ptr<Dialog> createDialog(const SipMessage& initial_request);
    std::shared_ptr<Dialog> findDialog(const std::string& dialog_id) const;
    std::shared_ptr<Dialog> findDialogByMessage(const SipMessage& message) const;
    
    void removeDialog(const std::string& dialog_id);
    void removeDialog(std::shared_ptr<Dialog> dialog);
    
    // Message routing
    bool routeMessage(const SipMessage& message);
    
    // Statistics
    size_t getDialogCount() const;
    std::vector<std::shared_ptr<Dialog>> getAllDialogs() const;
    
    // Callbacks
    void setDialogCreatedCallback(DialogCallback callback) { dialog_created_callback_ = callback; }
    void setDialogTerminatedCallback(DialogCallback callback) { dialog_terminated_callback_ = callback; }
    void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    
    // Cleanup
    void cleanup(); // Remove terminated dialogs

private:
    void onDialogStateChanged(std::shared_ptr<Dialog> dialog, DialogState old_state, DialogState new_state);
    
    std::vector<std::shared_ptr<Dialog>> dialogs_;
    
    // Callbacks
    DialogCallback dialog_created_callback_;
    DialogCallback dialog_terminated_callback_;
    MessageCallback message_callback_;
    
    // Synchronization
    mutable std::mutex mutex_;
};

} // namespace fmus::sip
