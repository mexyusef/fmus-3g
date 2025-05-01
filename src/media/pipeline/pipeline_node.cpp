#include <fmus/media/pipeline.hpp>
#include <fmus/core/logger.hpp>

#include <unordered_map>
#include <algorithm>
#include <sstream>

namespace fmus::media::pipeline {

// Base node implementation
class PipelineNodeBase : public PipelineNode {
public:
    PipelineNodeBase(const std::string& name, NodeKind kind = NodeKind::Custom)
        : name_(name), kind_(kind) {
        if (name.empty()) {
            throw MediaError(MediaErrorCode::InvalidParameter, "Node name cannot be empty");
        }
        logger_ = core::Logger::get("MediaNode:" + name);
        logger_->debug("Creating node '{}'", name);
    }

    ~PipelineNodeBase() override {
        // Pastikan node dihentikan saat dihancurkan
        if (is_running_) {
            try {
                stop();
            } catch (const std::exception& e) {
                logger_->error("Error stopping node in destructor: {}", e.what());
            }
        }
        logger_->debug("Destroying node '{}'", name_);
    }

    // Implementasi interface PipelineNode
    std::string name() const override { return name_; }
    NodeKind kind() const override { return kind_; }

    std::vector<PortDescriptor> inputPorts() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return input_ports_;
    }

    std::vector<PortDescriptor> outputPorts() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return output_ports_;
    }

    core::Task<void> connectOutput(const std::string& output_port,
                            std::shared_ptr<PipelineNode> target,
                            const std::string& input_port) override {
        if (!target) {
            throw MediaError(MediaErrorCode::InvalidParameter, "Target node cannot be null");
        }

        // Verifikasi port
        verifyOutputPortExists(output_port);

        // Verifikasi target port
        auto target_ports = target->inputPorts();
        auto target_port_it = std::find_if(target_ports.begin(), target_ports.end(),
            [&input_port](const auto& port) { return port.name == input_port; });

        if (target_port_it == target_ports.end()) {
            throw MediaError(MediaErrorCode::InvalidParameter,
                "Input port '" + input_port + "' not found on target node");
        }

        // Ambil deskriptor port
        PortDescriptor out_desc;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = std::find_if(output_ports_.begin(), output_ports_.end(),
                [&output_port](const auto& port) { return port.name == output_port; });
            if (it != output_ports_.end()) {
                out_desc = *it;
            }
        }

        // Verifikasi kompatibilitas port (tipe media harus cocok)
        if (out_desc.type != target_port_it->type) {
            throw MediaError(MediaErrorCode::InvalidParameter,
                "Port types don't match: " + std::to_string(static_cast<int>(out_desc.type)) +
                " -> " + std::to_string(static_cast<int>(target_port_it->type)));
        }

        // Simpan koneksi
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& conn = output_connections_[output_port];
            conn.target = target;
            conn.port = input_port;
        }

        logger_->debug("Connected output port '{}' to {}:{}",
                      output_port, target->name(), input_port);

        co_return;
    }

    core::Task<void> disconnectOutput(const std::string& output_port) override {
        verifyOutputPortExists(output_port);

        std::lock_guard<std::mutex> lock(mutex_);
        output_connections_.erase(output_port);

        logger_->debug("Disconnected output port '{}'", output_port);
        co_return;
    }

    core::Task<void> disconnectInput(const std::string& input_port) override {
        verifyInputPortExists(input_port);

        // Tidak perlu lakukan apapun di sisi input karena koneksi
        // disimpan di sisi output
        logger_->debug("Disconnected input port '{}'", input_port);
        co_return;
    }

    core::Task<void> initialize() override {
        if (is_initialized_) {
            co_return; // Node sudah diinisialisasi
        }

        logger_->debug("Initializing node");

        try {
            co_await onInitializeNode();
            is_initialized_ = true;
            logger_->debug("Node initialized");
        } catch (const std::exception& e) {
            logger_->error("Failed to initialize node: {}", e.what());
            onError.emit(MediaError(MediaErrorCode::DeviceInitFailed,
                "Failed to initialize node: " + std::string(e.what())));
            throw;
        }
    }

    core::Task<void> start() override {
        if (!is_initialized_) {
            co_await initialize();
        }

        if (is_running_) {
            co_return; // Node sudah berjalan
        }

        logger_->debug("Starting node");

        try {
            co_await onStartNode();
            is_running_ = true;
            onStarted.emit();
            logger_->debug("Node started");
        } catch (const std::exception& e) {
            logger_->error("Failed to start node: {}", e.what());
            onError.emit(MediaError(MediaErrorCode::DeviceInitFailed,
                "Failed to start node: " + std::string(e.what())));
            throw;
        }
    }

    core::Task<void> stop() override {
        if (!is_running_) {
            co_return; // Node sudah berhenti
        }

        logger_->debug("Stopping node");

        try {
            co_await onStopNode();
            is_running_ = false;
            onStopped.emit();
            logger_->debug("Node stopped");
        } catch (const std::exception& e) {
            logger_->error("Failed to stop node: {}", e.what());
            onError.emit(MediaError(MediaErrorCode::DeviceInitFailed,
                "Failed to stop node: " + std::string(e.what())));
            throw;
        }
    }

    core::Task<void> reset() override {
        if (is_running_) {
            co_await stop();
        }

        logger_->debug("Resetting node");

        try {
            co_await onResetNode();
            is_initialized_ = false;
            logger_->debug("Node reset");
        } catch (const std::exception& e) {
            logger_->error("Failed to reset node: {}", e.what());
            onError.emit(MediaError(MediaErrorCode::DeviceInitFailed,
                "Failed to reset node: " + std::string(e.what())));
            throw;
        }
    }

    bool isRunning() const override {
        return is_running_;
    }

    void setProperty(const std::string& name, const std::any& value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        properties_[name] = value;
    }

    std::any getProperty(const std::string& name) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = properties_.find(name);
        if (it == properties_.end()) {
            throw MediaError(MediaErrorCode::InvalidParameter,
                "Property '" + name + "' not found");
        }
        return it->second;
    }

    bool hasProperty(const std::string& name) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return properties_.find(name) != properties_.end();
    }

protected:
    // Method overrides yang perlu diimplementasikan oleh kelas turunan
    virtual core::Task<void> onInitializeNode() { co_return; }
    virtual core::Task<void> onStartNode() { co_return; }
    virtual core::Task<void> onStopNode() { co_return; }
    virtual core::Task<void> onResetNode() { co_return; }

    // Helper untuk menambahkan port
    void addInputPort(const PortDescriptor& port) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Verifikasi port dengan nama sama tidak ada
        auto it = std::find_if(input_ports_.begin(), input_ports_.end(),
            [&port](const auto& p) { return p.name == port.name; });

        if (it != input_ports_.end()) {
            throw MediaError(MediaErrorCode::InvalidParameter,
                "Input port '" + port.name + "' already exists");
        }

        input_ports_.push_back(port);
    }

    void addOutputPort(const PortDescriptor& port) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Verifikasi port dengan nama sama tidak ada
        auto it = std::find_if(output_ports_.begin(), output_ports_.end(),
            [&port](const auto& p) { return p.name == port.name; });

        if (it != output_ports_.end()) {
            throw MediaError(MediaErrorCode::InvalidParameter,
                "Output port '" + port.name + "' already exists");
        }

        output_ports_.push_back(port);
    }

    // Helper untuk memeriksa keberadaan port
    void verifyInputPortExists(const std::string& port_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(input_ports_.begin(), input_ports_.end(),
            [&port_name](const auto& port) { return port.name == port_name; });

        if (it == input_ports_.end()) {
            throw MediaError(MediaErrorCode::InvalidParameter,
                "Input port '" + port_name + "' not found");
        }
    }

    void verifyOutputPortExists(const std::string& port_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(output_ports_.begin(), output_ports_.end(),
            [&port_name](const auto& port) { return port.name == port_name; });

        if (it == output_ports_.end()) {
            throw MediaError(MediaErrorCode::InvalidParameter,
                "Output port '" + port_name + "' not found");
        }
    }

    // Helper untuk mendapatkan koneksi output
    std::optional<std::pair<std::shared_ptr<PipelineNode>, std::string>>
    getOutputConnection(const std::string& port_name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = output_connections_.find(port_name);
        if (it == output_connections_.end() || !it->second.target) {
            return std::nullopt;
        }

        return std::make_pair(it->second.target, it->second.port);
    }

    // Struktur untuk menyimpan koneksi antar node
    struct NodeConnection {
        std::shared_ptr<PipelineNode> target;
        std::string port;
    };

private:
    std::string name_;
    NodeKind kind_;
    std::shared_ptr<core::Logger> logger_;

    bool is_initialized_ = false;
    bool is_running_ = false;

    std::vector<PortDescriptor> input_ports_;
    std::vector<PortDescriptor> output_ports_;
    std::unordered_map<std::string, NodeConnection> output_connections_;
    std::unordered_map<std::string, std::any> properties_;

    mutable std::mutex mutex_;
};

// Ekspos fungsi untuk membuat node dasar yang dapat digunakan oleh implementasi lain
std::shared_ptr<PipelineNode> createBasePipelineNode(const std::string& name, NodeKind kind) {
    return std::make_shared<PipelineNodeBase>(name, kind);
}

} // namespace fmus::media::pipeline