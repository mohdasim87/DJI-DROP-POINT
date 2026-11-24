// hw/sd_notify.h
//
// Minimal systemd notify implementation: AF_UNIX datagram to
// $NOTIFY_SOCKET, no libsystemd dependency (the .deb stays glibc-only).
// No-ops returning false when NOTIFY_SOCKET is unset or on non-Linux
// builds, so callers need no #ifdefs.
#pragma once

#include <string>

namespace droppoint::hw {

// Sends one datagram ("READY=1", "WATCHDOG=1", "STATUS=...", ...) to the
// notification socket. Returns false when notifications are unavailable.
bool SdNotify(const std::string& state);

inline bool SdNotifyReady() { return SdNotify("READY=1"); }
inline bool SdNotifyWatchdog() { return SdNotify("WATCHDOG=1"); }
inline bool SdNotifyStopping() { return SdNotify("STOPPING=1"); }

}  // namespace droppoint::hw
