#include <string>
#include <vector>

#include "app/mission_state.h"
#include "app/release_manager.h"
#include "test_framework.h"

using namespace droppoint;

namespace {

struct FakeRelease : hal::IRelease {
    hal::ReleaseState state = hal::ReleaseState::kSafe;
    int fire_count = 0;
    int safe_count = 0;
    void Fire() override {
        if (state != hal::ReleaseState::kSafe) return;
        state = hal::ReleaseState::kFired;
        ++fire_count;
    }
    void SetSafe() override {
        state = hal::ReleaseState::kSafe;
        ++safe_count;
    }
    hal::ReleaseState GetState() override { return state; }
    bool IsResponding() override { return true; }
};

struct FakeAudio : hal::IAudio {
    std::vector<std::string> events;
    void Speak(const std::string& phrase) override { events.push_back(phrase); }
    void PlayTone() override { events.push_back("tone"); }
    bool IsBusy() override { return false; }
};

app::ReleaseManagerInputs MakeInputs(std::uint64_t now_us = 0) {
    app::ReleaseManagerInputs in;
    in.now_us = now_us;
    in.time_to_release_s = 30.0;
    in.mechanism_delay_s = 0.15;
    in.target_confirmed_age_s = 10.0;
    in.gates_pass = true;
    in.run_in_mode = true;
    in.release_window_open = true;
    in.release_hold_s = 1.5;
    in.auto_release_allowed = true;
    return in;
}

core::Target MakeTarget() {
    core::Target t;
    t.valid = true;
    return t;
}

struct Rig {
    FakeRelease release;
    FakeAudio audio;
    app::MissionState mission;
    app::ReleaseManager rm{release, audio, mission};

    void DriveToArmed() {
        mission.OnTargetDesignated(MakeTarget());
        mission.OnConfirmTarget(0);
        REQUIRE(mission.TryArm(0, true, nullptr));
    }
    void DriveToCountdown() {
        DriveToArmed();
        auto in = MakeInputs();
        in.time_to_release_s = 4.0;
        rm.Update(in);
        REQUIRE(mission.Id() == app::MissionStateId::kCountdown);
    }
};

}  // namespace

TEST_CASE("ReleaseManager: constructor safes a mechanism left fired") {
    FakeRelease release;
    release.state = hal::ReleaseState::kFired;  // simulated crash/restart mid-fire
    FakeAudio audio;
    app::MissionState mission;
    app::ReleaseManager rm(release, audio, mission);
    REQUIRE(release.state == hal::ReleaseState::kSafe);
    REQUIRE(release.safe_count >= 1);
}

TEST_CASE("ReleaseManager: SetSafe every cycle while not armed") {
    Rig rig;
    auto in = MakeInputs();
    for (std::uint64_t t = 0; t < 500'000; t += 100'000) {
        in.now_us = t;
        rig.rm.Update(in);
        REQUIRE(rig.release.state == hal::ReleaseState::kSafe);
    }
    REQUIRE(rig.release.fire_count == 0);
}

TEST_CASE("ReleaseManager: manual press fires exactly once in ARMED") {
    Rig rig;
    rig.DriveToArmed();
    auto in = MakeInputs(1'000'000);
    in.manual_release_pressed = true;
    rig.rm.Update(in);
    REQUIRE(rig.release.fire_count == 1);
    REQUIRE(rig.rm.JustFired());
    REQUIRE(rig.mission.Id() == app::MissionStateId::kReleased);
    REQUIRE(rig.audio.events.back() == "drop");
}

TEST_CASE("ReleaseManager: manual press fires exactly once in COUNTDOWN") {
    Rig rig;
    rig.DriveToCountdown();
    auto in = MakeInputs(2'000'000);
    in.time_to_release_s = 3.0;
    in.manual_release_pressed = true;
    rig.rm.Update(in);
    REQUIRE(rig.release.fire_count == 1);
}

TEST_CASE("ReleaseManager: a second press does not fire again") {
    Rig rig;
    rig.DriveToArmed();
    auto in = MakeInputs(1'000'000);
    in.manual_release_pressed = true;
    rig.rm.Update(in);
    REQUIRE(rig.release.fire_count == 1);
    // Press again during the post-fire hold and after return to IDLE.
    in.now_us = 1'200'000;
    rig.rm.Update(in);
    in.now_us = 3'000'000;
    rig.rm.Update(in);
    REQUIRE(rig.release.fire_count == 1);
}

TEST_CASE("ReleaseManager: fired state held for release_hold_s then safe + IDLE") {
    Rig rig;
    rig.DriveToArmed();
    auto in = MakeInputs(1'000'000);
    in.manual_release_pressed = true;
    rig.rm.Update(in);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kReleased);

    const int safes_before = rig.release.safe_count;
    in.manual_release_pressed = false;
    in.now_us = 1'500'000;  // 0.5 s into the 1.5 s hold
    rig.rm.Update(in);
    REQUIRE(rig.release.state == hal::ReleaseState::kFired);  // still held open
    REQUIRE(rig.release.safe_count == safes_before);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kReleased);

    in.now_us = 2'600'000;  // 1.6 s: hold elapsed
    rig.rm.Update(in);
    REQUIRE(rig.release.state == hal::ReleaseState::kSafe);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kIdle);
}

TEST_CASE("ReleaseManager: auto fires at mechanism delay only inside the corridor") {
    Rig rig;
    rig.mission.TrySetMode(app::ReleaseMode::kAuto, true, nullptr);
    rig.DriveToCountdown();

    auto in = MakeInputs(2'000'000);
    in.time_to_release_s = 0.10;  // <= mechanism_delay_s 0.15
    in.release_window_open = true;
    rig.rm.Update(in);
    REQUIRE(rig.release.fire_count == 1);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kReleased);
}

TEST_CASE("ReleaseManager: auto aborts when the corridor is closed at release time") {
    Rig rig;
    rig.mission.TrySetMode(app::ReleaseMode::kAuto, true, nullptr);
    rig.DriveToCountdown();

    auto in = MakeInputs(2'000'000);
    in.time_to_release_s = 0.10;
    in.release_window_open = false;
    rig.rm.Update(in);
    REQUIRE(rig.release.fire_count == 0);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kConfirmed);
    REQUIRE(!rig.audio.events.empty() && rig.audio.events.back() == "abort");
}

TEST_CASE("ReleaseManager: auto never fires in hover") {
    Rig rig;
    rig.mission.TrySetMode(app::ReleaseMode::kAuto, true, nullptr);
    rig.DriveToArmed();
    auto in = MakeInputs(1'000'000);
    in.run_in_mode = false;      // hover: no run-in, no countdown
    in.time_to_release_s = 0.0;
    for (std::uint64_t t = 0; t < 1'000'000; t += 100'000) {
        in.now_us = 1'000'000 + t;
        rig.rm.Update(in);
        REQUIRE(rig.release.fire_count == 0);
        REQUIRE(rig.mission.Id() == app::MissionStateId::kArmed);
    }
}

TEST_CASE("ReleaseManager: auto aborts on stale target confirmation") {
    Rig rig;
    rig.mission.TrySetMode(app::ReleaseMode::kAuto, true, nullptr);
    rig.DriveToArmed();
    auto in = MakeInputs(1'000'000);
    in.target_confirmed_age_s = 121.0;
    rig.rm.Update(in);
    REQUIRE(rig.release.fire_count == 0);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kConfirmed);
}

TEST_CASE("ReleaseManager: auto aborts when release is operator-actuated") {
    Rig rig;
    rig.mission.TrySetMode(app::ReleaseMode::kAuto, true, nullptr);
    rig.DriveToArmed();
    auto in = MakeInputs(1'000'000);
    in.auto_release_allowed = false;
    rig.rm.Update(in);
    REQUIRE(rig.release.fire_count == 0);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kConfirmed);
}

TEST_CASE("ReleaseManager: gate failure aborts while ARMED and while COUNTDOWN") {
    {
        Rig rig;
        rig.DriveToArmed();
        auto in = MakeInputs();
        in.gates_pass = false;
        in.trip_reason = "HOLD - TEST GATE";
        rig.rm.Update(in);
        REQUIRE(rig.release.fire_count == 0);
        REQUIRE(rig.release.state == hal::ReleaseState::kSafe);
        REQUIRE(rig.mission.Id() == app::MissionStateId::kConfirmed);
        REQUIRE(!rig.audio.events.empty() && rig.audio.events.back() == "abort");
    }
    {
        Rig rig;
        rig.DriveToCountdown();
        auto in = MakeInputs();
        in.time_to_release_s = 3.0;
        in.gates_pass = false;
        rig.rm.Update(in);
        REQUIRE(rig.release.fire_count == 0);
        REQUIRE(rig.mission.Id() == app::MissionStateId::kConfirmed);
    }
}

TEST_CASE("ReleaseManager: no-drop polygon violation aborts") {
    Rig rig;
    rig.DriveToArmed();
    auto in = MakeInputs();
    in.no_drop_polygon_violation = true;
    rig.rm.Update(in);
    REQUIRE(rig.release.fire_count == 0);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kConfirmed);
}

TEST_CASE("ReleaseManager: hardware abort works from ARMED") {
    Rig rig;
    rig.DriveToArmed();
    auto in = MakeInputs();
    in.hardware_abort_pressed = true;
    rig.rm.Update(in);
    REQUIRE(rig.release.state == hal::ReleaseState::kSafe);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kConfirmed);
    REQUIRE(!rig.audio.events.empty() && rig.audio.events.back() == "abort");
}

TEST_CASE("ReleaseManager: hardware abort works mid-countdown") {
    Rig rig;
    rig.DriveToCountdown();
    auto in = MakeInputs();
    in.time_to_release_s = 2.0;
    in.hardware_abort_pressed = true;
    rig.rm.Update(in);
    REQUIRE(rig.release.state == hal::ReleaseState::kSafe);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kConfirmed);
}

TEST_CASE("ReleaseManager: hardware abort forces safe during the RELEASED hold") {
    Rig rig;
    rig.DriveToArmed();
    auto in = MakeInputs(1'000'000);
    in.manual_release_pressed = true;
    rig.rm.Update(in);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kReleased);
    REQUIRE(rig.release.state == hal::ReleaseState::kFired);

    in.manual_release_pressed = false;
    in.hardware_abort_pressed = true;
    in.now_us = 1'300'000;
    rig.rm.Update(in);
    REQUIRE(rig.release.state == hal::ReleaseState::kSafe);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kIdle);
}

TEST_CASE("ReleaseManager: operator disarm aborts from ARMED") {
    Rig rig;
    rig.DriveToArmed();
    auto in = MakeInputs();
    in.disarm_pressed = true;
    rig.rm.Update(in);
    REQUIRE(rig.release.fire_count == 0);
    REQUIRE(rig.release.state == hal::ReleaseState::kSafe);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kConfirmed);
    REQUIRE(!rig.audio.events.empty() && rig.audio.events.back() == "disarmed");
}

TEST_CASE("ReleaseManager: missed manual pass returns to ARMED without firing") {
    Rig rig;
    rig.DriveToCountdown();
    auto in = MakeInputs(2'000'000);
    in.time_to_release_s = -2.5;  // flew past the window unpressed
    rig.rm.Update(in);
    REQUIRE(rig.release.fire_count == 0);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kArmed);
    REQUIRE(!rig.audio.events.empty() && rig.audio.events.back() == "missed");
}

TEST_CASE("ReleaseManager: countdown cue order tone/three/two/one/drop") {
    Rig rig;
    rig.DriveToArmed();

    const double steps[] = {4.9, 2.9, 1.9, 0.9};
    for (int i = 0; i < 4; ++i) {
        auto in = MakeInputs(static_cast<std::uint64_t>((i + 1) * 100'000));
        in.time_to_release_s = steps[i];
        rig.rm.Update(in);
    }
    auto in = MakeInputs(500'000);
    in.time_to_release_s = 0.5;
    in.manual_release_pressed = true;
    rig.rm.Update(in);

    const std::vector<std::string> expected = {"tone", "three", "two", "one", "drop"};
    REQUIRE(rig.audio.events == expected);
}

TEST_CASE("ReleaseManager: countdown reverts to ARMED if the window moves away") {
    Rig rig;
    rig.DriveToCountdown();
    auto in = MakeInputs(2'000'000);
    in.time_to_release_s = 8.0;  // jumped back out of the countdown window
    rig.rm.Update(in);
    REQUIRE(rig.mission.Id() == app::MissionStateId::kArmed);
}

TEST_CASE("ReleaseManager: auto announces armed once per arm") {
    Rig rig;
    rig.mission.TrySetMode(app::ReleaseMode::kAuto, true, nullptr);
    rig.DriveToArmed();
    auto in = MakeInputs();
    in.time_to_release_s = 30.0;
    rig.rm.Update(in);
    rig.rm.Update(in);
    int announces = 0;
    for (const auto& e : rig.audio.events) {
        if (e == "auto armed") ++announces;
    }
    REQUIRE(announces == 1);
}
