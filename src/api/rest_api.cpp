#include <fmus/api/rest_api.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/sip/sip.hpp>
#include <fmus/webrtc/webrtc.hpp>
#include <fmus/bridge/bridge.hpp>

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <unordered_map>

#include <uWebSockets/App.h>
#include <nlohmann/json.hpp>

namespace fmus::api {

using json = nlohmann::json;

class RESTAPIImpl : public RESTAPI {
public:
    RESTAPIImpl()
        : logger_(core::Logger::get("RESTAPI")) {
        initializeRoutes();
    }

    ~RESTAPIImpl() {
        stop().wait();
    }

    core::Task<void> start(const std::string& host, int port) override {
        if (is_running_) {
            logger_->warn("API already running");
            co_return;
        }

        host_ = host;
        port_ = port;

        logger_->info("Starting REST API on {}:{}", host, port);

        // Start the server in a separate thread
        server_thread_ = std::thread([this]() {
            // Setup uWebSockets app with SSL
            auto app = uWS::App();

            // Register routes
            setupRoutes(app);

            // Start listening
            app.listen(host_, port_, [this](auto* listen_socket) {
                if (listen_socket) {
                    is_running_ = true;
                    logger_->info("REST API listening on {}:{}", host_, port_);
                } else {
                    logger_->error("Failed to start REST API on {}:{}", host_, port_);
                }
            });

            // Run the event loop
            app.run();

            // Server has stopped
            is_running_ = false;
            logger_->info("REST API stopped");
        });

        co_return;
    }

    core::Task<void> stop() override {
        if (!is_running_) {
            co_return;
        }

        logger_->info("Stopping REST API");

        // Signal server to stop
        is_running_ = false;

        // TODO: Properly signal the uWebSockets app to stop

        // Wait for server thread to finish
        if (server_thread_.joinable()) {
            server_thread_.join();
        }

        co_return;
    }

    bool isRunning() const override {
        return is_running_;
    }

    void registerEndpoint(const std::string& path, HttpMethod method,
                         const EndpointHandler& handler) override {
        // Store the endpoint handler
        std::string method_str;
        switch (method) {
            case HttpMethod::GET: method_str = "GET"; break;
            case HttpMethod::POST: method_str = "POST"; break;
            case HttpMethod::PUT: method_str = "PUT"; break;
            case HttpMethod::DELETE: method_str = "DELETE"; break;
            case HttpMethod::PATCH: method_str = "PATCH"; break;
            default: method_str = "GET"; break;
        }

        std::string key = method_str + ":" + path;
        endpoints_[key] = handler;

        logger_->debug("Registered endpoint {} {}", method_str, path);
    }

private:
    // Initialize default API routes
    void initializeRoutes() {
        // Register SIP-related endpoints
        registerEndpoint("/api/sip/calls", HttpMethod::GET,
            [this](const HttpRequest& req) -> HttpResponse {
                // List active calls
                json response = json::array();
                // TODO: Implement call listing
                return { 200, response.dump(), "application/json" };
            });

        registerEndpoint("/api/sip/calls", HttpMethod::POST,
            [this](const HttpRequest& req) -> HttpResponse {
                // Make a new call
                try {
                    auto body = json::parse(req.body);

                    // Extract call parameters
                    std::string destination = body["destination"];
                    std::string caller_id = body.value("caller_id", "Anonymous");

                    logger_->info("API call request: {} -> {}", caller_id, destination);

                    // TODO: Actually make the call

                    json response = {
                        {"id", "call_123"},
                        {"destination", destination},
                        {"caller_id", caller_id},
                        {"state", "dialing"}
                    };

                    return { 200, response.dump(), "application/json" };
                } catch (const std::exception& ex) {
                    json error = {
                        {"error", "Bad request"},
                        {"message", ex.what()}
                    };
                    return { 400, error.dump(), "application/json" };
                }
            });

        registerEndpoint("/api/sip/calls/:id", HttpMethod::GET,
            [this](const HttpRequest& req) -> HttpResponse {
                // Get call details
                std::string call_id = req.path_params.at("id");

                // TODO: Implement call details retrieval

                json response = {
                    {"id", call_id},
                    {"destination", "sip:test@example.com"},
                    {"caller_id", "Anonymous"},
                    {"state", "active"},
                    {"duration", 120}
                };

                return { 200, response.dump(), "application/json" };
            });

        registerEndpoint("/api/sip/calls/:id", HttpMethod::DELETE,
            [this](const HttpRequest& req) -> HttpResponse {
                // Hang up a call
                std::string call_id = req.path_params.at("id");

                logger_->info("API hang up call: {}", call_id);

                // TODO: Implement call hangup

                json response = {
                    {"id", call_id},
                    {"state", "terminated"}
                };

                return { 200, response.dump(), "application/json" };
            });

        // Register WebRTC-related endpoints
        registerEndpoint("/api/webrtc/sessions", HttpMethod::POST,
            [this](const HttpRequest& req) -> HttpResponse {
                // Create a new WebRTC session
                try {
                    auto body = json::parse(req.body);

                    // Extract session parameters
                    std::string peer_id = body.value("peer_id", "");

                    logger_->info("API create WebRTC session: {}", peer_id);

                    // TODO: Create actual WebRTC session

                    json response = {
                        {"id", "session_123"},
                        {"peer_id", peer_id},
                        {"state", "new"}
                    };

                    return { 200, response.dump(), "application/json" };
                } catch (const std::exception& ex) {
                    json error = {
                        {"error", "Bad request"},
                        {"message", ex.what()}
                    };
                    return { 400, error.dump(), "application/json" };
                }
            });

        // Register Bridge-related endpoints
        registerEndpoint("/api/bridge/connections", HttpMethod::POST,
            [this](const HttpRequest& req) -> HttpResponse {
                // Create a new bridge connection
                try {
                    auto body = json::parse(req.body);

                    // Extract connection parameters
                    std::string sip_endpoint = body["sip_endpoint"];
                    std::string webrtc_session = body["webrtc_session"];

                    logger_->info("API create bridge: {} <-> {}", sip_endpoint, webrtc_session);

                    // TODO: Create actual bridge connection

                    json response = {
                        {"id", "bridge_123"},
                        {"sip_endpoint", sip_endpoint},
                        {"webrtc_session", webrtc_session},
                        {"state", "connecting"}
                    };

                    return { 200, response.dump(), "application/json" };
                } catch (const std::exception& ex) {
                    json error = {
                        {"error", "Bad request"},
                        {"message", ex.what()}
                    };
                    return { 400, error.dump(), "application/json" };
                }
            });
    }

    // Set up uWebSockets routes
    void setupRoutes(uWS::App& app) {
        // Helper to parse path parameters
        auto parsePath = [](const std::string& route_pattern, const std::string& actual_path) {
            std::unordered_map<std::string, std::string> params;

            std::vector<std::string> pattern_parts;
            std::vector<std::string> path_parts;

            std::stringstream ss_pattern(route_pattern);
            std::stringstream ss_path(actual_path);
            std::string part;

            while (std::getline(ss_pattern, part, '/')) {
                if (!part.empty()) pattern_parts.push_back(part);
            }

            while (std::getline(ss_path, part, '/')) {
                if (!part.empty()) path_parts.push_back(part);
            }

            if (pattern_parts.size() != path_parts.size()) {
                return params; // Not a match
            }

            for (size_t i = 0; i < pattern_parts.size(); i++) {
                if (pattern_parts[i][0] == ':') {
                    // This is a parameter
                    std::string param_name = pattern_parts[i].substr(1);
                    params[param_name] = path_parts[i];
                } else if (pattern_parts[i] != path_parts[i]) {
                    params.clear();
                    return params; // Not a match
                }
            }

            return params;
        };

        // Helper to handle HTTP method
        auto handleMethod = [this, parsePath](
            const std::string& method,
            uWS::HttpResponse<false>* res,
            uWS::HttpRequest* req
        ) {
            std::string path = std::string(req->getUrl());
            logger_->debug("{} {}", method, path);

            // Find a matching route
            for (const auto& [key, handler] : endpoints_) {
                if (key.substr(0, method.length()) != method) {
                    continue; // Method doesn't match
                }

                std::string route_pattern = key.substr(method.length() + 1);
                auto path_params = parsePath(route_pattern, path);

                if (!path_params.empty() || route_pattern == path) {
                    // Route matched

                    // Read request body if any
                    res->onData([this, res, req, handler, path_params, path](std::string_view body, bool is_last) {
                        if (is_last) {
                            // Prepare request object
                            HttpRequest request;
                            request.method = req->getMethod();
                            request.path = path;
                            request.query_string = req->getQuery();
                            request.body = std::string(body);
                            request.path_params = path_params;

                            // Get headers
                            req->forEach([&request](std::string_view key, std::string_view value) {
                                request.headers[std::string(key)] = std::string(value);
                                return true;
                            });

                            // Call the handler
                            try {
                                auto response = handler(request);

                                // Set response headers
                                res->writeHeader("Content-Type", response.content_type);

                                // Write response
                                res->writeStatus(std::to_string(response.status_code) + " " +
                                                getStatusMessage(response.status_code));
                                res->end(response.body);
                            } catch (const std::exception& ex) {
                                logger_->error("Error handling request: {}", ex.what());

                                json error = {
                                    {"error", "Internal server error"},
                                    {"message", ex.what()}
                                };

                                res->writeStatus("500 Internal Server Error");
                                res->writeHeader("Content-Type", "application/json");
                                res->end(error.dump());
                            }
                        }
                    });

                    // Handle case with no body
                    res->onAborted([]() {
                        // Handle aborted request
                    });

                    return true; // Route handled
                }
            }

            // No matching route found
            res->writeStatus("404 Not Found");
            res->end("Not Found");
            return false;
        };

        // Register methods
        app.get("/*", [handleMethod](auto* res, auto* req) {
            handleMethod("GET", res, req);
        });

        app.post("/*", [handleMethod](auto* res, auto* req) {
            handleMethod("POST", res, req);
        });

        app.put("/*", [handleMethod](auto* res, auto* req) {
            handleMethod("PUT", res, req);
        });

        app.del("/*", [handleMethod](auto* res, auto* req) {
            handleMethod("DELETE", res, req);
        });

        app.patch("/*", [handleMethod](auto* res, auto* req) {
            handleMethod("PATCH", res, req);
        });

        // WebSocket support for real-time events
        app.ws("/api/events", {
            .open = [this](auto* ws) {
                logger_->info("WebSocket connection opened");
            },
            .message = [this](auto* ws, std::string_view message, uWS::OpCode opCode) {
                logger_->debug("WebSocket message received: {}", message);

                // Handle incoming message
                try {
                    auto msg = json::parse(message);
                    std::string event = msg["event"];

                    // Process event
                    if (event == "subscribe") {
                        // Subscribe to event channel
                        std::string channel = msg["channel"];
                        logger_->info("WebSocket client subscribed to channel: {}", channel);

                        // Acknowledge subscription
                        json response = {
                            {"event", "subscribed"},
                            {"channel", channel}
                        };

                        ws->send(response.dump(), uWS::OpCode::TEXT);
                    }
                } catch (const std::exception& ex) {
                    logger_->error("Error processing WebSocket message: {}", ex.what());

                    json error = {
                        {"event", "error"},
                        {"message", ex.what()}
                    };

                    ws->send(error.dump(), uWS::OpCode::TEXT);
                }
            },
            .close = [this](auto* ws, int code, std::string_view message) {
                logger_->info("WebSocket connection closed");
            }
        });
    }

    // Helper to get HTTP status message
    std::string getStatusMessage(int status_code) {
        switch (status_code) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 500: return "Internal Server Error";
            default: return "Unknown";
        }
    }

    std::shared_ptr<core::Logger> logger_;
    std::string host_;
    int port_ = 0;
    std::atomic<bool> is_running_{false};
    std::thread server_thread_;

    // Endpoint storage
    std::unordered_map<std::string, EndpointHandler> endpoints_;
};

// Factory method implementation
std::unique_ptr<RESTAPI> RESTAPI::create() {
    return std::make_unique<RESTAPIImpl>();
}

} // namespace fmus::api