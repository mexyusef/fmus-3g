#include <fmus/media/pipeline.hpp>
#include <fmus/core/logger.hpp>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace fmus::media::pipeline {

// Pipeline implementation
Pipeline::Pipeline(const PipelineConfig& config)
    : config_(config) {
    // Membuat logger untuk pipeline
    auto logger = core::Logger::get("MediaPipeline");
    logger->debug("Creating new media pipeline");
}

Pipeline::~Pipeline() {
    // Ensure pipeline is stopped
    if (is_running_) {
        stop().wait();
    }
}

void Pipeline::addNode(std::shared_ptr<PipelineNode> node) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if node name already exists
    if (nodes_.find(node->name()) != nodes_.end()) {
        throw MediaError(MediaErrorCode::InvalidParameter,
                        "Node with name '" + node->name() + "' already exists in pipeline");
    }

    nodes_[node->name()] = node;

    // Subscribe to node events
    node->onError.subscribe([this](const core::Error& error) {
        onError.emit(error);
    });

    auto logger = core::Logger::get("MediaPipeline");
    logger->debug("Added node '{}' to pipeline", node->name());
}

void Pipeline::removeNode(const std::string& node_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = nodes_.find(node_name);
    if (it == nodes_.end()) {
        return; // Node doesn't exist, nothing to do
    }

    // If pipeline is running, stop the node first
    if (is_running_) {
        it->second->stop().wait();
    }

    // Remove the node
    nodes_.erase(it);

    auto logger = core::Logger::get("MediaPipeline");
    logger->debug("Removed node '{}' from pipeline", node_name);
}

std::shared_ptr<PipelineNode> Pipeline::getNode(const std::string& node_name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = nodes_.find(node_name);
    if (it == nodes_.end()) {
        return nullptr; // Node doesn't exist
    }

    return it->second;
}

std::vector<std::shared_ptr<PipelineNode>> Pipeline::getNodes() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::shared_ptr<PipelineNode>> result;
    result.reserve(nodes_.size());

    for (const auto& [name, node] : nodes_) {
        result.push_back(node);
    }

    return result;
}

core::Task<void> Pipeline::connectNodes(
    const std::string& source_node, const std::string& output_port,
    const std::string& target_node, const std::string& input_port) {

    auto source = getNode(source_node);
    if (!source) {
        co_return co_await core::Task<void>::fromException(
            MediaError(MediaErrorCode::InvalidParameter,
                      "Source node '" + source_node + "' not found"));
    }

    auto target = getNode(target_node);
    if (!target) {
        co_return co_await core::Task<void>::fromException(
            MediaError(MediaErrorCode::InvalidParameter,
                      "Target node '" + target_node + "' not found"));
    }

    // Connect the nodes
    try {
        co_await source->connectOutput(output_port, target, input_port);
    } catch (const std::exception& ex) {
        co_return co_await core::Task<void>::fromException(
            MediaError(MediaErrorCode::InvalidParameter,
                      std::string("Failed to connect nodes: ") + ex.what()));
    }
}

core::Task<void> Pipeline::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Initialize all nodes
    for (auto& [name, node] : nodes_) {
        try {
            co_await node->initialize();
        } catch (const std::exception& ex) {
            onError.emit(MediaError(MediaErrorCode::UnknownError,
                                   "Failed to initialize node '" + name + "': " + ex.what()));
            co_return co_await core::Task<void>::fromException(
                MediaError(MediaErrorCode::UnknownError,
                          "Failed to initialize pipeline"));
        }
    }

    // Auto-connect nodes if configured
    if (config_.auto_connect) {
        // Implementation of auto-connection logic would go here
        // This would involve analyzing all nodes' input and output ports
        // and connecting compatible ones
    }
}

core::Task<void> Pipeline::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (is_running_) {
        co_return; // Already running
    }

    // Start all nodes (start sinks first, then filters, then sources)
    std::vector<std::pair<NodeKind, std::shared_ptr<PipelineNode>>> sorted_nodes;

    for (auto& [name, node] : nodes_) {
        sorted_nodes.emplace_back(node->kind(), node);
    }

    // Sort by node kind (Sink, Filter/Processor, Source)
    std::sort(sorted_nodes.begin(), sorted_nodes.end(),
              [](const auto& a, const auto& b) {
                  // Order: Sink (2), Filter/Processor (1), Source (0)
                  int a_value = (a.first == NodeKind::Sink) ? 2 :
                               ((a.first == NodeKind::Filter || a.first == NodeKind::Processor) ? 1 : 0);
                  int b_value = (b.first == NodeKind::Sink) ? 2 :
                               ((b.first == NodeKind::Filter || b.first == NodeKind::Processor) ? 1 : 0);
                  return a_value > b_value;
              });

    // Start nodes in the sorted order
    for (auto& [kind, node] : sorted_nodes) {
        try {
            co_await node->start();
        } catch (const std::exception& ex) {
            // If any node fails to start, try to stop already started nodes
            is_running_ = false;
            onError.emit(MediaError(MediaErrorCode::UnknownError,
                                   "Failed to start node '" + node->name() + "': " + ex.what()));

            // Try to stop nodes that were started
            for (auto& [started_kind, started_node] : sorted_nodes) {
                if (started_node == node) {
                    break; // Stop when we reach the failed node
                }

                try {
                    co_await started_node->stop();
                } catch (...) {
                    // Ignore errors during cleanup
                }
            }

            co_return co_await core::Task<void>::fromException(
                MediaError(MediaErrorCode::UnknownError,
                          "Failed to start pipeline"));
        }
    }

    is_running_ = true;
    onStarted.emit();
}

core::Task<void> Pipeline::stop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!is_running_) {
        co_return; // Already stopped
    }

    // Stop all nodes (stop sources first, then filters, then sinks)
    std::vector<std::pair<NodeKind, std::shared_ptr<PipelineNode>>> sorted_nodes;

    for (auto& [name, node] : nodes_) {
        sorted_nodes.emplace_back(node->kind(), node);
    }

    // Sort by node kind (Source, Filter/Processor, Sink)
    std::sort(sorted_nodes.begin(), sorted_nodes.end(),
              [](const auto& a, const auto& b) {
                  // Order: Source (2), Filter/Processor (1), Sink (0)
                  int a_value = (a.first == NodeKind::Source) ? 2 :
                               ((a.first == NodeKind::Filter || a.first == NodeKind::Processor) ? 1 : 0);
                  int b_value = (b.first == NodeKind::Source) ? 2 :
                               ((b.first == NodeKind::Filter || b.first == NodeKind::Processor) ? 1 : 0);
                  return a_value > b_value;
              });

    // Stop nodes in the sorted order
    bool had_errors = false;
    for (auto& [kind, node] : sorted_nodes) {
        try {
            co_await node->stop();
        } catch (const std::exception& ex) {
            had_errors = true;
            onError.emit(MediaError(MediaErrorCode::UnknownError,
                                   "Failed to stop node '" + node->name() + "': " + ex.what()));
            // Continue trying to stop other nodes
        }
    }

    is_running_ = false;
    onStopped.emit();

    if (had_errors) {
        co_return co_await core::Task<void>::fromException(
            MediaError(MediaErrorCode::UnknownError,
                      "Failed to cleanly stop some pipeline nodes"));
    }
}

core::Task<void> Pipeline::reset() {
    // Stop if running
    if (is_running_) {
        co_await stop();
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // Reset all nodes
    for (auto& [name, node] : nodes_) {
        try {
            co_await node->reset();
        } catch (const std::exception& ex) {
            onError.emit(MediaError(MediaErrorCode::UnknownError,
                                   "Failed to reset node '" + name + "': " + ex.what()));
            // Continue trying to reset other nodes
        }
    }
}

bool Pipeline::isRunning() const {
    return is_running_;
}

} // namespace fmus::media::pipeline