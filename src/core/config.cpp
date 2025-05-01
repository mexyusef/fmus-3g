#include <fmus/core/config.hpp>
#include <nlohmann/json.hpp>
#include <fstream>

namespace fmus::core {

namespace {

// Konversi dari ConfigValue ke JSON
nlohmann::json configValueToJson(const ConfigValue& value) {
    return std::visit([](const auto& v) -> nlohmann::json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return nullptr;
        }
        else if constexpr (std::is_same_v<T, ConfigNodePtr>) {
            nlohmann::json obj;
            for (const auto& [key, val] : v->values()) {
                obj[key] = configValueToJson(val);
            }
            return obj;
        }
        else if constexpr (std::is_same_v<T, std::vector<ConfigValue>>) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& item : v) {
                arr.push_back(configValueToJson(item));
            }
            return arr;
        }
        else {
            return v;
        }
    }, value);
}

// Konversi dari JSON ke ConfigValue
ConfigValue jsonToConfigValue(const nlohmann::json& json) {
    if (json.is_null()) {
        return nullptr;
    }
    else if (json.is_boolean()) {
        return json.get<bool>();
    }
    else if (json.is_number_integer()) {
        return json.get<int64_t>();
    }
    else if (json.is_number_float()) {
        return json.get<double>();
    }
    else if (json.is_string()) {
        return json.get<std::string>();
    }
    else if (json.is_array()) {
        std::vector<ConfigValue> arr;
        arr.reserve(json.size());
        for (const auto& item : json) {
            arr.push_back(jsonToConfigValue(item));
        }
        return arr;
    }
    else if (json.is_object()) {
        ConfigNode::Map values;
        for (auto it = json.begin(); it != json.end(); ++it) {
            values[it.key()] = jsonToConfigValue(it.value());
        }
        return ConfigNode::create(std::move(values));
    }

    throw_error(ErrorCode::InvalidData, "Invalid JSON type");
}

} // namespace

Result<void> Config::loadFromFile(const std::filesystem::path& path) {
    try {
        std::ifstream file(path);
        if (!file) {
            return {ErrorCode::FileNotFound, "Failed to open config file: " + path.string()};
        }

        nlohmann::json json;
        file >> json;

        if (!json.is_object()) {
            return {ErrorCode::InvalidData, "Root configuration must be an object"};
        }

        ConfigNode::Map values;
        for (auto it = json.begin(); it != json.end(); ++it) {
            values[it.key()] = jsonToConfigValue(it.value());
        }

        root_ = ConfigNode::create(std::move(values));
        return {};
    }
    catch (const std::exception& e) {
        return {ErrorCode::InvalidData, "Failed to parse config file: " + std::string(e.what())};
    }
}

Result<void> Config::saveToFile(const std::filesystem::path& path) const {
    try {
        nlohmann::json json;
        for (const auto& [key, value] : root_->values()) {
            json[key] = configValueToJson(value);
        }

        std::ofstream file(path);
        if (!file) {
            return {ErrorCode::FileAccessDenied, "Failed to create config file: " + path.string()};
        }

        file << json.dump(2);  // Indent with 2 spaces
        return {};
    }
    catch (const std::exception& e) {
        return {ErrorCode::InvalidData, "Failed to save config file: " + std::string(e.what())};
    }
}

Result<void> Config::loadFromString(std::string_view data) {
    try {
        auto json = nlohmann::json::parse(data);

        if (!json.is_object()) {
            return {ErrorCode::InvalidData, "Root configuration must be an object"};
        }

        ConfigNode::Map values;
        for (auto it = json.begin(); it != json.end(); ++it) {
            values[it.key()] = jsonToConfigValue(it.value());
        }

        root_ = ConfigNode::create(std::move(values));
        return {};
    }
    catch (const std::exception& e) {
        return {ErrorCode::InvalidData, "Failed to parse config string: " + std::string(e.what())};
    }
}

Result<std::string> Config::saveToString() const {
    try {
        nlohmann::json json;
        for (const auto& [key, value] : root_->values()) {
            json[key] = configValueToJson(value);
        }

        return json.dump(2);  // Indent with 2 spaces
    }
    catch (const std::exception& e) {
        return {ErrorCode::InvalidData, "Failed to serialize config: " + std::string(e.what())};
    }
}

} // namespace fmus::core