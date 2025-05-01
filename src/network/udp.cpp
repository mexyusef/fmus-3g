#include <fmus/network/network.hpp>
#include <fmus/core/logger.hpp>

#include <uv.h>
#include <memory>
#include <cassert>

namespace fmus::network {

namespace {
    // UDP socket implementation
    class UdpSocketImpl : public UdpSocket {
    public:
        explicit UdpSocketImpl(uv_loop_t* loop) {
            handle_ = std::make_unique<uv_udp_t>();
            int result = uv_udp_init(loop, handle_.get());
            if (result != 0) {
                throw NetworkError(systemErrorToNetworkError(result),
                    "Failed to initialize UDP socket");
            }
            handle_->data = this;
        }

        ~UdpSocketImpl() override {
            close();
        }

        // Implement Socket interface
        void setOption(const SocketOptions& options) override {
            if (options.reuse_address) {
                uv_udp_set_broadcast(handle_.get(), 1);
            }

            if (options.send_buffer_size > 0) {
                uv_send_buffer_size(
                    reinterpret_cast<uv_handle_t*>(handle_.get()),
                    &options.send_buffer_size);
            }

            if (options.recv_buffer_size > 0) {
                uv_recv_buffer_size(
                    reinterpret_cast<uv_handle_t*>(handle_.get()),
                    &options.recv_buffer_size);
            }
        }

        void close() override {
            if (handle_) {
                uv_close(reinterpret_cast<uv_handle_t*>(handle_.get()),
                    [](uv_handle_t* handle) {
                        delete reinterpret_cast<uv_udp_t*>(handle);
                    });
                handle_.release();
            }
        }

        bool isOpen() const override {
            return handle_ != nullptr;
        }

        // Implement UdpSocket interface
        core::Task<size_t> sendTo(const NetworkBuffer& buffer,
            const NetworkAddress& addr) override {
            if (!isOpen()) {
                throw NetworkError(NetworkErrorCode::NotConnected,
                    "Socket is closed");
            }

            // Setup send request
            auto req = std::make_unique<uv_udp_send_t>();
            uv_buf_t buf = uv_buf_init(
                reinterpret_cast<char*>(const_cast<uint8_t*>(buffer.data())),
                buffer.size());

            // Setup destination address
            sockaddr_storage storage;
            toSockAddr(addr, storage);

            // Start send operation
            int result = uv_udp_send(req.get(),
                handle_.get(),
                &buf, 1,
                reinterpret_cast<sockaddr*>(&storage),
                [](uv_udp_send_t* req, int status) {
                    auto* impl = reinterpret_cast<UdpSocketImpl*>(
                        req->handle->data);
                    impl->send_error_ = status;
                    delete req;
                });

            if (result != 0) {
                throw NetworkError(systemErrorToNetworkError(result),
                    "Failed to start send operation");
            }

            // Wait untuk completion
            co_await std::suspend_never{};

            if (send_error_ != 0) {
                throw NetworkError(systemErrorToNetworkError(send_error_),
                    "Send operation failed");
            }

            co_return buffer.size();
        }

        core::Task<std::pair<size_t, NetworkAddress>> receiveFrom(
            NetworkBuffer& buffer) override {
            if (!isOpen()) {
                throw NetworkError(NetworkErrorCode::NotConnected,
                    "Socket is closed");
            }

            // Setup receive buffer
            buffer.reserve(65536);  // Maximum UDP datagram size

            // Start receive operation
            int result = uv_udp_recv_start(handle_.get(),
                [](uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
                    auto* socket = reinterpret_cast<UdpSocketImpl*>(handle->data);
                    buf->base = socket->recv_buffer_;
                    buf->len = sizeof(socket->recv_buffer_);
                },
                [](uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf,
                   const sockaddr* addr, unsigned flags) {
                    auto* impl = reinterpret_cast<UdpSocketImpl*>(handle->data);
                    if (nread > 0) {
                        impl->recv_size_ = static_cast<size_t>(nread);
                        if (addr) {
                            impl->recv_addr_ = fromSockAddr(addr);
                        }
                    }
                    else if (nread < 0) {
                        impl->recv_error_ = static_cast<int>(nread);
                    }
                    uv_udp_recv_stop(handle);
                });

            if (result != 0) {
                throw NetworkError(systemErrorToNetworkError(result),
                    "Failed to start receive operation");
            }

            // Wait untuk completion
            co_await std::suspend_never{};

            if (recv_error_ != 0) {
                throw NetworkError(systemErrorToNetworkError(recv_error_),
                    "Receive operation failed");
            }

            buffer.write(recv_buffer_, recv_size_);
            co_return std::make_pair(recv_size_, recv_addr_);
        }

        NetworkAddress localAddress() const override {
            sockaddr_storage addr;
            int len = sizeof(addr);
            int result = uv_udp_getsockname(handle_.get(),
                reinterpret_cast<sockaddr*>(&addr), &len);

            if (result != 0) {
                throw NetworkError(systemErrorToNetworkError(result),
                    "Failed to get local address");
            }

            return fromSockAddr(reinterpret_cast<sockaddr*>(&addr));
        }

        // Get raw handle
        uv_udp_t* handle() { return handle_.get(); }

    private:
        std::unique_ptr<uv_udp_t> handle_;
        char recv_buffer_[65536];
        size_t recv_size_ = 0;
        int recv_error_ = 0;
        int send_error_ = 0;
        NetworkAddress recv_addr_;
    };
} // namespace

// Factory function implementation
core::Task<std::unique_ptr<UdpSocket>> UdpSocket::bind(
    const NetworkAddress& addr) {
    // Get default loop
    auto* loop = uv_default_loop();
    if (!loop) {
        throw NetworkError(NetworkErrorCode::UnknownError,
            "Failed to get event loop");
    }

    // Create socket
    auto socket = std::make_unique<UdpSocketImpl>(loop);

    // Bind to address
    sockaddr_storage storage;
    toSockAddr(addr, storage);

    int result = uv_udp_bind(socket->handle(),
        reinterpret_cast<sockaddr*>(&storage), 0);

    if (result != 0) {
        throw NetworkError(systemErrorToNetworkError(result),
            "Failed to bind to address");
    }

    co_return std::move(socket);
}

} // namespace fmus::network