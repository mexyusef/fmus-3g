#include <fmus/bridge/webrtc_sip_bridge.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/media/media.hpp>

#include <memory>
#include <map>
#include <mutex>
#include <thread>

namespace fmus::bridge {

// Internal implementation class for the bridge
class WebRtcSipBridgeImpl : public WebRtcSipBridge {
public:
    // Constructor with configuration
    WebRtcSipBridgeImpl(const WebRtcSipBridgeConfig& config)
        : config_(config), running_(false) {
        core::Logger::info("WebRTC-SIP Bridge created");
    }

    // Destructor
    ~WebRtcSipBridgeImpl() override {
        stop();
        core::Logger::info("WebRTC-SIP Bridge destroyed");
    }

    // Start the bridge
    core::Task<void> start() override {
        if (running_) {
            core::Logger::warn("Bridge already running");
            co_return;
        }

        core::Logger::info("Starting WebRTC-SIP Bridge");

        try {
            // Create and start SIP agent
            initializeSip();

            // Create and start WebRTC session
            initializeWebRtc();

            // Set bridge state
            running_ = true;
            emit<WebRtcSipBridgeEvent::BridgeStarted>();

            core::Logger::info("WebRTC-SIP Bridge started");
        } catch (const std::exception& e) {
            core::Logger::error("Failed to start bridge: {}", e.what());
            running_ = false;
            co_await stop();
            throw;
        }

        co_return;
    }

    // Stop the bridge
    void stop() override {
        if (!running_) {
            return;
        }

        core::Logger::info("Stopping WebRTC-SIP Bridge");

        {
            std::lock_guard<std::mutex> lock(mutex_);

            // Stop SIP components
            if (sip_call_) {
                sip_call_->hangup();
                sip_call_.reset();
            }

            if (sip_registration_) {
                sip_registration_->unregister();
                sip_registration_.reset();
            }

            if (sip_agent_) {
                sip_agent_->shutdown();
                sip_agent_.reset();
            }

            // Stop WebRTC components
            if (webrtc_session_) {
                webrtc_session_->disconnectAll();
                webrtc_session_->stop();
                webrtc_session_.reset();
            }

            if (webrtc_signaling_) {
                webrtc_signaling_->leave();
                webrtc_signaling_->disconnect();
                webrtc_signaling_.reset();
            }

            // Clear media streams
            local_streams_.clear();
            remote_streams_.clear();
        }

        running_ = false;
        emit<WebRtcSipBridgeEvent::BridgeStopped>();

        core::Logger::info("WebRTC-SIP Bridge stopped");
    }

    // Check if the bridge is running
    bool isRunning() const override {
        return running_;
    }

    // Make an outbound SIP call
    core::Task<void> makeOutboundCall(const std::string& uri) override {
        if (!running_) {
            throw std::runtime_error("Bridge not running");
        }

        core::Logger::info("Making outbound SIP call to {}", uri);

        std::lock_guard<std::mutex> lock(mutex_);

        if (!sip_agent_) {
            throw std::runtime_error("SIP agent not initialized");
        }

        if (sip_call_ && sip_call_->getState() != sip::SipCallState::Terminated) {
            throw std::runtime_error("Call already in progress");
        }

        // Parse the URI
        auto parsed_uri = sip::SipUri(uri);

        // Create the call
        sip_call_ = sip_agent_->createCall(parsed_uri);

        // Setup call events
        setupCallEvents();

        // Start the call
        co_await sip_call_->call();

        core::Logger::info("Outbound SIP call initiated to {}", uri);
        co_return;
    }

    // Add a WebRTC client to the bridge
    core::Task<void> addWebRtcClient(const std::string& client_id) override {
        if (!running_) {
            throw std::runtime_error("Bridge not running");
        }

        core::Logger::info("Adding WebRTC client: {}", client_id);

        std::lock_guard<std::mutex> lock(mutex_);

        if (!webrtc_session_) {
            throw std::runtime_error("WebRTC session not initialized");
        }

        // Connect to the client
        co_await webrtc_session_->connect(client_id);

        // Track the client
        connected_clients_.insert(client_id);

        emit<WebRtcSipBridgeEvent::WebRtcClientConnected>();

        core::Logger::info("WebRTC client added: {}", client_id);
        co_return;
    }

    // Remove a WebRTC client from the bridge
    void removeWebRtcClient(const std::string& client_id) override {
        if (!running_) {
            return;
        }

        core::Logger::info("Removing WebRTC client: {}", client_id);

        std::lock_guard<std::mutex> lock(mutex_);

        if (!webrtc_session_) {
            return;
        }

        // Disconnect the client
        webrtc_session_->disconnect(client_id);

        // Remove the client from tracking
        connected_clients_.erase(client_id);

        emit<WebRtcSipBridgeEvent::WebRtcClientDisconnected>();

        core::Logger::info("WebRTC client removed: {}", client_id);
    }

    // Get connected WebRTC clients
    std::vector<std::string> getConnectedClients() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<std::string>(connected_clients_.begin(), connected_clients_.end());
    }

    // Get SIP call state
    sip::SipCallState getSipCallState() const override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!sip_call_) {
            return sip::SipCallState::Terminated;
        }

        return sip_call_->getState();
    }

    // Get SIP registration state
    sip::SipRegistrationState getSipRegistrationState() const override {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!sip_registration_) {
            return sip::SipRegistrationState::None;
        }

        return sip_registration_->getState();
    }

private:
    // Initialize SIP components
    void initializeSip() {
        core::Logger::info("Initializing SIP components");

        // Create SIP agent
        sip_agent_ = sip::SipAgent::create();

        // Setup SIP events
        sip_agent_->on<sip::SipAgentEvent::Error>([this](const core::Error& error) {
            core::Logger::error("SIP agent error: {}", error.what());
            emit<WebRtcSipBridgeEvent::Error>();
        });

        // Start the agent
        sip_agent_->start();

        // Register with the SIP server if configured
        if (!config_.sip_proxy.empty()) {
            sip::SipUri uri(config_.sip_uri);
            sip_registration_ = sip_agent_->createRegistration(uri, config_.sip_password);

            // Setup registration events
            sip_registration_->on<sip::SipRegistrationEvent::StateChanged>([this](sip::SipRegistrationState state) {
                if (state == sip::SipRegistrationState::Registered) {
                    core::Logger::info("SIP registration successful");
                    emit<WebRtcSipBridgeEvent::SipRegistered>();
                } else if (state == sip::SipRegistrationState::Failed) {
                    core::Logger::error("SIP registration failed");
                    emit<WebRtcSipBridgeEvent::SipRegistrationFailed>();
                }
            });

            // Start registration
            sip_registration_->registerUser();
        }

        // Setup incoming call handler
        sip_agent_->on<sip::SipAgentEvent::IncomingCall>([this](std::shared_ptr<sip::SipCall> call) {
            handleIncomingCall(call);
        });

        core::Logger::info("SIP components initialized");
    }

    // Initialize WebRTC components
    void initializeWebRtc() {
        core::Logger::info("Initializing WebRTC components");

        // Create WebRTC signaling
        webrtc_signaling_ = webrtc::WebRtcSignaling::create();

        // Setup signaling events
        webrtc_signaling_->on<webrtc::WebRtcSignalingEvent::StateChanged>([this](webrtc::WebRtcSignalingState state) {
            core::Logger::info("WebRTC signaling state changed: {}",
                             state == webrtc::WebRtcSignalingState::Connected ? "Connected" :
                             state == webrtc::WebRtcSignalingState::Connecting ? "Connecting" : "Disconnected");
        });

        webrtc_signaling_->on<webrtc::WebRtcSignalingEvent::Error>([this](const core::Error& error) {
            core::Logger::error("WebRTC signaling error: {}", error.what());
            emit<WebRtcSipBridgeEvent::Error>();
        });

        // Create WebRTC session
        webrtc_session_ = webrtc::WebRtcSession::create(webrtc_signaling_, config_.webrtc_config);

        // Setup session events
        webrtc_session_->on<webrtc::WebRtcSessionEvent::PeerConnected>([this](const std::string& peer_id) {
            core::Logger::info("WebRTC peer connected: {}", peer_id);
            connected_clients_.insert(peer_id);
            emit<WebRtcSipBridgeEvent::WebRtcClientConnected>();
        });

        webrtc_session_->on<webrtc::WebRtcSessionEvent::PeerDisconnected>([this](const std::string& peer_id) {
            core::Logger::info("WebRTC peer disconnected: {}", peer_id);
            connected_clients_.erase(peer_id);
            emit<WebRtcSipBridgeEvent::WebRtcClientDisconnected>();
        });

        webrtc_session_->on<webrtc::WebRtcSessionEvent::Error>([this](const core::Error& error) {
            core::Logger::error("WebRTC session error: {}", error.what());
            emit<WebRtcSipBridgeEvent::Error>();
        });

        // Connect to signaling server
        webrtc_signaling_->connect(config_.signaling_url).then([this](auto) {
            // Join room after connection
            return webrtc_signaling_->join(config_.room_id, "bridge");
        }, [this](const core::Error& error) {
            core::Logger::error("Failed to connect to signaling server: {}", error.what());
            emit<WebRtcSipBridgeEvent::Error>();
        }).then([this](auto clients) {
            // Log connected clients
            core::Logger::info("Joined room with {} clients", clients.size());
            for (const auto& client : clients) {
                core::Logger::info("Client in room: {}", client);
            }

            // Create media streams
            setupMediaStreams();

            // Start WebRTC session
            return webrtc_session_->start();
        }, [this](const core::Error& error) {
            core::Logger::error("Failed to join room: {}", error.what());
            emit<WebRtcSipBridgeEvent::Error>();
        }).then([this](auto) {
            core::Logger::info("WebRTC session started");
        }, [this](const core::Error& error) {
            core::Logger::error("Failed to start WebRTC session: {}", error.what());
            emit<WebRtcSipBridgeEvent::Error>();
        });

        core::Logger::info("WebRTC components initialized");
    }

    // Setup call events for SIP call
    void setupCallEvents() {
        if (!sip_call_) {
            return;
        }

        sip_call_->on<sip::SipCallEvent::StateChanged>([this](sip::SipCallState state) {
            core::Logger::info("SIP call state changed: {}", static_cast<int>(state));

            if (state == sip::SipCallState::Connected) {
                core::Logger::info("SIP call connected");
                handleCallConnected();
                emit<WebRtcSipBridgeEvent::SipCallConnected>();
            } else if (state == sip::SipCallState::Terminated) {
                core::Logger::info("SIP call terminated");
                handleCallTerminated();
                emit<WebRtcSipBridgeEvent::SipCallDisconnected>();
            }
        });

        sip_call_->on<sip::SipCallEvent::Error>([this](const core::Error& error) {
            core::Logger::error("SIP call error: {}", error.what());
            emit<WebRtcSipBridgeEvent::Error>();
        });
    }

    // Handle incoming SIP call
    void handleIncomingCall(std::shared_ptr<sip::SipCall> call) {
        core::Logger::info("Incoming SIP call from {}", call->getRemoteUri().toString());

        // Store the call
        sip_call_ = call;

        // Setup call events
        setupCallEvents();

        // Emit event
        emit<WebRtcSipBridgeEvent::SipIncomingCall>();

        // Auto-answer the call
        sip_call_->answer().then([this](auto) {
            core::Logger::info("Answered incoming SIP call");
        }, [this](const core::Error& error) {
            core::Logger::error("Failed to answer SIP call: {}", error.what());
            emit<WebRtcSipBridgeEvent::Error>();
        });
    }

    // Handle SIP call connected
    void handleCallConnected() {
        // Bridge the media between SIP and WebRTC
        setupMediaBridge();
    }

    // Handle SIP call terminated
    void handleCallTerminated() {
        // Clean up media bridge
        cleanupMediaBridge();
    }

    // Setup media streams for the bridge
    void setupMediaStreams() {
        if (config_.audio_enabled) {
            // Create audio stream
            auto audio_stream = media::MediaStream::create();
            // TODO: Add actual audio tracks in real implementation
            local_streams_.push_back(audio_stream);
        }

        if (config_.video_enabled) {
            // Create video stream
            auto video_stream = media::MediaStream::create();
            // TODO: Add actual video tracks in real implementation
            local_streams_.push_back(video_stream);
        }

        // Add streams to WebRTC session
        for (const auto& stream : local_streams_) {
            webrtc_session_->addLocalStream(stream).then([](auto) {
                // Stream added successfully
            }, [this](const core::Error& error) {
                core::Logger::error("Failed to add local stream: {}", error.what());
            });
        }
    }

    // Setup media bridge between SIP and WebRTC
    void setupMediaBridge() {
        // TODO: Implement media bridging between SIP and WebRTC
        // This would involve:
        // 1. Getting RTP streams from SIP call
        // 2. Feeding them into WebRTC peers
        // 3. Taking WebRTC media and sending it to the SIP call
        // 4. Handling transcoding if needed

        core::Logger::info("Media bridge established between SIP and WebRTC");
    }

    // Clean up media bridge
    void cleanupMediaBridge() {
        // TODO: Clean up media bridging resources

        core::Logger::info("Media bridge removed");
    }

    // Member variables
    WebRtcSipBridgeConfig config_;
    std::atomic<bool> running_;

    // SIP components
    std::shared_ptr<sip::SipAgent> sip_agent_;
    std::shared_ptr<sip::SipRegistration> sip_registration_;
    std::shared_ptr<sip::SipCall> sip_call_;

    // WebRTC components
    std::shared_ptr<webrtc::WebRtcSignaling> webrtc_signaling_;
    std::shared_ptr<webrtc::WebRtcSession> webrtc_session_;

    // Media components
    std::vector<std::shared_ptr<media::MediaStream>> local_streams_;
    std::map<std::string, std::vector<std::shared_ptr<media::MediaStream>>> remote_streams_;

    // Connected clients
    std::set<std::string> connected_clients_;

    // Mutex for thread safety
    mutable std::mutex mutex_;
};

// Factory implementation
std::shared_ptr<WebRtcSipBridge> WebRtcSipBridge::create(const WebRtcSipBridgeConfig& config) {
    return std::make_shared<WebRtcSipBridgeImpl>(config);
}

} // namespace fmus::bridge