// tests/test_psdk_credentials.cpp
//
// Validates that the PSDK credential loader parses credentials.yaml correctly.
// This test does NOT use the real config/credentials.yaml; it uses a fixture
// in tests/data/credentials_test.yaml so no secrets are present in tests.
#include "test_framework.h"

#include "psdk/app_registration.h"

TEST_CASE("psdk_credentials_loads_all_fields") {
    const auto creds = droppoint::psdk::LoadCredentials(DROPPOINT_TEST_DATA_DIR "/credentials_test.yaml");
    REQUIRE(creds.app_name == "Test App");
    REQUIRE(creds.app_id == "123456");
    REQUIRE(creds.app_key == "test_app_key_value");
    REQUIRE(creds.app_license == "test_license_value");
    REQUIRE(creds.developer_account == "test@example.com");
    REQUIRE(creds.baud_rate == "460800");
}

TEST_CASE("psdk_credentials_uses_default_baud_rate") {
    const auto creds = droppoint::psdk::LoadCredentials(DROPPOINT_TEST_DATA_DIR "/credentials_test.yaml");
    REQUIRE(creds.baud_rate == "460800");
}

TEST_CASE("psdk_credentials_rejects_missing_file") {
    bool threw = false;
    try {
        droppoint::psdk::LoadCredentials(DROPPOINT_TEST_DATA_DIR "/nonexistent_credentials.yaml");
    } catch (const std::exception&) {
        threw = true;
    }
    REQUIRE(threw);
}
