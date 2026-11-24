// hw/file_ownership.cpp — see header.
#include "file_ownership.h"

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace droppoint::hw {

bool AdoptOwnerFromDir(const std::string& path, const std::string& dir) {
#ifdef _WIN32
    (void)path;
    (void)dir;
    return true;
#else
    if (::geteuid() != 0) return true;  // only root can leave (or fix) a mismatch
    struct stat dir_st {};
    if (::stat(dir.c_str(), &dir_st) != 0) return false;
    struct stat file_st {};
    if (::stat(path.c_str(), &file_st) != 0) return false;
    if (file_st.st_uid == dir_st.st_uid && file_st.st_gid == dir_st.st_gid) return true;
    return ::chown(path.c_str(), dir_st.st_uid, dir_st.st_gid) == 0;
#endif
}

}  // namespace droppoint::hw
