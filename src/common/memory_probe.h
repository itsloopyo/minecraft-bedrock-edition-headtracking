#pragma once

#include <windows.h>

#include <cstddef>

// Probes for game memory this mod is about to touch, and the exception filter
// that catches the case a probe cannot rule out.
//
// The hook path and the fairness gate both walk pointer chains into another
// process's structures, on the render thread, at a few hundred calls a second.
// A probe that says no costs a frame of tracking; a fault that is not caught
// takes the player's session with it. Both used to carry their own copy of
// this.
namespace mcht::memory {

namespace detail {

// True when [address, address + size) lies entirely inside one committed
// region that is not a guard page. The protection the caller needs is its own
// question; this only settles that the bytes are there.
inline bool CommittedRegionCovers(const void* address, std::size_t size,
                                  MEMORY_BASIC_INFORMATION& info) {
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT) {
        return false;
    }
    if ((info.Protect & PAGE_GUARD) != 0) {
        return false;
    }
    const auto start = static_cast<const unsigned char*>(info.BaseAddress);
    return static_cast<const unsigned char*>(address) + size <= start + info.RegionSize;
}

}  // namespace detail

inline bool IsReadable(const void* address, std::size_t size) {
    MEMORY_BASIC_INFORMATION info;
    return detail::CommittedRegionCovers(address, size, info) &&
           (info.Protect & PAGE_NOACCESS) == 0;
}

inline bool IsWritable(const void* address, std::size_t size) {
    constexpr DWORD kWritable =
        PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    MEMORY_BASIC_INFORMATION info;
    return detail::CommittedRegionCovers(address, size, info) && (info.Protect & kWritable) != 0;
}

// For `__except (AccessViolationFilter(GetExceptionCode()))`. Handles a bad
// read or write and lets everything else - a breakpoint, a C++ exception
// travelling through, a stack overflow - keep unwinding to whoever owns it.
inline int AccessViolationFilter(DWORD code) {
    return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                              : EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace mcht::memory
