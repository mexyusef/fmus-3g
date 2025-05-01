#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>

#include <fmus/core/error.hpp>
#include <fmus/core/task.hpp>
#include <fmus/core/event.hpp>
#include <fmus/media/media.hpp>
#include <fmus/rtp/rtp.hpp>

namespace fmus::sip {

// Forward declarations
class SipAgent;
class SipCall;
class SipMessage;
class SipRegistration;
class SipUri;
class SdpSession;

// SIP error codes
enum class SipErrorCode {
    Success = 0,
    InvalidMessage,
    NetworkError,
    RegistrationFailed,
    CallFailed,
    AuthenticationFailed,
    Timeout,
    InvalidState,
    NotImplemented,
    UnknownError
};

// SIP error class
class SipError : public core::Error {
public:
    explicit SipError(SipErrorCode code, const std::string& message);
    SipErrorCode sipCode() const noexcept { return code_; }

private:
    SipErrorCode code_;
};

// SIP Response Codes (RFC 3261)
enum class SipResponseCode {
    // Informational
    Trying = 100,
    Ringing = 180,
    CallIsBeingForwarded = 181,
    Queued = 182,
    SessionProgress = 183,

    // Success
    OK = 200,

    // Redirection
    MultipleChoices = 300,
    MovedPermanently = 301,
    MovedTemporarily = 302,
    UseProxy = 305,
    AlternativeService = 380,

    // Client Error
    BadRequest = 400,
    Unauthorized = 401,
    PaymentRequired = 402,
    Forbidden = 403,
    NotFound = 404,
    MethodNotAllowed = 405,
    NotAcceptable = 406,
    ProxyAuthenticationRequired = 407,
    RequestTimeout = 408,
    Gone = 410,
    RequestEntityTooLarge = 413,
    RequestURITooLong = 414,
    UnsupportedMediaType = 415,
    UnsupportedURIScheme = 416,
    BadExtension = 420,
    ExtensionRequired = 421,
    IntervalTooBrief = 423,
    TemporarilyUnavailable = 480,
    CallTransactionDoesNotExist = 481,
    LoopDetected = 482,
    TooManyHops = 483,
    AddressIncomplete = 484,
    Ambiguous = 485,
    BusyHere = 486,
    RequestTerminated = 487,
    NotAcceptableHere = 488,
    RequestPending = 491,
    Undecipherable = 493,

    // Server Error
    ServerInternalError = 500,
    NotImplemented = 501,
    BadGateway = 502,
    ServiceUnavailable = 503,
    ServerTimeout = 504,
    VersionNotSupported = 505,
    MessageTooLarge = 513,

    // Global Failure
    BusyEverywhere = 600,
    Decline = 603,
    DoesNotExistAnywhere = 604,
    NotAcceptable606 = 606
};

// SIP Methods (RFC 3261 and extensions)
enum class SipMethod {
    REGISTER,
    INVITE,
    ACK,
    BYE,
    CANCEL,
    OPTIONS,
    REFER,
    SUBSCRIBE,
    NOTIFY,
    MESSAGE,
    INFO,
    PRACK,
    UPDATE
};

// SIP Transport Protocol
enum class SipTransport {
    UDP,
    TCP,
    TLS,
    SCTP,
    WS,    // WebSocket
    WSS    // Secure WebSocket
};

// SIP URI class
class SipUri {
public:
    SipUri() = default;
    SipUri(const std::string& uri);
    SipUri(const std::string& user, const std::string& host, uint16_t port = 0);

    // Getters
    const std::string& scheme() const { return scheme_; }
    const std::string& user() const { return user_; }
    const std::string& host() const { return host_; }
    uint16_t port() const { return port_; }
    const std::map<std::string, std::string>& parameters() const { return parameters_; }

    // Setters
    void setScheme(const std::string& scheme) { scheme_ = scheme; }
    void setUser(const std::string& user) { user_ = user; }
    void setHost(const std::string& host) { host_ = host; }
    void setPort(uint16_t port) { port_ = port; }
    void addParameter(const std::string& name, const std::string& value) { parameters_[name] = value; }

    // Convert to string
    std::string toString() const;

private:
    std::string scheme_ = "sip";
    std::string user_;
    std::string host_;
    uint16_t port_ = 0;
    std::map<std::string, std::string> parameters_;
};

// SDP (Session Description Protocol) Session
class SdpSession {
public:
    SdpSession() = default;

    // Getters
    const std::string& sessionId() const { return session_id_; }
    const std::string& sessionName() const { return session_name_; }
    const std::string& username() const { return username_; }
    const std::string& connectionAddress() const { return connection_address_; }
    int audioPort() const { return audio_port_; }
    int videoPort() const { return video_port_; }
    const std::vector<rtp::RtpPayloadType>& audioPayloadTypes() const { return audio_payload_types_; }
    const std::vector<rtp::RtpPayloadType>& videoPayloadTypes() const { return video_payload_types_; }

    // Setters
    void setSessionId(const std::string& id) { session_id_ = id; }
    void setSessionName(const std::string& name) { session_name_ = name; }
    void setUsername(const std::string& username) { username_ = username; }
    void setConnectionAddress(const std::string& address) { connection_address_ = address; }
    void setAudioPort(int port) { audio_port_ = port; }
    void setVideoPort(int port) { video_port_ = port; }
    void addAudioPayloadType(rtp::RtpPayloadType pt) { audio_payload_types_.push_back(pt); }
    void addVideoPayloadType(rtp::RtpPayloadType pt) { video_payload_types_.push_back(pt); }

    // Parse from string
    static SdpSession parse(const std::string& sdp);

    // Convert to string
    std::string toString() const;

private:
    std::string session_id_;
    std::string session_name_ = "fmus-3g";
    std::string username_ = "fmus";
    std::string connection_address_ = "127.0.0.1";
    int audio_port_ = 0;
    int video_port_ = 0;
    std::vector<rtp::RtpPayloadType> audio_payload_types_;
    std::vector<rtp::RtpPayloadType> video_payload_types_;
};

// SIP Message Header
class SipHeader {
public:
    SipHeader() = default;

    void addField(const std::string& name, const std::string& value);
    bool hasField(const std::string& name) const;
    std::string getField(const std::string& name) const;
    std::vector<std::string> getFields(const std::string& name) const;

    // Common header accessors
    void setFrom(const SipUri& uri, const std::string& tag = "");
    void setTo(const SipUri& uri, const std::string& tag = "");
    void setCallId(const std::string& call_id);
    void setCSeq(uint32_t seq, SipMethod method);
    void setContact(const SipUri& uri);
    void setVia(const std::string& branch, const std::string& host, uint16_t port, SipTransport transport);
    void setContentType(const std::string& type);
    void setContentLength(size_t length);

    SipUri getFrom() const;
    std::string getFromTag() const;
    SipUri getTo() const;
    std::string getToTag() const;
    std::string getCallId() const;
    uint32_t getCSeqNumber() const;
    SipMethod getCSeqMethod() const;
    SipUri getContact() const;
    std::string getVia() const;
    std::string getContentType() const;
    size_t getContentLength() const;

    // Serialize to string
    std::string toString() const;

private:
    std::multimap<std::string, std::string> fields_;
};

// SIP Message
class SipMessage {
public:
    SipMessage() = default;

    // Request constructor
    SipMessage(SipMethod method, const SipUri& uri);

    // Response constructor
    SipMessage(SipResponseCode code, const std::string& reason = "");

    // Type check
    bool isRequest() const { return is_request_; }
    bool isResponse() const { return !is_request_; }

    // Request accessors
    SipMethod getMethod() const { return method_; }
    const SipUri& getRequestUri() const { return request_uri_; }

    // Response accessors
    SipResponseCode getResponseCode() const { return response_code_; }
    const std::string& getReasonPhrase() const { return reason_phrase_; }

    // Header accessors
    SipHeader& header() { return header_; }
    const SipHeader& header() const { return header_; }

    // Body accessors
    void setBody(const std::string& body) {
        body_ = body;
        header_.setContentLength(body.size());
    }
    const std::string& getBody() const { return body_; }

    // SDP session
    void setSdpSession(const SdpSession& sdp) {
        setBody(sdp.toString());
        header_.setContentType("application/sdp");
    }

    SdpSession getSdpSession() const {
        if (header_.getContentType() == "application/sdp") {
            return SdpSession::parse(body_);
        }
        return SdpSession();
    }

    // Parse from string
    static SipMessage parse(const std::string& message);

    // Serialize to string
    std::string toString() const;

private:
    bool is_request_ = true;
    SipMethod method_ = SipMethod::INVITE;
    SipUri request_uri_;
    SipResponseCode response_code_ = SipResponseCode::OK;
    std::string reason_phrase_ = "OK";
    SipHeader header_;
    std::string body_;
};

// Call direction
enum class CallDirection {
    Outgoing,
    Incoming
};

// Call state
enum class CallState {
    Null,           // Initial state
    Calling,        // Outgoing call, INVITE sent
    Proceeding,     // Provisional response received
    Ringing,        // Call is ringing
    Connecting,     // Early media/pre-answer
    Active,         // Call is active
    Holding,        // Call is on hold
    Held,           // Call is being held
    Resuming,       // Resuming from hold
    Transferring,   // Call is being transferred
    Terminating,    // Call is being terminated
    Terminated      // Call is terminated
};

// Registration state
enum class RegistrationState {
    Unregistered,   // Not registered
    Registering,    // Registration in progress
    Registered,     // Successfully registered
    Refreshing,     // Refreshing registration
    Failing,        // Registration failing
    Failed          // Registration failed
};

// SIP Registration
class SipRegistration {
public:
    virtual ~SipRegistration() = default;

    // Registration control
    virtual core::Task<void> start() = 0;
    virtual core::Task<void> stop() = 0;
    virtual core::Task<void> refresh() = 0;

    // Registration info
    virtual RegistrationState getState() const = 0;
    virtual uint32_t getExpirySeconds() const = 0;
    virtual const SipUri& getRegistrarUri() const = 0;
    virtual const SipUri& getAccountUri() const = 0;

    // Events
    core::EventEmitter<RegistrationState> onStateChanged;
    core::EventEmitter<const SipError&> onError;
};

// SIP Call
class SipCall {
public:
    virtual ~SipCall() = default;

    // Call control
    virtual core::Task<void> answer() = 0;
    virtual core::Task<void> reject(SipResponseCode code = SipResponseCode::Decline) = 0;
    virtual core::Task<void> hangup() = 0;
    virtual core::Task<void> hold() = 0;
    virtual core::Task<void> resume() = 0;
    virtual core::Task<void> sendDtmf(char digit) = 0;

    // Call info
    virtual CallState getState() const = 0;
    virtual CallDirection getDirection() const = 0;
    virtual const SipUri& getRemoteUri() const = 0;
    virtual const SipUri& getLocalUri() const = 0;
    virtual std::chrono::seconds getDuration() const = 0;
    virtual const std::string& getCallId() const = 0;

    // Media handling
    virtual std::shared_ptr<rtp::RtpSender> getAudioSender() = 0;
    virtual std::shared_ptr<rtp::RtpSender> getVideoSender() = 0;
    virtual std::shared_ptr<rtp::RtpReceiver> getAudioReceiver() = 0;
    virtual std::shared_ptr<rtp::RtpReceiver> getVideoReceiver() = 0;

    // Events
    core::EventEmitter<CallState> onStateChanged;
    core::EventEmitter<const SipError&> onError;
    core::EventEmitter<const SipMessage&> onMessageReceived;
};

// SIP Agent configuration
struct SipAgentConfig {
    SipUri local_uri;
    SipUri outbound_proxy;
    std::string username;
    std::string password;
    std::string display_name;
    SipTransport transport = SipTransport::UDP;
    uint16_t local_port = 5060;
    uint16_t rtp_port_min = 10000;
    uint16_t rtp_port_max = 20000;
    bool use_ice = false;
    bool use_srtp = false;
    std::chrono::seconds registration_timeout = std::chrono::seconds(3600);
    std::chrono::seconds transaction_timeout = std::chrono::seconds(32);
};

// SIP Agent
class SipAgent {
public:
    virtual ~SipAgent() = default;

    // Agent control
    virtual core::Task<void> start() = 0;
    virtual core::Task<void> stop() = 0;

    // Registration
    virtual std::shared_ptr<SipRegistration> createRegistration(
        const SipUri& registrar_uri) = 0;

    // Call control
    virtual std::shared_ptr<SipCall> createCall(const SipUri& target_uri) = 0;

    // Configuration
    virtual const SipAgentConfig& getConfig() const = 0;
    virtual void setConfig(const SipAgentConfig& config) = 0;

    // Factory method
    static std::shared_ptr<SipAgent> create(const SipAgentConfig& config);

    // Events
    core::EventEmitter<std::shared_ptr<SipCall>> onIncomingCall;
    core::EventEmitter<const SipMessage&> onMessageReceived;
    core::EventEmitter<const SipError&> onError;
};

} // namespace fmus::sip