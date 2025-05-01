#include <fmus/sip/sip.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/core/task.hpp>

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <functional>

// Global variables
std::atomic<bool> g_running(true);

// Signal handler
void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_running = false;
    }
}

// Helper function to create a basic SDP session
fmus::sip::SdpSession createBasicSdp(const std::string& username, const std::string& ip_address) {
    fmus::sip::SdpSession sdp;
    sdp.setUsername(username);
    sdp.setSessionName("FMUS Call");
    sdp.setConnectionAddress(ip_address);

    // Set audio port and codec
    sdp.setAudioPort(10000);
    sdp.addAudioPayloadType(fmus::rtp::RtpPayloadType::PCMU);

    // Set video port and codec (optional)
    sdp.setVideoPort(10002);
    sdp.addVideoPayloadType(fmus::rtp::RtpPayloadType::H264);

    return sdp;
}

// Example SIP user agent application
class SipExampleApp {
public:
    SipExampleApp() {
        // Initialize the task scheduler
        scheduler_.start();

        // Create SIP agent
        agent_ = std::make_shared<fmus::sip::SipAgent>();

        // Set up event handlers
        agent_->events().on("incomingCall",
            [this](std::shared_ptr<fmus::sip::SipCall> call) {
                handleIncomingCall(call);
            });
    }

    ~SipExampleApp() {
        // Stop the agent
        if (agent_) {
            agent_->stop();
        }

        // Stop the scheduler
        scheduler_.stop();
    }

    // Start the application
    void start() {
        agent_->start();
        fmus::core::Logger::info("SIP Example Application started");
    }

    // Stop the application
    void stop() {
        // Unregister if registered
        if (registration_ &&
            registration_->getState() == fmus::sip::SipRegistrationState::Registered) {
            try {
                registration_->unregister().get();
            } catch (const std::exception& e) {
                fmus::core::Logger::error("Failed to unregister: {}", e.what());
            }
        }

        // Hang up active call
        if (current_call_ &&
            current_call_->getState() != fmus::sip::SipCallState::Disconnected) {
            try {
                current_call_->hangup().get();
            } catch (const std::exception& e) {
                fmus::core::Logger::error("Failed to hang up call: {}", e.what());
            }
        }

        agent_->stop();
        fmus::core::Logger::info("SIP Example Application stopped");
    }

    // Configure the client
    void configure(const std::string& display_name,
                  const std::string& username,
                  const std::string& domain,
                  const std::string& password,
                  const std::string& local_ip,
                  int local_port) {
        // Set local contact URI
        std::string user_part = username;
        if (!display_name.empty()) {
            user_part = display_name + " <" + username + ">";
        }

        fmus::sip::SipUri contact("sip:" + username + "@" + local_ip + ":" + std::to_string(local_port));
        agent_->setContactUri(contact);

        // Store configuration
        username_ = username;
        domain_ = domain;
        password_ = password;
        local_ip_ = local_ip;

        fmus::core::Logger::info("Configured SIP client for {}@{}", username, domain);
    }

    // Register with SIP server
    fmus::core::Task<bool> registerWithServer() {
        if (registration_ &&
            registration_->getState() == fmus::sip::SipRegistrationState::Registered) {
            fmus::core::Logger::warn("Already registered");
            co_return true;
        }

        // Create registrar URI
        fmus::sip::SipUri registrar_uri("sip:" + domain_);

        // Create registration
        registration_ = agent_->createRegistration(registrar_uri);

        // Set credentials
        registration_->setCredentials(username_, password_);

        // Set up event handlers
        registration_->events().on("stateChanged",
            [this](fmus::sip::SipRegistrationState state) {
                fmus::core::Logger::info("Registration state changed: {}",
                                     fmus::sip::SipRegistration::stateToString(state));
            });

        registration_->events().on("error",
            [](const fmus::sip::SipError& error) {
                fmus::core::Logger::error("Registration error: {}", error.what());
            });

        // Start registration
        try {
            fmus::core::Logger::info("Registering with {}...", domain_);
            co_await registration_->register_();
            fmus::core::Logger::info("Successfully registered with {}", domain_);
            co_return true;
        } catch (const fmus::sip::SipError& e) {
            fmus::core::Logger::error("Registration failed: {}", e.what());
            co_return false;
        }
    }

    // Make a call
    fmus::core::Task<bool> makeCall(const std::string& target) {
        if (current_call_ &&
            current_call_->getState() != fmus::sip::SipCallState::Disconnected) {
            fmus::core::Logger::warn("Already in a call");
            co_return false;
        }

        // Create target URI
        fmus::sip::SipUri target_uri;

        // Check if target contains a domain
        if (target.find('@') != std::string::npos) {
            target_uri = fmus::sip::SipUri("sip:" + target);
        } else {
            target_uri = fmus::sip::SipUri("sip:" + target + "@" + domain_);
        }

        // Create a call
        current_call_ = co_await agent_->createCall(target_uri);

        // Set up event handlers
        current_call_->events().on("stateChanged",
            [this](fmus::sip::SipCallState state) {
                fmus::core::Logger::info("Call state changed: {}",
                                     fmus::sip::SipCall::stateToString(state));

                if (state == fmus::sip::SipCallState::Disconnected) {
                    call_duration_ = std::chrono::steady_clock::now() - call_start_time_;
                    fmus::core::Logger::info("Call ended, duration: {:.1f} seconds",
                                         call_duration_.count());
                } else if (state == fmus::sip::SipCallState::Connected) {
                    call_start_time_ = std::chrono::steady_clock::now();
                    fmus::core::Logger::info("Call connected");
                }
            });

        current_call_->events().on("error",
            [](const fmus::sip::SipError& error) {
                fmus::core::Logger::error("Call error: {}", error.what());
            });

        // Create SDP
        auto sdp = createBasicSdp(username_, local_ip_);

        // Start dialing
        try {
            fmus::core::Logger::info("Calling {}...", target_uri.toString());
            co_await current_call_->dial(target_uri, sdp);
            co_return true;
        } catch (const fmus::sip::SipError& e) {
            fmus::core::Logger::error("Call failed: {}", e.what());
            co_return false;
        }
    }

    // Hang up the current call
    fmus::core::Task<void> hangupCall() {
        if (!current_call_ ||
            current_call_->getState() == fmus::sip::SipCallState::Disconnected) {
            fmus::core::Logger::warn("No active call to hang up");
            co_return;
        }

        try {
            fmus::core::Logger::info("Hanging up call...");
            co_await current_call_->hangup();
            fmus::core::Logger::info("Call hung up");
        } catch (const fmus::sip::SipError& e) {
            fmus::core::Logger::error("Failed to hang up call: {}", e.what());
        }

        co_return;
    }

    // Handle incoming call
    void handleIncomingCall(std::shared_ptr<fmus::sip::SipCall> call) {
        if (current_call_ &&
            current_call_->getState() != fmus::sip::SipCallState::Disconnected) {
            // Already in a call, reject
            fmus::core::Logger::info("Rejecting incoming call, already in a call");
            call->reject(fmus::sip::SipResponseCode::Busy).get();
            return;
        }

        // Store the call
        current_call_ = call;

        // Set up event handlers
        current_call_->events().on("stateChanged",
            [this](fmus::sip::SipCallState state) {
                fmus::core::Logger::info("Call state changed: {}",
                                     fmus::sip::SipCall::stateToString(state));

                if (state == fmus::sip::SipCallState::Disconnected) {
                    if (call_start_time_ != std::chrono::steady_clock::time_point()) {
                        call_duration_ = std::chrono::steady_clock::now() - call_start_time_;
                        fmus::core::Logger::info("Call ended, duration: {:.1f} seconds",
                                             call_duration_.count());
                    } else {
                        fmus::core::Logger::info("Call ended before connecting");
                    }
                } else if (state == fmus::sip::SipCallState::Connected) {
                    call_start_time_ = std::chrono::steady_clock::now();
                    fmus::core::Logger::info("Call connected");
                }
            });

        // Get caller info
        fmus::core::Logger::info("Incoming call from {}", call->remote_uri_.toString());

        // Auto-answer the call
        fmus::core::Logger::info("Auto-answering call...");

        // Create SDP for answer
        auto sdp = createBasicSdp(username_, local_ip_);

        // Answer the call
        try {
            call->answer(sdp).get();
        } catch (const fmus::sip::SipError& e) {
            fmus::core::Logger::error("Failed to answer call: {}", e.what());
        }
    }

private:
    fmus::core::TaskScheduler scheduler_;
    std::shared_ptr<fmus::sip::SipAgent> agent_;
    std::shared_ptr<fmus::sip::SipRegistration> registration_;
    std::shared_ptr<fmus::sip::SipCall> current_call_;

    std::string username_;
    std::string domain_;
    std::string password_;
    std::string local_ip_;

    std::chrono::steady_clock::time_point call_start_time_;
    std::chrono::duration<double> call_duration_;
};

// Main function
int main(int argc, char* argv[]) {
    // Set up signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Initialize logger
    fmus::core::Logger::init();
    fmus::core::Logger::setLevel(fmus::core::LogLevel::Debug);

    // Parse command line arguments
    std::string username = "user";
    std::string domain = "localhost";
    std::string password = "password";
    std::string local_ip = "127.0.0.1";
    int local_port = 5060;
    std::string display_name = "FMUS User";
    std::string target_uri;

    // Simple command line argument parsing
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--username" && i + 1 < argc) {
            username = argv[++i];
        } else if (arg == "--domain" && i + 1 < argc) {
            domain = argv[++i];
        } else if (arg == "--password" && i + 1 < argc) {
            password = argv[++i];
        } else if (arg == "--local-ip" && i + 1 < argc) {
            local_ip = argv[++i];
        } else if (arg == "--local-port" && i + 1 < argc) {
            local_port = std::stoi(argv[++i]);
        } else if (arg == "--display-name" && i + 1 < argc) {
            display_name = argv[++i];
        } else if (arg == "--call" && i + 1 < argc) {
            target_uri = argv[++i];
        } else if (arg == "--help") {
            std::cout << "FMUS SIP Example Application\n\n";
            std::cout << "Usage: " << argv[0] << " [options]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --username USER      SIP username (default: user)\n";
            std::cout << "  --domain DOMAIN      SIP domain (default: localhost)\n";
            std::cout << "  --password PASSWORD  SIP password (default: password)\n";
            std::cout << "  --local-ip IP        Local IP address (default: 127.0.0.1)\n";
            std::cout << "  --local-port PORT    Local port (default: 5060)\n";
            std::cout << "  --display-name NAME  Display name (default: FMUS User)\n";
            std::cout << "  --call TARGET        Call a target URI on startup\n";
            std::cout << "  --help               Show this help message\n";
            return 0;
        }
    }

    try {
        // Create and start the example application
        SipExampleApp app;

        // Configure the application
        app.configure(display_name, username, domain, password, local_ip, local_port);

        // Start the application
        app.start();

        // Register with server
        app.registerWithServer().get();

        // If a target URI was specified, make a call
        if (!target_uri.empty()) {
            app.makeCall(target_uri).get();
        }

        // Main loop
        fmus::core::Logger::info("Press Ctrl+C to exit");
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Cleanup
        fmus::core::Logger::info("Shutting down...");

        // Hang up call if any
        app.hangupCall().get();

        // Stop the application
        app.stop();

        return 0;
    } catch (const std::exception& e) {
        fmus::core::Logger::error("Exception: {}", e.what());
        return 1;
    }
}