#include "fmus/enterprise/features.hpp"
#include "fmus/core/logger.hpp"
#include <sstream>
#include <random>
#include <algorithm>
#include <iomanip>

namespace fmus::enterprise {

// InstantMessage implementation
std::string InstantMessage::toJson() const {
    std::ostringstream oss;
    oss << "{"
        << "\"id\":\"" << id << "\","
        << "\"from\":\"" << from << "\","
        << "\"to\":\"" << to << "\","
        << "\"type\":\"" << messageTypeToString(type) << "\","
        << "\"content\":\"" << content << "\","
        << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count() << ","
        << "\"delivered\":" << (delivered ? "true" : "false") << ","
        << "\"read\":" << (read ? "true" : "false")
        << "}";
    return oss.str();
}

InstantMessage InstantMessage::fromJson(const std::string& json) {
    InstantMessage message;
    
    // Simple JSON parsing
    size_t id_pos = json.find("\"id\":");
    if (id_pos != std::string::npos) {
        size_t start = json.find("\"", id_pos + 5);
        if (start != std::string::npos) {
            size_t end = json.find("\"", start + 1);
            if (end != std::string::npos) {
                message.id = json.substr(start + 1, end - start - 1);
            }
        }
    }
    
    // Parse other fields similarly...
    return message;
}

// ConferenceRoom implementation
bool ConferenceRoom::hasParticipant(const std::string& user_id) const {
    return std::any_of(participants.begin(), participants.end(),
                      [&user_id](const ConferenceParticipant& p) { return p.user_id == user_id; });
}

void ConferenceRoom::addParticipant(const ConferenceParticipant& participant) {
    if (!hasParticipant(participant.user_id)) {
        participants.push_back(participant);
    }
}

void ConferenceRoom::removeParticipant(const std::string& user_id) {
    participants.erase(
        std::remove_if(participants.begin(), participants.end(),
                      [&user_id](const ConferenceParticipant& p) { return p.user_id == user_id; }),
        participants.end());
}

// PresenceManager implementation
PresenceManager::PresenceManager() {
}

PresenceManager::~PresenceManager() {
}

bool PresenceManager::updatePresence(const std::string& user_id, PresenceState state, const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    PresenceInfo& info = presence_info_[user_id];
    info.user_id = user_id;
    info.state = state;
    info.status_message = status;
    info.last_update = std::chrono::system_clock::now();
    
    notifySubscribers(info);
    
    core::Logger::info("Updated presence for {}: {} - {}", user_id, presenceStateToString(state), status);
    return true;
}

PresenceInfo PresenceManager::getPresence(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = presence_info_.find(user_id);
    if (it != presence_info_.end()) {
        return it->second;
    }
    
    PresenceInfo info;
    info.user_id = user_id;
    info.state = PresenceState::OFFLINE;
    return info;
}

std::vector<PresenceInfo> PresenceManager::getAllPresence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<PresenceInfo> result;
    for (const auto& [user_id, info] : presence_info_) {
        result.push_back(info);
    }
    
    return result;
}

bool PresenceManager::subscribe(const std::string& subscriber, const std::string& presentity) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& subscribers = subscriptions_[presentity];
    if (std::find(subscribers.begin(), subscribers.end(), subscriber) == subscribers.end()) {
        subscribers.push_back(subscriber);
        
        if (subscription_callback_) {
            subscription_callback_(subscriber, presentity);
        }
        
        core::Logger::info("User {} subscribed to presence of {}", subscriber, presentity);
        return true;
    }
    
    return false;
}

bool PresenceManager::unsubscribe(const std::string& subscriber, const std::string& presentity) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = subscriptions_.find(presentity);
    if (it != subscriptions_.end()) {
        auto& subscribers = it->second;
        subscribers.erase(std::remove(subscribers.begin(), subscribers.end(), subscriber), subscribers.end());
        
        if (subscribers.empty()) {
            subscriptions_.erase(it);
        }
        
        core::Logger::info("User {} unsubscribed from presence of {}", subscriber, presentity);
        return true;
    }
    
    return false;
}

std::vector<std::string> PresenceManager::getSubscribers(const std::string& presentity) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = subscriptions_.find(presentity);
    if (it != subscriptions_.end()) {
        return it->second;
    }
    
    return {};
}

std::vector<std::string> PresenceManager::getSubscriptions(const std::string& subscriber) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    for (const auto& [presentity, subscribers] : subscriptions_) {
        if (std::find(subscribers.begin(), subscribers.end(), subscriber) != subscribers.end()) {
            result.push_back(presentity);
        }
    }
    
    return result;
}

void PresenceManager::cleanupExpiredPresence() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = presence_info_.begin();
    while (it != presence_info_.end()) {
        if (it->second.isExpired()) {
            core::Logger::debug("Cleaning up expired presence for {}", it->first);
            it = presence_info_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t PresenceManager::getPresenceCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return presence_info_.size();
}

size_t PresenceManager::getSubscriptionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t count = 0;
    for (const auto& [presentity, subscribers] : subscriptions_) {
        count += subscribers.size();
    }
    return count;
}

void PresenceManager::notifySubscribers(const PresenceInfo& presence) {
    if (presence_callback_) {
        presence_callback_(presence);
    }
}

// InstantMessagingManager implementation
InstantMessagingManager::InstantMessagingManager() {
}

InstantMessagingManager::~InstantMessagingManager() {
}

std::string InstantMessagingManager::sendMessage(const std::string& from, const std::string& to, 
                                                const std::string& content, MessageType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    InstantMessage message;
    message.id = generateMessageId();
    message.from = from;
    message.to = to;
    message.type = type;
    message.content = content;
    message.timestamp = std::chrono::system_clock::now();
    
    messages_[message.id] = message;
    user_messages_[from].push_back(message.id);
    user_messages_[to].push_back(message.id);
    
    if (message_callback_) {
        message_callback_(message);
    }
    
    core::Logger::info("Message sent from {} to {}: {}", from, to, content.substr(0, 50));
    return message.id;
}

bool InstantMessagingManager::markDelivered(const std::string& message_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = messages_.find(message_id);
    if (it != messages_.end()) {
        it->second.delivered = true;
        
        if (delivery_callback_) {
            delivery_callback_(message_id, true);
        }
        
        return true;
    }
    
    return false;
}

bool InstantMessagingManager::markRead(const std::string& message_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = messages_.find(message_id);
    if (it != messages_.end()) {
        it->second.read = true;
        return true;
    }
    
    return false;
}

std::vector<InstantMessage> InstantMessagingManager::getMessages(const std::string& user_id, size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<InstantMessage> result;
    
    auto it = user_messages_.find(user_id);
    if (it != user_messages_.end()) {
        const auto& message_ids = it->second;
        
        size_t start = message_ids.size() > limit ? message_ids.size() - limit : 0;
        for (size_t i = start; i < message_ids.size(); ++i) {
            auto msg_it = messages_.find(message_ids[i]);
            if (msg_it != messages_.end()) {
                result.push_back(msg_it->second);
            }
        }
    }
    
    return result;
}

std::vector<InstantMessage> InstantMessagingManager::getConversation(const std::string& user1, const std::string& user2, size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<InstantMessage> result;
    
    for (const auto& [id, message] : messages_) {
        if ((message.from == user1 && message.to == user2) ||
            (message.from == user2 && message.to == user1)) {
            result.push_back(message);
        }
    }
    
    // Sort by timestamp
    std::sort(result.begin(), result.end(),
             [](const InstantMessage& a, const InstantMessage& b) {
                 return a.timestamp < b.timestamp;
             });
    
    // Limit results
    if (result.size() > limit) {
        result.erase(result.begin(), result.end() - limit);
    }
    
    return result;
}

InstantMessage InstantMessagingManager::getMessage(const std::string& message_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = messages_.find(message_id);
    if (it != messages_.end()) {
        return it->second;
    }
    
    return InstantMessage();
}

size_t InstantMessagingManager::getMessageCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return messages_.size();
}

size_t InstantMessagingManager::getUndeliveredCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t count = 0;
    for (const auto& [id, message] : messages_) {
        if (!message.delivered) {
            count++;
        }
    }
    return count;
}

std::string InstantMessagingManager::generateMessageId() const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::ostringstream oss;
    oss << "msg-";
    
    for (int i = 0; i < 16; ++i) {
        oss << std::hex << dis(gen);
    }
    
    return oss.str();
}

// Utility functions
std::string presenceStateToString(PresenceState state) {
    switch (state) {
        case PresenceState::ONLINE: return "online";
        case PresenceState::AWAY: return "away";
        case PresenceState::BUSY: return "busy";
        case PresenceState::DO_NOT_DISTURB: return "dnd";
        case PresenceState::OFFLINE: return "offline";
        default: return "unknown";
    }
}

PresenceState stringToPresenceState(const std::string& state) {
    if (state == "online") return PresenceState::ONLINE;
    if (state == "away") return PresenceState::AWAY;
    if (state == "busy") return PresenceState::BUSY;
    if (state == "dnd") return PresenceState::DO_NOT_DISTURB;
    if (state == "offline") return PresenceState::OFFLINE;
    return PresenceState::UNKNOWN;
}

std::string messageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::TEXT: return "text";
        case MessageType::FILE: return "file";
        case MessageType::IMAGE: return "image";
        case MessageType::SYSTEM: return "system";
        default: return "unknown";
    }
}

MessageType stringToMessageType(const std::string& type) {
    if (type == "text") return MessageType::TEXT;
    if (type == "file") return MessageType::FILE;
    if (type == "image") return MessageType::IMAGE;
    if (type == "system") return MessageType::SYSTEM;
    return MessageType::TEXT;
}

std::string transferTypeToString(TransferType type) {
    switch (type) {
        case TransferType::BLIND: return "blind";
        case TransferType::ATTENDED: return "attended";
        default: return "unknown";
    }
}

TransferType stringToTransferType(const std::string& type) {
    if (type == "blind") return TransferType::BLIND;
    if (type == "attended") return TransferType::ATTENDED;
    return TransferType::BLIND;
}

std::string conferenceTypeToString(ConferenceType type) {
    switch (type) {
        case ConferenceType::AUDIO_ONLY: return "audio";
        case ConferenceType::VIDEO: return "video";
        case ConferenceType::MIXED_MEDIA: return "mixed";
        default: return "unknown";
    }
}

ConferenceType stringToConferenceType(const std::string& type) {
    if (type == "audio") return ConferenceType::AUDIO_ONLY;
    if (type == "video") return ConferenceType::VIDEO;
    if (type == "mixed") return ConferenceType::MIXED_MEDIA;
    return ConferenceType::AUDIO_ONLY;
}

// EnterpriseManager implementation
EnterpriseManager::EnterpriseManager() : initialized_(false) {
}

EnterpriseManager::~EnterpriseManager() {
    shutdown();
}

bool EnterpriseManager::initialize() {
    if (initialized_) {
        return true;
    }

    initialized_ = true;
    core::Logger::info("Enterprise manager initialized");
    return true;
}

void EnterpriseManager::shutdown() {
    if (!initialized_) {
        return;
    }

    initialized_ = false;
    core::Logger::info("Enterprise manager shutdown");
}

void EnterpriseManager::performMaintenance() {
    presence_manager_.cleanupExpiredPresence();
    core::Logger::debug("Enterprise maintenance completed");
}

EnterpriseManager::Stats EnterpriseManager::getStats() const {
    Stats stats;
    stats.active_presence = presence_manager_.getPresenceCount();
    stats.total_messages = messaging_manager_.getMessageCount();
    stats.active_transfers = transfer_manager_.getActiveTransfers().size();
    stats.active_conferences = conference_manager_.getConferenceCount();
    stats.active_recordings = 0; // Placeholder

    return stats;
}

// ConferenceManager implementation
ConferenceManager::ConferenceManager() {
}

ConferenceManager::~ConferenceManager() {
}

std::string ConferenceManager::createConference(const std::string& name, const std::string& moderator_id, ConferenceType type) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string room_id = generateRoomId();

    ConferenceRoom room;
    room.room_id = room_id;
    room.name = name;
    room.moderator_id = moderator_id;
    room.type = type;
    room.created_time = std::chrono::system_clock::now();

    // Add moderator as first participant
    ConferenceParticipant moderator;
    moderator.user_id = moderator_id;
    moderator.display_name = moderator_id;
    moderator.is_moderator = true;
    room.addParticipant(moderator);

    conferences_[room_id] = room;

    notifyConferenceEvent(room_id, "created");
    notifyParticipantEvent(room_id, moderator, true);

    core::Logger::info("Created conference {} with moderator {}", room_id, moderator_id);
    return room_id;
}

bool ConferenceManager::destroyConference(const std::string& room_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = conferences_.find(room_id);
    if (it != conferences_.end()) {
        notifyConferenceEvent(room_id, "destroyed");
        conferences_.erase(it);

        core::Logger::info("Destroyed conference {}", room_id);
        return true;
    }

    return false;
}

ConferenceRoom ConferenceManager::getConference(const std::string& room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = conferences_.find(room_id);
    if (it != conferences_.end()) {
        return it->second;
    }

    return ConferenceRoom();
}

std::vector<ConferenceRoom> ConferenceManager::getAllConferences() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ConferenceRoom> result;
    for (const auto& [room_id, room] : conferences_) {
        result.push_back(room);
    }

    return result;
}

bool ConferenceManager::joinConference(const std::string& room_id, const std::string& user_id, const std::string& display_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = conferences_.find(room_id);
    if (it != conferences_.end()) {
        ConferenceParticipant participant;
        participant.user_id = user_id;
        participant.display_name = display_name;
        participant.joined_time = std::chrono::system_clock::now();

        it->second.addParticipant(participant);

        notifyParticipantEvent(room_id, participant, true);

        core::Logger::info("User {} joined conference {}", user_id, room_id);
        return true;
    }

    return false;
}

bool ConferenceManager::leaveConference(const std::string& room_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = conferences_.find(room_id);
    if (it != conferences_.end()) {
        // Find participant before removing
        ConferenceParticipant participant;
        for (const auto& p : it->second.participants) {
            if (p.user_id == user_id) {
                participant = p;
                break;
            }
        }

        it->second.removeParticipant(user_id);

        if (!participant.user_id.empty()) {
            notifyParticipantEvent(room_id, participant, false);
        }

        // Destroy conference if empty
        if (it->second.participants.empty()) {
            destroyConference(room_id);
        }

        core::Logger::info("User {} left conference {}", user_id, room_id);
        return true;
    }

    return false;
}

bool ConferenceManager::muteParticipant(const std::string& room_id, const std::string& user_id, bool audio, bool video) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = conferences_.find(room_id);
    if (it != conferences_.end()) {
        for (auto& participant : it->second.participants) {
            if (participant.user_id == user_id) {
                participant.audio_muted = audio;
                participant.video_muted = video;

                core::Logger::info("Muted participant {} in conference {}: audio={}, video={}",
                                  user_id, room_id, audio, video);
                return true;
            }
        }
    }

    return false;
}

bool ConferenceManager::setModerator(const std::string& room_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = conferences_.find(room_id);
    if (it != conferences_.end()) {
        // Remove moderator status from all participants
        for (auto& participant : it->second.participants) {
            participant.is_moderator = false;
        }

        // Set new moderator
        for (auto& participant : it->second.participants) {
            if (participant.user_id == user_id) {
                participant.is_moderator = true;
                it->second.moderator_id = user_id;

                core::Logger::info("Set {} as moderator of conference {}", user_id, room_id);
                return true;
            }
        }
    }

    return false;
}

bool ConferenceManager::startRecording(const std::string& room_id, const std::string& output_path) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = conferences_.find(room_id);
    if (it != conferences_.end()) {
        it->second.recording_enabled = true;
        it->second.recording_path = output_path;

        notifyConferenceEvent(room_id, "recording_started");

        core::Logger::info("Started recording conference {} to {}", room_id, output_path);
        return true;
    }

    return false;
}

bool ConferenceManager::stopRecording(const std::string& room_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = conferences_.find(room_id);
    if (it != conferences_.end()) {
        it->second.recording_enabled = false;

        notifyConferenceEvent(room_id, "recording_stopped");

        core::Logger::info("Stopped recording conference {}", room_id);
        return true;
    }

    return false;
}

bool ConferenceManager::isRecording(const std::string& room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = conferences_.find(room_id);
    if (it != conferences_.end()) {
        return it->second.recording_enabled;
    }

    return false;
}

size_t ConferenceManager::getConferenceCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return conferences_.size();
}

size_t ConferenceManager::getTotalParticipants() const {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t count = 0;
    for (const auto& [room_id, room] : conferences_) {
        count += room.participants.size();
    }
    return count;
}

std::string ConferenceManager::generateRoomId() const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::ostringstream oss;
    oss << "conf-";

    for (int i = 0; i < 12; ++i) {
        oss << std::hex << dis(gen);
    }

    return oss.str();
}

void ConferenceManager::notifyConferenceEvent(const std::string& room_id, const std::string& event) {
    if (conference_callback_) {
        conference_callback_(room_id, event);
    }
}

void ConferenceManager::notifyParticipantEvent(const std::string& room_id, const ConferenceParticipant& participant, bool joined) {
    if (participant_callback_) {
        participant_callback_(room_id, participant, joined);
    }
}

// CallTransferManager implementation
CallTransferManager::CallTransferManager() {
}

CallTransferManager::~CallTransferManager() {
}

bool CallTransferManager::initiateBlindTransfer(const std::string& call_id, const std::string& transferor,
                                               const std::string& transferee, const std::string& target) {
    std::lock_guard<std::mutex> lock(mutex_);

    TransferRequest request;
    request.call_id = call_id;
    request.transferor = transferor;
    request.transferee = transferee;
    request.target = target;
    request.type = TransferType::BLIND;
    request.refer_to = "sip:" + target;

    active_transfers_[call_id] = request;

    if (transfer_callback_) {
        transfer_callback_(request, true);
    }

    core::Logger::info("Initiated blind transfer for call {} from {} to {}", call_id, transferor, target);
    return true;
}

bool CallTransferManager::initiateAttendedTransfer(const std::string& call_id, const std::string& transferor,
                                                  const std::string& transferee, const std::string& target) {
    std::lock_guard<std::mutex> lock(mutex_);

    TransferRequest request;
    request.call_id = call_id;
    request.transferor = transferor;
    request.transferee = transferee;
    request.target = target;
    request.type = TransferType::ATTENDED;
    request.refer_to = "sip:" + target;

    active_transfers_[call_id] = request;

    if (transfer_callback_) {
        transfer_callback_(request, true);
    }

    core::Logger::info("Initiated attended transfer for call {} from {} to {}", call_id, transferor, target);
    return true;
}

bool CallTransferManager::completeTransfer(const std::string& call_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = active_transfers_.find(call_id);
    if (it != active_transfers_.end()) {
        active_transfers_.erase(it);
        core::Logger::info("Completed transfer for call {}", call_id);
        return true;
    }

    return false;
}

bool CallTransferManager::cancelTransfer(const std::string& call_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = active_transfers_.find(call_id);
    if (it != active_transfers_.end()) {
        if (transfer_callback_) {
            transfer_callback_(it->second, false);
        }

        active_transfers_.erase(it);
        core::Logger::info("Cancelled transfer for call {}", call_id);
        return true;
    }

    return false;
}

std::vector<TransferRequest> CallTransferManager::getActiveTransfers() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<TransferRequest> result;
    for (const auto& [call_id, request] : active_transfers_) {
        result.push_back(request);
    }

    return result;
}

TransferRequest CallTransferManager::getTransfer(const std::string& call_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = active_transfers_.find(call_id);
    if (it != active_transfers_.end()) {
        return it->second;
    }

    return TransferRequest();
}

sip::SipMessage CallTransferManager::createReferMessage(const TransferRequest& request) const {
    sip::SipUri target_uri("sip:" + request.transferee);
    sip::SipMessage refer(sip::SipMethod::REFER, target_uri);

    refer.getHeaders().setFrom("sip:" + request.transferor);
    refer.getHeaders().setTo("sip:" + request.transferee);
    refer.getHeaders().setCallId(request.call_id);
    refer.getHeaders().setCSeq("1 REFER");
    refer.getHeaders().set("Refer-To", request.refer_to);
    refer.getHeaders().set("Referred-By", "sip:" + request.transferor);

    return refer;
}

bool CallTransferManager::processReferResponse(const sip::SipMessage& response) {
    if (response.getResponseCode() == sip::SipResponseCode::Accepted) {
        std::string call_id = response.getHeaders().getCallId();
        return completeTransfer(call_id);
    }

    return false;
}

// CallRecordingManager implementation
CallRecordingManager::CallRecordingManager() {
}

CallRecordingManager::~CallRecordingManager() {
}

bool CallRecordingManager::startRecording(const std::string& call_id, const std::string& output_path) {
    std::lock_guard<std::mutex> lock(mutex_);

    RecordingSession session;
    session.call_id = call_id;
    session.file_path = output_path;
    session.active = true;
    session.start_time = std::chrono::system_clock::now();

    recordings_[call_id] = session;

    if (recording_callback_) {
        recording_callback_(call_id, output_path, true);
    }

    core::Logger::info("Started recording call {} to {}", call_id, output_path);
    return true;
}

bool CallRecordingManager::stopRecording(const std::string& call_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = recordings_.find(call_id);
    if (it != recordings_.end()) {
        it->second.active = false;

        if (recording_callback_) {
            recording_callback_(call_id, it->second.file_path, false);
        }

        core::Logger::info("Stopped recording call {}", call_id);
        return true;
    }

    return false;
}

bool CallRecordingManager::pauseRecording(const std::string& call_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = recordings_.find(call_id);
    if (it != recordings_.end() && it->second.active) {
        it->second.paused = true;
        core::Logger::info("Paused recording call {}", call_id);
        return true;
    }

    return false;
}

bool CallRecordingManager::resumeRecording(const std::string& call_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = recordings_.find(call_id);
    if (it != recordings_.end() && it->second.active) {
        it->second.paused = false;
        core::Logger::info("Resumed recording call {}", call_id);
        return true;
    }

    return false;
}

bool CallRecordingManager::isRecording(const std::string& call_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = recordings_.find(call_id);
    return (it != recordings_.end()) && it->second.active && !it->second.paused;
}

std::string CallRecordingManager::getRecordingPath(const std::string& call_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = recordings_.find(call_id);
    if (it != recordings_.end()) {
        return it->second.file_path;
    }

    return "";
}

std::vector<std::string> CallRecordingManager::getActiveRecordings() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> active;
    for (const auto& [call_id, session] : recordings_) {
        if (session.active && !session.paused) {
            active.push_back(call_id);
        }
    }

    return active;
}

std::vector<std::string> CallRecordingManager::getRecordingFiles(const std::string& directory) const {
    // Placeholder implementation - would scan directory in production
    return {"recording1.wav", "recording2.wav", "recording3.mp4"};
}

bool CallRecordingManager::deleteRecording(const std::string& file_path) {
    // Placeholder implementation - would delete file in production
    core::Logger::info("Deleted recording file: {}", file_path);
    return true;
}

} // namespace fmus::enterprise
