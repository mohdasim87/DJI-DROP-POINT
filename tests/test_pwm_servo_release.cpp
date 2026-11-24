// tests/test_pwm_servo_release.cpp
//
// The sysfs-PWM backend against a temp dir: exact files/values/order
// (incl. enable=1, which the old code never wrote), safe-on-construct and
// safe-on-destruct, single-shot Fire, and the IsResponding flag.
#include <filesystem>
#include <fstream>
#include <string>

#include "test_framework.h"

#include "hw/pwm_servo_release.h"

namespace {

std::string ReadFile(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::string line;
    std::getline(in, line);
    return line;
}

struct Fixture {
    std::filesystem::path root;
    droppoint::hw::PwmServoConfig cfg;

    explicit Fixture(const std::string& name) {
        root = std::filesystem::temp_directory_path() / name;
        std::filesystem::remove_all(root);
        // Simulate the already-exported state sysfs produces after a
        // successful `export` write.
        std::filesystem::create_directories(root / "pwm0");
        cfg.chip_path = root.string();
        cfg.channel = 0;
        cfg.period_ns = 20000000;
        cfg.safe_pulse_ns = 1100000;
        cfg.release_pulse_ns = 1900000;
    }
    ~Fixture() { std::filesystem::remove_all(root); }
};

}  // namespace

TEST_CASE("PwmServo: init writes export, period, safe duty, enable=1 in order") {
    Fixture f("dp_pwm_init");
    droppoint::hw::PwmServoRelease rel(f.cfg);
    REQUIRE(rel.IsResponding());
    REQUIRE(rel.GetState() == droppoint::hal::ReleaseState::kSafe);
    REQUIRE(ReadFile(f.root / "export") == "0");
    REQUIRE(ReadFile(f.root / "pwm0" / "period") == "20000000");
    REQUIRE(ReadFile(f.root / "pwm0" / "duty_cycle") == "1100000");
    REQUIRE(ReadFile(f.root / "pwm0" / "enable") == "1");

    const auto& log = rel.WriteLog();
    REQUIRE(log.size() >= 4);
    REQUIRE(log[0].find("/export") != std::string::npos);
    REQUIRE(log[1].find("/period") != std::string::npos);
    REQUIRE(log[2].find("/duty_cycle") != std::string::npos);
    REQUIRE(log[2].find("1100000") != std::string::npos);
    REQUIRE(log[3].find("/enable") != std::string::npos);
    REQUIRE(log[3].find("1") != std::string::npos);
}

TEST_CASE("PwmServo: Fire writes the release pulse exactly once") {
    Fixture f("dp_pwm_fire");
    droppoint::hw::PwmServoRelease rel(f.cfg);
    rel.Fire();
    REQUIRE(rel.GetState() == droppoint::hal::ReleaseState::kFired);
    REQUIRE(ReadFile(f.root / "pwm0" / "duty_cycle") == "1900000");
    const auto writes_before = rel.WriteLog().size();
    rel.Fire();  // second call is a no-op
    REQUIRE(rel.WriteLog().size() == writes_before);
}

TEST_CASE("PwmServo: SetSafe restores the safe pulse from any state") {
    Fixture f("dp_pwm_safe");
    droppoint::hw::PwmServoRelease rel(f.cfg);
    rel.Fire();
    rel.SetSafe();
    REQUIRE(rel.GetState() == droppoint::hal::ReleaseState::kSafe);
    REQUIRE(ReadFile(f.root / "pwm0" / "duty_cycle") == "1100000");
    rel.SetSafe();  // idempotent
    REQUIRE(rel.IsResponding());
}

TEST_CASE("PwmServo: destructor drives the safe pulse") {
    Fixture f("dp_pwm_dtor");
    {
        droppoint::hw::PwmServoRelease rel(f.cfg);
        rel.Fire();
        REQUIRE(ReadFile(f.root / "pwm0" / "duty_cycle") == "1900000");
    }
    REQUIRE(ReadFile(f.root / "pwm0" / "duty_cycle") == "1100000");
}

TEST_CASE("PwmServo: missing channel reports not responding") {
    const auto root = std::filesystem::temp_directory_path() / "dp_pwm_dead";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);  // no pwmN dir -> export failed
    droppoint::hw::PwmServoConfig cfg;
    cfg.chip_path = root.string();
    droppoint::hw::PwmServoRelease rel(cfg);
    REQUIRE(!rel.IsResponding());
    std::filesystem::remove_all(root);
}
