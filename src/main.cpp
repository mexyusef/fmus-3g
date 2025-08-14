#include "fmus/core/logger.hpp"
#include "fmus/sip/message.hpp"
#include "fmus/sip/sdp.hpp"
#include "fmus/sip/registrar.hpp"
#include "fmus/rtp/packet.hpp"
#include "fmus/webrtc/session.hpp"
#include "fmus/webrtc/signaling.hpp"
#include "fmus/media/frame.hpp"
#include "fmus/media/codec.hpp"
#include "fmus/network/transport.hpp"
#include "fmus/network/stun.hpp"
#include "fmus/enterprise/features.hpp"
#include "fmus/management/api.hpp"
#include "fmus/security/encryption.hpp"

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>

using namespace fmus;

int main() {
    // Set up logging
    core::Logger::setLevel(core::LogLevel::DEBUG);
    core::Logger::info("Starting FMUS-3G SIP/WebRTC Application");
    
    try {
        // Test SIP message creation
        core::Logger::info("Testing SIP message functionality...");
        sip::SipUri uri("sip:user@example.com:5060");
        sip::SipMessage invite(sip::SipMethod::INVITE, uri);
        
        invite.getHeaders().setFrom("sip:alice@example.com");
        invite.getHeaders().setTo("sip:bob@example.com");
        invite.getHeaders().setCallId("12345@example.com");
        invite.getHeaders().setCSeq("1 INVITE");
        invite.getHeaders().setVia("SIP/2.0/UDP 192.168.1.100:5060");
        invite.getHeaders().setContentType("application/sdp");
        
        std::string sdp = "v=0\r\n"
                         "o=alice 2890844526 2890844527 IN IP4 192.168.1.100\r\n"
                         "s=-\r\n"
                         "c=IN IP4 192.168.1.100\r\n"
                         "t=0 0\r\n"
                         "m=audio 5004 RTP/AVP 0\r\n"
                         "a=rtpmap:0 PCMU/8000\r\n";
        
        invite.setBody(sdp);
        invite.getHeaders().setContentLength(sdp.length());
        
        core::Logger::info("Created SIP INVITE message:");
        std::cout << invite.toString() << std::endl;
        
        // Test RTP packet creation
        core::Logger::info("Testing RTP packet functionality...");
        rtp::RtpHeader rtp_header;
        rtp_header.payload_type = 0; // PCMU
        rtp_header.sequence_number = 1234;
        rtp_header.timestamp = 160000;
        rtp_header.ssrc = 0x12345678;
        rtp_header.marker = true;
        
        std::vector<uint8_t> audio_payload = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87};
        rtp::RtpPacket rtp_packet(rtp_header, audio_payload);
        
        auto serialized = rtp_packet.serialize();
        core::Logger::info("Created RTP packet with {} bytes", serialized.size());
        
        // Test deserialization
        auto deserialized = rtp::RtpPacket::deserialize(serialized.data(), serialized.size());
        if (deserialized) {
            core::Logger::info("Successfully deserialized RTP packet");
            core::Logger::info("  Payload Type: {}", static_cast<int>(deserialized->getHeader().payload_type));
            core::Logger::info("  Sequence: {}", static_cast<int>(deserialized->getHeader().sequence_number));
            core::Logger::info("  Timestamp: {}", static_cast<int>(deserialized->getHeader().timestamp));
            core::Logger::info("  SSRC: {}", static_cast<int>(deserialized->getHeader().ssrc));
        }
        
        // Test WebRTC session
        core::Logger::info("Testing WebRTC session functionality...");
        webrtc::Session session;
        
        session.onStateChange = [](webrtc::SessionState state) {
            core::Logger::info("WebRTC Session state changed to: {}", static_cast<int>(state));
        };
        
        session.onError = [](const std::string& error) {
            core::Logger::error("WebRTC Session error: {}", error);
        };
        
        if (session.start()) {
            core::Logger::info("WebRTC session started successfully");
            
            // Simulate some work
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            session.stop();
            core::Logger::info("WebRTC session stopped");
        }
        
        // Test media frames
        core::Logger::info("Testing media frame functionality...");
        std::vector<uint8_t> audio_data(320, 0x80); // 20ms of silence at 8kHz, 16-bit
        media::AudioFrame audio_frame(audio_data, 8000, 1);
        
        core::Logger::info("Created audio frame: {} bytes, {}Hz, {} channels", 
                          audio_frame.getSize(), audio_frame.getSampleRate(), audio_frame.getChannels());
        
        std::vector<uint8_t> video_data(320 * 240 * 3, 0x00); // Black 320x240 RGB frame
        media::VideoFrame video_frame(video_data, 320, 240);
        
        core::Logger::info("Created video frame: {} bytes, {}x{}",
                          video_frame.getSize(), video_frame.getWidth(), video_frame.getHeight());

        // Test SDP functionality
        core::Logger::info("Testing SDP functionality...");

        // Create an audio offer
        sip::SessionDescription audio_offer = sip::SdpBuilder::createBasicAudioOffer(
            "123456789", "192.168.1.100", 5004);

        core::Logger::info("Created SDP audio offer:");
        std::cout << audio_offer.toString() << std::endl;

        // Create a video offer
        sip::SessionDescription video_offer = sip::SdpBuilder::createBasicVideoOffer(
            "987654321", "192.168.1.100", 5004, 5006);

        core::Logger::info("Created SDP video offer:");
        std::cout << video_offer.toString() << std::endl;

        // Create an answer
        sip::SessionDescription answer = sip::SdpBuilder::createAnswer(
            video_offer, "192.168.1.200", 6004, 6006);

        core::Logger::info("Created SDP answer:");
        std::cout << answer.toString() << std::endl;

        // Test SDP parsing
        std::string test_sdp =
            "v=0\r\n"
            "o=alice 2890844526 2890844527 IN IP4 host.atlanta.com\r\n"
            "s=Session Description\r\n"
            "c=IN IP4 host.atlanta.com\r\n"
            "t=0 0\r\n"
            "m=audio 49170 RTP/AVP 0\r\n"
            "a=rtpmap:0 PCMU/8000\r\n";

        try {
            sip::SessionDescription parsed = sip::SessionDescription::fromString(test_sdp);
            core::Logger::info("Successfully parsed SDP:");
            core::Logger::info("  Session: {}", parsed.getSessionName());
            core::Logger::info("  Origin: {}", parsed.getOrigin().username);
            core::Logger::info("  Media count: {}", parsed.getMediaDescriptions().size());

            if (!parsed.getMediaDescriptions().empty()) {
                const auto& media = parsed.getMediaDescriptions()[0];
                core::Logger::info("  First media: {} on port {}",
                                  static_cast<int>(media.getType()), media.getPort());
            }
        } catch (const std::exception& e) {
            core::Logger::error("Failed to parse SDP: {}", e.what());
        }

        // Test codec functionality
        core::Logger::info("Testing codec functionality...");

        media::CodecManager codec_manager;

        // Set up audio codecs
        media::CodecParameters audio_params;
        audio_params.sample_rate = 8000;
        audio_params.channels = 1;

        if (codec_manager.setAudioEncoder(media::AudioCodecId::PCMU, audio_params)) {
            core::Logger::info("PCMU encoder configured successfully");
        }

        if (codec_manager.setAudioDecoder(media::AudioCodecId::PCMU, audio_params)) {
            core::Logger::info("PCMU decoder configured successfully");
        }

        // Test audio encoding/decoding
        std::vector<uint8_t> test_audio_data(320, 0x80); // 20ms of silence at 8kHz, 16-bit
        media::AudioFrame test_audio_frame(test_audio_data, 8000, 1);

        auto encoded_audio = codec_manager.encodeAudio(test_audio_frame);
        core::Logger::info("Encoded audio: {} bytes -> {} bytes",
                          test_audio_data.size(), encoded_audio.size());

        auto decoded_audio = codec_manager.decodeAudio(encoded_audio);
        core::Logger::info("Decoded audio: {} bytes -> {} bytes",
                          encoded_audio.size(), decoded_audio.getSize());

        // Set up video codecs
        media::CodecParameters video_params;
        video_params.width = 320;
        video_params.height = 240;
        video_params.fps = 30;

        if (codec_manager.setVideoEncoder(media::VideoCodecId::H264, video_params)) {
            core::Logger::info("H.264 encoder configured successfully");
        }

        if (codec_manager.setVideoDecoder(media::VideoCodecId::H264, video_params)) {
            core::Logger::info("H.264 decoder configured successfully");
        }

        // Test video encoding/decoding
        std::vector<uint8_t> test_video_data(320 * 240 * 3, 0x80); // Gray 320x240 RGB frame
        media::VideoFrame test_video_frame(test_video_data, 320, 240);

        auto encoded_video = codec_manager.encodeVideo(test_video_frame, true); // Keyframe
        core::Logger::info("Encoded video: {} bytes -> {} bytes",
                          test_video_data.size(), encoded_video.size());

        auto decoded_video = codec_manager.decodeVideo(encoded_video);
        core::Logger::info("Decoded video: {} bytes -> {} bytes ({}x{})",
                          encoded_video.size(), decoded_video.getSize(),
                          decoded_video.getWidth(), decoded_video.getHeight());

        // Show codec statistics
        auto stats = codec_manager.getStats();
        core::Logger::info("Codec stats: Audio frames encoded={}, decoded={}, Video frames encoded={}, decoded={}",
                          stats.audio_frames_encoded, stats.audio_frames_decoded,
                          stats.video_frames_encoded, stats.video_frames_decoded);

        // Test STUN/ICE functionality
        core::Logger::info("Testing STUN/ICE functionality...");

        // Test STUN message creation and parsing
        network::StunMessage binding_request(network::StunMessageType::BINDING_REQUEST);
        binding_request.addAttribute(network::StunAttributeType::SOFTWARE, "FMUS-3G Test");

        auto stun_serialized = binding_request.serialize();
        core::Logger::info("Created STUN binding request: {} bytes", stun_serialized.size());

        network::StunMessage parsed_message(stun_serialized.data(), stun_serialized.size());
        if (parsed_message.isValid()) {
            core::Logger::info("Successfully parsed STUN message");
            if (parsed_message.hasAttribute(network::StunAttributeType::SOFTWARE)) {
                auto software_attr = parsed_message.getAttribute(network::StunAttributeType::SOFTWARE);
                core::Logger::info("  Software: {}", software_attr.asString());
            }
        }

        // Test ICE candidate creation
        network::IceCandidate host_candidate;
        host_candidate.foundation = "1";
        host_candidate.component_id = 1;
        host_candidate.transport = "udp";
        host_candidate.type = network::IceCandidateType::HOST;
        host_candidate.address = network::SocketAddress("192.168.1.100", 5000);
        host_candidate.priority = host_candidate.calculatePriority(network::IceCandidateType::HOST);

        std::string candidate_string = host_candidate.toString();
        core::Logger::info("Created ICE candidate: {}", candidate_string);

        // Test candidate parsing
        auto parsed_candidate = network::IceCandidate::fromString(candidate_string);
        core::Logger::info("Parsed ICE candidate: foundation={}, priority={}, address={}",
                          parsed_candidate.foundation, parsed_candidate.priority,
                          parsed_candidate.address.toString());

        // Test ICE agent (basic functionality)
        network::IceAgent ice_agent;
        ice_agent.setStunServer(network::SocketAddress("stun.l.google.com", 19302));
        ice_agent.setTurnServer(network::SocketAddress("turn.example.com", 3478), "user", "pass");

        ice_agent.setCandidateCallback([](const network::IceCandidate& candidate) {
            core::Logger::info("ICE candidate gathered: {}", candidate.toString());
        });

        ice_agent.setConnectivityCallback([](const network::IceCandidate& local, const network::IceCandidate& remote) {
            core::Logger::info("ICE connectivity established: {} <-> {}",
                              local.address.toString(), remote.address.toString());
        });

        if (ice_agent.start(network::SocketAddress("0.0.0.0", 5010))) {
            core::Logger::info("ICE agent started successfully");

            // Simulate adding a remote candidate
            network::IceCandidate remote_candidate;
            remote_candidate.foundation = "2";
            remote_candidate.component_id = 1;
            remote_candidate.transport = "udp";
            remote_candidate.type = network::IceCandidateType::HOST;
            remote_candidate.address = network::SocketAddress("192.168.1.200", 5020);
            remote_candidate.priority = remote_candidate.calculatePriority(network::IceCandidateType::HOST);

            ice_agent.addRemoteCandidate(remote_candidate);
            ice_agent.startConnectivityChecks();

            // Give some time for candidate gathering
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            core::Logger::info("ICE agent has {} local candidates",
                              ice_agent.getLocalCandidates().size());

            ice_agent.stop();
        } else {
            core::Logger::error("Failed to start ICE agent");
        }

        // Test SIP Registration functionality
        core::Logger::info("Testing SIP Registration functionality...");

        // Create registrar
        sip::SipRegistrar registrar("fmus.local");

        // Add test users
        registrar.addUser("alice", "secret123", "Alice Smith");
        registrar.addUser("bob", "password456", "Bob Johnson");

        core::Logger::info("Added test users to registrar");

        // Test authentication challenge creation
        auto challenge = registrar.createChallenge();
        core::Logger::info("Created auth challenge: realm={}, nonce={}",
                          challenge.realm, challenge.nonce.substr(0, 8) + "...");

        // Create a test REGISTER request
        sip::SipUri alice_uri("sip:alice@fmus.local");
        sip::SipMessage register_request(sip::SipMethod::REGISTER, alice_uri);
        register_request.getHeaders().setFrom("sip:alice@fmus.local");
        register_request.getHeaders().setTo("sip:alice@fmus.local");
        register_request.getHeaders().setCallId("test-register-123");
        register_request.getHeaders().setCSeq("1 REGISTER");
        register_request.getHeaders().setVia("SIP/2.0/UDP 192.168.1.100:5060");
        register_request.getHeaders().set("Contact", "sip:alice@192.168.1.100:5060");
        register_request.getHeaders().set("Expires", "3600");
        register_request.getHeaders().set("User-Agent", "FMUS-3G Test Client");

        // Process initial REGISTER (should get 401 Unauthorized)
        auto response1 = registrar.processRegister(register_request);
        core::Logger::info("Initial REGISTER response: {} {}",
                          static_cast<int>(response1.getResponseCode()),
                          response1.getReasonPhrase());

        if (response1.getResponseCode() == sip::SipResponseCode::Unauthorized) {
            std::string www_auth = response1.getHeaders().get("WWW-Authenticate");
            core::Logger::info("Received authentication challenge: {}", www_auth);

            // Create authenticated REGISTER request
            sip::SipMessage auth_register = register_request;

            // Parse challenge and create response
            if (www_auth.substr(0, 7) == "Digest ") {
                auto auth_challenge = sip::AuthChallenge::fromString(www_auth.substr(7));

                // Calculate digest response
                std::string auth_response = sip::auth::calculateDigestResponse(
                    "alice", auth_challenge.realm, "secret123", "REGISTER",
                    "sip:alice@fmus.local", auth_challenge.nonce);

                // Create Authorization header
                std::ostringstream auth_header;
                auth_header << "Digest username=\"alice\", "
                           << "realm=\"" << auth_challenge.realm << "\", "
                           << "nonce=\"" << auth_challenge.nonce << "\", "
                           << "uri=\"sip:alice@fmus.local\", "
                           << "response=\"" << auth_response << "\", "
                           << "algorithm=MD5";

                auth_register.getHeaders().set("Authorization", auth_header.str());

                // Process authenticated REGISTER
                auto response2 = registrar.processRegister(auth_register);
                core::Logger::info("Authenticated REGISTER response: {} {}",
                                  static_cast<int>(response2.getResponseCode()),
                                  response2.getReasonPhrase());

                if (response2.getResponseCode() == sip::SipResponseCode::OK) {
                    core::Logger::info("User alice successfully registered!");

                    // Check registration status
                    if (registrar.isRegistered("alice")) {
                        core::Logger::info("Registration confirmed - alice is registered");
                    }

                    auto registered_users = registrar.getRegisteredUsers();
                    core::Logger::info("Total registered users: {}", registered_users.size());
                }
            }
        }

        // Test registration client
        sip::SipRegistrationClient reg_client;
        reg_client.setRegistrar(network::SocketAddress("sip.fmus.local", 5060));
        reg_client.setCredentials("bob", "password456");
        reg_client.setUserUri("sip:bob@fmus.local");
        reg_client.setContactUri("sip:bob@192.168.1.200:5060");

        reg_client.setStateCallback([](sip::RegistrationState old_state, sip::RegistrationState new_state) {
            core::Logger::info("Registration state changed: {} -> {}",
                              static_cast<int>(old_state), static_cast<int>(new_state));
        });

        reg_client.setErrorCallback([](const std::string& error) {
            core::Logger::error("Registration error: {}", error);
        });

        if (reg_client.registerUser()) {
            core::Logger::info("Registration client initiated registration for bob");
        }

        // Test registration manager
        sip::RegistrationManager reg_manager;
        reg_manager.getRegistrar().addUser("charlie", "test789", "Charlie Brown");

        core::Logger::info("Registration manager created with {} users",
                          reg_manager.getRegistrar().getRegisteredUsers().size());

        // Perform maintenance
        reg_manager.performMaintenance();
        core::Logger::info("Registration maintenance completed");

        // Test WebRTC Signaling functionality
        core::Logger::info("Testing WebRTC Signaling functionality...");

        // Test signaling message creation and parsing
        webrtc::SignalingMessage offer_message(
            webrtc::SignalingMessageType::OFFER,
            "session-123",
            "client-alice",
            "client-bob",
            R"({"type":"offer","sdp":"v=0\r\no=alice 123 456 IN IP4 192.168.1.100\r\n..."})"
        );

        std::string json_message = offer_message.toJson();
        core::Logger::info("Created signaling message: {}", json_message.substr(0, 100) + "...");

        auto parsed_signaling_message = webrtc::SignalingMessage::fromJson(json_message);
        if (parsed_signaling_message.isValid()) {
            core::Logger::info("Successfully parsed signaling message: type={}, session={}, from={}, to={}",
                              webrtc::signalingMessageTypeToString(parsed_signaling_message.type),
                              parsed_signaling_message.session_id, parsed_signaling_message.from, parsed_signaling_message.to);
        }

        // Test WebSocket frame creation
        webrtc::WebSocketFrame text_frame;
        text_frame.opcode = webrtc::WebSocketOpcode::TEXT;
        text_frame.fin = true;
        text_frame.payload.assign(json_message.begin(), json_message.end());

        auto frame_data = text_frame.serialize();
        core::Logger::info("Created WebSocket frame: {} bytes", frame_data.size());

        // Test frame parsing
        if (webrtc::WebSocketFrame::isWebSocketFrame(frame_data.data(), frame_data.size())) {
            auto parsed_frame = webrtc::WebSocketFrame::deserialize(frame_data.data(), frame_data.size());
            std::string payload_text(parsed_frame.payload.begin(), parsed_frame.payload.end());
            core::Logger::info("Parsed WebSocket frame: opcode={}, payload_size={}",
                              static_cast<int>(parsed_frame.opcode), parsed_frame.payload.size());
        }

        // Test signaling server
        webrtc::SignalingServer signaling_server;

        signaling_server.setClientConnectedCallback([](const std::string& client_id) {
            core::Logger::info("Signaling client connected: {}", client_id);
        });

        signaling_server.setClientDisconnectedCallback([](const std::string& client_id) {
            core::Logger::info("Signaling client disconnected: {}", client_id);
        });

        signaling_server.setMessageCallback([](const webrtc::SignalingMessage& message, const std::string& client_id) {
            core::Logger::info("Signaling message from {}: type={}, session={}",
                              client_id, webrtc::signalingMessageTypeToString(message.type), message.session_id);
        });

        if (signaling_server.start(network::SocketAddress("127.0.0.1", 8080))) {
            core::Logger::info("WebRTC signaling server started on port 8080");

            // Create test sessions
            signaling_server.createSession("test-session-1", "client-alice");
            signaling_server.joinSession("test-session-1", "client-bob");

            auto participants = signaling_server.getSessionParticipants("test-session-1");
            core::Logger::info("Session test-session-1 has {} participants", participants.size());

            auto active_sessions = signaling_server.getActiveSessions();
            core::Logger::info("Server has {} active sessions", active_sessions.size());

            // Test message routing
            webrtc::SignalingMessage test_message(
                webrtc::SignalingMessageType::ICE_CANDIDATE,
                "test-session-1",
                "client-alice",
                "client-bob",
                R"({"candidate":"candidate:1 1 UDP 2130706431 192.168.1.100 54400 typ host","sdpMid":"0","sdpMLineIndex":0})"
            );

            // This would normally route to connected clients
            core::Logger::info("Would route ICE candidate message in session");

            // Show server statistics
            auto server_stats = signaling_server.getStats();
            core::Logger::info("Signaling server stats: connections={}, sessions={}",
                              server_stats.connections_accepted, server_stats.sessions_created);

            // Give some time for any async operations
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            signaling_server.stop();
        } else {
            core::Logger::error("Failed to start WebRTC signaling server");
        }

        // Test utility functions
        std::string session_id = webrtc::generateSessionId();
        std::string client_id = webrtc::generateClientId();
        core::Logger::info("Generated IDs: session={}, client={}", session_id, client_id);

        // Test Enterprise Features functionality
        core::Logger::info("Testing Enterprise Features functionality...");

        enterprise::EnterpriseManager enterprise_mgr;
        if (enterprise_mgr.initialize()) {
            core::Logger::info("Enterprise manager initialized successfully");

            // Test Presence Management
            auto& presence_mgr = enterprise_mgr.getPresenceManager();

            presence_mgr.updatePresence("alice", enterprise::PresenceState::ONLINE, "Available");
            presence_mgr.updatePresence("bob", enterprise::PresenceState::BUSY, "In a meeting");
            presence_mgr.updatePresence("charlie", enterprise::PresenceState::AWAY, "Out for lunch");

            presence_mgr.subscribe("alice", "bob");
            presence_mgr.subscribe("bob", "alice");

            auto alice_presence = presence_mgr.getPresence("alice");
            core::Logger::info("Alice presence: {} - {}",
                              enterprise::presenceStateToString(alice_presence.state),
                              alice_presence.status_message);

            auto all_presence = presence_mgr.getAllPresence();
            core::Logger::info("Total presence entries: {}", all_presence.size());

            // Test Instant Messaging
            auto& messaging_mgr = enterprise_mgr.getMessagingManager();

            std::string msg_id1 = messaging_mgr.sendMessage("alice", "bob", "Hello Bob!", enterprise::MessageType::TEXT);
            std::string msg_id2 = messaging_mgr.sendMessage("bob", "alice", "Hi Alice! How are you?", enterprise::MessageType::TEXT);
            std::string msg_id3 = messaging_mgr.sendMessage("alice", "bob", "I'm doing great, thanks!", enterprise::MessageType::TEXT);

            messaging_mgr.markDelivered(msg_id1);
            messaging_mgr.markDelivered(msg_id2);
            messaging_mgr.markRead(msg_id1);

            auto conversation = messaging_mgr.getConversation("alice", "bob", 10);
            core::Logger::info("Conversation between Alice and Bob: {} messages", conversation.size());

            // Test Conference Management
            auto& conference_mgr = enterprise_mgr.getConferenceManager();

            std::string conf_id = conference_mgr.createConference("Weekly Team Meeting", "alice", enterprise::ConferenceType::VIDEO);
            core::Logger::info("Created conference: {}", conf_id);

            conference_mgr.joinConference(conf_id, "bob", "Bob Smith");
            conference_mgr.joinConference(conf_id, "charlie", "Charlie Brown");

            auto conference = conference_mgr.getConference(conf_id);
            core::Logger::info("Conference '{}' has {} participants", conference.name, conference.getParticipantCount());

            conference_mgr.muteParticipant(conf_id, "bob", true, false); // Mute audio only
            conference_mgr.startRecording(conf_id, "/tmp/meeting_recording.mp4");

            // Test Call Transfer
            auto& transfer_mgr = enterprise_mgr.getTransferManager();

            transfer_mgr.initiateBlindTransfer("call-123", "alice", "bob", "charlie");
            auto active_transfers = transfer_mgr.getActiveTransfers();
            core::Logger::info("Active transfers: {}", active_transfers.size());

            // Show enterprise statistics
            auto enterprise_stats = enterprise_mgr.getStats();
            core::Logger::info("Enterprise stats: presence={}, messages={}, conferences={}, transfers={}",
                              enterprise_stats.active_presence, enterprise_stats.total_messages,
                              enterprise_stats.active_conferences, enterprise_stats.active_transfers);

            enterprise_mgr.shutdown();
        }

        // Test Security & Encryption functionality
        core::Logger::info("Testing Security & Encryption functionality...");

        security::SecurityManager security_mgr;
        if (security_mgr.initialize()) {
            core::Logger::info("Security manager initialized successfully");

            auto crypto_engine = security_mgr.getCryptoEngine();

            // Test key generation
            auto aes_key = crypto_engine->generateKey(security::EncryptionAlgorithm::AES_256_GCM);
            core::Logger::info("Generated AES-256-GCM key: {} bytes", aes_key.getKeySize());

            // Test encryption/decryption
            std::string plaintext = "This is a secret message that needs to be encrypted!";
            std::vector<uint8_t> plaintext_bytes(plaintext.begin(), plaintext.end());

            auto encrypted = crypto_engine->encrypt(plaintext_bytes, aes_key);
            auto decrypted = crypto_engine->decrypt(encrypted, aes_key);

            std::string decrypted_text(decrypted.begin(), decrypted.end());
            core::Logger::info("Encryption test: '{}' -> {} bytes -> '{}'",
                              plaintext, encrypted.size(), decrypted_text);

            // Test hashing
            auto hash_sha256 = crypto_engine->hash(plaintext_bytes, security::HashAlgorithm::SHA256);
            core::Logger::info("SHA-256 hash: {}", security::bytesToHex(hash_sha256));

            // Test HMAC
            auto hmac_key = crypto_engine->generateRandom(32);
            auto hmac_result = crypto_engine->hmac(plaintext_bytes, hmac_key, security::HashAlgorithm::SHA256);
            core::Logger::info("HMAC-SHA256: {}", security::bytesToHex(hmac_result));

            // Test digital signatures
            auto [private_key, public_key] = crypto_engine->generateKeyPair(security::KeyExchangeMethod::RSA_2048);
            auto signature = crypto_engine->sign(plaintext_bytes, private_key);
            bool signature_valid = crypto_engine->verify(plaintext_bytes, signature, public_key);
            core::Logger::info("Digital signature: {} bytes, valid: {}", signature.size(), signature_valid);

            // Test certificate management
            auto& cert_mgr = security_mgr.getCertificateManager();

            auto server_cert = cert_mgr.generateSelfSignedCertificate("CN=FMUS-3G Server,O=FMUS,C=US");
            core::Logger::info("Generated server certificate: subject='{}', serial={}",
                              server_cert.subject, server_cert.serial_number);

            cert_mgr.addTrustedCertificate(server_cert);

            auto client_cert = cert_mgr.generateSelfSignedCertificate("CN=FMUS-3G Client,O=FMUS,C=US");
            cert_mgr.addTrustedCertificate(client_cert);

            auto trusted_certs = cert_mgr.getTrustedCertificates();
            core::Logger::info("Trusted certificates: {}", trusted_certs.size());

            // Test security audit
            auto security_audit = security_mgr.performSecurityAudit();
            core::Logger::info("Security audit: {} trusted certificates", security_audit.trusted_certificates);

            security_mgr.shutdown();
        }

        // Test Management API functionality
        core::Logger::info("Testing Management API functionality...");

        management::RestApiServer api_server;
        management::ManagementApi management_api(enterprise_mgr, reg_manager, signaling_server);

        // Setup API routes
        management_api.setupRoutes(api_server);

        // Add some middleware
        api_server.addMiddleware([](management::HttpRequest& req, management::HttpResponse& resp) -> bool {
            core::Logger::debug("API request: {} {}", management::httpMethodToString(req.method), req.path);
            return true; // Continue processing
        });

        api_server.enableCors();

        if (api_server.start(network::SocketAddress("127.0.0.1", 8080))) {
            core::Logger::info("Management API server started on port 8080");

            // Test HTTP request parsing
            std::string test_request =
                "GET /api/system/status HTTP/1.1\r\n"
                "Host: localhost:8080\r\n"
                "User-Agent: FMUS-Test/1.0\r\n"
                "Accept: application/json\r\n"
                "\r\n";

            auto parsed_request = management::HttpRequest::parse(test_request);
            core::Logger::info("Parsed HTTP request: {} {}",
                              management::httpMethodToString(parsed_request.method), parsed_request.path);

            // Test HTTP response creation
            management::HttpResponse test_response(management::HttpStatus::OK);
            test_response.setJson(R"({"status": "ok", "message": "API is working"})");

            std::string response_str = test_response.toString();
            core::Logger::info("Generated HTTP response: {} bytes", response_str.length());

            // Give some time for any connections
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            api_server.stop();
        } else {
            core::Logger::error("Failed to start Management API server");
        }

        // Test network transport
        core::Logger::info("Testing network transport functionality...");
        network::TransportManager transport_manager;

        network::TransportManager::Config config;
        config.sip_udp_address = network::SocketAddress("127.0.0.1", 5060);
        config.sip_tcp_address = network::SocketAddress("127.0.0.1", 5061);
        config.rtp_address = network::SocketAddress("127.0.0.1", 5004);
        config.rtcp_address = network::SocketAddress("127.0.0.1", 5005);

        // Set up callbacks
        transport_manager.getSipTransport().setMessageCallback(
            [](const sip::SipMessage& msg, const network::SocketAddress& from) {
                core::Logger::info("Received SIP message from {}: {} {}",
                                  from.toString(),
                                  static_cast<int>(msg.getMethod()),
                                  msg.getRequestUri().toString());
            });

        transport_manager.getRtpTransport().setRtpCallback(
            [](const rtp::RtpPacket& packet, const network::SocketAddress& from) {
                core::Logger::info("Received RTP packet from {}: PT={}, Seq={}",
                                  from.toString(),
                                  static_cast<int>(packet.getHeader().payload_type),
                                  static_cast<int>(packet.getHeader().sequence_number));
            });

        if (transport_manager.initialize(config)) {
            core::Logger::info("Network transport initialized successfully");

            // Give some time for sockets to be ready
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // Test sending a SIP message to ourselves
            sip::SipUri test_uri("sip:test@127.0.0.1:5060");
            sip::SipMessage test_invite(sip::SipMethod::INVITE, test_uri);
            test_invite.getHeaders().setFrom("sip:alice@127.0.0.1");
            test_invite.getHeaders().setTo("sip:bob@127.0.0.1");
            test_invite.getHeaders().setCallId("test-call-123");
            test_invite.getHeaders().setCSeq("1 INVITE");
            test_invite.getHeaders().setVia("SIP/2.0/UDP 127.0.0.1:5060");

            network::SocketAddress dest("127.0.0.1", 5060);
            if (transport_manager.getSipTransport().sendMessage(test_invite, dest)) {
                core::Logger::info("Test SIP message sent successfully");
            }

            // Test sending an RTP packet
            rtp::RtpHeader test_rtp_header;
            test_rtp_header.payload_type = 0;
            test_rtp_header.sequence_number = 1000;
            test_rtp_header.timestamp = 8000;
            test_rtp_header.ssrc = 0x12345678;

            std::vector<uint8_t> test_payload = {0x80, 0x80, 0x80, 0x80};
            rtp::RtpPacket test_rtp_packet(test_rtp_header, test_payload);

            network::SocketAddress rtp_dest("127.0.0.1", 5004);
            if (transport_manager.getRtpTransport().sendRtpPacket(test_rtp_packet, rtp_dest)) {
                core::Logger::info("Test RTP packet sent successfully");
            }

            // Wait a bit to receive any responses
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            transport_manager.shutdown();
        } else {
            core::Logger::error("Failed to initialize network transport");
        }

        core::Logger::info("All tests completed successfully!");
        
    } catch (const std::exception& e) {
        core::Logger::error("Application error: {}", e.what());
        return 1;
    }
    
    core::Logger::info("FMUS-3G application finished");
    return 0;
}
