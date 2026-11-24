// psdk/app_registration.cpp
//
// Status: unverified — credential loader only; DjiCore_Init wiring requires
// the vendored Payload-SDK and real hardware.
//
// Reads config/credentials.yaml and returns a strongly-typed Credentials
// struct. No real credential value is hard-coded in this file.
#include "app_registration.h"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <stdexcept>
#include <string>

namespace droppoint::psdk {

namespace {

class CredentialError : public std::runtime_error {
public:
    explicit CredentialError(const std::string& what) : std::runtime_error(what) {}
};

std::string RequireString(const YAML::Node& node, const std::string& field) {
    if (!node[field]) {
        throw CredentialError("credentials.yaml: missing required field '" + field + "'");
    }
    try {
        const std::string value = node[field].as<std::string>();
        if (value.empty()) {
            throw CredentialError("credentials.yaml: field '" + field + "' is empty");
        }
        return value;
    } catch (const YAML::Exception&) {
        throw CredentialError("credentials.yaml: field '" + field + "' must be a string");
    }
}

}  // namespace

Credentials LoadCredentials(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        throw CredentialError("credentials file not found: " + path);
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        throw CredentialError(std::string("failed to parse credentials.yaml: ") + e.what());
    }

    Credentials creds;
    creds.app_name = RequireString(root, "app_name");
    creds.app_id = RequireString(root, "app_id");
    creds.app_key = RequireString(root, "app_key");
    creds.app_license = RequireString(root, "app_license");
    creds.developer_account = RequireString(root, "developer_account");
    creds.baud_rate = "921600";
    if (root["baud_rate"]) {
        creds.baud_rate = root["baud_rate"].as<std::string>();
    }
    return creds;
}

bool IsExampleCredentials(const Credentials& creds) {
    return creds.app_name == "your_app_name" || creds.app_id == "your_app_id" ||
           creds.app_key == "your_app_key" || creds.app_license == "your_app_license" ||
           creds.developer_account == "your_developer_account_email";
}

}  // namespace droppoint::psdk
