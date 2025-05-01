#include <fmus/sip/sip.hpp>
#include <fmus/core/logger.hpp>
#include <fmus/core/task.hpp>

#include <sstream>
#include <regex>

namespace fmus::sip {

// SIP Call implementation
SipCall::SipCall(std::shared_ptr<SipAgent> agent, const std::string& call_id)
    : agent_(agent),
      call_id_(call_id.empty() ? SipMessage::generateCallId() : call_id),
      direction_(SipCallDirection::Outgoing),
      state_(SipCallState::Idle) {}

std::string SipCall::getCallId() const {
    return call_id_;
}

SipCallDirection SipCall::getDirection() const {
    return direction_;
}

SipCallState SipCall::getState() const {
    return state_;
}

core::EventEmitter<SipCallEvents>& SipCall::events() {
    return events_;
}

void SipCall::setState(SipCallState state) {
    // Merekam state sebelumnya untuk acara logging
    SipCallState old_state = state_;
    state_ = state;

    // Log state change
    core::Logger::debug("SIP Call {} state changed: {} -> {}",
                     call_id_,
                     stateToString(old_state),
                     stateToString(state_));

    // Emit state change event
    events_.emit("stateChanged", state_);
}

std::string SipCall::stateToString(SipCallState state) {
    switch (state) {
        case SipCallState::Idle:
            return "Idle";
        case SipCallState::Calling:
            return "Calling";
        case SipCallState::Ringing:
            return "Ringing";
        case SipCallState::Connecting:
            return "Connecting";
        case SipCallState::Connected:
            return "Connected";
        case SipCallState::Disconnecting:
            return "Disconnecting";
        case SipCallState::Disconnected:
            return "Disconnected";
        default:
            return "Unknown";
    }
}

core::Task<void> SipCall::dial(const SipUri& target, const SdpSession& sdp) {
    if (state_ != SipCallState::Idle) {
        throw SipError(SipErrorCode::InvalidState, "Call is not in Idle state");
    }

    // Menyimpan target URI
    target_uri_ = target;

    // Menyimpan local SDP
    local_sdp_ = sdp;

    // Mengubah state menjadi Calling
    setState(SipCallState::Calling);

    // Membuat INVITE request
    SipMessage invite = SipMessage::createRequest(SipMethod::INVITE, target);

    // Set From header
    std::string from_uri = agent_->getContactUri()->toString();
    std::string from_tag = SipMessage::generateTag();
    local_tag_ = from_tag;
    invite.getHeaders().setFrom(from_uri + ";tag=" + from_tag);

    // Set To header
    invite.getHeaders().setTo(target.toString());

    // Set Call-ID header
    invite.getHeaders().setCallId(call_id_);

    // Set CSeq header
    invite.getHeaders().setCSeq("1 INVITE");

    // Set Via header
    std::string via = "SIP/2.0/UDP " + agent_->getContactUri()->getHost();
    if (agent_->getContactUri()->getPort() > 0) {
        via += ":" + std::to_string(agent_->getContactUri()->getPort());
    }
    via += ";branch=" + SipMessage::generateBranch();
    invite.getHeaders().setVia(via);

    // Set Max-Forwards header
    invite.getHeaders().setMaxForwards("70");

    // Set Contact header
    invite.getHeaders().setContact("<" + agent_->getContactUri()->toString() + ">");

    // Set User-Agent header
    invite.getHeaders().setUserAgent("FMUS SIP Client");

    // Set SDP body
    invite.setBody(local_sdp_.toString(), "application/sdp");

    // Membuat transaction
    invite_transaction_ = agent_->getTransactionManager()->createClientTransaction(invite);

    // Set up event handlers for transaction
    invite_transaction_->events().on("response", [this](const SipMessage& response) {
        handleInviteResponse(response);
    });

    // Wait for completion
    setState(SipCallState::Calling);

    // Return a task that resolves when the call is established or fails
    core::TaskPromise<void> promise;
    auto future = promise.getFuture();

    // Set up a handler for state changes
    auto handler = events_.on("stateChanged", [this, promise](SipCallState state) mutable {
        if (state == SipCallState::Connected) {
            // Call established - resolve the promise
            promise.setReady();
        } else if (state == SipCallState::Disconnected) {
            // Call failed - reject the promise
            promise.setError(SipError(SipErrorCode::CallFailed, "Call failed"));
        }
    });

    // Return the task
    co_await future;

    // Cleanup listener
    events_.off("stateChanged", handler);

    co_return;
}

core::Task<void> SipCall::answer(const SdpSession& sdp) {
    if (state_ != SipCallState::Ringing) {
        throw SipError(SipErrorCode::InvalidState, "Call is not in Ringing state");
    }

    // Menyimpan local SDP
    local_sdp_ = sdp;

    // Mengubah state menjadi Connecting
    setState(SipCallState::Connecting);

    // Create 200 OK response
    SipMessage ok = SipMessage::createResponse(SipResponseCode::OK, *initial_invite_);

    // Add Contact header
    ok.getHeaders().setContact("<" + agent_->getContactUri()->toString() + ">");

    // Ensure To header has tag
    std::string to = ok.getHeaders().getTo();
    if (to.find("tag=") == std::string::npos) {
        local_tag_ = SipMessage::generateTag();
        ok.getHeaders().setTo(to + ";tag=" + local_tag_);
    }

    // Set User-Agent header
    ok.getHeaders().setUserAgent("FMUS SIP Client");

    // Set SDP body
    ok.setBody(local_sdp_.toString(), "application/sdp");

    // Send 200 OK
    invite_transaction_->sendResponse(ok);

    // Wait for ACK (in a real implementation)
    // For now, just transition to Connected state
    setState(SipCallState::Connected);

    co_return;
}

core::Task<void> SipCall::reject(SipResponseCode code) {
    if (state_ != SipCallState::Ringing) {
        throw SipError(SipErrorCode::InvalidState, "Call is not in Ringing state");
    }

    // Create rejection response
    SipMessage response = SipMessage::createResponse(code, *initial_invite_);

    // Ensure To header has tag
    std::string to = response.getHeaders().getTo();
    if (to.find("tag=") == std::string::npos) {
        local_tag_ = SipMessage::generateTag();
        response.getHeaders().setTo(to + ";tag=" + local_tag_);
    }

    // Set User-Agent header
    response.getHeaders().setUserAgent("FMUS SIP Client");

    // Send rejection response
    invite_transaction_->sendResponse(response);

    // Transition to Disconnected state
    setState(SipCallState::Disconnected);

    co_return;
}

core::Task<void> SipCall::hangup() {
    if (state_ != SipCallState::Connected &&
        state_ != SipCallState::Connecting &&
        state_ != SipCallState::Ringing &&
        state_ != SipCallState::Calling) {
        throw SipError(SipErrorCode::InvalidState, "Call is not in an active state");
    }

    // Transition to Disconnecting state
    setState(SipCallState::Disconnecting);

    if (direction_ == SipCallDirection::Outgoing) {
        // For outgoing calls, send CANCEL if not connected yet
        if (state_ == SipCallState::Calling || state_ == SipCallState::Ringing) {
            // Create CANCEL request
            SipMessage cancel = SipMessage::createRequest(SipMethod::CANCEL, target_uri_);

            // Set From header (same as INVITE)
            cancel.getHeaders().setFrom(invite_transaction_->getRequest().getHeaders().getFrom());

            // Set To header (same as INVITE)
            cancel.getHeaders().setTo(invite_transaction_->getRequest().getHeaders().getTo());

            // Set Call-ID header (same as INVITE)
            cancel.getHeaders().setCallId(call_id_);

            // Set CSeq header (same as INVITE but method is CANCEL)
            std::string cseq = invite_transaction_->getRequest().getHeaders().getCSeq();
            size_t space_pos = cseq.find(' ');
            if (space_pos != std::string::npos) {
                std::string cseq_num = cseq.substr(0, space_pos);
                cancel.getHeaders().setCSeq(cseq_num + " CANCEL");
            }

            // Set Via header (same as INVITE)
            cancel.getHeaders().setVia(invite_transaction_->getRequest().getHeaders().getVia());

            // Set Max-Forwards header
            cancel.getHeaders().setMaxForwards("70");

            // Set User-Agent header
            cancel.getHeaders().setUserAgent("FMUS SIP Client");

            // Send CANCEL
            auto cancel_transaction = agent_->getTransactionManager()->createClientTransaction(cancel);

            // Wait for completion (in a real implementation)
            // For now, just transition to Disconnected state
            setState(SipCallState::Disconnected);
        } else if (state_ == SipCallState::Connected) {
            // For established calls, send BYE
            // Create BYE request
            SipMessage bye = SipMessage::createRequest(SipMethod::BYE, target_uri_);

            // Set From header
            bye.getHeaders().setFrom(invite_transaction_->getRequest().getHeaders().getFrom());

            // Set To header
            bye.getHeaders().setTo(invite_transaction_->getLastResponse().getHeaders().getTo());

            // Set Call-ID header
            bye.getHeaders().setCallId(call_id_);

            // Set CSeq header
            // Increment CSeq number
            std::string cseq = invite_transaction_->getRequest().getHeaders().getCSeq();
            size_t space_pos = cseq.find(' ');
            if (space_pos != std::string::npos) {
                int cseq_num = std::stoi(cseq.substr(0, space_pos)) + 1;
                bye.getHeaders().setCSeq(std::to_string(cseq_num) + " BYE");
            }

            // Set Via header
            std::string via = "SIP/2.0/UDP " + agent_->getContactUri()->getHost();
            if (agent_->getContactUri()->getPort() > 0) {
                via += ":" + std::to_string(agent_->getContactUri()->getPort());
            }
            via += ";branch=" + SipMessage::generateBranch();
            bye.getHeaders().setVia(via);

            // Set Max-Forwards header
            bye.getHeaders().setMaxForwards("70");

            // Set Contact header
            bye.getHeaders().setContact("<" + agent_->getContactUri()->toString() + ">");

            // Set User-Agent header
            bye.getHeaders().setUserAgent("FMUS SIP Client");

            // Send BYE
            auto bye_transaction = agent_->getTransactionManager()->createClientTransaction(bye);

            // Wait for completion (in a real implementation)
            // For now, just transition to Disconnected state
            setState(SipCallState::Disconnected);
        }
    } else {
        // For incoming calls, send BYE if connected, or 603 Decline if ringing
        if (state_ == SipCallState::Connected) {
            // Create BYE request
            SipMessage bye = SipMessage::createRequest(SipMethod::BYE, remote_uri_);

            // Set From header
            bye.getHeaders().setFrom("<" + agent_->getContactUri()->toString() + ">;tag=" + local_tag_);

            // Set To header
            bye.getHeaders().setTo(initial_invite_->getHeaders().getFrom());

            // Set Call-ID header
            bye.getHeaders().setCallId(call_id_);

            // Set CSeq header
            bye.getHeaders().setCSeq("1 BYE");

            // Set Via header
            std::string via = "SIP/2.0/UDP " + agent_->getContactUri()->getHost();
            if (agent_->getContactUri()->getPort() > 0) {
                via += ":" + std::to_string(agent_->getContactUri()->getPort());
            }
            via += ";branch=" + SipMessage::generateBranch();
            bye.getHeaders().setVia(via);

            // Set Max-Forwards header
            bye.getHeaders().setMaxForwards("70");

            // Set Contact header
            bye.getHeaders().setContact("<" + agent_->getContactUri()->toString() + ">");

            // Set User-Agent header
            bye.getHeaders().setUserAgent("FMUS SIP Client");

            // Send BYE
            auto bye_transaction = agent_->getTransactionManager()->createClientTransaction(bye);

            // Wait for completion (in a real implementation)
            // For now, just transition to Disconnected state
            setState(SipCallState::Disconnected);
        } else if (state_ == SipCallState::Ringing) {
            // Create 603 Decline response
            SipMessage decline = SipMessage::createResponse(SipResponseCode::Decline, *initial_invite_);

            // Ensure To header has tag
            std::string to = decline.getHeaders().getTo();
            if (to.find("tag=") == std::string::npos) {
                local_tag_ = SipMessage::generateTag();
                decline.getHeaders().setTo(to + ";tag=" + local_tag_);
            }

            // Set User-Agent header
            decline.getHeaders().setUserAgent("FMUS SIP Client");

            // Send decline response
            invite_transaction_->sendResponse(decline);

            // Transition to Disconnected state
            setState(SipCallState::Disconnected);
        }
    }

    co_return;
}

void SipCall::processIncomingInvite(std::shared_ptr<SipTransaction> transaction, const SipMessage& invite) {
    // Store the INVITE message and transaction
    initial_invite_ = std::make_shared<SipMessage>(invite);
    invite_transaction_ = transaction;

    // Set direction to Incoming
    direction_ = SipCallDirection::Incoming;

    // Extract From URI
    std::string from = invite.getHeaders().getFrom();
    std::regex uri_regex("<([^>]+)>");
    std::smatch match;
    if (std::regex_search(from, match, uri_regex)) {
        remote_uri_ = SipUri(match[1].str());
    } else {
        // If no <uri> format, try to extract the URI part
        size_t colon_pos = from.find(':');
        if (colon_pos != std::string::npos) {
            size_t semicolon_pos = from.find(';', colon_pos);
            if (semicolon_pos != std::string::npos) {
                remote_uri_ = SipUri(from.substr(0, semicolon_pos));
            } else {
                remote_uri_ = SipUri(from);
            }
        } else {
            core::Logger::error("Failed to extract From URI from INVITE");
            remote_uri_ = SipUri("sip:unknown@example.com");
        }
    }

    // Extract remote tag
    std::regex tag_regex("tag=([^;]+)");
    if (std::regex_search(from, match, tag_regex)) {
        remote_tag_ = match[1].str();
    }

    // Extract SDP if present
    if (invite.getHeaders().getContentType() == "application/sdp") {
        try {
            remote_sdp_ = SdpSession::parse(invite.getBody());
        } catch (const std::exception& e) {
            core::Logger::error("Failed to parse SDP in INVITE: {}", e.what());
        }
    }

    // Send 100 Trying response
    SipMessage trying = SipMessage::createResponse(SipResponseCode::Trying, invite);

    // Ensure To header has no tag
    std::string to = trying.getHeaders().getTo();
    if (to.find("tag=") != std::string::npos) {
        // Hapus tag dari To header
        size_t tag_pos = to.find("tag=");
        if (tag_pos != std::string::npos) {
            size_t tag_end = to.find(';', tag_pos);
            if (tag_end != std::string::npos) {
                to = to.substr(0, tag_pos) + to.substr(tag_end + 1);
            } else {
                to = to.substr(0, tag_pos - 1);  // -1 to remove the semicolon before tag
            }
            trying.getHeaders().setTo(to);
        }
    }

    // Set User-Agent header
    trying.getHeaders().setUserAgent("FMUS SIP Client");

    // Send 100 Trying
    transaction->sendResponse(trying);

    // Send 180 Ringing
    SipMessage ringing = SipMessage::createResponse(SipResponseCode::Ringing, invite);

    // Ensure To header has no tag yet (will be added when answering)
    ringing.getHeaders().setTo(to);

    // Set User-Agent header
    ringing.getHeaders().setUserAgent("FMUS SIP Client");

    // Send 180 Ringing
    transaction->sendResponse(ringing);

    // Transition to Ringing state
    setState(SipCallState::Ringing);

    // Notify application of incoming call
    events_.emit("incomingCall", this);
}

void SipCall::handleInviteResponse(const SipMessage& response) {
    SipResponseCode code = response.getResponseCode();
    int code_value = static_cast<int>(code);

    if (code_value >= 100 && code_value < 200) {
        // Provisional response
        if (code == SipResponseCode::Ringing) {
            // 180 Ringing
            setState(SipCallState::Ringing);
        }
        // Ignore other provisional responses
    } else if (code_value >= 200 && code_value < 300) {
        // Success response

        // Extract To header for remote tag
        std::string to = response.getHeaders().getTo();
        std::regex tag_regex("tag=([^;]+)");
        std::smatch match;
        if (std::regex_search(to, match, tag_regex)) {
            remote_tag_ = match[1].str();
        }

        // Extract SDP if present
        if (response.getHeaders().getContentType() == "application/sdp") {
            try {
                remote_sdp_ = SdpSession::parse(response.getBody());
            } catch (const std::exception& e) {
                core::Logger::error("Failed to parse SDP in 200 OK: {}", e.what());
            }
        }

        // Send ACK
        SipMessage ack = SipMessage::createRequest(SipMethod::ACK, target_uri_);

        // Set From header (same as INVITE)
        ack.getHeaders().setFrom(invite_transaction_->getRequest().getHeaders().getFrom());

        // Set To header (from response, includes remote tag)
        ack.getHeaders().setTo(response.getHeaders().getTo());

        // Set Call-ID header
        ack.getHeaders().setCallId(call_id_);

        // Set CSeq header
        std::string cseq = invite_transaction_->getRequest().getHeaders().getCSeq();
        size_t space_pos = cseq.find(' ');
        if (space_pos != std::string::npos) {
            std::string cseq_num = cseq.substr(0, space_pos);
            ack.getHeaders().setCSeq(cseq_num + " ACK");
        }

        // Set Via header
        std::string via = "SIP/2.0/UDP " + agent_->getContactUri()->getHost();
        if (agent_->getContactUri()->getPort() > 0) {
            via += ":" + std::to_string(agent_->getContactUri()->getPort());
        }
        via += ";branch=" + SipMessage::generateBranch();
        ack.getHeaders().setVia(via);

        // Set Max-Forwards header
        ack.getHeaders().setMaxForwards("70");

        // Set Contact header
        ack.getHeaders().setContact("<" + agent_->getContactUri()->toString() + ">");

        // Set User-Agent header
        ack.getHeaders().setUserAgent("FMUS SIP Client");

        // Create transaction for ACK
        auto ack_transaction = agent_->getTransactionManager()->createClientTransaction(ack);

        // Transition to Connected state
        setState(SipCallState::Connected);
    } else {
        // Error response
        core::Logger::warn("INVITE failed with code {}: {}",
                       code_value, response.getReasonPhrase());

        // Transition to Disconnected state
        setState(SipCallState::Disconnected);

        // Emit error event
        events_.emit("error", SipError(SipErrorCode::CallFailed,
                                     "Call failed with code " + std::to_string(code_value) +
                                     ": " + response.getReasonPhrase()));
    }
}

void SipCall::processBye(std::shared_ptr<SipTransaction> transaction, const SipMessage& bye) {
    // Send 200 OK response to BYE
    SipMessage ok = SipMessage::createResponse(SipResponseCode::OK, bye);

    // Set User-Agent header
    ok.getHeaders().setUserAgent("FMUS SIP Client");

    // Send 200 OK
    transaction->sendResponse(ok);

    // Transition to Disconnected state
    setState(SipCallState::Disconnected);
}

void SipCall::processCancel(std::shared_ptr<SipTransaction> transaction, const SipMessage& cancel) {
    // Send 200 OK response to CANCEL
    SipMessage ok = SipMessage::createResponse(SipResponseCode::OK, cancel);

    // Set User-Agent header
    ok.getHeaders().setUserAgent("FMUS SIP Client");

    // Send 200 OK
    transaction->sendResponse(ok);

    // Also send 487 Request Terminated for the INVITE
    if (invite_transaction_) {
        SipMessage terminated = SipMessage::createResponse(
            SipResponseCode::RequestTerminated,
            invite_transaction_->getRequest()
        );

        // Ensure To header has tag
        std::string to = terminated.getHeaders().getTo();
        if (to.find("tag=") == std::string::npos) {
            local_tag_ = SipMessage::generateTag();
            terminated.getHeaders().setTo(to + ";tag=" + local_tag_);
        }

        // Set User-Agent header
        terminated.getHeaders().setUserAgent("FMUS SIP Client");

        // Send 487 Request Terminated
        invite_transaction_->sendResponse(terminated);
    }

    // Transition to Disconnected state
    setState(SipCallState::Disconnected);
}

const SdpSession& SipCall::getLocalSdp() const {
    return local_sdp_;
}

const SdpSession& SipCall::getRemoteSdp() const {
    return remote_sdp_;
}

} // namespace fmus::sip