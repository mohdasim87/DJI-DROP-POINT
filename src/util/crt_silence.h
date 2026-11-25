// util/crt_silence.h
//
// On Windows/MSVC, CRT assertions (invalid distribution parameters, iterator
// debugging faults, ...) pop a modal "Debug Assertion Failed" dialog by
// default — unacceptable in a test binary or a shipped tool. Call
// SilenceCrtDialogs() first in main(): assertions then print to stderr and
// the process exits non-zero. No-op everywhere else.
#pragma once

#if defined(_WIN32) && defined(_MSC_VER)
#include <crtdbg.h>
#include <cstdlib>
#include <windows.h>
// windows.h defines `interface` as a macro; DropKit and the HAL interfaces
// use it as a field/type name.
#undef interface
#endif

namespace droppoint::util {

inline void SilenceCrtDialogs() {
#if defined(_WIN32) && defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_error_mode(_OUT_TO_STDERR);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
}

}  // namespace droppoint::util
