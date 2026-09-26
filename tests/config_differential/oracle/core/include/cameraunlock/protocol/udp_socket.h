#pragma once

#include "cameraunlock/protocol/socket_types.h"
#include <cstdint>

namespace cameraunlock {

/// RAII wrapper for UDP socket setup/teardown.
/// Handles WSA init, socket creation, non-blocking mode, binding, and cleanup.
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();

    // Non-copyable, non-movable
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&&) = delete;
    UdpSocket& operator=(UdpSocket&&) = delete;

    /// Creates and binds a non-blocking UDP socket on the given port.
    /// @return True if successful.
    bool Open(uint16_t port);

    /// Closes the socket and cleans up WSA if needed.
    void Close();

    /// Returns the raw socket handle.
    SOCKET GetHandle() const { return m_socket; }

    /// True if the socket is open and valid.
    bool IsOpen() const { return m_socket != INVALID_SOCKET; }

private:
    SOCKET m_socket = INVALID_SOCKET;
    bool m_wsaInitialized = false;
};

}  // namespace cameraunlock
