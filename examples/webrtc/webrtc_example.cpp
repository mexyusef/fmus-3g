/**
 * WebRTC Example Application
 *
 * This example demonstrates a simple WebRTC peer-to-peer connection
 * using the FMUS-3G WebRTC implementation.
 */

#include <fmus/webrtc/webrtc.hpp>
#include <fmus/core/task.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/media/media.hpp>

#include <iostream>
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>

using namespace fmus;

std::atomic<bool> running{true};

// Signal handler to gracefully exit the application
void signalHandler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    running = false;
}

// Create a configuration with ICE servers
webrtc::WebRtcConfiguration createConfiguration() {
    webrtc::WebRtcConfiguration config;

    // Add public STUN server
    webrtc::WebRtcIceServer stun_server;
    stun_server.urls = "stun:stun.l.google.com:19302";
    config.ice_servers.push_back(stun_server);

    return config;
}

// Helper function to create a dummy video source
std::shared_ptr<media::MediaStream> createDummyStream() {
    auto stream = media::MediaStream::create();

    // In a real application, we would add actual audio and video tracks from devices
    // This is just a placeholder for the example
    core::Logger::info("Created dummy media stream (in real usage, would capture from camera/mic)");

    return stream;
}

// Print usage instructions
void printUsage(const char* programName) {
    std::cout << "WebRTC Example Application" << std::endl;
    std::cout << "Usage: " << programName << " <role> <signaling_url> <room>" << std::endl;
    std::cout << "  role: 'host' or 'guest'" << std::endl;
    std::cout << "  signaling_url: WebSocket URL of the signaling server" << std::endl;
    std::cout << "  room: Room name to join" << std::endl;
    std::cout << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  " << programName << " host wss://example.com/signaling test-room" << std::endl;
}

int main(int argc, char* argv[]) {
    // Register signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Parse command line arguments
    if (argc < 4) {
        printUsage(argv[0]);
        return 1;
    }

    std::string role = argv[1];
    std::string signaling_url = argv[2];
    std::string room = argv[3];
    std::string peer_id = role + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

    if (role != "host" && role != "guest") {
        std::cerr << "Invalid role. Must be 'host' or 'guest'." << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    try {
        // Initialize the logger
        core::Logger::init(core::LogLevel::Info);
        core::Logger::info("Starting WebRTC example application");
        core::Logger::info("Role: {}, Signaling URL: {}, Room: {}, Peer ID: {}",
                          role, signaling_url, room, peer_id);

        // Create the task scheduler for async operations
        auto scheduler = std::make_unique<core::TaskScheduler>();
        scheduler->start(4);

        // Create WebRTC configuration
        webrtc::WebRtcConfiguration config = createConfiguration();

        // Create signaling and session
        auto signaling = webrtc::WebRtcSignaling::create();
        auto session = webrtc::WebRtcSession::create(signaling, config);

        // Setup event handlers
        signaling->on<webrtc::WebRtcSignalingEvent::StateChanged>([](webrtc::WebRtcSignalingState state) {
            core::Logger::info("Signaling state changed to: {}",
                (state == webrtc::WebRtcSignalingState::Connected ? "Connected" :
                 state == webrtc::WebRtcSignalingState::Disconnected ? "Disconnected" : "Connecting"));
        });

        signaling->on<webrtc::WebRtcSignalingEvent::Error>([](const core::Error& error) {
            core::Logger::error("Signaling error: {}", error.what());
        });

        session->on<webrtc::WebRtcSessionEvent::PeerConnected>([](const std::string& peer_id) {
            core::Logger::info("Peer connected: {}", peer_id);
        });

        session->on<webrtc::WebRtcSessionEvent::PeerDisconnected>([](const std::string& peer_id) {
            core::Logger::info("Peer disconnected: {}", peer_id);
        });

        session->on<webrtc::WebRtcSessionEvent::Error>([](const core::Error& error) {
            core::Logger::error("Session error: {}", error.what());
        });

        // Add a data channel event listener
        session->on<webrtc::WebRtcSessionEvent::DataChannelMessage>(
            [](const std::string& peer_id, const std::string& channel, const std::string& message) {
                core::Logger::info("Received message from {} on channel {}: {}",
                                  peer_id, channel, message);
        });

        // Connect to signaling server
        bool connect_done = false;
        signaling->connect(signaling_url).then([&](auto) {
            core::Logger::info("Connected to signaling server");
            connect_done = true;
        }, [&](const core::Error& error) {
            core::Logger::error("Failed to connect to signaling server: {}", error.what());
            running = false;
        });

        // Wait for connection
        for (int i = 0; i < 50 && !connect_done && running; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!connect_done) {
            core::Logger::error("Timed out waiting for signaling connection");
            running = false;
        }

        // Join the room
        std::vector<std::string> peers;
        bool join_done = false;

        if (running) {
            signaling->join(room, peer_id).then([&](auto result) {
                peers = result;
                core::Logger::info("Joined room '{}' as '{}'", room, peer_id);
                if (!peers.empty()) {
                    core::Logger::info("Found {} existing peers in the room", peers.size());
                    for (const auto& peer : peers) {
                        core::Logger::info("- {}", peer);
                    }
                }
                join_done = true;
            }, [&](const core::Error& error) {
                core::Logger::error("Failed to join room: {}", error.what());
                running = false;
            });

            // Wait for join
            for (int i = 0; i < 50 && !join_done && running; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (!join_done && running) {
                core::Logger::error("Timed out waiting to join room");
                running = false;
            }
        }

        // Create and add media stream
        if (running) {
            auto stream = createDummyStream();
            session->addLocalStream(stream).then([&](auto) {
                core::Logger::info("Added local media stream");
            }, [&](const core::Error& error) {
                core::Logger::error("Failed to add local stream: {}", error.what());
            });

            // If we're a host, wait for guests to connect
            // If we're a guest, connect to the host
            if (role == "host") {
                core::Logger::info("Waiting for guests to connect...");
            } else {
                // Find a host to connect to
                std::string host_id;
                for (const auto& peer : peers) {
                    if (peer.find("host_") == 0) {
                        host_id = peer;
                        break;
                    }
                }

                if (!host_id.empty()) {
                    core::Logger::info("Connecting to host: {}", host_id);
                    session->connect(host_id).then([&](auto) {
                        core::Logger::info("Connected to host");

                        // Create a data channel
                        auto channel = session->createDataChannel(host_id, "chat");

                        // Send a welcome message
                        channel->send("Hello from guest!").then([&](auto) {
                            core::Logger::info("Sent welcome message to host");
                        }, [&](const core::Error& error) {
                            core::Logger::error("Failed to send message: {}", error.what());
                        });

                    }, [&](const core::Error& error) {
                        core::Logger::error("Failed to connect to host: {}", error.what());
                    });
                } else {
                    core::Logger::warning("No host found in the room");
                }
            }

            // Main loop - keep the application running
            core::Logger::info("Application running (press Ctrl+C to exit)");
            while (running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        // Clean up
        core::Logger::info("Cleaning up...");

        if (session) {
            session->disconnectAll();
        }

        if (signaling) {
            if (join_done) {
                signaling->leave();
            }
            signaling->disconnect();
        }

        // Stop the scheduler
        scheduler->stop();
        scheduler.reset();

        core::Logger::info("WebRTC example application finished");
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return 1;
    }
}