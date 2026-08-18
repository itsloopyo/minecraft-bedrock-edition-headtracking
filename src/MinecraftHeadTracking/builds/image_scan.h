#pragma once

#include <cstddef>
#include <cstdint>

namespace mcht::builds {

// The running Minecraft.Windows.exe as the loader mapped it: the two sections
// discovery reads, and the .pdata table that turns an address inside a function
// into that function's bounds.
//
// Everything here is an RVA relative to Base, which is what the log and the
// build profiles speak in.
struct ModuleImage {
    const unsigned char* Base = nullptr;
    std::uint32_t TextRva = 0;
    std::uint32_t TextSize = 0;
    std::uint32_t RdataRva = 0;
    std::uint32_t RdataSize = 0;
    // RUNTIME_FUNCTION[], three uint32 each: begin RVA, end RVA, unwind info.
    const std::uint32_t* Functions = nullptr;
    std::size_t FunctionCount = 0;
};

struct FunctionBounds {
    std::uint32_t Begin;
    std::uint32_t End;
};

// Read the running executable's headers. False on anything malformed, which
// hands the caller no addresses and so leaves the mod dormant.
bool MapRunningImage(ModuleImage& out);

// The .pdata entry covering `rva`. MSVC emits several entries for one logical
// function when it has separated chunks, so this reports the chunk rather than
// promising an entry point.
bool FunctionContaining(const ModuleImage& image, std::uint32_t rva, FunctionBounds& out);

}  // namespace mcht::builds
