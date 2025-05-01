#include <fmus/core/logger.hpp>
#include <fmus/sip/sip.hpp>
#include <fmus/webrtc/webrtc.hpp>
#include <fmus/bridge/bridge.hpp>
#include <fmus/media/media.hpp>
#include <fmus/ivrjs/engine.hpp>
#include <fmus/api/rest_api.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>

// Global flag for signal handling
std::atomic<bool> g_running = true;

// Signal handler
void signalHandler(int signal) {
    g_running = false;
    std::cout << "Signal received, shutting down..." << std::endl;
}

int main(int argc, char* argv[]) {
    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Initialize logger
    auto logger = fmus::core::Logger::get("Main");
    logger->info("Starting fmus-3g application");

    try {
        // Initialize components
        logger->info("Initializing components");

        // Create SIP agent
        auto sip_agent = fmus::sip::SIPAgent::create("fmus-3g");

        // Create WebRTC server
        auto webrtc_server = fmus::webrtc::WebRTCServer::create();

        // Create bridge
        auto bridge = fmus::bridge::WebRTCSIPBridge::create(webrtc_server, sip_agent);

        // Create JavaScript engine
        auto js_engine = fmus::ivrjs::JSEngine::create();

        // Create REST API server
        auto api_server = fmus::api::RESTAPI::create();

        // Configure SIP agent
        // In a real application, these would come from config files
        logger->info("Configuring SIP agent");

        // Start SIP agent
        logger->info("Starting SIP agent");
        sip_agent->start().wait();

        // Start WebRTC server
        logger->info("Starting WebRTC server");
        webrtc_server->start().wait();

        // Start bridge
        logger->info("Starting SIP-WebRTC bridge");
        bridge->start().wait();

        // Start REST API server
        logger->info("Starting REST API server");
        api_server->start("0.0.0.0", 8080).wait();

        // Main application loop
        logger->info("Application running");
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Clean shutdown
        logger->info("Shutting down application");

        // Stop API server
        logger->info("Stopping REST API server");
        api_server->stop().wait();

        // Stop bridge
        logger->info("Stopping SIP-WebRTC bridge");
        bridge->stop().wait();

        // Stop WebRTC server
        logger->info("Stopping WebRTC server");
        webrtc_server->stop().wait();

        // Stop SIP agent
        logger->info("Stopping SIP agent");
        sip_agent->stop().wait();

        logger->info("Application shutdown complete");
        return 0;
    } catch (const std::exception& ex) {
        logger->error("Error: {}", ex.what());
        return 1;
    }
}