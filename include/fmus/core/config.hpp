#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <memory>
#include <optional>
#include <filesystem>

#include <fmus/core/error.hpp>

namespace fmus::core {

// Forward declarations
class ConfigNode;
using ConfigNodePtr = std::shared_ptr<ConfigNode>;

// Tipe nilai yang didukung dalam konfigurasi
using ConfigValue = std::variant<
    std::nullptr_t,    // Untuk nilai null
    bool,              // Untuk nilai boolean
    int64_t,           // Untuk nilai integer
    double,            // Untuk nilai floating point
    std::string,       // Untuk nilai string
    std::vector<ConfigValue>,  // Untuk array
    ConfigNodePtr      // Untuk object/nested config
>;

// Class untuk node konfigurasi
class ConfigNode : public std::enable_shared_from_this<ConfigNode> {
public:
    using Map = std::unordered_map<std::string, ConfigValue>;

    // Constructors
    ConfigNode() = default;
    explicit ConfigNode(Map values) : values_(std::move(values)) {}

    // Factory methods
    static ConfigNodePtr create() {
        return std::make_shared<ConfigNode>();
    }

    static ConfigNodePtr create(Map values) {
        return std::make_shared<ConfigNode>(std::move(values));
    }

    // Akses nilai
    template<typename T>
    Result<T> get(const std::string& key) const {
        auto it = values_.find(key);
        if (it == values_.end()) {
            return {ErrorCode::ResourceNotFound, "Configuration key not found: " + key};
        }

        try {
            if constexpr (std::is_same_v<T, ConfigNodePtr>) {
                return std::get<ConfigNodePtr>(it->second);
            }
            else {
                return std::get<T>(it->second);
            }
        }
        catch (const std::bad_variant_access&) {
            return {ErrorCode::InvalidData, "Invalid type for key: " + key};
        }
    }

    // Set nilai
    template<typename T>
    void set(const std::string& key, T&& value) {
        values_[key] = std::forward<T>(value);
    }

    // Cek keberadaan key
    bool has(const std::string& key) const {
        return values_.find(key) != values_.end();
    }

    // Hapus key
    void remove(const std::string& key) {
        values_.erase(key);
    }

    // Akses nested config
    Result<ConfigNodePtr> getObject(const std::string& key) {
        return get<ConfigNodePtr>(key);
    }

    // Buat atau dapat nested config
    ConfigNodePtr getOrCreateObject(const std::string& key) {
        auto it = values_.find(key);
        if (it == values_.end()) {
            auto node = create();
            values_[key] = node;
            return node;
        }

        try {
            return std::get<ConfigNodePtr>(it->second);
        }
        catch (const std::bad_variant_access&) {
            auto node = create();
            values_[key] = node;
            return node;
        }
    }

    // Iterasi
    const Map& values() const { return values_; }
    Map& values() { return values_; }

private:
    Map values_;
};

// Class utama untuk konfigurasi
class Config {
public:
    static Config& instance() {
        static Config instance;
        return instance;
    }

    // Mencegah copy dan move
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&) = delete;
    Config& operator=(Config&&) = delete;

    // Load dari file
    Result<void> loadFromFile(const std::filesystem::path& path);

    // Save ke file
    Result<void> saveToFile(const std::filesystem::path& path) const;

    // Load dari string
    Result<void> loadFromString(std::string_view data);

    // Save ke string
    Result<std::string> saveToString() const;

    // Akses root node
    ConfigNodePtr root() { return root_; }
    const ConfigNodePtr root() const { return root_; }

    // Helper untuk akses langsung ke nilai
    template<typename T>
    Result<T> get(const std::string& key) const {
        return root_->get<T>(key);
    }

    template<typename T>
    void set(const std::string& key, T&& value) {
        root_->set(key, std::forward<T>(value));
    }

    bool has(const std::string& key) const {
        return root_->has(key);
    }

    void remove(const std::string& key) {
        root_->remove(key);
    }

    // Reset konfigurasi
    void clear() {
        root_ = ConfigNode::create();
    }

private:
    Config() : root_(ConfigNode::create()) {}
    ConfigNodePtr root_;
};

// Helper untuk akses global config
inline Config& config() {
    return Config::instance();
}

} // namespace fmus::core