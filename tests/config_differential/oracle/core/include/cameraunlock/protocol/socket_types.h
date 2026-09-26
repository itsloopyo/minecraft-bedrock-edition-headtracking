#pragma once

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

#include <cstdint>

namespace cameraunlock {

/// Checks whether a sockaddr_in represents a remote (non-loopback) address.
///
/// Loopback is the whole 127.0.0.0/8 block, not just 127.0.0.1. This must agree with
/// the C# side, which classifies through IPAddress.IsLoopback and has always covered
/// the full range: pointing OpenTrack at 127.0.0.2 to keep several local streams apart
/// is a real pattern, and an exact-127.0.0.1 test made C++ mods call that sender remote
/// while C# and Rust mods called the identical sender local. Same machine, same config,
/// opposite smoothing, no diagnostic.
///
/// Takes a sockaddr_in, so IPv4 only by construction. Nothing binds dual-stack today;
/// if a receiver ever does, a local IPv4 sender arrives as ::ffff:127.0.0.1 and the
/// classifier must be widened to sockaddr_storage and taught to unmap BEFORE the bind
/// changes, or every same-machine user is silently reclassified as remote.
inline bool IsRemoteAddress(const sockaddr_in& addr) {
    constexpr uint32_t kLoopbackNet = 0x7F000000u;  // 127.0.0.0/8
    constexpr uint32_t kLoopbackMask = 0xFF000000u;
    return (ntohl(addr.sin_addr.s_addr) & kLoopbackMask) != kLoopbackNet;
}

}  // namespace cameraunlock
