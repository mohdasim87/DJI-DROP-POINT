// test_file_ownership.cpp — AdoptOwnerFromDir: the no-op paths are
// platform-independent (non-POSIX, non-root, already-matching ownership);
// the real chown path is exercised by the ARM64 install test, which needs
// root and a foreign-owned log directory.
#include "test_framework.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "hw/file_ownership.h"

TEST_CASE("AdoptOwnerFromDir returns true for an existing file (no-op path)") {
    const auto dir = std::filesystem::temp_directory_path() / "dp-own-test";
    std::filesystem::create_directories(dir);
    const auto file = dir / "f.txt";
    { std::ofstream(file.string()) << "x"; }
    // On Windows and for any non-root run this is a pure no-op; under root
    // on POSIX it chowns to the directory owner, which is also true.
    REQUIRE(droppoint::hw::AdoptOwnerFromDir(file.string(), dir.string()));
    std::filesystem::remove_all(dir);
}

TEST_CASE("AdoptOwnerFromDir handles a missing reference dir gracefully") {
    // Must not throw; the return value is allowed to be false on POSIX-root
    // or true as a no-op elsewhere — the contract is "never crash".
    (void)droppoint::hw::AdoptOwnerFromDir(
        "/nonexistent-path-xyz/f", "/nonexistent-dir-xyz");
    REQUIRE(true);
}
