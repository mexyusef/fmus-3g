# fmus-3g

Video telephony platform based on the my old 3G video codebase, originally developed in 2008-2009.

## Features

### Core Components
- **Robust Logging System**: Thread-safe logging with multiple levels (DEBUG, INFO, WARN, ERROR)
- **SIP Message Handling**: Complete SIP message parsing and generation
- **RTP/RTCP Support**: Real-time transport protocol implementation
- **WebRTC Integration**: Basic WebRTC session management
- **Media Processing**: Audio and video frame handling

### SIP Protocol Support
- SIP message creation and parsing
- Support for all major SIP methods (INVITE, ACK, BYE, REGISTER, etc.)
- Complete header management
- URI parsing and manipulation
- Response code handling

### RTP/RTCP Features
- RTP packet serialization/deserialization
- RTCP packet support
- Header field management
- Payload handling

### Media Capabilities
- Audio frame processing (PCM, various sample rates)
- Video frame handling (RGB, various resolutions)
- Extensible media format support

## Building

### Prerequisites
- CMake 3.16 or higher
- C++20 compatible compiler (GCC 12+ recommended)
- Linux development environment

### Build Instructions
```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

### Running Tests
```bash
./fmus-3g
```

## Architecture

The project is organized into modular libraries:

- **fmus-core**: Logging and utility functions
- **fmus-sip**: SIP protocol implementation
- **fmus-rtp**: RTP/RTCP packet handling
- **fmus-webrtc**: WebRTC session management
- **fmus-media**: Media frame processing

## Usage Example

```cpp
#include "fmus/sip/message.hpp"
#include "fmus/rtp/packet.hpp"
#include "fmus/webrtc/session.hpp"

// Create SIP INVITE message
sip::SipUri uri("sip:user@example.com:5060");
sip::SipMessage invite(sip::SipMethod::INVITE, uri);
invite.getHeaders().setFrom("sip:alice@example.com");
invite.getHeaders().setTo("sip:bob@example.com");

// Create RTP packet
rtp::RtpHeader header;
header.payload_type = 0; // PCMU
header.sequence_number = 1234;
std::vector<uint8_t> payload = {0x80, 0x81, 0x82, 0x83};
rtp::RtpPacket packet(header, payload);

// Start WebRTC session
webrtc::Session session;
session.onStateChange = [](webrtc::SessionState state) {
    // Handle state changes
};
session.start();
```

## Project Status

This is a clean, foundational implementation that provides:
- Working build system
- Core SIP message handling
- RTP packet processing
- Basic WebRTC session management
- Media frame structures
- Comprehensive logging

## License

MIT License.
