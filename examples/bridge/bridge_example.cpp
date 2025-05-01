/**
 * WebRTC-SIP Bridge Example Application
 *
 * This example demonstrates the integration of WebRTC and SIP.
 * It creates a bridge that allows WebRTC clients to communicate with SIP endpoints.
 */

#include <fmus/bridge/webrtc_sip_bridge.hpp>
#include <fmus/core/task.hpp>
#include <fmus/core/logger.hpp>

#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

using namespace fmus;

std::atomic<bool> running{true};

// Signal handler to gracefully exit the application
void signalHandler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    running = false;
}

void printUsage(const char* programName) {
    std::cout << "WebRTC-SIP Bridge Example" << std::endl;
    std::cout << "Usage: " << programName << " <sip_uri> <sip_password> <sip_proxy> <webrtc_room>" << std::endl;
    std::cout << "  sip_uri: SIP URI (e.g., sip:user@example.com)" << std::endl;
    std::cout << "  sip_password: SIP password" << std::endl;
    std::cout << "  sip_proxy: SIP proxy server (e.g., sip:proxy.example.com)" << std::endl;
    std::cout << "  webrtc_room: WebRTC room ID (e.g., test-room)" << std::endl;
    std::cout << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  " << programName << " sip:user@example.com password sip:proxy.example.com test-room" << std::endl;
}

int main(int argc, char* argv[]) {
    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Parse command line arguments
    if (argc < 5) {
        printUsage(argv[0]);
        return 1;
    }

    std::string sip_uri = argv[1];
    std::string sip_password = argv[2];
    std::string sip_proxy = argv[3];
    std::string webrtc_room = argv[4];

    try {
        // Initialize the logger
        core::Logger::init(core::LogLevel::Info);
        core::Logger::info("Starting WebRTC-SIP Bridge Example");
        core::Logger::info("SIP URI: {}, SIP Proxy: {}, WebRTC Room: {}",
                          sip_uri, sip_proxy, webrtc_room);

        // Create the task scheduler for async operations
        auto scheduler = std::make_unique<core::TaskScheduler>();
        scheduler->start(4);

        // Configure the WebRTC-SIP bridge
        bridge::WebRtcSipBridgeConfig config;
        config.sip_uri = sip_uri;
        config.sip_password = sip_password;
        config.sip_proxy = sip_proxy;
        config.room_id = webrtc_room;
        config.signaling_url = "wss://example.com/signaling"; // Replace with actual signaling server
        config.audio_enabled = true;
        config.video_enabled = true;

        // Add ICE servers to WebRTC config
        webrtc::WebRtcIceServer stun_server;
        stun_server.urls = "stun:stun.l.google.com:19302";
        config.webrtc_config.ice_servers.push_back(stun_server);

        // Create the bridge
        auto bridge = bridge::WebRtcSipBridge::create(config);

        // Set up event handlers
        bridge->on<bridge::WebRtcSipBridgeEvent::SipRegistered>([&]() {
            core::Logger::info("SIP registration successful");
        });

        bridge->on<bridge::WebRtcSipBridgeEvent::SipRegistrationFailed>([&]() {
            core::Logger::error("SIP registration failed");
            running = false;
        });

        bridge->on<bridge::WebRtcSipBridgeEvent::SipIncomingCall>([&]() {
            core::Logger::info("Incoming SIP call - auto-answering");
        });

        bridge->on<bridge::WebRtcSipBridgeEvent::SipCallConnected>([&]() {
            core::Logger::info("SIP call connected");
        });

        bridge->on<bridge::WebRtcSipBridgeEvent::SipCallDisconnected>([&]() {
            core::Logger::info("SIP call disconnected");
        });

        bridge->on<bridge::WebRtcSipBridgeEvent::WebRtcClientConnected>([&]() {
            core::Logger::info("WebRTC client connected");
            // Log all connected clients
            auto clients = bridge->getConnectedClients();
            core::Logger::info("Connected clients: {}", clients.size());
            for (const auto& client : clients) {
                core::Logger::info("- {}", client);
            }
        });

        bridge->on<bridge::WebRtcSipBridgeEvent::WebRtcClientDisconnected>([&]() {
            core::Logger::info("WebRTC client disconnected");
        });

        bridge->on<bridge::WebRtcSipBridgeEvent::Error>([&]() {
            core::Logger::error("Bridge error occurred");
        });

        // Start the bridge
        bridge->start().then([&]() {
            core::Logger::info("Bridge started successfully");

            // Print help message
            std::cout << "\nBridge is running. Commands:" << std::endl;
            std::cout << "  call <uri> - Make outbound SIP call" << std::endl;
            std::cout << "  clients - List connected WebRTC clients" << std::endl;
            std::cout << "  status - Show bridge status" << std::endl;
            std::cout << "  quit - Exit the application" << std::endl;

        }, [&](const core::Error& error) {
            core::Logger::error("Failed to start bridge: {}", error.what());
            running = false;
        });

        // Command processing loop
        std::string line;
        while (running) {
            // Check for command input
            if (std::cout << "\nCommand> " && std::getline(std::cin, line)) {
                // Process command
                if (line == "quit" || line == "exit") {
                    core::Logger::info("Exiting...");
                    running = false;
                } else if (line == "status") {
                    // Show bridge status
                    auto sip_state = bridge->getSipRegistrationState();
                    auto call_state = bridge->getSipCallState();
                    auto clients = bridge->getConnectedClients();

                    std::cout << "Bridge status:" << std::endl;
                    std::cout << "- Running: " << (bridge->isRunning() ? "Yes" : "No") << std::endl;
                    std::cout << "- SIP Registration: " << static_cast<int>(sip_state) << std::endl;
                    std::cout << "- SIP Call: " << static_cast<int>(call_state) << std::endl;
                    std::cout << "- WebRTC Clients: " << clients.size() << std::endl;
                    for (const auto& client : clients) {
                        std::cout << "  - " << client << std::endl;
                    }
                } else if (line == "clients") {
                    // List connected WebRTC clients
                    auto clients = bridge->getConnectedClients();
                    std::cout << "Connected WebRTC clients (" << clients.size() << "):" << std::endl;
                    for (const auto& client : clients) {
                        std::cout << "- " << client << std::endl;
                    }
                } else if (line.find("call ") == 0 && line.length() > 5) {
                    // Make outbound call
                    std::string uri = line.substr(5);
                    std::cout << "Making call to: " << uri << std::endl;

                    bridge->makeOutboundCall(uri).then([&]() {
                        core::Logger::info("Call initiated successfully");
                    }, [&](const core::Error& error) {
                        core::Logger::error("Failed to initiate call: {}", error.what());
                    });
                } else if (!line.empty()) {
                    std::cout << "Unknown command. Type 'quit' to exit." << std::endl;
                }
            }

            // Sleep to avoid busy loop
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Stop the bridge
        core::Logger::info("Stopping bridge...");
        bridge->stop();

        // Stop the scheduler
        scheduler->stop();
        scheduler.reset();

        core::Logger::info("WebRTC-SIP Bridge Example finished");
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return 1;
    }
}