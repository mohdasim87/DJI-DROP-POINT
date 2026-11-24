// tests/test_framework.h
//
// Minimal header-only test harness. DropPoint avoids pulling in a network-
// fetched test framework so the whole repository (including `tests/`)
// builds fully offline, matching the "no internet on the aircraft" spirit
// of the rest of the project and keeping the dev build free of internet
// dependencies too.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace droppoint::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

class Failure {
public:
    explicit Failure(std::string msg) : message_(std::move(msg)) {}
    const std::string& what() const { return message_; }
private:
    std::string message_;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        Registry().push_back(TestCase{name, std::move(fn)});
    }
};

inline int RunAll() {
    int failed = 0;
    for (const auto& tc : Registry()) {
        try {
            tc.fn();
            std::printf("[PASS] %s\n", tc.name.c_str());
        } catch (const Failure& f) {
            std::printf("[FAIL] %s: %s\n", tc.name.c_str(), f.what().c_str());
            ++failed;
        } catch (const std::exception& e) {
            std::printf("[FAIL] %s: uncaught exception: %s\n", tc.name.c_str(), e.what());
            ++failed;
        }
    }
    std::printf("\n%zu tests, %d failed\n", Registry().size(), failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace droppoint::test

#define DP_CONCAT_INNER(a, b) a##b
#define DP_CONCAT(a, b) DP_CONCAT_INNER(a, b)

// `static` gives each test function/registrar internal linkage, so
// identical __COUNTER__ values in different translation units (it resets
// per TU) never collide at link time; __COUNTER__ is passed as a single
// macro argument so it is only evaluated once and reused consistently for
// the declaration, registration, and definition below.
#define TEST_CASE(name) DP_TEST_CASE_IMPL(name, __COUNTER__)
#define DP_TEST_CASE_IMPL(name, id) \
    static void DP_CONCAT(dp_test_fn_, id)(); \
    static droppoint::test::Registrar DP_CONCAT(dp_test_reg_, id)(name, DP_CONCAT(dp_test_fn_, id)); \
    static void DP_CONCAT(dp_test_fn_, id)()

#define REQUIRE(cond) \
    do { \
        if (!(cond)) { \
            throw droppoint::test::Failure(std::string("REQUIRE failed: ") + #cond + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

#define REQUIRE_NEAR(a, b, tol) \
    do { \
        const double av = (a); \
        const double bv = (b); \
        if (std::fabs(av - bv) > (tol)) { \
            throw droppoint::test::Failure(std::string("REQUIRE_NEAR failed: ") + #a + " (" + std::to_string(av) + \
                ") vs " + #b + " (" + std::to_string(bv) + "), tol=" + std::to_string(tol) + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)

// Relative tolerance check: |a-b| <= rel_tol * max(|a|,|b|, epsilon)
#define REQUIRE_REL_NEAR(a, b, rel_tol) \
    do { \
        const double av = (a); \
        const double bv = (b); \
        const double scale = std::max({std::fabs(av), std::fabs(bv), 1e-9}); \
        if (std::fabs(av - bv) > (rel_tol) * scale) { \
            throw droppoint::test::Failure(std::string("REQUIRE_REL_NEAR failed: ") + #a + " (" + std::to_string(av) + \
                ") vs " + #b + " (" + std::to_string(bv) + "), rel_tol=" + std::to_string(rel_tol) + " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)
