#pragma once

// Stands in for v1.1.2's common/module_path.h in the config oracle library. Bootstrap puts its
// log and ini in the folder DirectoryOfModule names, which the test sets to a scratch folder of
// its own, with the trailing backslash the real function returns.

#include <windows.h>

#include <string>

namespace mcht::paths {

inline std::wstring& OracleModuleDirectory() {
    static std::wstring directory;
    return directory;
}

inline std::wstring DirectoryOfModule(HMODULE) { return OracleModuleDirectory(); }

}  // namespace mcht::paths
