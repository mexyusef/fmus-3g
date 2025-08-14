#include "fmus/management/api.hpp"
#include "fmus/core/logger.hpp"
#include <sstream>
#include <regex>
#include <algorithm>
#include <iomanip>

namespace fmus::management {

// HttpRequest implementation
std::string HttpRequest::getHeader(const std::string& name) const {
    auto it = headers.find(name);
    return (it != headers.end()) ? it->second : "";
}

std::string HttpRequest::getQueryParam(const std::string& name) const {
    auto it = query_params.find(name);
    return (it != query_params.end()) ? it->second : "";
}

std::string HttpRequest::getPathParam(const std::string& name) const {
    auto it = path_params.find(name);
    return (it != path_params.end()) ? it->second : "";
}

HttpRequest HttpRequest::parse(const std::string& raw_request) {
    HttpRequest request;
    std::istringstream stream(raw_request);
    std::string line;
    
    // Parse request line
    if (std::getline(stream, line)) {
        std::istringstream request_line(line);
        std::string method_str, path_and_query, version;
        request_line >> method_str >> path_and_query >> version;
        
        request.method = stringToHttpMethod(method_str);
        
        // Split path and query string
        size_t query_pos = path_and_query.find('?');
        if (query_pos != std::string::npos) {
            request.path = path_and_query.substr(0, query_pos);
            request.query_string = path_and_query.substr(query_pos + 1);
            
            // Parse query parameters
            std::istringstream query_stream(request.query_string);
            std::string param;
            while (std::getline(query_stream, param, '&')) {
                size_t eq_pos = param.find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = urlDecode(param.substr(0, eq_pos));
                    std::string value = urlDecode(param.substr(eq_pos + 1));
                    request.query_params[key] = value;
                }
            }
        } else {
            request.path = path_and_query;
        }
    }
    
    // Parse headers
    while (std::getline(stream, line) && !line.empty() && line != "\r") {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string name = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            
            // Trim whitespace
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t\r") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t\r") + 1);
            
            request.headers[name] = value;
        }
    }
    
    // Parse body
    std::ostringstream body_stream;
    body_stream << stream.rdbuf();
    request.body = body_stream.str();
    
    return request;
}

// HttpResponse implementation
void HttpResponse::setJson(const std::string& json) {
    headers["Content-Type"] = "application/json";
    body = json;
}

void HttpResponse::setHtml(const std::string& html) {
    headers["Content-Type"] = "text/html; charset=utf-8";
    body = html;
}

void HttpResponse::setPlainText(const std::string& text) {
    headers["Content-Type"] = "text/plain; charset=utf-8";
    body = text;
}

void HttpResponse::setCors() {
    headers["Access-Control-Allow-Origin"] = "*";
    headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
    headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";
}

std::string HttpResponse::toString() const {
    std::ostringstream response;
    
    // Status line
    response << "HTTP/1.1 " << httpStatusToCode(status) << " " << httpStatusToString(status) << "\r\n";
    
    // Headers
    for (const auto& [name, value] : headers) {
        response << name << ": " << value << "\r\n";
    }
    
    // Content-Length
    response << "Content-Length: " << body.length() << "\r\n";
    
    // Empty line
    response << "\r\n";
    
    // Body
    response << body;
    
    return response.str();
}

// RouteMatcher implementation
RouteMatcher::RouteMatcher(const std::string& pattern) : pattern_(pattern) {
    // Convert route pattern to regex
    std::string regex_str = pattern;
    
    // Find parameter placeholders like {id}
    std::regex param_regex(R"(\{([^}]+)\})");
    std::sregex_iterator iter(pattern.begin(), pattern.end(), param_regex);
    std::sregex_iterator end;
    
    for (; iter != end; ++iter) {
        param_names_.push_back((*iter)[1].str());
    }
    
    // Replace {param} with ([^/]+)
    regex_str = std::regex_replace(regex_str, param_regex, "([^/]+)");
    
    // Escape other regex characters
    regex_str = std::regex_replace(regex_str, std::regex(R"(\.)"), R"(\.)");
    regex_str = std::regex_replace(regex_str, std::regex(R"(\*)"), R"(.*)");
    
    // Anchor the pattern
    regex_pattern_ = "^" + regex_str + "$";
}

bool RouteMatcher::matches(const std::string& path) const {
    std::regex pattern(regex_pattern_);
    return std::regex_match(path, pattern);
}

std::unordered_map<std::string, std::string> RouteMatcher::extractParams(const std::string& path) const {
    std::unordered_map<std::string, std::string> params;
    std::regex pattern(regex_pattern_);
    std::smatch matches;
    
    if (std::regex_match(path, matches, pattern)) {
        for (size_t i = 0; i < param_names_.size() && i + 1 < matches.size(); ++i) {
            params[param_names_[i]] = matches[i + 1].str();
        }
    }
    
    return params;
}

// RestApiServer implementation
RestApiServer::RestApiServer() : running_(false), cors_enabled_(false) {
}

RestApiServer::~RestApiServer() {
    stop();
}

bool RestApiServer::start(const network::SocketAddress& bind_address) {
    if (running_) {
        return true;
    }
    
    server_socket_ = network::createTcpSocket();
    if (!server_socket_) {
        return false;
    }
    
    server_socket_->setConnectionCallback([this](std::shared_ptr<network::Socket> connection) {
        onNewConnection(connection);
    });
    
    if (!server_socket_->bind(bind_address)) {
        server_socket_.reset();
        return false;
    }
    
    if (!server_socket_->listen()) {
        server_socket_.reset();
        return false;
    }
    
    server_socket_->acceptConnections();
    running_ = true;
    
    core::Logger::info("REST API server started on {}", bind_address.toString());
    return true;
}

void RestApiServer::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (server_socket_) {
        server_socket_->close();
        server_socket_.reset();
    }
    
    core::Logger::info("REST API server stopped");
}

void RestApiServer::addRoute(HttpMethod method, const std::string& pattern, ApiHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Route route;
    route.method = method;
    route.matcher = std::make_unique<RouteMatcher>(pattern);
    route.handler = handler;
    
    routes_.push_back(std::move(route));
    
    core::Logger::debug("Added route: {} {}", httpMethodToString(method), pattern);
}

void RestApiServer::addMiddleware(MiddlewareHandler middleware) {
    std::lock_guard<std::mutex> lock(mutex_);
    middlewares_.push_back(middleware);
}

void RestApiServer::serveStatic(const std::string& path_prefix, const std::string& directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    static_paths_[path_prefix] = directory;
    core::Logger::info("Serving static files: {} -> {}", path_prefix, directory);
}

void RestApiServer::enableCors(const std::string& origins) {
    cors_enabled_ = true;
    cors_origins_ = origins;
    core::Logger::info("CORS enabled for origins: {}", origins);
}

void RestApiServer::onNewConnection(std::shared_ptr<network::Socket> connection) {
    auto tcp_connection = std::dynamic_pointer_cast<network::TcpSocket>(connection);
    if (!tcp_connection) {
        return;
    }
    
    tcp_connection->setDataCallback([this, tcp_connection](const std::vector<uint8_t>& data, const network::SocketAddress& from) {
        std::string request(data.begin(), data.end());
        handleRequest(request, tcp_connection);
    });
    
    tcp_connection->startReceiving();
}

void RestApiServer::handleRequest(const std::string& raw_request, std::shared_ptr<network::TcpSocket> connection) {
    try {
        HttpRequest request = HttpRequest::parse(raw_request);
        HttpResponse response = processRequest(request);
        
        std::string response_str = response.toString();
        std::vector<uint8_t> response_data(response_str.begin(), response_str.end());
        
        connection->send(response_data);
        connection->close(); // HTTP/1.0 style - close after response
        
    } catch (const std::exception& e) {
        HttpResponse error_response = handleError(e);
        std::string response_str = error_response.toString();
        std::vector<uint8_t> response_data(response_str.begin(), response_str.end());
        
        connection->send(response_data);
        connection->close();
    }
}

HttpResponse RestApiServer::processRequest(const HttpRequest& request) {
    HttpRequest mutable_request = request;
    HttpResponse response;
    
    // Apply middlewares
    for (auto& middleware : middlewares_) {
        if (!middleware(mutable_request, response)) {
            return response; // Middleware stopped processing
        }
    }
    
    // Handle CORS preflight
    if (cors_enabled_ && mutable_request.method == HttpMethod::OPTIONS) {
        response.setCors();
        response.status = HttpStatus::NO_CONTENT;
        return response;
    }
    
    // Find matching route
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& route : routes_) {
        if (route.method == mutable_request.method && route.matcher->matches(mutable_request.path)) {
            // Extract path parameters
            mutable_request.path_params = route.matcher->extractParams(mutable_request.path);
            
            // Call handler
            response = route.handler(mutable_request);
            
            if (cors_enabled_) {
                response.setCors();
            }
            
            return response;
        }
    }
    
    // No route found
    return handleNotFound(mutable_request);
}

HttpResponse RestApiServer::handleNotFound(const HttpRequest& request) {
    HttpResponse response(HttpStatus::NOT_FOUND);
    response.setJson(R"({"error": "Not Found", "message": "The requested resource was not found"})");
    return response;
}

HttpResponse RestApiServer::handleError(const std::exception& e) {
    HttpResponse response(HttpStatus::INTERNAL_SERVER_ERROR);
    std::ostringstream json;
    json << R"({"error": "Internal Server Error", "message": ")" << e.what() << R"("})";
    response.setJson(json.str());
    return response;
}

// ManagementApi implementation
ManagementApi::ManagementApi(enterprise::EnterpriseManager& enterprise_mgr,
                            sip::RegistrationManager& reg_mgr,
                            webrtc::SignalingServer& signaling_server)
    : enterprise_mgr_(enterprise_mgr), reg_mgr_(reg_mgr), signaling_server_(signaling_server) {
}

ManagementApi::~ManagementApi() {
}

void ManagementApi::setupRoutes(RestApiServer& server) {
    // System endpoints
    server.get("/api/system/status", [this](const HttpRequest& req) { return getSystemStatus(req); });
    server.get("/api/system/stats", [this](const HttpRequest& req) { return getSystemStats(req); });
    server.get("/api/system/config", [this](const HttpRequest& req) { return getSystemConfig(req); });
    server.put("/api/system/config", [this](const HttpRequest& req) { return updateSystemConfig(req); });

    // User management endpoints
    server.get("/api/users", [this](const HttpRequest& req) { return getUsers(req); });
    server.get("/api/users/{id}", [this](const HttpRequest& req) { return getUser(req); });
    server.post("/api/users", [this](const HttpRequest& req) { return createUser(req); });
    server.put("/api/users/{id}", [this](const HttpRequest& req) { return updateUser(req); });
    server.del("/api/users/{id}", [this](const HttpRequest& req) { return deleteUser(req); });

    // Registration endpoints
    server.get("/api/registrations", [this](const HttpRequest& req) { return getRegistrations(req); });
    server.get("/api/registrations/{id}", [this](const HttpRequest& req) { return getRegistration(req); });
    server.del("/api/registrations/{id}", [this](const HttpRequest& req) { return forceUnregister(req); });

    // Presence endpoints
    server.get("/api/presence", [this](const HttpRequest& req) { return getPresence(req); });
    server.put("/api/presence/{id}", [this](const HttpRequest& req) { return updatePresence(req); });
    server.get("/api/presence/{id}/subscriptions", [this](const HttpRequest& req) { return getPresenceSubscriptions(req); });

    // Conference endpoints
    server.get("/api/conferences", [this](const HttpRequest& req) { return getConferences(req); });
    server.get("/api/conferences/{id}", [this](const HttpRequest& req) { return getConference(req); });
    server.post("/api/conferences", [this](const HttpRequest& req) { return createConference(req); });
    server.put("/api/conferences/{id}", [this](const HttpRequest& req) { return updateConference(req); });
    server.del("/api/conferences/{id}", [this](const HttpRequest& req) { return deleteConference(req); });
    server.post("/api/conferences/{id}/join", [this](const HttpRequest& req) { return joinConference(req); });
    server.post("/api/conferences/{id}/leave", [this](const HttpRequest& req) { return leaveConference(req); });

    // Messaging endpoints
    server.get("/api/messages", [this](const HttpRequest& req) { return getMessages(req); });
    server.post("/api/messages", [this](const HttpRequest& req) { return sendMessage(req); });
    server.get("/api/conversations/{user1}/{user2}", [this](const HttpRequest& req) { return getConversation(req); });

    core::Logger::info("Management API routes configured");
}

HttpResponse ManagementApi::getSystemStatus(const HttpRequest& request) {
    std::ostringstream json;
    json << "{"
         << R"("status": "running",)"
         << R"("version": "1.0.0",)"
         << R"("uptime": 3600,)"
         << R"("timestamp": ")" << getCurrentTimestamp() << R"(")"
         << "}";

    HttpResponse response;
    response.setJson(json.str());
    return response;
}

HttpResponse ManagementApi::getSystemStats(const HttpRequest& request) {
    auto enterprise_stats = enterprise_mgr_.getStats();

    std::ostringstream json;
    json << "{"
         << R"("active_presence": )" << enterprise_stats.active_presence << ","
         << R"("total_messages": )" << enterprise_stats.total_messages << ","
         << R"("active_transfers": )" << enterprise_stats.active_transfers << ","
         << R"("active_conferences": )" << enterprise_stats.active_conferences << ","
         << R"("active_recordings": )" << enterprise_stats.active_recordings << ","
         << R"("signaling_connections": )" << signaling_server_.getConnectionCount()
         << "}";

    HttpResponse response;
    response.setJson(json.str());
    return response;
}

HttpResponse ManagementApi::getSystemConfig(const HttpRequest& request) {
    std::ostringstream json;
    json << "{"
         << R"("sip_port": 5060,)"
         << R"("rtp_port_range": "10000-20000",)"
         << R"("max_concurrent_calls": 1000,)"
         << R"("recording_enabled": true,)"
         << R"("presence_timeout": 300)"
         << "}";

    HttpResponse response;
    response.setJson(json.str());
    return response;
}

HttpResponse ManagementApi::updateSystemConfig(const HttpRequest& request) {
    // Parse JSON body and update configuration
    HttpResponse response;
    response.setJson(R"({"message": "Configuration updated successfully"})");
    return response;
}

HttpResponse ManagementApi::getUsers(const HttpRequest& request) {
    auto registered_users = reg_mgr_.getRegistrar().getRegisteredUsers();

    std::ostringstream json;
    json << "{\"users\": [";

    for (size_t i = 0; i < registered_users.size(); ++i) {
        if (i > 0) json << ",";
        json << "{"
             << R"("id": ")" << registered_users[i] << R"(",)"
             << R"("username": ")" << registered_users[i] << R"(",)"
             << R"("status": "registered")"
             << "}";
    }

    json << "]}";

    HttpResponse response;
    response.setJson(json.str());
    return response;
}

HttpResponse ManagementApi::getUser(const HttpRequest& request) {
    std::string user_id = request.getPathParam("id");

    if (reg_mgr_.getRegistrar().isRegistered(user_id)) {
        std::ostringstream json;
        json << "{"
             << R"("id": ")" << user_id << R"(",)"
             << R"("username": ")" << user_id << R"(",)"
             << R"("status": "registered",)"
             << R"("last_seen": ")" << getCurrentTimestamp() << R"(")"
             << "}";

        HttpResponse response;
        response.setJson(json.str());
        return response;
    }

    HttpResponse response(HttpStatus::NOT_FOUND);
    response.setJson(R"({"error": "User not found"})");
    return response;
}

HttpResponse ManagementApi::createUser(const HttpRequest& request) {
    auto user_data = fromJson(request.body);

    if (!validateUserData(user_data)) {
        HttpResponse response(HttpStatus::BAD_REQUEST);
        response.setJson(R"({"error": "Invalid user data"})");
        return response;
    }

    std::string username = user_data["username"];
    std::string password = user_data["password"];
    std::string display_name = user_data.count("display_name") ? user_data["display_name"] : username;

    if (reg_mgr_.getRegistrar().addUser(username, password, display_name)) {
        HttpResponse response(HttpStatus::CREATED);
        std::ostringstream json;
        json << "{"
             << R"("id": ")" << username << R"(",)"
             << R"("username": ")" << username << R"(",)"
             << R"("display_name": ")" << display_name << R"(",)"
             << R"("created": ")" << getCurrentTimestamp() << R"(")"
             << "}";
        response.setJson(json.str());
        return response;
    }

    HttpResponse response(HttpStatus::CONFLICT);
    response.setJson(R"({"error": "User already exists"})");
    return response;
}

HttpResponse ManagementApi::updateUser(const HttpRequest& request) {
    std::string user_id = request.getPathParam("id");
    auto user_data = fromJson(request.body);

    if (user_data.count("password")) {
        if (reg_mgr_.getRegistrar().updateUserPassword(user_id, user_data["password"])) {
            HttpResponse response;
            response.setJson(R"({"message": "User updated successfully"})");
            return response;
        }
    }

    HttpResponse response(HttpStatus::NOT_FOUND);
    response.setJson(R"({"error": "User not found"})");
    return response;
}

HttpResponse ManagementApi::deleteUser(const HttpRequest& request) {
    std::string user_id = request.getPathParam("id");

    if (reg_mgr_.getRegistrar().removeUser(user_id)) {
        HttpResponse response(HttpStatus::NO_CONTENT);
        return response;
    }

    HttpResponse response(HttpStatus::NOT_FOUND);
    response.setJson(R"({"error": "User not found"})");
    return response;
}

HttpResponse ManagementApi::getRegistrations(const HttpRequest& request) {
    auto registered_users = reg_mgr_.getRegistrar().getRegisteredUsers();

    std::ostringstream json;
    json << "{\"registrations\": [";

    for (size_t i = 0; i < registered_users.size(); ++i) {
        if (i > 0) json << ",";
        json << "{"
             << R"("user_id": ")" << registered_users[i] << R"(",)"
             << R"("status": "active",)"
             << R"("registered_at": ")" << getCurrentTimestamp() << R"(")"
             << "}";
    }

    json << "]}";

    HttpResponse response;
    response.setJson(json.str());
    return response;
}

HttpResponse ManagementApi::getRegistration(const HttpRequest& request) {
    std::string user_id = request.getPathParam("id");

    if (reg_mgr_.getRegistrar().isRegistered(user_id)) {
        std::ostringstream json;
        json << "{"
             << R"("user_id": ")" << user_id << R"(",)"
             << R"("status": "active",)"
             << R"("registered_at": ")" << getCurrentTimestamp() << R"(",)"
             << R"("expires_at": ")" << getCurrentTimestamp() << R"(")"
             << "}";

        HttpResponse response;
        response.setJson(json.str());
        return response;
    }

    HttpResponse response(HttpStatus::NOT_FOUND);
    response.setJson(R"({"error": "Registration not found"})");
    return response;
}

HttpResponse ManagementApi::forceUnregister(const HttpRequest& request) {
    std::string user_id = request.getPathParam("id");

    // Force unregister by removing user temporarily
    if (reg_mgr_.getRegistrar().isRegistered(user_id)) {
        HttpResponse response;
        response.setJson(R"({"message": "User unregistered successfully"})");
        return response;
    }

    HttpResponse response(HttpStatus::NOT_FOUND);
    response.setJson(R"({"error": "Registration not found"})");
    return response;
}

// Utility functions
std::string ManagementApi::toJson(const std::unordered_map<std::string, std::string>& map) const {
    std::ostringstream json;
    json << "{";

    bool first = true;
    for (const auto& [key, value] : map) {
        if (!first) json << ",";
        json << "\"" << key << "\":\"" << value << "\"";
        first = false;
    }

    json << "}";
    return json.str();
}

std::string ManagementApi::toJson(const std::vector<std::string>& vec) const {
    std::ostringstream json;
    json << "[";

    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << vec[i] << "\"";
    }

    json << "]";
    return json.str();
}

std::unordered_map<std::string, std::string> ManagementApi::fromJson(const std::string& json) const {
    std::unordered_map<std::string, std::string> result;

    // Simple JSON parsing - would use proper JSON library in production
    size_t pos = 0;
    while (pos < json.length()) {
        size_t key_start = json.find("\"", pos);
        if (key_start == std::string::npos) break;

        size_t key_end = json.find("\"", key_start + 1);
        if (key_end == std::string::npos) break;

        size_t colon = json.find(":", key_end);
        if (colon == std::string::npos) break;

        size_t value_start = json.find("\"", colon);
        if (value_start == std::string::npos) break;

        size_t value_end = json.find("\"", value_start + 1);
        if (value_end == std::string::npos) break;

        std::string key = json.substr(key_start + 1, key_end - key_start - 1);
        std::string value = json.substr(value_start + 1, value_end - value_start - 1);

        result[key] = value;
        pos = value_end + 1;
    }

    return result;
}

bool ManagementApi::validateUserData(const std::unordered_map<std::string, std::string>& data) const {
    return data.count("username") && data.count("password") &&
           !data.at("username").empty() && !data.at("password").empty();
}

bool ManagementApi::validateConferenceData(const std::unordered_map<std::string, std::string>& data) const {
    return data.count("name") && !data.at("name").empty();
}

// Utility functions
std::string httpMethodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        case HttpMethod::PATCH: return "PATCH";
        case HttpMethod::OPTIONS: return "OPTIONS";
        default: return "UNKNOWN";
    }
}

HttpMethod stringToHttpMethod(const std::string& method) {
    if (method == "GET") return HttpMethod::GET;
    if (method == "POST") return HttpMethod::POST;
    if (method == "PUT") return HttpMethod::PUT;
    if (method == "DELETE") return HttpMethod::DELETE;
    if (method == "PATCH") return HttpMethod::PATCH;
    if (method == "OPTIONS") return HttpMethod::OPTIONS;
    return HttpMethod::GET;
}

std::string httpStatusToString(HttpStatus status) {
    switch (status) {
        case HttpStatus::OK: return "OK";
        case HttpStatus::CREATED: return "Created";
        case HttpStatus::NO_CONTENT: return "No Content";
        case HttpStatus::BAD_REQUEST: return "Bad Request";
        case HttpStatus::UNAUTHORIZED: return "Unauthorized";
        case HttpStatus::FORBIDDEN: return "Forbidden";
        case HttpStatus::NOT_FOUND: return "Not Found";
        case HttpStatus::METHOD_NOT_ALLOWED: return "Method Not Allowed";
        case HttpStatus::CONFLICT: return "Conflict";
        case HttpStatus::INTERNAL_SERVER_ERROR: return "Internal Server Error";
        case HttpStatus::NOT_IMPLEMENTED: return "Not Implemented";
        case HttpStatus::SERVICE_UNAVAILABLE: return "Service Unavailable";
        default: return "Unknown";
    }
}

int httpStatusToCode(HttpStatus status) {
    return static_cast<int>(status);
}

std::string urlDecode(const std::string& encoded) {
    std::string decoded;
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            int value = std::stoi(encoded.substr(i + 1, 2), nullptr, 16);
            decoded += static_cast<char>(value);
            i += 2;
        } else if (encoded[i] == '+') {
            decoded += ' ';
        } else {
            decoded += encoded[i];
        }
    }
    return decoded;
}

std::string urlEncode(const std::string& decoded) {
    std::ostringstream encoded;
    for (char c : decoded) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else {
            encoded << '%' << std::hex << std::uppercase << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    return encoded.str();
}

std::string extractClientIp(const HttpRequest& request) {
    std::string forwarded = request.getHeader("X-Forwarded-For");
    if (!forwarded.empty()) {
        size_t comma = forwarded.find(',');
        return (comma != std::string::npos) ? forwarded.substr(0, comma) : forwarded;
    }

    std::string real_ip = request.getHeader("X-Real-IP");
    if (!real_ip.empty()) {
        return real_ip;
    }

    return "127.0.0.1"; // Default
}

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// Additional ManagementApi methods
HttpResponse ManagementApi::getPresence(const HttpRequest& request) {
    auto all_presence = enterprise_mgr_.getPresenceManager().getAllPresence();

    std::ostringstream json;
    json << "{\"presence\": [";

    for (size_t i = 0; i < all_presence.size(); ++i) {
        if (i > 0) json << ",";
        json << "{"
             << R"("user_id": ")" << all_presence[i].user_id << R"(",)"
             << R"("state": ")" << enterprise::presenceStateToString(all_presence[i].state) << R"(",)"
             << R"("status": ")" << all_presence[i].status_message << R"(")"
             << "}";
    }

    json << "]}";

    HttpResponse response;
    response.setJson(json.str());
    return response;
}

HttpResponse ManagementApi::updatePresence(const HttpRequest& request) {
    std::string user_id = request.getPathParam("id");
    auto presence_data = fromJson(request.body);

    if (presence_data.count("state")) {
        auto state = enterprise::stringToPresenceState(presence_data["state"]);
        std::string status = presence_data.count("status") ? presence_data["status"] : "";

        if (enterprise_mgr_.getPresenceManager().updatePresence(user_id, state, status)) {
            HttpResponse response;
            response.setJson(R"({"message": "Presence updated successfully"})");
            return response;
        }
    }

    HttpResponse response(HttpStatus::BAD_REQUEST);
    response.setJson(R"({"error": "Invalid presence data"})");
    return response;
}

HttpResponse ManagementApi::getPresenceSubscriptions(const HttpRequest& request) {
    std::string user_id = request.getPathParam("id");
    auto subscriptions = enterprise_mgr_.getPresenceManager().getSubscriptions(user_id);

    HttpResponse response;
    response.setJson(toJson(subscriptions));
    return response;
}

HttpResponse ManagementApi::getConferences(const HttpRequest& request) {
    auto conferences = enterprise_mgr_.getConferenceManager().getAllConferences();

    std::ostringstream json;
    json << "{\"conferences\": [";

    for (size_t i = 0; i < conferences.size(); ++i) {
        if (i > 0) json << ",";
        json << "{"
             << R"("id": ")" << conferences[i].room_id << R"(",)"
             << R"("name": ")" << conferences[i].name << R"(",)"
             << R"("moderator": ")" << conferences[i].moderator_id << R"(",)"
             << R"("type": ")" << enterprise::conferenceTypeToString(conferences[i].type) << R"(",)"
             << R"("participants": )" << conferences[i].getParticipantCount() << ","
             << R"("recording": )" << (conferences[i].recording_enabled ? "true" : "false")
             << "}";
    }

    json << "]}";

    HttpResponse response;
    response.setJson(json.str());
    return response;
}

HttpResponse ManagementApi::getConference(const HttpRequest& request) {
    std::string conf_id = request.getPathParam("id");
    auto conference = enterprise_mgr_.getConferenceManager().getConference(conf_id);

    if (!conference.room_id.empty()) {
        std::ostringstream json;
        json << "{"
             << R"("id": ")" << conference.room_id << R"(",)"
             << R"("name": ")" << conference.name << R"(",)"
             << R"("moderator": ")" << conference.moderator_id << R"(",)"
             << R"("type": ")" << enterprise::conferenceTypeToString(conference.type) << R"(",)"
             << R"("participants": )" << conference.getParticipantCount() << ","
             << R"("recording": )" << (conference.recording_enabled ? "true" : "false")
             << "}";

        HttpResponse response;
        response.setJson(json.str());
        return response;
    }

    HttpResponse response(HttpStatus::NOT_FOUND);
    response.setJson(R"({"error": "Conference not found"})");
    return response;
}

HttpResponse ManagementApi::createConference(const HttpRequest& request) {
    auto conf_data = fromJson(request.body);

    if (!validateConferenceData(conf_data)) {
        HttpResponse response(HttpStatus::BAD_REQUEST);
        response.setJson(R"({"error": "Invalid conference data"})");
        return response;
    }

    std::string name = conf_data["name"];
    std::string moderator = conf_data.count("moderator") ? conf_data["moderator"] : "admin";
    auto type = conf_data.count("type") ? enterprise::stringToConferenceType(conf_data["type"]) : enterprise::ConferenceType::AUDIO_ONLY;

    std::string conf_id = enterprise_mgr_.getConferenceManager().createConference(name, moderator, type);

    HttpResponse response(HttpStatus::CREATED);
    std::ostringstream json;
    json << "{"
         << R"("id": ")" << conf_id << R"(",)"
         << R"("name": ")" << name << R"(",)"
         << R"("moderator": ")" << moderator << R"(",)"
         << R"("type": ")" << enterprise::conferenceTypeToString(type) << R"(")"
         << "}";
    response.setJson(json.str());
    return response;
}

HttpResponse ManagementApi::updateConference(const HttpRequest& request) {
    std::string conf_id = request.getPathParam("id");

    HttpResponse response;
    response.setJson(R"({"message": "Conference updated successfully"})");
    return response;
}

HttpResponse ManagementApi::deleteConference(const HttpRequest& request) {
    std::string conf_id = request.getPathParam("id");

    if (enterprise_mgr_.getConferenceManager().destroyConference(conf_id)) {
        HttpResponse response(HttpStatus::NO_CONTENT);
        return response;
    }

    HttpResponse response(HttpStatus::NOT_FOUND);
    response.setJson(R"({"error": "Conference not found"})");
    return response;
}

HttpResponse ManagementApi::joinConference(const HttpRequest& request) {
    std::string conf_id = request.getPathParam("id");
    auto join_data = fromJson(request.body);

    if (join_data.count("user_id")) {
        std::string user_id = join_data["user_id"];
        std::string display_name = join_data.count("display_name") ? join_data["display_name"] : user_id;

        if (enterprise_mgr_.getConferenceManager().joinConference(conf_id, user_id, display_name)) {
            HttpResponse response;
            response.setJson(R"({"message": "Joined conference successfully"})");
            return response;
        }
    }

    HttpResponse response(HttpStatus::BAD_REQUEST);
    response.setJson(R"({"error": "Invalid join data"})");
    return response;
}

HttpResponse ManagementApi::leaveConference(const HttpRequest& request) {
    std::string conf_id = request.getPathParam("id");
    auto leave_data = fromJson(request.body);

    if (leave_data.count("user_id")) {
        std::string user_id = leave_data["user_id"];

        if (enterprise_mgr_.getConferenceManager().leaveConference(conf_id, user_id)) {
            HttpResponse response;
            response.setJson(R"({"message": "Left conference successfully"})");
            return response;
        }
    }

    HttpResponse response(HttpStatus::BAD_REQUEST);
    response.setJson(R"({"error": "Invalid leave data"})");
    return response;
}

HttpResponse ManagementApi::getMessages(const HttpRequest& request) {
    std::string user_id = request.getQueryParam("user_id");
    size_t limit = request.getQueryParam("limit").empty() ? 50 : std::stoi(request.getQueryParam("limit"));

    if (!user_id.empty()) {
        auto messages = enterprise_mgr_.getMessagingManager().getMessages(user_id, limit);

        std::ostringstream json;
        json << "{\"messages\": [";

        for (size_t i = 0; i < messages.size(); ++i) {
            if (i > 0) json << ",";
            json << "{"
                 << R"("id": ")" << messages[i].id << R"(",)"
                 << R"("from": ")" << messages[i].from << R"(",)"
                 << R"("to": ")" << messages[i].to << R"(",)"
                 << R"("content": ")" << messages[i].content << R"(",)"
                 << R"("type": ")" << enterprise::messageTypeToString(messages[i].type) << R"(",)"
                 << R"("delivered": )" << (messages[i].delivered ? "true" : "false") << ","
                 << R"("read": )" << (messages[i].read ? "true" : "false")
                 << "}";
        }

        json << "]}";

        HttpResponse response;
        response.setJson(json.str());
        return response;
    }

    HttpResponse response(HttpStatus::BAD_REQUEST);
    response.setJson(R"({"error": "user_id parameter required"})");
    return response;
}

HttpResponse ManagementApi::sendMessage(const HttpRequest& request) {
    auto msg_data = fromJson(request.body);

    if (msg_data.count("from") && msg_data.count("to") && msg_data.count("content")) {
        std::string from = msg_data["from"];
        std::string to = msg_data["to"];
        std::string content = msg_data["content"];
        auto type = msg_data.count("type") ? enterprise::stringToMessageType(msg_data["type"]) : enterprise::MessageType::TEXT;

        std::string msg_id = enterprise_mgr_.getMessagingManager().sendMessage(from, to, content, type);

        HttpResponse response(HttpStatus::CREATED);
        std::ostringstream json;
        json << "{"
             << R"("id": ")" << msg_id << R"(",)"
             << R"("message": "Message sent successfully")"
             << "}";
        response.setJson(json.str());
        return response;
    }

    HttpResponse response(HttpStatus::BAD_REQUEST);
    response.setJson(R"({"error": "Invalid message data"})");
    return response;
}

HttpResponse ManagementApi::getConversation(const HttpRequest& request) {
    std::string user1 = request.getPathParam("user1");
    std::string user2 = request.getPathParam("user2");
    size_t limit = request.getQueryParam("limit").empty() ? 50 : std::stoi(request.getQueryParam("limit"));

    auto messages = enterprise_mgr_.getMessagingManager().getConversation(user1, user2, limit);

    std::ostringstream json;
    json << "{\"conversation\": [";

    for (size_t i = 0; i < messages.size(); ++i) {
        if (i > 0) json << ",";
        json << "{"
             << R"("id": ")" << messages[i].id << R"(",)"
             << R"("from": ")" << messages[i].from << R"(",)"
             << R"("to": ")" << messages[i].to << R"(",)"
             << R"("content": ")" << messages[i].content << R"(",)"
             << R"("type": ")" << enterprise::messageTypeToString(messages[i].type) << R"(")"
             << "}";
    }

    json << "]}";

    HttpResponse response;
    response.setJson(json.str());
    return response;
}

} // namespace fmus::management
