#pragma once

#include <windows.h>

#include <string>

namespace mcht::paths {

// The directory a module was loaded from, with a trailing backslash, or empty
// if the path could not be read. Pass nullptr for the running executable.
//
// The launcher resolves the mod DLL beside itself with this, and the mod puts
// its log and ini beside itself with this. Both need the same answer about
// where "beside me" is, so they ask the same function.
inline std::wstring DirectoryOfModule(HMODULE module) {
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return L"";
    }
    const std::wstring full(path, length);
    return full.substr(0, full.find_last_of(L'\\') + 1);
}

}  // namespace mcht::paths
