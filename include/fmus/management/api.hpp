#pragma once

#include "../network/socket.hpp"
#include "../enterprise/features.hpp"
#include "../sip/registrar.hpp"
#include "../webrtc/signaling.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>

namespace fmus::management {

// HTTP Methods
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    PATCH,
    OPTIONS
};

// HTTP Status Codes
enum class HttpStatus {
    OK = 200,
    CREATED = 201,
    NO_CONTENT = 204,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    METHOD_NOT_ALLOWED = 405,
    CONFLICT = 409,
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    SERVICE_UNAVAILABLE = 503
};

// HTTP Request
struct HttpRequest {
    HttpMethod method;
    std::string path;
    std::string query_string;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    
    // Parsed query parameters
    std::unordered_map<std::string, std::string> query_params;
    
    // Path parameters (e.g., /users/{id})
    std::unordered_map<std::string, std::string> path_params;
    
    HttpRequest() : method(HttpMethod::GET) {}
    
    std::string getHeader(const std::string& name) const;
    std::string getQueryParam(const std::string& name) const;
    std::string getPathParam(const std::string& name) const;
    
    static HttpRequest parse(const std::string& raw_request);
};

// HTTP Response
struct HttpResponse {
    HttpStatus status;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    
    HttpResponse(HttpStatus s = HttpStatus::OK) : status(s) {
        headers["Content-Type"] = "application/json";
        headers["Server"] = "FMUS-3G/1.0";
    }
    
    void setJson(const std::string& json);
    void setHtml(const std::string& html);
    void setPlainText(const std::string& text);
    void setCors();
    
    std::string toString() const;
};

// REST API Endpoint Handler
using ApiHandler = std::function<HttpResponse(const HttpRequest&)>;

// Route Pattern Matcher
class RouteMatcher {
public:
    RouteMatcher(const std::string& pattern);
    
    bool matches(const std::string& path) const;
    std::unordered_map<std::string, std::string> extractParams(const std::string& path) const;

private:
    std::string pattern_;
    std::vector<std::string> param_names_;
    std::string regex_pattern_;
};

// REST API Server
class RestApiServer {
public:
    using MiddlewareHandler = std::function<bool(HttpRequest&, HttpResponse&)>; // return false to stop processing

    RestApiServer();
    ~RestApiServer();
    
    // Server management
    bool start(const network::SocketAddress& bind_address);
    void stop();
    bool isRunning() const { return running_; }
    
    // Route registration
    void addRoute(HttpMethod method, const std::string& pattern, ApiHandler handler);
    void addMiddleware(MiddlewareHandler middleware);
    
    // Convenience methods for common routes
    void get(const std::string& pattern, ApiHandler handler) { addRoute(HttpMethod::GET, pattern, handler); }
    void post(const std::string& pattern, ApiHandler handler) { addRoute(HttpMethod::POST, pattern, handler); }
    void put(const std::string& pattern, ApiHandler handler) { addRoute(HttpMethod::PUT, pattern, handler); }
    void del(const std::string& pattern, ApiHandler handler) { addRoute(HttpMethod::DELETE, pattern, handler); }
    
    // Static file serving
    void serveStatic(const std::string& path_prefix, const std::string& directory);
    
    // CORS support
    void enableCors(const std::string& origins = "*");

private:
    struct Route {
        HttpMethod method;
        std::unique_ptr<RouteMatcher> matcher;
        ApiHandler handler;
    };
    
    void onNewConnection(std::shared_ptr<network::Socket> connection);
    void handleRequest(const std::string& raw_request, std::shared_ptr<network::TcpSocket> connection);
    HttpResponse processRequest(const HttpRequest& request);
    HttpResponse handleNotFound(const HttpRequest& request);
    HttpResponse handleError(const std::exception& e);
    
    std::shared_ptr<network::TcpSocket> server_socket_;
    std::atomic<bool> running_;
    
    std::vector<Route> routes_;
    std::vector<MiddlewareHandler> middlewares_;
    
    // Static file serving
    std::unordered_map<std::string, std::string> static_paths_;
    
    // CORS settings
    bool cors_enabled_ = false;
    std::string cors_origins_;
    
    mutable std::mutex mutex_;
};

// Management API Implementation
class ManagementApi {
public:
    ManagementApi(enterprise::EnterpriseManager& enterprise_mgr,
                  sip::RegistrationManager& reg_mgr,
                  webrtc::SignalingServer& signaling_server);
    ~ManagementApi();
    
    // API setup
    void setupRoutes(RestApiServer& server);
    
    // System endpoints
    HttpResponse getSystemStatus(const HttpRequest& request);
    HttpResponse getSystemStats(const HttpRequest& request);
    HttpResponse getSystemConfig(const HttpRequest& request);
    HttpResponse updateSystemConfig(const HttpRequest& request);
    
    // User management endpoints
    HttpResponse getUsers(const HttpRequest& request);
    HttpResponse getUser(const HttpRequest& request);
    HttpResponse createUser(const HttpRequest& request);
    HttpResponse updateUser(const HttpRequest& request);
    HttpResponse deleteUser(const HttpRequest& request);
    
    // Registration endpoints
    HttpResponse getRegistrations(const HttpRequest& request);
    HttpResponse getRegistration(const HttpRequest& request);
    HttpResponse forceUnregister(const HttpRequest& request);
    
    // Presence endpoints
    HttpResponse getPresence(const HttpRequest& request);
    HttpResponse updatePresence(const HttpRequest& request);
    HttpResponse getPresenceSubscriptions(const HttpRequest& request);
    
    // Conference endpoints
    HttpResponse getConferences(const HttpRequest& request);
    HttpResponse getConference(const HttpRequest& request);
    HttpResponse createConference(const HttpRequest& request);
    HttpResponse updateConference(const HttpRequest& request);
    HttpResponse deleteConference(const HttpRequest& request);
    HttpResponse joinConference(const HttpRequest& request);
    HttpResponse leaveConference(const HttpRequest& request);
    
    // Call management endpoints
    HttpResponse getActiveCalls(const HttpRequest& request);
    HttpResponse getCall(const HttpRequest& request);
    HttpResponse transferCall(const HttpRequest& request);
    HttpResponse hangupCall(const HttpRequest& request);
    
    // Messaging endpoints
    HttpResponse getMessages(const HttpRequest& request);
    HttpResponse sendMessage(const HttpRequest& request);
    HttpResponse getConversation(const HttpRequest& request);
    
    // Recording endpoints
    HttpResponse getRecordings(const HttpRequest& request);
    HttpResponse startRecording(const HttpRequest& request);
    HttpResponse stopRecording(const HttpRequest& request);
    HttpResponse downloadRecording(const HttpRequest& request);
    
    // WebRTC signaling endpoints
    HttpResponse getSignalingSessions(const HttpRequest& request);
    HttpResponse getSignalingStats(const HttpRequest& request);

private:
    // JSON utilities
    std::string toJson(const std::unordered_map<std::string, std::string>& map) const;
    std::string toJson(const std::vector<std::string>& vec) const;
    std::unordered_map<std::string, std::string> fromJson(const std::string& json) const;
    
    // Validation utilities
    bool validateUserData(const std::unordered_map<std::string, std::string>& data) const;
    bool validateConferenceData(const std::unordered_map<std::string, std::string>& data) const;
    
    enterprise::EnterpriseManager& enterprise_mgr_;
    sip::RegistrationManager& reg_mgr_;
    webrtc::SignalingServer& signaling_server_;
};

// Web Dashboard Server
class WebDashboard {
public:
    WebDashboard(ManagementApi& api);
    ~WebDashboard();
    
    // Dashboard setup
    void setupRoutes(RestApiServer& server);
    
    // Dashboard pages
    HttpResponse serveDashboard(const HttpRequest& request);
    HttpResponse serveUsers(const HttpRequest& request);
    HttpResponse serveConferences(const HttpRequest& request);
    HttpResponse serveSystem(const HttpRequest& request);
    HttpResponse serveLogin(const HttpRequest& request);
    
    // Static resources
    HttpResponse serveCSS(const HttpRequest& request);
    HttpResponse serveJS(const HttpRequest& request);
    
private:
    std::string generateDashboardHtml() const;
    std::string generateUsersHtml() const;
    std::string generateConferencesHtml() const;
    std::string generateSystemHtml() const;
    std::string generateLoginHtml() const;
    
    std::string getBaseTemplate() const;
    std::string getCSS() const;
    std::string getJS() const;
    
    ManagementApi& api_;
};

// Authentication Middleware
class AuthMiddleware {
public:
    AuthMiddleware(const std::string& secret_key);
    
    bool authenticate(HttpRequest& request, HttpResponse& response);
    std::string generateToken(const std::string& username) const;
    bool validateToken(const std::string& token) const;
    
private:
    std::string secret_key_;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> active_tokens_;
    mutable std::mutex mutex_;
};

// Logging Middleware
class LoggingMiddleware {
public:
    LoggingMiddleware();
    
    bool logRequest(HttpRequest& request, HttpResponse& response);

private:
    void logAccess(const HttpRequest& request, HttpStatus status, size_t response_size);
};

// Rate Limiting Middleware
class RateLimitMiddleware {
public:
    RateLimitMiddleware(size_t max_requests_per_minute = 60);
    
    bool checkRateLimit(HttpRequest& request, HttpResponse& response);

private:
    struct ClientInfo {
        size_t request_count = 0;
        std::chrono::steady_clock::time_point window_start;
    };
    
    size_t max_requests_per_minute_;
    std::unordered_map<std::string, ClientInfo> clients_;
    mutable std::mutex mutex_;
};

// Utility functions
std::string httpMethodToString(HttpMethod method);
HttpMethod stringToHttpMethod(const std::string& method);

std::string httpStatusToString(HttpStatus status);
int httpStatusToCode(HttpStatus status);

std::string urlDecode(const std::string& encoded);
std::string urlEncode(const std::string& decoded);

std::string extractClientIp(const HttpRequest& request);
std::string getCurrentTimestamp();

} // namespace fmus::management
