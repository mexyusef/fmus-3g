#include "fmus/network/socket.hpp"
#include "fmus/core/logger.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <netinet/tcp.h>

namespace fmus::network {

sockaddr_in SocketAddress::toSockAddr() const {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (ip.empty() || ip == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    }
    return addr;
}

SocketAddress SocketAddress::fromSockAddr(const sockaddr_in& addr) {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    return SocketAddress(ip_str, ntohs(addr.sin_port));
}

Socket::Socket(SocketType type) 
    : type_(type), state_(SocketState::CLOSED), socket_fd_(-1), receiving_(false) {
}

Socket::~Socket() {
    close();
}

bool Socket::bind(const SocketAddress& address) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != SocketState::CLOSED) {
        notifyError("Socket must be closed before binding");
        return false;
    }
    
    setState(SocketState::BINDING);
    
    // Create socket
    int domain = AF_INET;
    int type = (type_ == SocketType::UDP) ? SOCK_DGRAM : SOCK_STREAM;
    socket_fd_ = socket(domain, type, 0);
    
    if (socket_fd_ < 0) {
        notifyError("Failed to create socket: " + std::string(strerror(errno)));
        setState(SocketState::ERROR);
        return false;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        core::Logger::warn("Failed to set SO_REUSEADDR: {}", strerror(errno));
    }
    
    // Bind socket
    sockaddr_in addr = address.toSockAddr();
    if (::bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        notifyError("Failed to bind socket: " + std::string(strerror(errno)));
        ::close(socket_fd_);
        socket_fd_ = -1;
        setState(SocketState::ERROR);
        return false;
    }
    
    // Get actual bound address (in case port was 0)
    socklen_t addr_len = sizeof(addr);
    if (getsockname(socket_fd_, (struct sockaddr*)&addr, &addr_len) == 0) {
        local_address_ = SocketAddress::fromSockAddr(addr);
    } else {
        local_address_ = address;
    }
    
    setState(SocketState::BOUND);
    core::Logger::info("Socket bound to {}", local_address_.toString());
    return true;
}

bool Socket::listen(int backlog) {
    if (type_ != SocketType::TCP) {
        notifyError("Listen is only supported for TCP sockets");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != SocketState::BOUND) {
        notifyError("Socket must be bound before listening");
        return false;
    }
    
    if (::listen(socket_fd_, backlog) < 0) {
        notifyError("Failed to listen: " + std::string(strerror(errno)));
        setState(SocketState::ERROR);
        return false;
    }
    
    setState(SocketState::LISTENING);
    core::Logger::info("Socket listening on {}", local_address_.toString());
    return true;
}

bool Socket::connect(const SocketAddress& address) {
    if (type_ != SocketType::TCP) {
        notifyError("Connect is only supported for TCP sockets");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != SocketState::BOUND && state_ != SocketState::CLOSED) {
        notifyError("Invalid state for connect");
        return false;
    }
    
    if (state_ == SocketState::CLOSED) {
        // Create socket if not already created
        socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd_ < 0) {
            notifyError("Failed to create socket: " + std::string(strerror(errno)));
            setState(SocketState::ERROR);
            return false;
        }
    }
    
    setState(SocketState::CONNECTING);
    
    sockaddr_in addr = address.toSockAddr();
    if (::connect(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        notifyError("Failed to connect: " + std::string(strerror(errno)));
        setState(SocketState::ERROR);
        return false;
    }
    
    remote_address_ = address;
    setState(SocketState::CONNECTED);
    core::Logger::info("Connected to {}", remote_address_.toString());
    return true;
}

void Socket::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (socket_fd_ >= 0) {
        stopReceiving();
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
    
    setState(SocketState::CLOSED);
}

bool Socket::send(const std::vector<uint8_t>& data, const SocketAddress& to) {
    return send(data.data(), data.size(), to);
}

bool Socket::send(const uint8_t* data, size_t size, const SocketAddress& to) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (socket_fd_ < 0) {
        notifyError("Socket not open");
        return false;
    }
    
    ssize_t sent;
    if (type_ == SocketType::UDP) {
        sockaddr_in addr = to.toSockAddr();
        sent = sendto(socket_fd_, data, size, 0, (struct sockaddr*)&addr, sizeof(addr));
    } else {
        sent = ::send(socket_fd_, data, size, 0);
    }
    
    if (sent < 0) {
        notifyError("Send failed: " + std::string(strerror(errno)));
        return false;
    }
    
    if (static_cast<size_t>(sent) != size) {
        core::Logger::warn("Partial send: {} of {} bytes", sent, size);
    }
    
    return true;
}

void Socket::startReceiving() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (receiving_ || socket_fd_ < 0) {
        return;
    }
    
    receiving_ = true;
    receive_thread_ = std::thread(&Socket::receiveLoop, this);
}

void Socket::stopReceiving() {
    receiving_ = false;
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
}

void Socket::setState(SocketState state) {
    state_ = state;
    if (state_callback_) {
        state_callback_(state);
    }
}

void Socket::notifyError(const std::string& error) {
    core::Logger::error("Socket error: {}", error);
    if (error_callback_) {
        error_callback_(error);
    }
}

void Socket::receiveLoop() {
    core::Logger::debug("Starting receive loop for socket");
    
    std::vector<uint8_t> buffer(65536); // 64KB buffer
    
    while (receiving_) {
        sockaddr_in from_addr{};
        socklen_t from_len = sizeof(from_addr);
        
        ssize_t received;
        if (type_ == SocketType::UDP) {
            received = recvfrom(socket_fd_, buffer.data(), buffer.size(), 0,
                              (struct sockaddr*)&from_addr, &from_len);
        } else {
            received = recv(socket_fd_, buffer.data(), buffer.size(), 0);
            // For TCP, we need to get peer address differently
            if (received > 0 && getpeername(socket_fd_, (struct sockaddr*)&from_addr, &from_len) != 0) {
                from_addr = remote_address_.toSockAddr();
            }
        }
        
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (receiving_) { // Only report error if we're still supposed to be receiving
                notifyError("Receive failed: " + std::string(strerror(errno)));
            }
            break;
        } else if (received == 0) {
            // Connection closed
            if (type_ == SocketType::TCP) {
                core::Logger::info("TCP connection closed by peer");
                setState(SocketState::CLOSED);
            }
            break;
        }
        
        if (data_callback_) {
            std::vector<uint8_t> data(buffer.begin(), buffer.begin() + received);
            SocketAddress from = SocketAddress::fromSockAddr(from_addr);
            data_callback_(data, from);
        }
    }
    
    core::Logger::debug("Receive loop ended");
}

// UdpSocket implementation
UdpSocket::UdpSocket() : Socket(SocketType::UDP) {
}

bool UdpSocket::enableBroadcast(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (socket_fd_ < 0) {
        notifyError("Socket not created");
        return false;
    }
    
    int opt = enable ? 1 : 0;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        notifyError("Failed to set broadcast option: " + std::string(strerror(errno)));
        return false;
    }
    
    return true;
}

bool UdpSocket::enableMulticast(const std::string& group) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (socket_fd_ < 0) {
        notifyError("Socket not created");
        return false;
    }
    
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(group.c_str());
    mreq.imr_interface.s_addr = INADDR_ANY;
    
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        notifyError("Failed to join multicast group: " + std::string(strerror(errno)));
        return false;
    }
    
    return true;
}

bool UdpSocket::setReceiveBufferSize(int size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (socket_fd_ < 0) {
        notifyError("Socket not created");
        return false;
    }
    
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) < 0) {
        notifyError("Failed to set receive buffer size: " + std::string(strerror(errno)));
        return false;
    }
    
    return true;
}

bool UdpSocket::setSendBufferSize(int size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (socket_fd_ < 0) {
        notifyError("Socket not created");
        return false;
    }
    
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) < 0) {
        notifyError("Failed to set send buffer size: " + std::string(strerror(errno)));
        return false;
    }
    
    return true;
}

// TcpSocket implementation
TcpSocket::TcpSocket() : Socket(SocketType::TCP), accepting_(false) {
}

TcpSocket::TcpSocket(int existing_fd, const SocketAddress& remote_addr)
    : Socket(SocketType::TCP), accepting_(false) {
    socket_fd_ = existing_fd;
    remote_address_ = remote_addr;
    setState(SocketState::CONNECTED);

    // Get local address
    sockaddr_in addr{};
    socklen_t addr_len = sizeof(addr);
    if (getsockname(socket_fd_, (struct sockaddr*)&addr, &addr_len) == 0) {
        local_address_ = SocketAddress::fromSockAddr(addr);
    }
}

bool TcpSocket::setNoDelay(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (socket_fd_ < 0) {
        notifyError("Socket not created");
        return false;
    }

    int opt = enable ? 1 : 0;
    if (setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        notifyError("Failed to set TCP_NODELAY: " + std::string(strerror(errno)));
        return false;
    }

    return true;
}

bool TcpSocket::setKeepAlive(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (socket_fd_ < 0) {
        notifyError("Socket not created");
        return false;
    }

    int opt = enable ? 1 : 0;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
        notifyError("Failed to set SO_KEEPALIVE: " + std::string(strerror(errno)));
        return false;
    }

    return true;
}

bool TcpSocket::setReuseAddress(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (socket_fd_ < 0) {
        notifyError("Socket not created");
        return false;
    }

    int opt = enable ? 1 : 0;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        notifyError("Failed to set SO_REUSEADDR: " + std::string(strerror(errno)));
        return false;
    }

    return true;
}

void TcpSocket::acceptConnections() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (accepting_ || state_ != SocketState::LISTENING) {
        return;
    }

    accepting_ = true;
    accept_thread_ = std::thread(&TcpSocket::acceptLoop, this);
}

void TcpSocket::stopAccepting() {
    accepting_ = false;
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void TcpSocket::acceptLoop() {
    core::Logger::debug("Starting accept loop for TCP server");

    while (accepting_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(socket_fd_, (struct sockaddr*)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (accepting_) { // Only report error if we're still supposed to be accepting
                notifyError("Accept failed: " + std::string(strerror(errno)));
            }
            break;
        }

        SocketAddress client_address = SocketAddress::fromSockAddr(client_addr);
        core::Logger::info("Accepted connection from {}", client_address.toString());

        if (connection_callback_) {
            auto client_socket = std::make_shared<TcpSocket>(client_fd, client_address);
            connection_callback_(client_socket);
        } else {
            // No callback set, close the connection
            ::close(client_fd);
        }
    }

    core::Logger::debug("Accept loop ended");
}

// Factory functions
std::shared_ptr<UdpSocket> createUdpSocket() {
    return std::make_shared<UdpSocket>();
}

std::shared_ptr<TcpSocket> createTcpSocket() {
    return std::make_shared<TcpSocket>();
}

} // namespace fmus::network
