// test_auth_chip.cpp — the --check-auth-chip decision/formatting logic is
// DJI-free and platform-independent: a fake bus covers found / NACK /
// missing node / permission denied / generic open error. The real
// LinuxAuthChipBus path is exercised by the ARM64 install test.
#include "test_framework.h"

#include <string>

#include "hw/auth_chip.h"

namespace {

class FakeBus final : public droppoint::hw::AuthChipBus {
public:
    OpenResult open_result = OpenResult::kOk;
    bool ack = false;
    bool opened = false;
    std::uint16_t probed_addr = 0;

    OpenResult Open() override {
        opened = true;
        return open_result;
    }
    bool ReadOneByte(std::uint16_t dev_address) override {
        probed_addr = dev_address;
        return ack;
    }
};

constexpr std::uint16_t kAddr = 0x2A;
const std::string kDev = "/dev/i2c-1";

}  // namespace

TEST_CASE("auth chip probe: device ACKs -> found, exit 0, names address+bus") {
    FakeBus bus;
    bus.ack = true;
    const auto st = droppoint::hw::ProbeAuthChip(bus, kAddr);
    REQUIRE(st == droppoint::hw::AuthChipStatus::kFound);
    REQUIRE(bus.opened);
    REQUIRE(bus.probed_addr == kAddr);
    REQUIRE(droppoint::hw::AuthChipExitCode(st) == 0);
    const std::string msg = droppoint::hw::FormatAuthChipResult(st, kDev, kAddr);
    REQUIRE(msg.find("0x2A") != std::string::npos);
    REQUIRE(msg.find(kDev) != std::string::npos);
    REQUIRE(msg.find("found") != std::string::npos);
}

TEST_CASE("auth chip probe: NACK -> not found, exit 1") {
    FakeBus bus;
    bus.ack = false;
    const auto st = droppoint::hw::ProbeAuthChip(bus, kAddr);
    REQUIRE(st == droppoint::hw::AuthChipStatus::kNotFound);
    REQUIRE(droppoint::hw::AuthChipExitCode(st) == 1);
    const std::string msg = droppoint::hw::FormatAuthChipResult(st, kDev, kAddr);
    REQUIRE(msg.find("no device answered") != std::string::npos);
    REQUIRE(msg.find("0x2A") != std::string::npos);
}

TEST_CASE("auth chip probe: device node missing -> exit 2 naming the node") {
    FakeBus bus;
    bus.open_result = droppoint::hw::AuthChipBus::OpenResult::kMissing;
    const auto st = droppoint::hw::ProbeAuthChip(bus, kAddr);
    REQUIRE(st == droppoint::hw::AuthChipStatus::kBusMissing);
    REQUIRE(droppoint::hw::AuthChipExitCode(st) == 2);
    const std::string msg = droppoint::hw::FormatAuthChipResult(st, kDev, kAddr);
    REQUIRE(msg.find("device node") != std::string::npos);
    REQUIRE(msg.find(kDev) != std::string::npos);
}

TEST_CASE("auth chip probe: permission denied -> exit 2 naming the i2c group") {
    FakeBus bus;
    bus.open_result = droppoint::hw::AuthChipBus::OpenResult::kPermissionDenied;
    const auto st = droppoint::hw::ProbeAuthChip(bus, kAddr);
    REQUIRE(st == droppoint::hw::AuthChipStatus::kBusPermission);
    REQUIRE(droppoint::hw::AuthChipExitCode(st) == 2);
    const std::string msg = droppoint::hw::FormatAuthChipResult(st, kDev, kAddr);
    REQUIRE(msg.find("permission denied") != std::string::npos);
    REQUIRE(msg.find("i2c") != std::string::npos);
    REQUIRE(msg.find(kDev) != std::string::npos);
}

TEST_CASE("auth chip probe: generic open error -> exit 2") {
    FakeBus bus;
    bus.open_result = droppoint::hw::AuthChipBus::OpenResult::kError;
    const auto st = droppoint::hw::ProbeAuthChip(bus, kAddr);
    REQUIRE(st == droppoint::hw::AuthChipStatus::kBusError);
    REQUIRE(droppoint::hw::AuthChipExitCode(st) == 2);
    REQUIRE(droppoint::hw::FormatAuthChipResult(st, kDev, kAddr)
                .find(kDev) != std::string::npos);
}
