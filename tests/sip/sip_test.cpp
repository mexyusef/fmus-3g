#include <gtest/gtest.h>
#include <fmus/sip/sip.hpp>
#include <fmus/core/task.hpp>
#include <fmus/core/logger.hpp>

#include <string>
#include <memory>
#include <thread>
#include <chrono>

namespace fmus::sip::test {

// Helper class for testing SIP functionality
class SipTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Start the task scheduler for async operations
        scheduler_.start();
    }

    void TearDown() override {
        // Stop the task scheduler
        scheduler_.stop();
    }

    core::TaskScheduler scheduler_;
};

// Test SIP URI parsing
TEST_F(SipTest, UriParsing) {
    // Test basic URI
    SipUri uri1("sip:user@example.com");
    EXPECT_EQ(uri1.getScheme(), "sip");
    EXPECT_EQ(uri1.getUser(), "user");
    EXPECT_EQ(uri1.getHost(), "example.com");
    EXPECT_EQ(uri1.getPort(), 0); // Default port

    // Test URI with port
    SipUri uri2("sip:user@example.com:5060");
    EXPECT_EQ(uri2.getScheme(), "sip");
    EXPECT_EQ(uri2.getUser(), "user");
    EXPECT_EQ(uri2.getHost(), "example.com");
    EXPECT_EQ(uri2.getPort(), 5060);

    // Test URI with parameters
    SipUri uri3("sip:user@example.com;transport=udp");
    EXPECT_EQ(uri3.getScheme(), "sip");
    EXPECT_EQ(uri3.getUser(), "user");
    EXPECT_EQ(uri3.getHost(), "example.com");
    EXPECT_EQ(uri3.getParameters(), "transport=udp");

    // Test URI with header
    SipUri uri4("sip:user@example.com?subject=meeting");
    EXPECT_EQ(uri4.getScheme(), "sip");
    EXPECT_EQ(uri4.getUser(), "user");
    EXPECT_EQ(uri4.getHost(), "example.com");
    EXPECT_EQ(uri4.getHeaders(), "subject=meeting");

    // Test complex URI
    SipUri uri5("sips:alice:password@example.com:5061;transport=tls?header1=value1&header2=value2");
    EXPECT_EQ(uri5.getScheme(), "sips");
    EXPECT_EQ(uri5.getUser(), "alice");
    EXPECT_EQ(uri5.getPassword(), "password");
    EXPECT_EQ(uri5.getHost(), "example.com");
    EXPECT_EQ(uri5.getPort(), 5061);
    EXPECT_EQ(uri5.getParameters(), "transport=tls");
    EXPECT_EQ(uri5.getHeaders(), "header1=value1&header2=value2");

    // Test toString()
    EXPECT_EQ(uri1.toString(), "sip:user@example.com");
    EXPECT_EQ(uri2.toString(), "sip:user@example.com:5060");
    EXPECT_EQ(uri3.toString(), "sip:user@example.com;transport=udp");
    EXPECT_EQ(uri4.toString(), "sip:user@example.com?subject=meeting");
    EXPECT_EQ(uri5.toString(), "sips:alice:password@example.com:5061;transport=tls?header1=value1&header2=value2");

    // Test invalid URI
    EXPECT_THROW(SipUri("invalid"), SipError);
}

// Test SDP session parsing and serialization
TEST_F(SipTest, SdpSession) {
    // Create a sample SDP message
    std::string sdp =
        "v=0\r\n"
        "o=jdoe 2890844526 2890842807 IN IP4 10.47.16.5\r\n"
        "s=SDP Seminar\r\n"
        "c=IN IP4 224.2.17.12\r\n"
        "t=0 0\r\n"
        "m=audio 49170 RTP/AVP 0\r\n"
        "a=rtpmap:0 PCMU/8000\r\n"
        "m=video 51372 RTP/AVP 31\r\n"
        "a=rtpmap:31 H261/90000\r\n";

    // Parse the SDP
    SdpSession session = SdpSession::parse(sdp);

    // Verify the parsed values
    EXPECT_EQ(session.getUsername(), "jdoe");
    EXPECT_EQ(session.getSessionId(), "2890844526");
    EXPECT_EQ(session.getSessionName(), "SDP Seminar");
    EXPECT_EQ(session.getConnectionAddress(), "224.2.17.12");
    EXPECT_EQ(session.getAudioPort(), 49170);
    EXPECT_EQ(session.getVideoPort(), 51372);

    // Check audio payload types
    auto audio_pts = session.getAudioPayloadTypes();
    EXPECT_EQ(audio_pts.size(), 1);
    EXPECT_EQ(audio_pts[0], rtp::RtpPayloadType::PCMU);

    // Check video payload types
    auto video_pts = session.getVideoPayloadTypes();
    EXPECT_EQ(video_pts.size(), 1);
    EXPECT_EQ(static_cast<int>(video_pts[0]), 31); // H261

    // Test serialization and re-parsing
    std::string serialized = session.toString();
    SdpSession reparsed = SdpSession::parse(serialized);

    // Verify the re-parsed values
    EXPECT_EQ(reparsed.getUsername(), session.getUsername());
    EXPECT_EQ(reparsed.getSessionName(), session.getSessionName());
    EXPECT_EQ(reparsed.getConnectionAddress(), session.getConnectionAddress());
    EXPECT_EQ(reparsed.getAudioPort(), session.getAudioPort());
    EXPECT_EQ(reparsed.getVideoPort(), session.getVideoPort());

    // Check audio payload types after re-parsing
    auto reparsed_audio_pts = reparsed.getAudioPayloadTypes();
    EXPECT_EQ(reparsed_audio_pts.size(), audio_pts.size());

    // Check video payload types after re-parsing
    auto reparsed_video_pts = reparsed.getVideoPayloadTypes();
    EXPECT_EQ(reparsed_video_pts.size(), video_pts.size());
}

// Test SIP message creation and parsing
TEST_F(SipTest, SipMessage) {
    // Create a SIP INVITE request
    SipUri target_uri("sip:bob@example.com");
    SipMessage invite = SipMessage::createRequest(SipMethod::INVITE, target_uri);

    // Set up headers
    invite.getHeaders().setFrom("sip:alice@example.com;tag=12345");
    invite.getHeaders().setTo("sip:bob@example.com");
    invite.getHeaders().setCallId("a84b4c76e66710");
    invite.getHeaders().setCSeq("1 INVITE");
    invite.getHeaders().setVia("SIP/2.0/UDP pc33.example.com;branch=z9hG4bK776asdhds");
    invite.getHeaders().setMaxForwards("70");
    invite.getHeaders().setContact("<sip:alice@pc33.example.com>");

    // Set body
    std::string sdp =
        "v=0\r\n"
        "o=alice 2890844526 2890844526 IN IP4 pc33.example.com\r\n"
        "s=Session SDP\r\n"
        "c=IN IP4 pc33.example.com\r\n"
        "t=0 0\r\n"
        "m=audio 49172 RTP/AVP 0\r\n"
        "a=rtpmap:0 PCMU/8000\r\n";

    invite.setBody(sdp, "application/sdp");

    // Convert to string
    std::string invite_str = invite.toString();

    // Parse the message back
    SipMessage parsed = SipMessage::parse(invite_str);

    // Verify the parsed message
    EXPECT_EQ(parsed.getType(), SipMessageType::Request);
    EXPECT_EQ(parsed.getMethod(), SipMethod::INVITE);
    EXPECT_EQ(parsed.getRequestUri().toString(), "sip:bob@example.com");
    EXPECT_EQ(parsed.getHeaders().getFrom(), "sip:alice@example.com;tag=12345");
    EXPECT_EQ(parsed.getHeaders().getTo(), "sip:bob@example.com");
    EXPECT_EQ(parsed.getHeaders().getCallId(), "a84b4c76e66710");
    EXPECT_EQ(parsed.getHeaders().getCSeq(), "1 INVITE");
    EXPECT_EQ(parsed.getHeaders().getContentType(), "application/sdp");
    EXPECT_EQ(parsed.getHeaders().getContentLength(), sdp.length());
    EXPECT_EQ(parsed.getBody(), sdp);

    // Create a SIP response
    SipMessage response = SipMessage::createResponse(SipResponseCode::OK, invite);

    // Add or modify headers
    response.getHeaders().setContact("<sip:bob@192.0.2.4>");
    response.getHeaders().setTo(response.getHeaders().getTo() + ";tag=8321234356");

    // Set response body
    std::string response_sdp =
        "v=0\r\n"
        "o=bob 2890844527 2890844527 IN IP4 192.0.2.4\r\n"
        "s=Session SDP\r\n"
        "c=IN IP4 192.0.2.4\r\n"
        "t=0 0\r\n"
        "m=audio 49174 RTP/AVP 0\r\n"
        "a=rtpmap:0 PCMU/8000\r\n";

    response.setBody(response_sdp, "application/sdp");

    // Convert to string
    std::string response_str = response.toString();

    // Parse the response back
    SipMessage parsed_response = SipMessage::parse(response_str);

    // Verify the parsed response
    EXPECT_EQ(parsed_response.getType(), SipMessageType::Response);
    EXPECT_EQ(parsed_response.getResponseCode(), SipResponseCode::OK);
    EXPECT_EQ(parsed_response.getHeaders().getFrom(), "sip:alice@example.com;tag=12345");
    EXPECT_EQ(parsed_response.getHeaders().getTo(), "sip:bob@example.com;tag=8321234356");
    EXPECT_EQ(parsed_response.getHeaders().getCallId(), "a84b4c76e66710");
    EXPECT_EQ(parsed_response.getHeaders().getCSeq(), "1 INVITE");
    EXPECT_EQ(parsed_response.getHeaders().getContentType(), "application/sdp");
    EXPECT_EQ(parsed_response.getHeaders().getContentLength(), response_sdp.length());
    EXPECT_EQ(parsed_response.getBody(), response_sdp);
}

// Test SIP transaction handling
TEST_F(SipTest, SipTransaction) {
    // Create a transaction manager
    auto transaction_manager = std::make_shared<SipTransactionManager>();

    // Create a request
    SipUri target_uri("sip:bob@example.com");
    SipMessage invite = SipMessage::createRequest(SipMethod::INVITE, target_uri);

    // Set up headers
    invite.getHeaders().setFrom("sip:alice@example.com;tag=12345");
    invite.getHeaders().setTo("sip:bob@example.com");
    invite.getHeaders().setCallId("a84b4c76e66710");
    invite.getHeaders().setCSeq("1 INVITE");
    invite.getHeaders().setVia("SIP/2.0/UDP pc33.example.com;branch=z9hG4bK776asdhds");
    invite.getHeaders().setMaxForwards("70");
    invite.getHeaders().setContact("<sip:alice@pc33.example.com>");

    // Create a client transaction
    auto transaction = transaction_manager->createClientTransaction(invite);

    // Verify transaction properties
    EXPECT_EQ(transaction->getType(), SipTransactionType::Client);
    EXPECT_EQ(transaction->getBranch(), "z9hG4bK776asdhds");
    EXPECT_EQ(transaction->getMethod(), SipMethod::INVITE);
    EXPECT_EQ(transaction->getState(), SipTransactionState::Trying);
    EXPECT_FALSE(transaction->isCompleted());
    EXPECT_TRUE(transaction->isClientTransaction());
    EXPECT_TRUE(transaction->isInvite());

    // Create a response
    SipMessage response = SipMessage::createResponse(SipResponseCode::Ringing, invite);
    response.getHeaders().setTo(response.getHeaders().getTo() + ";tag=8321234356");

    // Process the response through the transaction
    transaction->processResponse(response);

    // Verify transaction state changed
    EXPECT_EQ(transaction->getState(), SipTransactionState::Proceeding);
    EXPECT_FALSE(transaction->isCompleted());

    // Create a final response
    SipMessage final_response = SipMessage::createResponse(SipResponseCode::OK, invite);
    final_response.getHeaders().setTo(final_response.getHeaders().getTo() + ";tag=8321234356");

    // Process the final response
    transaction->processResponse(final_response);

    // Verify transaction is completed for 2xx responses to INVITE
    EXPECT_EQ(transaction->getState(), SipTransactionState::Terminated);
    EXPECT_TRUE(transaction->isCompleted());

    // Create a server transaction from a request
    SipMessage register_request = SipMessage::createRequest(SipMethod::REGISTER, SipUri("sip:registrar.example.com"));
    register_request.getHeaders().setFrom("sip:user@example.com;tag=123");
    register_request.getHeaders().setTo("sip:user@example.com");
    register_request.getHeaders().setCallId("register123");
    register_request.getHeaders().setCSeq("1 REGISTER");
    register_request.getHeaders().setVia("SIP/2.0/UDP pc33.example.com;branch=z9hG4bKnashds8");

    auto server_transaction = transaction_manager->createServerTransaction(register_request);

    // Verify server transaction properties
    EXPECT_EQ(server_transaction->getType(), SipTransactionType::Server);
    EXPECT_EQ(server_transaction->getBranch(), "z9hG4bKnashds8");
    EXPECT_EQ(server_transaction->getMethod(), SipMethod::REGISTER);
    EXPECT_EQ(server_transaction->getState(), SipTransactionState::Proceeding);
    EXPECT_FALSE(server_transaction->isCompleted());
    EXPECT_FALSE(server_transaction->isClientTransaction());
    EXPECT_FALSE(server_transaction->isInvite());

    // Send a response from the server transaction
    SipMessage server_response = SipMessage::createResponse(SipResponseCode::OK, register_request);
    server_transaction->sendResponse(server_response);

    // Verify server transaction state
    EXPECT_EQ(server_transaction->getState(), SipTransactionState::Completed);
    EXPECT_TRUE(server_transaction->isCompleted());

    // Cleanup
    transaction_manager->removeTransaction(transaction->getBranch());
    transaction_manager->removeTransaction(server_transaction->getBranch());
}

// Test simple SIP registration flow
TEST_F(SipTest, SipRegistration) {
    // Create a SIP agent
    auto agent = std::make_shared<SipAgent>();
    agent->start();

    // Set contact URI
    agent->setContactUri(SipUri("sip:alice@192.168.1.100:5060"));

    // Create a registration
    auto registration = agent->createRegistration(SipUri("sip:registrar.example.com"));

    // Set credentials
    registration->setCredentials("alice", "secret");
    registration->setExpires(3600);

    // Mock a registration flow

    // 1. REGISTER sent by the client
    registration->events().on("stateChanged", [](SipRegistrationState state) {
        // Log state changes for debugging
        core::Logger::debug("Registration state: {}",
                         SipRegistration::stateToString(state));
    });

    // When the register method is called, this would normally trigger a network request
    // Instead, we'll simulate responses directly

    // 2. Receive a 401 Unauthorized response first
    SipMessage challenge = SipMessage::createResponse(
        SipResponseCode::Unauthorized,
        registration->current_transaction_->getRequest()
    );

    // Add authentication challenge
    challenge.getHeaders().setHeader("WWW-Authenticate",
        "Digest realm=\"example.com\", nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\", "
        "algorithm=MD5, qop=\"auth\"");

    // Process the challenge via the transaction
    registration->current_transaction_->processResponse(challenge);

    // Wait a bit for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 3. Receive a 200 OK response to the authenticated request
    SipMessage ok = SipMessage::createResponse(
        SipResponseCode::OK,
        registration->current_transaction_->getRequest()
    );

    // Add Contact with expires parameter
    ok.getHeaders().setContact("<sip:alice@192.168.1.100:5060>;expires=3600");

    // Process the OK response via the transaction
    registration->current_transaction_->processResponse(ok);

    // Wait a bit for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify registration state
    EXPECT_EQ(registration->getState(), SipRegistrationState::Registered);
    EXPECT_EQ(registration->getExpires(), 3600);

    // Test unregistration
    registration->refresh();

    // Wait a bit for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Simulate a 200 OK response to the refresh
    SipMessage refresh_ok = SipMessage::createResponse(
        SipResponseCode::OK,
        registration->current_transaction_->getRequest()
    );

    // Process the OK response
    registration->current_transaction_->processResponse(refresh_ok);

    // Wait a bit for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify registration is still registered
    EXPECT_EQ(registration->getState(), SipRegistrationState::Registered);

    // Clean up
    agent->stop();
}

// Test basic SIP call flow
TEST_F(SipTest, SipCall) {
    // Create a SIP agent
    auto agent = std::make_shared<SipAgent>();
    agent->start();

    // Set contact URI
    agent->setContactUri(SipUri("sip:alice@192.168.1.100:5060"));

    // Create a task to test call flow
    auto test_call = [agent]() -> core::Task<void> {
        // Create a call
        auto call = co_await agent->createCall(SipUri("sip:bob@example.com"));

        // Create SDP for the call
        SdpSession sdp;
        sdp.setUsername("alice");
        sdp.setSessionName("Call");
        sdp.setConnectionAddress("192.168.1.100");
        sdp.setAudioPort(49172);
        sdp.addAudioPayloadType(rtp::RtpPayloadType::PCMU);

        // Set up event handlers
        call->events().on("stateChanged", [](SipCallState state) {
            // Log state changes for debugging
            core::Logger::debug("Call state: {}", SipCall::stateToString(state));
        });

        // Start dialing
        auto dial_task = call->dial(SipUri("sip:bob@example.com"), sdp);

        // Simulate a 100 Trying response
        SipMessage trying = SipMessage::createResponse(
            SipResponseCode::Trying,
            call->invite_transaction_->getRequest()
        );

        // Process the response
        call->invite_transaction_->processResponse(trying);

        // Wait a bit for processing
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Simulate a 180 Ringing response
        SipMessage ringing = SipMessage::createResponse(
            SipResponseCode::Ringing,
            call->invite_transaction_->getRequest()
        );
        ringing.getHeaders().setTo(ringing.getHeaders().getTo() + ";tag=314159");

        // Process the response
        call->invite_transaction_->processResponse(ringing);

        // Wait a bit for processing
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Verify call state
        EXPECT_EQ(call->getState(), SipCallState::Ringing);

        // Simulate a 200 OK response
        SipMessage ok = SipMessage::createResponse(
            SipResponseCode::OK,
            call->invite_transaction_->getRequest()
        );
        ok.getHeaders().setTo(ok.getHeaders().getTo() + ";tag=314159");

        // Add SDP body
        std::string remote_sdp =
            "v=0\r\n"
            "o=bob 2890844527 2890844527 IN IP4 192.0.2.4\r\n"
            "s=Call\r\n"
            "c=IN IP4 192.0.2.4\r\n"
            "t=0 0\r\n"
            "m=audio 49174 RTP/AVP 0\r\n"
            "a=rtpmap:0 PCMU/8000\r\n";

        ok.setBody(remote_sdp, "application/sdp");

        // Process the response
        call->invite_transaction_->processResponse(ok);

        // Wait a bit for processing
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Verify call state
        EXPECT_EQ(call->getState(), SipCallState::Connected);

        // Verify SDP
        EXPECT_EQ(call->getRemoteSdp().getUsername(), "bob");
        EXPECT_EQ(call->getRemoteSdp().getConnectionAddress(), "192.0.2.4");
        EXPECT_EQ(call->getRemoteSdp().getAudioPort(), 49174);

        // Hangup the call
        co_await call->hangup();

        // Verify call state
        EXPECT_EQ(call->getState(), SipCallState::Disconnected);

        co_return;
    };

    // Run the test
    test_call().get();

    // Clean up
    agent->stop();
}

} // namespace fmus::sip::test