#include <fmus/webrtc/webrtc.hpp>
#include <fmus/core/logger.hpp>
#include <regex>
#include <string>

namespace fmus::webrtc {

/**
 * Validate an ICE candidate string according to RFC 5245
 */
bool validateIceCandidate(const std::string& candidate) {
    // Validator regex pattern for ICE candidates
    // Basic RFC 5245 pattern validation
    static const std::regex ice_pattern(
        R"(candidate:((\d+)\s+)  # foundation
           (\d+)\s+              # component ID
           ([a-zA-Z0-9]+)\s+     # transport
           (\d+)\s+              # priority
           ([0-9a-fA-F.:]+)\s+   # IP address (IPv4 or IPv6)
           (\d+)\s+              # port
           typ\s+([a-z]+)        # candidate type
           (\s+.+)?              # optional extensions
         )",
        std::regex::extended
    );

    std::smatch match;
    if (!std::regex_search(candidate, match, ice_pattern)) {
        core::Logger::warn("Invalid ICE candidate format: {}", candidate);
        return false;
    }

    return true;
}

/**
 * Parse ICE candidate components from a candidate string
 */
std::optional<std::map<std::string, std::string>> parseIceCandidateComponents(
    const std::string& candidate) {

    // Coba parse komponen-komponen candidate
    static const std::regex component_pattern(
        R"(candidate:(\S+)\s+  # foundation
           (\d+)\s+            # component ID
           (\S+)\s+            # transport
           (\S+)\s+            # priority
           (\S+)\s+            # IP address
           (\d+)\s+            # port
           typ\s+(\S+)         # candidate type
           (.*)                # extensions
         )",
        std::regex::extended
    );

    std::smatch match;
    if (!std::regex_search(candidate, match, component_pattern)) {
        core::Logger::warn("Failed to parse ICE candidate components: {}", candidate);
        return std::nullopt;
    }

    std::map<std::string, std::string> components;
    components["foundation"] = match[1];
    components["component"] = match[2];
    components["transport"] = match[3];
    components["priority"] = match[4];
    components["ip"] = match[5];
    components["port"] = match[6];
    components["type"] = match[7];

    // Parse extended attributes jika ada
    std::string extensions = match[8];
    if (!extensions.empty()) {
        std::regex ext_pattern(R"((\S+)(?:\s+(\S+))?)");

        auto ext_begin = std::sregex_iterator(extensions.begin(), extensions.end(), ext_pattern);
        auto ext_end = std::sregex_iterator();

        std::string current_attr;
        for (std::sregex_iterator i = ext_begin; i != ext_end; ++i) {
            std::smatch ext_match = *i;
            std::string key = ext_match[1];
            std::string value = ext_match[2];

            if (key == "raddr" || key == "rport" || key == "tcptype" || key == "generation") {
                current_attr = key;
                if (!value.empty()) {
                    components[current_attr] = value;
                }
            } else if (!current_attr.empty() && value.empty()) {
                components[current_attr] = key;
                current_attr.clear();
            }
        }
    }

    return components;
}

/**
 * Generate a proper ICE candidate string from components
 */
std::optional<std::string> generateIceCandidateString(
    const std::map<std::string, std::string>& components) {

    // Memeriksa apakah semua komponen yang diperlukan ada
    static const std::vector<std::string> required = {
        "foundation", "component", "transport", "priority", "ip", "port", "type"
    };

    for (const auto& field : required) {
        if (components.find(field) == components.end()) {
            core::Logger::warn("Missing required ICE component: {}", field);
            return std::nullopt;
        }
    }

    // Membuat string candidate
    std::stringstream ss;
    ss << "candidate:" << components.at("foundation") << " "
       << components.at("component") << " "
       << components.at("transport") << " "
       << components.at("priority") << " "
       << components.at("ip") << " "
       << components.at("port") << " "
       << "typ " << components.at("type");

    // Menambahkan extensions jika ada
    static const std::vector<std::string> extensions = {
        "raddr", "rport", "tcptype", "generation"
    };

    for (const auto& ext : extensions) {
        if (components.find(ext) != components.end()) {
            ss << " " << ext << " " << components.at(ext);
        }
    }

    return ss.str();
}

} // namespace fmus::webrtc