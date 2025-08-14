#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace fmus::sip {

enum class SipMethod {
    INVITE,
    ACK,
    BYE,
    CANCEL,
    REGISTER,
    OPTIONS,
    INFO,
    PRACK,
    UPDATE,
    REFER,
    NOTIFY,
    SUBSCRIBE,
    MESSAGE
};

enum class SipResponseCode {
    // 1xx Provisional
    Trying = 100,
    Ringing = 180,
    SessionProgress = 183,
    
    // 2xx Success
    OK = 200,
    Accepted = 202,
    
    // 3xx Redirection
    MultipleChoices = 300,
    MovedPermanently = 301,
    MovedTemporarily = 302,
    
    // 4xx Client Error
    BadRequest = 400,
    Unauthorized = 401,
    Forbidden = 403,
    NotFound = 404,
    MethodNotAllowed = 405,
    RequestTimeout = 408,
    
    // 5xx Server Error
    ServerInternalError = 500,
    NotImplemented = 501,
    BadGateway = 502,
    ServiceUnavailable = 503,
    
    // 6xx Global Failure
    BusyEverywhere = 600,
    Decline = 603
};

class SipUri {
public:
    SipUri() = default;
    SipUri(const std::string& uri);
    
    std::string toString() const;
    
    std::string scheme;
    std::string user;
    std::string host;
    int port = 5060;
    std::unordered_map<std::string, std::string> parameters;
};

class SipHeaders {
public:
    void set(const std::string& name, const std::string& value);
    std::string get(const std::string& name) const;
    bool has(const std::string& name) const;
    void remove(const std::string& name);
    
    // Convenience methods for common headers
    void setFrom(const std::string& from) { set("From", from); }
    void setTo(const std::string& to) { set("To", to); }
    void setCallId(const std::string& call_id) { set("Call-ID", call_id); }
    void setCSeq(const std::string& cseq) { set("CSeq", cseq); }
    void setVia(const std::string& via) { set("Via", via); }
    void setContact(const std::string& contact) { set("Contact", contact); }
    void setContentType(const std::string& content_type) { set("Content-Type", content_type); }
    void setContentLength(size_t length) { set("Content-Length", std::to_string(length)); }
    
    std::string getFrom() const { return get("From"); }
    std::string getTo() const { return get("To"); }
    std::string getCallId() const { return get("Call-ID"); }
    std::string getCSeq() const { return get("CSeq"); }
    std::string getVia() const { return get("Via"); }
    std::string getContact() const { return get("Contact"); }
    std::string getContentType() const { return get("Content-Type"); }
    size_t getContentLength() const;
    
    const std::unordered_map<std::string, std::string>& getAll() const { return headers_; }

private:
    std::unordered_map<std::string, std::string> headers_;
};

class SipMessage {
public:
    SipMessage() = default;
    
    // Request constructor
    SipMessage(SipMethod method, const SipUri& request_uri);
    
    // Response constructor  
    SipMessage(SipResponseCode code, const std::string& reason_phrase = "");
    
    bool isRequest() const { return is_request_; }
    bool isResponse() const { return !is_request_; }
    
    // Request methods
    SipMethod getMethod() const { return method_; }
    const SipUri& getRequestUri() const { return request_uri_; }
    
    // Response methods
    SipResponseCode getResponseCode() const { return response_code_; }
    const std::string& getReasonPhrase() const { return reason_phrase_; }
    
    // Common methods
    SipHeaders& getHeaders() { return headers_; }
    const SipHeaders& getHeaders() const { return headers_; }
    
    void setBody(const std::string& body) { body_ = body; }
    const std::string& getBody() const { return body_; }
    
    std::string toString() const;
    static SipMessage fromString(const std::string& message);

private:
    bool is_request_ = true;
    
    // Request fields
    SipMethod method_ = SipMethod::INVITE;
    SipUri request_uri_;
    
    // Response fields
    SipResponseCode response_code_ = SipResponseCode::OK;
    std::string reason_phrase_;
    
    // Common fields
    SipHeaders headers_;
    std::string body_;
};

// Utility functions
std::string methodToString(SipMethod method);
SipMethod stringToMethod(const std::string& method);
std::string responseCodeToString(SipResponseCode code);

} // namespace fmus::sip
