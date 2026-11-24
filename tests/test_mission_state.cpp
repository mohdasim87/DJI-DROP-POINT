#include "app/mission_state.h"
#include "test_framework.h"

using namespace droppoint;

namespace {
core::Target MakeTarget(std::uint64_t designated_us = 0) {
    core::Target t;
    t.valid = true;
    t.designated_at_us = designated_us;
    return t;
}
}  // namespace

TEST_CASE("MissionState: full IDLE -> RELEASED -> IDLE path") {
    app::MissionState m;
    REQUIRE(m.Id() == app::MissionStateId::kIdle);

    m.OnTargetDesignated(MakeTarget(1'000'000));
    REQUIRE(m.Id() == app::MissionStateId::kTargetLatched);

    m.OnConfirmTarget(2'000'000);
    REQUIRE(m.Id() == app::MissionStateId::kConfirmed);
    REQUIRE(m.CurrentTarget().confirmed);

    std::string reason;
    REQUIRE(m.TryArm(3'000'000, true, &reason));
    REQUIRE(m.Id() == app::MissionStateId::kArmed);

    m.EnterCountdown();
    REQUIRE(m.Id() == app::MissionStateId::kCountdown);

    m.OnReleased(5'000'000);
    REQUIRE(m.Id() == app::MissionStateId::kReleased);

    m.ReturnToIdleAfterRelease();
    REQUIRE(m.Id() == app::MissionStateId::kIdle);
    REQUIRE(!m.CurrentTarget().valid);
}

TEST_CASE("MissionState: cannot arm an unconfirmed target") {
    app::MissionState m;
    m.OnTargetDesignated(MakeTarget());
    std::string reason;
    REQUIRE(!m.TryArm(1'000'000, true, &reason));
    REQUIRE(m.Id() == app::MissionStateId::kTargetLatched);
}

TEST_CASE("MissionState: cannot arm with gates failing") {
    app::MissionState m;
    m.OnTargetDesignated(MakeTarget());
    m.OnConfirmTarget(1'000'000);
    std::string reason;
    REQUIRE(!m.TryArm(2'000'000, false, &reason));
    REQUIRE(!reason.empty());
    REQUIRE(m.Id() == app::MissionStateId::kConfirmed);
}

TEST_CASE("MissionState: re-designation while armed aborts") {
    app::MissionState m;
    m.OnTargetDesignated(MakeTarget());
    m.OnConfirmTarget(1'000'000);
    REQUIRE(m.TryArm(2'000'000, true, nullptr));
    REQUIRE(m.Id() == app::MissionStateId::kArmed);

    m.OnTargetDesignated(MakeTarget(3'000'000));
    REQUIRE(m.Id() == app::MissionStateId::kTargetLatched);
    REQUIRE(!m.LastAbortReason().empty());
}

TEST_CASE("MissionState: arm expires after 60 s idle") {
    app::MissionState m;
    m.OnTargetDesignated(MakeTarget());
    m.OnConfirmTarget(1'000'000);
    REQUIRE(m.TryArm(2'000'000, true, nullptr));

    m.Tick(30'000'000);
    REQUIRE(m.Id() == app::MissionStateId::kArmed);

    m.Tick(2'000'000 + 61'000'000);
    REQUIRE(m.Id() == app::MissionStateId::kConfirmed);
    REQUIRE(!m.LastAbortReason().empty());
}

TEST_CASE("MissionState: gate trips no longer abort inside Tick") {
    // Gate-failure aborts are ReleaseManager's job now; Tick only expires
    // the arm. Verified by Tick having no gate argument.
    app::MissionState m;
    m.OnTargetDesignated(MakeTarget());
    m.OnConfirmTarget(1'000'000);
    REQUIRE(m.TryArm(2'000'000, true, nullptr));
    m.Tick(3'000'000);
    REQUIRE(m.Id() == app::MissionStateId::kArmed);
}

TEST_CASE("MissionState: TrySetMode rejected while armed") {
    app::MissionState m;
    m.OnTargetDesignated(MakeTarget());
    m.OnConfirmTarget(1'000'000);
    std::string reason;
    REQUIRE(m.TrySetMode(app::ReleaseMode::kAuto, true, &reason));
    REQUIRE(m.Mode() == app::ReleaseMode::kAuto);

    REQUIRE(m.TryArm(2'000'000, true, nullptr));
    REQUIRE(!m.TrySetMode(app::ReleaseMode::kManual, true, &reason));
    REQUIRE(!reason.empty());
    REQUIRE(m.Mode() == app::ReleaseMode::kAuto);
}

TEST_CASE("MissionState: AUTO rejected when not allowed (operator-actuated)") {
    app::MissionState m;
    std::string reason;
    REQUIRE(!m.TrySetMode(app::ReleaseMode::kAuto, false, &reason));
    REQUIRE(reason.find("OPERATOR-ACTUATED") != std::string::npos);
    REQUIRE(m.Mode() == app::ReleaseMode::kManual);

    REQUIRE(m.TrySetMode(app::ReleaseMode::kAuto, true, nullptr));
    REQUIRE(m.Mode() == app::ReleaseMode::kAuto);
}

TEST_CASE("MissionState: ReturnToArmed moves COUNTDOWN back to ARMED") {
    app::MissionState m;
    m.OnTargetDesignated(MakeTarget());
    m.OnConfirmTarget(1'000'000);
    REQUIRE(m.TryArm(2'000'000, true, nullptr));
    m.EnterCountdown();
    REQUIRE(m.Id() == app::MissionStateId::kCountdown);

    m.ReturnToArmed(10'000'000);
    REQUIRE(m.Id() == app::MissionStateId::kArmed);

    // Arm-idle timer was reset by ReturnToArmed: another 59 s is fine.
    m.Tick(10'000'000 + 59'000'000);
    REQUIRE(m.Id() == app::MissionStateId::kArmed);
}
