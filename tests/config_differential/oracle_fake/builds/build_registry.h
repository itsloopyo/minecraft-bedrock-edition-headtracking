#pragma once

// Stands in for v1.1.2's builds/build_registry.h in the config oracle library, which compiles
// that build's dllmain.cpp. Only SelectProfile and the result Bootstrap tests are needed;
// oracle_config_adapter.cpp defines SelectProfile to report the camera as recovered, so
// Bootstrap goes on to write and read the ini.

namespace mcht::builds {

enum class SelectResult {
    Matched,
    Adopted,
    Unresolved,
    ReadFailed,
};

SelectResult SelectProfile();

}  // namespace mcht::builds
