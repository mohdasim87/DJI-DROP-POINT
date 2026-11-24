// hw/file_ownership.h
//
// Keeps files the program creates inside the log directory owned by the
// directory's owner. A root-invoked run (`sudo droppoint --selftest`,
// an ad-hoc debug run) must never leave behind root-owned files that the
// droppoint service user then cannot overwrite — selftest.txt, drops.csv,
// probe output.
#pragma once

#include <string>

namespace droppoint::hw {

// On POSIX, when running as root (geteuid()==0): if `path` exists and its
// owner:group differ from `dir`'s, chown it to match the directory. Returns
// true when nothing needed doing or the chown succeeded.
// No-op (returns true) on non-POSIX builds and when not running as root —
// a non-root process can neither have created the mismatch nor fix it.
// Returns false when `path` or `dir` cannot be stat'ed or chown fails.
bool AdoptOwnerFromDir(const std::string& path, const std::string& dir);

}  // namespace droppoint::hw
