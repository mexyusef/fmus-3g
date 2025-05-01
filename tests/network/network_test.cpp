#include <gtest/gtest.h>
#include <fmus/network/network.hpp>
#include <fmus/core/task.hpp>
#include <thread>
#include <future>

namespace fmus::network::test {

class NetworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        scheduler_ = core::make_task_scheduler();
        scheduler_->start();
    }

    void TearDown() override {
        scheduler_->stop();
        scheduler_.reset();
    }

    std::unique_ptr<core::TaskScheduler> scheduler_;
};

// Test network address
TEST_F(NetworkTest, NetworkAddress) {
    // Test construction
    NetworkAddress addr1("127.0.0.1", 8080);
    EXPECT_EQ(addr1.host(), "127.0.0.1");
    EXPECT_EQ(addr1.port(), 8080);

    // Test parsing
    auto addr2 = NetworkAddress::parse("localhost:8081");
    EXPECT_EQ(addr2.host(), "localhost");
    EXPECT_EQ(addr2.port(), 8081);

    // Test invalid format
    EXPECT_THROW(NetworkAddress::parse("invalid"), NetworkError);
    EXPECT_THROW(NetworkAddress::parse("host:invalid"), NetworkError);
    EXPECT_THROW(NetworkAddress::parse("host:99999"), NetworkError);
}

// Test network buffer
TEST_F(NetworkTest, NetworkBuffer) {
    // Test construction
    NetworkBuffer buf1(1024);
    EXPECT_EQ(buf1.size(), 0);
    EXPECT_EQ(buf1.capacity(), 1024);

    // Test write/read
    const char* data = "Hello, World!";
    size_t len = strlen(data);

    buf1.write(data, len);
    EXPECT_EQ(buf1.size(), len);

    char read_data[64];
    buf1.read(read_data, len);
    EXPECT_EQ(memcmp(read_data, data, len), 0);

    // Test buffer overflow
    EXPECT_THROW(buf1.read(read_data, 1), NetworkError);
}

// Test TCP echo server/client
TEST_F(NetworkTest, TcpEcho) {
    // Start echo server
    auto server_task = [this]() -> core::Task<void> {
        auto listener = co_await TcpListener::bind(
            NetworkAddress("127.0.0.1", 8082));

        auto client = co_await listener->accept();

        NetworkBuffer buffer(1024);
        auto bytes_read = co_await client->read(buffer, 1024);
        co_await client->write(buffer);
    }();

    scheduler_->schedule(server_task);

    // Wait untuk server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Connect client dan send data
    auto client_task = [this]() -> core::Task<void> {
        auto client = co_await TcpStream::connect(
            NetworkAddress("127.0.0.1", 8082));

        const char* message = "Echo Test";
        NetworkBuffer send_buf(reinterpret_cast<const void*>(message),
            strlen(message));

        co_await client->write(send_buf);

        NetworkBuffer recv_buf(1024);
        auto bytes_read = co_await client->read(recv_buf, 1024);

        EXPECT_EQ(bytes_read, strlen(message));
        EXPECT_EQ(memcmp(recv_buf.data(), message, bytes_read), 0);
    }();

    scheduler_->runSync(client_task);
}

// Test UDP echo server/client
TEST_F(NetworkTest, UdpEcho) {
    // Start echo server
    auto server_task = [this]() -> core::Task<void> {
        auto socket = co_await UdpSocket::bind(
            NetworkAddress("127.0.0.1", 8083));

        NetworkBuffer buffer(1024);
        auto [bytes_read, client_addr] = co_await socket->receiveFrom(buffer);

        co_await socket->sendTo(buffer, client_addr);
    }();

    scheduler_->schedule(server_task);

    // Wait untuk server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send data from client
    auto client_task = [this]() -> core::Task<void> {
        auto socket = co_await UdpSocket::bind(
            NetworkAddress("127.0.0.1", 0));

        const char* message = "UDP Echo Test";
        NetworkBuffer send_buf(reinterpret_cast<const void*>(message),
            strlen(message));

        co_await socket->sendTo(send_buf,
            NetworkAddress("127.0.0.1", 8083));

        NetworkBuffer recv_buf(1024);
        auto [bytes_read, server_addr] = co_await socket->receiveFrom(recv_buf);

        EXPECT_EQ(bytes_read, strlen(message));
        EXPECT_EQ(memcmp(recv_buf.data(), message, bytes_read), 0);
    }();

    scheduler_->runSync(client_task);
}

// Test socket options
TEST_F(NetworkTest, SocketOptions) {
    auto tcp_task = [this]() -> core::Task<void> {
        auto socket = co_await TcpStream::connect(
            NetworkAddress("127.0.0.1", 8084));

        SocketOptions options;
        options.reuse_address = true;
        options.send_buffer_size = 8192;
        options.recv_buffer_size = 8192;

        socket->setOption(options);
        EXPECT_TRUE(socket->isOpen());
    }();

    auto udp_task = [this]() -> core::Task<void> {
        auto socket = co_await UdpSocket::bind(
            NetworkAddress("127.0.0.1", 8085));

        SocketOptions options;
        options.reuse_address = true;
        options.send_buffer_size = 8192;
        options.recv_buffer_size = 8192;

        socket->setOption(options);
        EXPECT_TRUE(socket->isOpen());
    }();

    EXPECT_NO_THROW(scheduler_->runSync(tcp_task));
    EXPECT_NO_THROW(scheduler_->runSync(udp_task));
}

// Test error handling
TEST_F(NetworkTest, ErrorHandling) {
    // Test connection refused
    auto connect_task = [this]() -> core::Task<void> {
        co_await TcpStream::connect(
            NetworkAddress("127.0.0.1", 9999));
    }();

    EXPECT_THROW(scheduler_->runSync(connect_task), NetworkError);

    // Test invalid address
    EXPECT_THROW(
        TcpListener::bind(NetworkAddress("invalid", 8086)),
        NetworkError);

    EXPECT_THROW(
        UdpSocket::bind(NetworkAddress("invalid", 8087)),
        NetworkError);
}

// Test concurrent connections
TEST_F(NetworkTest, ConcurrentConnections) {
    static constexpr int kNumClients = 10;
    std::atomic<int> completed_clients{0};

    // Start server
    auto server_task = [this]() -> core::Task<void> {
        auto listener = co_await TcpListener::bind(
            NetworkAddress("127.0.0.1", 8088));

        for (int i = 0; i < kNumClients; ++i) {
            auto client = co_await listener->accept();

            // Handle client in separate task
            auto handler = [](std::unique_ptr<TcpStream> stream) -> core::Task<void> {
                NetworkBuffer buffer(1024);
                auto bytes_read = co_await stream->read(buffer, 1024);
                co_await stream->write(buffer);
            }(std::move(client));

            scheduler_->schedule(handler);
        }
    }();

    scheduler_->schedule(server_task);

    // Wait untuk server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Start clients
    std::vector<core::Task<void>> client_tasks;
    for (int i = 0; i < kNumClients; ++i) {
        auto client_task = [this, &completed_clients]() -> core::Task<void> {
            auto client = co_await TcpStream::connect(
                NetworkAddress("127.0.0.1", 8088));

            const char* message = "Test";
            NetworkBuffer send_buf(reinterpret_cast<const void*>(message),
                strlen(message));

            co_await client->write(send_buf);

            NetworkBuffer recv_buf(1024);
            auto bytes_read = co_await client->read(recv_buf, 1024);

            completed_clients++;
        }();

        client_tasks.push_back(std::move(client_task));
    }

    // Schedule all clients
    for (auto& task : client_tasks) {
        scheduler_->schedule(task);
    }

    // Wait untuk all clients to complete
    while (completed_clients < kNumClients) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(completed_clients, kNumClients);
}

} // namespace fmus::network::test