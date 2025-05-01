#include <fmus/network/network.hpp>
#include <fmus/core/logger.hpp>

#include <uv.h>
#include <memory>
#include <cassert>

namespace fmus::network {

namespace {
    // Wrapper untuk libuv TCP handle
    class TcpHandleImpl : public TcpStream {
    public:
        explicit TcpHandleImpl(uv_loop_t* loop) {
            handle_ = std::make_unique<uv_tcp_t>();
            int result = uv_tcp_init(loop, handle_.get());
            if (result != 0) {
                throw NetworkError(systemErrorToNetworkError(result),
                    "Failed to initialize TCP handle");
            }
            handle_->data = this;
        }

        ~TcpHandleImpl() override {
            close();
        }

        // Implement Socket interface
        void setOption(const SocketOptions& options) override {
            if (options.reuse_address) {
                uv_tcp_bind_flags flags = UV_TCP_IPV6ONLY;
                uv_tcp_simultaneous_accepts(handle_.get(), 1);
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
                        delete reinterpret_cast<uv_tcp_t*>(handle);
                    });
                handle_.release();
            }
        }

        bool isOpen() const override {
            return handle_ != nullptr;
        }

        // Implement TcpStream interface
        core::Task<size_t> read(NetworkBuffer& buffer, size_t max_size) override {
            if (!isOpen()) {
                throw NetworkError(NetworkErrorCode::NotConnected, "Socket is closed");
            }

            buffer.reserve(max_size);

            // Setup read request
            auto req = std::make_unique<uv_buf_t>();
            req->base = reinterpret_cast<char*>(buffer.data() + buffer.size());
            req->len = max_size;

            // Start read operation
            int result = uv_read_start(
                reinterpret_cast<uv_stream_t*>(handle_.get()),
                [](uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
                    auto* stream = reinterpret_cast<TcpHandleImpl*>(handle->data);
                    buf->base = stream->read_buffer_;
                    buf->len = sizeof(stream->read_buffer_);
                },
                [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
                    auto* impl = reinterpret_cast<TcpHandleImpl*>(stream->data);
                    if (nread > 0) {
                        impl->read_size_ = static_cast<size_t>(nread);
                    }
                    else if (nread < 0) {
                        impl->read_error_ = static_cast<int>(nread);
                    }
                    uv_read_stop(stream);
                });

            if (result != 0) {
                throw NetworkError(systemErrorToNetworkError(result),
                    "Failed to start read operation");
            }

            // Wait untuk completion
            co_await std::suspend_never{};

            if (read_error_ != 0) {
                throw NetworkError(systemErrorToNetworkError(read_error_),
                    "Read operation failed");
            }

            buffer.write(read_buffer_, read_size_);
            co_return read_size_;
        }

        core::Task<size_t> write(const NetworkBuffer& buffer) override {
            if (!isOpen()) {
                throw NetworkError(NetworkErrorCode::NotConnected, "Socket is closed");
            }

            // Setup write request
            auto req = std::make_unique<uv_write_t>();
            uv_buf_t buf = uv_buf_init(
                reinterpret_cast<char*>(const_cast<uint8_t*>(buffer.data())),
                buffer.size());

            // Start write operation
            int result = uv_write(req.get(),
                reinterpret_cast<uv_stream_t*>(handle_.get()),
                &buf, 1,
                [](uv_write_t* req, int status) {
                    auto* impl = reinterpret_cast<TcpHandleImpl*>(
                        req->handle->data);
                    impl->write_error_ = status;
                    delete req;
                });

            if (result != 0) {
                throw NetworkError(systemErrorToNetworkError(result),
                    "Failed to start write operation");
            }

            // Wait untuk completion
            co_await std::suspend_never{};

            if (write_error_ != 0) {
                throw NetworkError(systemErrorToNetworkError(write_error_),
                    "Write operation failed");
            }

            co_return buffer.size();
        }

        NetworkAddress localAddress() const override {
            sockaddr_storage addr;
            int len = sizeof(addr);
            int result = uv_tcp_getsockname(handle_.get(),
                reinterpret_cast<sockaddr*>(&addr), &len);

            if (result != 0) {
                throw NetworkError(systemErrorToNetworkError(result),
                    "Failed to get local address");
            }

            return fromSockAddr(reinterpret_cast<sockaddr*>(&addr));
        }

        NetworkAddress remoteAddress() const override {
            sockaddr_storage addr;
            int len = sizeof(addr);
            int result = uv_tcp_getpeername(handle_.get(),
                reinterpret_cast<sockaddr*>(&addr), &len);

            if (result != 0) {
                throw NetworkError(systemErrorToNetworkError(result),
                    "Failed to get remote address");
            }

            return fromSockAddr(reinterpret_cast<sockaddr*>(&addr));
        }

        // Get raw handle
        uv_tcp_t* handle() { return handle_.get(); }

    private:
        std::unique_ptr<uv_tcp_t> handle_;
        char read_buffer_[65536];
        size_t read_size_ = 0;
        int read_error_ = 0;
        int write_error_ = 0;
    };

    // TCP listener implementation
    class TcpListenerImpl : public TcpListener {
    public:
        explicit TcpListenerImpl(uv_loop_t* loop) {
            handle_ = std::make_unique<uv_tcp_t>();
            int result = uv_tcp_init(loop, handle_.get());
            if (result != 0) {
                throw NetworkError(systemErrorToNetworkError(result),
                    "Failed to initialize TCP listener");
            }
            handle_->data = this;
        }

        ~TcpListenerImpl() override {
            close();
        }

        // Implement Socket interface
        void setOption(const SocketOptions& options) override {
            if (options.reuse_address) {
                uv_tcp_bind_flags flags = UV_TCP_IPV6ONLY;
                uv_tcp_simultaneous_accepts(handle_.get(), 1);
            }
        }

        void close() override {
            if (handle_) {
                uv_close(reinterpret_cast<uv_handle_t*>(handle_.get()),
                    [](uv_handle_t* handle) {
                        delete reinterpret_cast<uv_tcp_t*>(handle);
                    });
                handle_.release();
            }
        }

        bool isOpen() const override {
            return handle_ != nullptr;
        }

        // Implement TcpListener interface
        core::Task<std::unique_ptr<TcpStream>> accept() override {
            if (!isOpen()) {
                throw NetworkError(NetworkErrorCode::NotConnected,
                    "Listener is closed");
            }

            // Create new connection handle
            auto client = std::make_unique<TcpHandleImpl>(
                handle_->loop);

            // Accept connection
            int result = uv_accept(
                reinterpret_cast<uv_stream_t*>(handle_.get()),
                reinterpret_cast<uv_stream_t*>(client->handle()));

            if (result != 0) {
                throw NetworkError(systemErrorToNetworkError(result),
                    "Failed to accept connection");
            }

            co_return std::move(client);
        }

        NetworkAddress localAddress() const override {
            sockaddr_storage addr;
            int len = sizeof(addr);
            int result = uv_tcp_getsockname(handle_.get(),
                reinterpret_cast<sockaddr*>(&addr), &len);

            if (result != 0) {
                throw NetworkError(systemErrorToNetworkError(result),
                    "Failed to get local address");
            }

            return fromSockAddr(reinterpret_cast<sockaddr*>(&addr));
        }

        // Get raw handle
        uv_tcp_t* handle() { return handle_.get(); }

    private:
        std::unique_ptr<uv_tcp_t> handle_;
    };
} // namespace

// Factory functions implementation
core::Task<std::unique_ptr<TcpListener>> TcpListener::bind(
    const NetworkAddress& addr) {
    // Get default loop
    auto* loop = uv_default_loop();
    if (!loop) {
        throw NetworkError(NetworkErrorCode::UnknownError,
            "Failed to get event loop");
    }

    // Create listener
    auto listener = std::make_unique<TcpListenerImpl>(loop);

    // Bind to address
    sockaddr_storage storage;
    toSockAddr(addr, storage);

    int result = uv_tcp_bind(listener->handle(),
        reinterpret_cast<sockaddr*>(&storage), 0);

    if (result != 0) {
        throw NetworkError(systemErrorToNetworkError(result),
            "Failed to bind to address");
    }

    // Start listening
    result = uv_listen(
        reinterpret_cast<uv_stream_t*>(listener->handle()),
        SOMAXCONN,
        nullptr);

    if (result != 0) {
        throw NetworkError(systemErrorToNetworkError(result),
            "Failed to start listening");
    }

    co_return std::move(listener);
}

core::Task<std::unique_ptr<TcpStream>> TcpStream::connect(
    const NetworkAddress& addr) {
    // Get default loop
    auto* loop = uv_default_loop();
    if (!loop) {
        throw NetworkError(NetworkErrorCode::UnknownError,
            "Failed to get event loop");
    }

    // Create connection handle
    auto stream = std::make_unique<TcpHandleImpl>(loop);

    // Connect to address
    sockaddr_storage storage;
    toSockAddr(addr, storage);

    auto req = std::make_unique<uv_connect_t>();
    req->data = stream.get();

    int result = uv_tcp_connect(req.get(),
        stream->handle(),
        reinterpret_cast<sockaddr*>(&storage),
        [](uv_connect_t* req, int status) {
            auto* impl = reinterpret_cast<TcpHandleImpl*>(req->data);
            impl->write_error_ = status;
            delete req;
        });

    if (result != 0) {
        throw NetworkError(systemErrorToNetworkError(result),
            "Failed to start connection");
    }

    // Wait untuk completion
    co_await std::suspend_never{};

    if (stream->write_error_ != 0) {
        throw NetworkError(systemErrorToNetworkError(stream->write_error_),
            "Connection failed");
    }

    co_return std::move(stream);
}

} // namespace fmus::network