#pragma once

#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <fmus/core/task.hpp>

namespace fmus::api {

// HTTP methods supported by the API
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    PATCH
};

// HTTP request structure
struct HttpRequest {
    std::string method;
    std::string path;
    std::string query_string;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> path_params;
};

// HTTP response structure
struct HttpResponse {
    int status_code;
    std::string body;
    std::string content_type;
};

// Endpoint handler function type
using EndpointHandler = std::function<HttpResponse(const HttpRequest&)>;

// RESTful API interface
class RESTAPI {
public:
    virtual ~RESTAPI() = default;

    // Start the API server
    virtual core::Task<void> start(const std::string& host, int port) = 0;

    // Stop the API server
    virtual core::Task<void> stop() = 0;

    // Check if server is running
    virtual bool isRunning() const = 0;

    // Register an endpoint
    virtual void registerEndpoint(const std::string& path, HttpMethod method,
                                const EndpointHandler& handler) = 0;

    // Factory method to create an instance
    static std::unique_ptr<RESTAPI> create();
};

} // namespace fmus::api