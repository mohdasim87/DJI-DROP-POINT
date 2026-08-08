// psdk/app_registration.h
//
// Status: unverified — requires hardware and a registered DJI developer
// application.
//
// Wires config/credentials.yaml into the T_DjiUserInfo struct
// (docs/PSDK-RESEARCH.md section 2) and calls DjiCore_Init. Every field
// here is a credential slot: this file must never contain a real appKey,
// appLicense, or developerAccount value (hard constraint 3).
#pragma once

#include <string>

namespace droppoint::psdk {

struct Credentials {
    std::string app_name;
    std::string app_id;
    std::string app_key;
    std::string app_license;
    std::string developer_account;
    std::string baud_rate = "921600";
};

// Reads config/credentials.yaml (not config/, deliberately kept out of the
// schema-validated config/ directory so it is easy to exclude from backups
// / version control / support bundles). Throws if the file is missing or
// any field is empty — there is no default credential, by design.
Credentials LoadCredentials(const std::string& path);

// TODO(hardware): call DjiCore_Init(&user_info) with a T_DjiUserInfo built
// from Credentials, then DjiCore_ApplicationStart(). Requires linking
// libpayloadsdk.a and the platform HAL registration in platform_linux.cpp
// to have run first (DjiCore_Init fails otherwise per the PSDK samples).

}  // namespace droppoint::psdk
