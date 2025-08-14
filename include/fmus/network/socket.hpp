#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace fmus::network {

enum class SocketType {
    UDP,
    TCP
};

enum class SocketState {
    CLOSED,
    BINDING,
    BOUND,
    LISTENING,
    CONNECTING,
    CONNECTED,
    ERROR
};

struct SocketAddress {
    std::string ip;
    uint16_t port;
    
    SocketAddress() : port(0) {}
    SocketAddress(const std::string& ip, uint16_t port) : ip(ip), port(port) {}
    
    std::string toString() const {
        return ip + ":" + std::to_string(port);
    }
    
    sockaddr_in toSockAddr() const;
    static SocketAddress fromSockAddr(const sockaddr_in& addr);
};

class Socket {
public:
    using DataCallback = std::function<void(const std::vector<uint8_t>&, const SocketAddress&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using StateCallback = std::function<void(SocketState)>;
    using ConnectionCallback = std::function<void(std::shared_ptr<Socket>)>;

    Socket(SocketType type);
    virtual ~Socket();
    
    // Non-copyable
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    
    // Basic socket operations
    bool bind(const SocketAddress& address);
    bool listen(int backlog = 10); // TCP only
    bool connect(const SocketAddress& address); // TCP only
    void close();
    
    // Data operations
    bool send(const std::vector<uint8_t>& data, const SocketAddress& to = {});
    bool send(const uint8_t* data, size_t size, const SocketAddress& to = {});
    
    // Async operations
    void startReceiving();
    void stopReceiving();
    
    // Getters
    SocketType getType() const { return type_; }
    SocketState getState() const { return state_; }
    SocketAddress getLocalAddress() const { return local_address_; }
    SocketAddress getRemoteAddress() const { return remote_address_; }
    int getSocketFd() const { return socket_fd_; }
    
    // Callbacks
    void setDataCallback(DataCallback callback) { data_callback_ = callback; }
    void setErrorCallback(ErrorCallback callback) { error_callback_ = callback; }
    void setStateCallback(StateCallback callback) { state_callback_ = callback; }
    void setConnectionCallback(ConnectionCallback callback) { connection_callback_ = callback; }

protected:
    void setState(SocketState state);
    void notifyError(const std::string& error);
    void receiveLoop();
    
    SocketType type_;
    std::atomic<SocketState> state_;
    int socket_fd_;
    SocketAddress local_address_;
    SocketAddress remote_address_;
    
    // Threading
    std::atomic<bool> receiving_;
    std::thread receive_thread_;
    
    // Callbacks
    DataCallback data_callback_;
    ErrorCallback error_callback_;
    StateCallback state_callback_;
    ConnectionCallback connection_callback_;
    
    // Synchronization
    mutable std::mutex mutex_;
};

class UdpSocket : public Socket {
public:
    UdpSocket();
    
    // UDP-specific methods
    bool enableBroadcast(bool enable = true);
    bool enableMulticast(const std::string& group);
    bool setReceiveBufferSize(int size);
    bool setSendBufferSize(int size);
};

class TcpSocket : public Socket {
public:
    TcpSocket();
    explicit TcpSocket(int existing_fd, const SocketAddress& remote_addr);
    
    // TCP-specific methods
    bool setNoDelay(bool enable = true);
    bool setKeepAlive(bool enable = true);
    bool setReuseAddress(bool enable = true);
    
    // Server functionality
    void acceptConnections(); // Starts accepting connections in background
    void stopAccepting();
    
private:
    void acceptLoop();
    std::atomic<bool> accepting_;
    std::thread accept_thread_;
};

// Factory functions
std::shared_ptr<UdpSocket> createUdpSocket();
std::shared_ptr<TcpSocket> createTcpSocket();

} // namespace fmus::network
