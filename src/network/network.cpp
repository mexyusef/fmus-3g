#include <fmus/network/network.hpp>
#include <fmus/core/logger.hpp>

#include <uv.h>
#include <string>
#include <cstring>
#include <system_error>

namespace fmus::network {

// NetworkAddress implementation
NetworkAddress::NetworkAddress(const std::string& host, uint16_t port)
    : host_(host), port_(port) {}

NetworkAddress NetworkAddress::parse(const std::string& address) {
    auto pos = address.find(':');
    if (pos == std::string::npos) {
        throw NetworkError(NetworkErrorCode::InvalidArgument, "Invalid address format");
    }

    auto host = address.substr(0, pos);
    auto port_str = address.substr(pos + 1);

    try {
        auto port = std::stoi(port_str);
        if (port < 0 || port > 65535) {
            throw NetworkError(NetworkErrorCode::InvalidArgument, "Invalid port number");
        }
        return NetworkAddress(host, static_cast<uint16_t>(port));
    }
    catch (const std::exception&) {
        throw NetworkError(NetworkErrorCode::InvalidArgument, "Invalid port number");
    }
}

std::string NetworkAddress::toString() const {
    return host_ + ":" + std::to_string(port_);
}

// NetworkBuffer implementation
NetworkBuffer::NetworkBuffer(size_t initial_size)
    : data_(initial_size), size_(0) {}

NetworkBuffer::NetworkBuffer(const void* data, size_t size)
    : data_(static_cast<const uint8_t*>(data),
           static_cast<const uint8_t*>(data) + size),
      size_(size) {}

void NetworkBuffer::write(const void* data, size_t size) {
    if (size_ + size > data_.size()) {
        data_.resize(size_ + size);
    }
    std::memcpy(data_.data() + size_, data, size);
    size_ += size;
}

void NetworkBuffer::read(void* data, size_t size) {
    if (read_pos_ + size > size_) {
        throw NetworkError(NetworkErrorCode::InvalidArgument, "Buffer underflow");
    }
    std::memcpy(data, data_.data() + read_pos_, size);
    read_pos_ += size;
}

void NetworkBuffer::reserve(size_t size) {
    data_.reserve(size);
}

void NetworkBuffer::resize(size_t size) {
    data_.resize(size);
    size_ = std::min(size_, size);
    read_pos_ = std::min(read_pos_, size_);
}

void NetworkBuffer::clear() noexcept {
    size_ = 0;
    read_pos_ = 0;
}

// Network error handling
NetworkErrorCode systemErrorToNetworkError(int error_code) {
    switch (error_code) {
        case UV_ECONNREFUSED:
            return NetworkErrorCode::ConnectionRefused;
        case UV_ECONNRESET:
            return NetworkErrorCode::ConnectionReset;
        case UV_ECONNABORTED:
            return NetworkErrorCode::ConnectionAborted;
        case UV_ENOTCONN:
            return NetworkErrorCode::NotConnected;
        case UV_ETIMEDOUT:
            return NetworkErrorCode::TimedOut;
        case UV_EADDRINUSE:
            return NetworkErrorCode::AddressInUse;
        case UV_EADDRNOTAVAIL:
            return NetworkErrorCode::AddressNotAvailable;
        case UV_EPIPE:
            return NetworkErrorCode::BrokenPipe;
        case UV_EAGAIN:
            return NetworkErrorCode::OperationWouldBlock;
        case UV_EINVAL:
            return NetworkErrorCode::InvalidArgument;
        case UV_EACCES:
            return NetworkErrorCode::PermissionDenied;
        default:
            return NetworkErrorCode::UnknownError;
    }
}

NetworkError::NetworkError(NetworkErrorCode code, const std::string& message)
    : Error(core::ErrorCode::NetworkError, message), code_(code) {}

// Helper untuk converting addresses
static void toSockAddr(const NetworkAddress& addr, sockaddr_storage& storage) {
    std::memset(&storage, 0, sizeof(storage));

    // Try IPv4 first
    sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&storage);
    if (uv_ip4_addr(addr.host().c_str(), addr.port(), addr4) == 0) {
        return;
    }

    // Try IPv6
    sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(&storage);
    if (uv_ip6_addr(addr.host().c_str(), addr.port(), addr6) == 0) {
        return;
    }

    throw NetworkError(NetworkErrorCode::InvalidArgument, "Invalid IP address");
}

static NetworkAddress fromSockAddr(const sockaddr* addr) {
    char ip[INET6_ADDRSTRLEN];
    int port;

    if (addr->sa_family == AF_INET) {
        auto addr4 = reinterpret_cast<const sockaddr_in*>(addr);
        uv_ip4_name(addr4, ip, sizeof(ip));
        port = ntohs(addr4->sin_port);
    }
    else if (addr->sa_family == AF_INET6) {
        auto addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
        uv_ip6_name(addr6, ip, sizeof(ip));
        port = ntohs(addr6->sin6_port);
    }
    else {
        throw NetworkError(NetworkErrorCode::InvalidArgument, "Unknown address family");
    }

    return NetworkAddress(ip, static_cast<uint16_t>(port));
}

} // namespace fmus::network