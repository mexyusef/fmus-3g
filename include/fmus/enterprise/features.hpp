#pragma once

#include "../sip/message.hpp"
#include "../network/socket.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>
#include <mutex>

namespace fmus::enterprise {

// Presence States
enum class PresenceState {
    ONLINE,
    AWAY,
    BUSY,
    DO_NOT_DISTURB,
    OFFLINE,
    UNKNOWN
};

// Call Transfer Types
enum class TransferType {
    BLIND,      // Transfer without consultation
    ATTENDED    // Transfer with consultation
};

// Conference Types
enum class ConferenceType {
    AUDIO_ONLY,
    VIDEO,
    MIXED_MEDIA
};

// Message Types for Instant Messaging
enum class MessageType {
    TEXT,
    FILE,
    IMAGE,
    SYSTEM
};

// Presence Information
struct PresenceInfo {
    std::string user_id;
    PresenceState state;
    std::string status_message;
    std::chrono::system_clock::time_point last_update;
    std::string location;
    
    PresenceInfo() : state(PresenceState::OFFLINE), last_update(std::chrono::system_clock::now()) {}
    
    bool isExpired(std::chrono::seconds timeout = std::chrono::seconds(300)) const {
        auto now = std::chrono::system_clock::now();
        return (now - last_update) > timeout;
    }
};

// Instant Message
struct InstantMessage {
    std::string id;
    std::string from;
    std::string to;
    MessageType type;
    std::string content;
    std::chrono::system_clock::time_point timestamp;
    bool delivered = false;
    bool read = false;
    
    InstantMessage() : type(MessageType::TEXT), timestamp(std::chrono::system_clock::now()) {}
    
    std::string toJson() const;
    static InstantMessage fromJson(const std::string& json);
};

// Call Transfer Request
struct TransferRequest {
    std::string call_id;
    std::string transferor;  // Who is transferring
    std::string transferee;  // Who is being transferred
    std::string target;      // Transfer target
    TransferType type;
    std::string refer_to;    // SIP REFER-To header
    
    TransferRequest() : type(TransferType::BLIND) {}
};

// Conference Participant
struct ConferenceParticipant {
    std::string user_id;
    std::string display_name;
    bool audio_muted = false;
    bool video_muted = false;
    bool is_moderator = false;
    std::chrono::system_clock::time_point joined_time;
    
    ConferenceParticipant() : joined_time(std::chrono::system_clock::now()) {}
};

// Conference Room
struct ConferenceRoom {
    std::string room_id;
    std::string name;
    std::string moderator_id;
    ConferenceType type;
    std::vector<ConferenceParticipant> participants;
    std::chrono::system_clock::time_point created_time;
    bool recording_enabled = false;
    std::string recording_path;
    
    ConferenceRoom() : type(ConferenceType::AUDIO_ONLY), created_time(std::chrono::system_clock::now()) {}
    
    size_t getParticipantCount() const { return participants.size(); }
    bool hasParticipant(const std::string& user_id) const;
    void addParticipant(const ConferenceParticipant& participant);
    void removeParticipant(const std::string& user_id);
};

// Presence Manager
class PresenceManager {
public:
    using PresenceUpdateCallback = std::function<void(const PresenceInfo&)>;
    using SubscriptionCallback = std::function<void(const std::string&, const std::string&)>; // subscriber, presentity

    PresenceManager();
    ~PresenceManager();
    
    // Presence operations
    bool updatePresence(const std::string& user_id, PresenceState state, const std::string& status = "");
    PresenceInfo getPresence(const std::string& user_id) const;
    std::vector<PresenceInfo> getAllPresence() const;
    
    // Subscription management
    bool subscribe(const std::string& subscriber, const std::string& presentity);
    bool unsubscribe(const std::string& subscriber, const std::string& presentity);
    std::vector<std::string> getSubscribers(const std::string& presentity) const;
    std::vector<std::string> getSubscriptions(const std::string& subscriber) const;
    
    // Callbacks
    void setPresenceUpdateCallback(PresenceUpdateCallback callback) { presence_callback_ = callback; }
    void setSubscriptionCallback(SubscriptionCallback callback) { subscription_callback_ = callback; }
    
    // Maintenance
    void cleanupExpiredPresence();
    
    // Statistics
    size_t getPresenceCount() const;
    size_t getSubscriptionCount() const;

private:
    void notifySubscribers(const PresenceInfo& presence);
    
    std::unordered_map<std::string, PresenceInfo> presence_info_;
    std::unordered_map<std::string, std::vector<std::string>> subscriptions_; // presentity -> subscribers
    
    PresenceUpdateCallback presence_callback_;
    SubscriptionCallback subscription_callback_;
    
    mutable std::mutex mutex_;
};

// Instant Messaging Manager
class InstantMessagingManager {
public:
    using MessageCallback = std::function<void(const InstantMessage&)>;
    using DeliveryCallback = std::function<void(const std::string&, bool)>; // message_id, delivered

    InstantMessagingManager();
    ~InstantMessagingManager();
    
    // Message operations
    std::string sendMessage(const std::string& from, const std::string& to, 
                           const std::string& content, MessageType type = MessageType::TEXT);
    bool markDelivered(const std::string& message_id);
    bool markRead(const std::string& message_id);
    
    // Message retrieval
    std::vector<InstantMessage> getMessages(const std::string& user_id, size_t limit = 50) const;
    std::vector<InstantMessage> getConversation(const std::string& user1, const std::string& user2, size_t limit = 50) const;
    InstantMessage getMessage(const std::string& message_id) const;
    
    // Callbacks
    void setMessageCallback(MessageCallback callback) { message_callback_ = callback; }
    void setDeliveryCallback(DeliveryCallback callback) { delivery_callback_ = callback; }
    
    // Statistics
    size_t getMessageCount() const;
    size_t getUndeliveredCount() const;

private:
    std::string generateMessageId() const;
    
    std::unordered_map<std::string, InstantMessage> messages_;
    std::unordered_map<std::string, std::vector<std::string>> user_messages_; // user_id -> message_ids
    
    MessageCallback message_callback_;
    DeliveryCallback delivery_callback_;
    
    mutable std::mutex mutex_;
};

// Call Transfer Manager
class CallTransferManager {
public:
    using TransferCallback = std::function<void(const TransferRequest&, bool)>; // request, success

    CallTransferManager();
    ~CallTransferManager();
    
    // Transfer operations
    bool initiateBlindTransfer(const std::string& call_id, const std::string& transferor,
                              const std::string& transferee, const std::string& target);
    bool initiateAttendedTransfer(const std::string& call_id, const std::string& transferor,
                                 const std::string& transferee, const std::string& target);
    bool completeTransfer(const std::string& call_id);
    bool cancelTransfer(const std::string& call_id);
    
    // Transfer state
    std::vector<TransferRequest> getActiveTransfers() const;
    TransferRequest getTransfer(const std::string& call_id) const;
    
    // Callbacks
    void setTransferCallback(TransferCallback callback) { transfer_callback_ = callback; }
    
    // SIP REFER handling
    sip::SipMessage createReferMessage(const TransferRequest& request) const;
    bool processReferResponse(const sip::SipMessage& response);

private:
    std::unordered_map<std::string, TransferRequest> active_transfers_;
    
    TransferCallback transfer_callback_;
    
    mutable std::mutex mutex_;
};

// Conference Manager
class ConferenceManager {
public:
    using ConferenceCallback = std::function<void(const std::string&, const std::string&)>; // room_id, event
    using ParticipantCallback = std::function<void(const std::string&, const ConferenceParticipant&, bool)>; // room_id, participant, joined

    ConferenceManager();
    ~ConferenceManager();
    
    // Conference room operations
    std::string createConference(const std::string& name, const std::string& moderator_id, ConferenceType type);
    bool destroyConference(const std::string& room_id);
    ConferenceRoom getConference(const std::string& room_id) const;
    std::vector<ConferenceRoom> getAllConferences() const;
    
    // Participant operations
    bool joinConference(const std::string& room_id, const std::string& user_id, const std::string& display_name);
    bool leaveConference(const std::string& room_id, const std::string& user_id);
    bool muteParticipant(const std::string& room_id, const std::string& user_id, bool audio, bool video);
    bool setModerator(const std::string& room_id, const std::string& user_id);
    
    // Recording operations
    bool startRecording(const std::string& room_id, const std::string& output_path);
    bool stopRecording(const std::string& room_id);
    bool isRecording(const std::string& room_id) const;
    
    // Callbacks
    void setConferenceCallback(ConferenceCallback callback) { conference_callback_ = callback; }
    void setParticipantCallback(ParticipantCallback callback) { participant_callback_ = callback; }
    
    // Statistics
    size_t getConferenceCount() const;
    size_t getTotalParticipants() const;

private:
    std::string generateRoomId() const;
    void notifyConferenceEvent(const std::string& room_id, const std::string& event);
    void notifyParticipantEvent(const std::string& room_id, const ConferenceParticipant& participant, bool joined);
    
    std::unordered_map<std::string, ConferenceRoom> conferences_;
    
    ConferenceCallback conference_callback_;
    ParticipantCallback participant_callback_;
    
    mutable std::mutex mutex_;
};

// Call Recording Manager
class CallRecordingManager {
public:
    using RecordingCallback = std::function<void(const std::string&, const std::string&, bool)>; // call_id, file_path, started

    CallRecordingManager();
    ~CallRecordingManager();
    
    // Recording operations
    bool startRecording(const std::string& call_id, const std::string& output_path);
    bool stopRecording(const std::string& call_id);
    bool pauseRecording(const std::string& call_id);
    bool resumeRecording(const std::string& call_id);
    
    // Recording state
    bool isRecording(const std::string& call_id) const;
    std::string getRecordingPath(const std::string& call_id) const;
    std::vector<std::string> getActiveRecordings() const;
    
    // Callbacks
    void setRecordingCallback(RecordingCallback callback) { recording_callback_ = callback; }
    
    // File management
    std::vector<std::string> getRecordingFiles(const std::string& directory) const;
    bool deleteRecording(const std::string& file_path);

private:
    struct RecordingSession {
        std::string call_id;
        std::string file_path;
        bool active = false;
        bool paused = false;
        std::chrono::system_clock::time_point start_time;
    };
    
    std::unordered_map<std::string, RecordingSession> recordings_;
    
    RecordingCallback recording_callback_;
    
    mutable std::mutex mutex_;
};

// Enterprise Features Manager (coordinates all enterprise features)
class EnterpriseManager {
public:
    EnterpriseManager();
    ~EnterpriseManager();
    
    // Component access
    PresenceManager& getPresenceManager() { return presence_manager_; }
    InstantMessagingManager& getMessagingManager() { return messaging_manager_; }
    CallTransferManager& getTransferManager() { return transfer_manager_; }
    ConferenceManager& getConferenceManager() { return conference_manager_; }
    CallRecordingManager& getRecordingManager() { return recording_manager_; }
    
    // Unified operations
    bool initialize();
    void shutdown();
    
    // Maintenance
    void performMaintenance();
    
    // Statistics
    struct Stats {
        size_t active_presence = 0;
        size_t total_messages = 0;
        size_t active_transfers = 0;
        size_t active_conferences = 0;
        size_t active_recordings = 0;
    };
    
    Stats getStats() const;

private:
    PresenceManager presence_manager_;
    InstantMessagingManager messaging_manager_;
    CallTransferManager transfer_manager_;
    ConferenceManager conference_manager_;
    CallRecordingManager recording_manager_;
    
    std::atomic<bool> initialized_;
};

// Utility functions
std::string presenceStateToString(PresenceState state);
PresenceState stringToPresenceState(const std::string& state);

std::string messageTypeToString(MessageType type);
MessageType stringToMessageType(const std::string& type);

std::string transferTypeToString(TransferType type);
TransferType stringToTransferType(const std::string& type);

std::string conferenceTypeToString(ConferenceType type);
ConferenceType stringToConferenceType(const std::string& type);

} // namespace fmus::enterprise
