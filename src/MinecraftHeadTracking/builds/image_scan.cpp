#include "image_scan.h"

#include <windows.h>

#include <cstring>

#include "common/memory_probe.h"

namespace mcht::builds {
namespace {

using mcht::memory::AccessViolationFilter;

bool ReadHeaders(const unsigned char* base, ModuleImage& out) {
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }

    const IMAGE_SECTION_HEADER* const sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const IMAGE_SECTION_HEADER& section = sections[i];
        // The name field is eight bytes and is only NUL-terminated when it is
        // shorter than that, so it cannot be compared as a C string in place.
        char name[9] = {};
        std::memcpy(name, section.Name, 8);
        if (out.TextRva == 0 && std::strcmp(name, ".text") == 0) {
            out.TextRva = section.VirtualAddress;
            out.TextSize = section.Misc.VirtualSize;
        } else if (out.RdataRva == 0 && std::strcmp(name, ".rdata") == 0) {
            out.RdataRva = section.VirtualAddress;
            out.RdataSize = section.Misc.VirtualSize;
        }
    }

    const IMAGE_DATA_DIRECTORY& pdata =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (pdata.VirtualAddress == 0 || pdata.Size < sizeof(std::uint32_t) * 3) {
        return false;
    }

    out.Base = base;
    out.Functions = reinterpret_cast<const std::uint32_t*>(base + pdata.VirtualAddress);
    out.FunctionCount = pdata.Size / (sizeof(std::uint32_t) * 3);
    return out.TextRva != 0 && out.TextSize != 0 && out.RdataRva != 0 && out.RdataSize != 0;
}

}  // namespace

bool MapRunningImage(ModuleImage& out) {
    const auto base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (base == nullptr) {
        return false;
    }
    ModuleImage image;
    bool ok = false;
    __try {
        ok = ReadHeaders(base, image);
    } __except (AccessViolationFilter(GetExceptionCode())) {
        return false;
    }
    if (!ok) {
        return false;
    }
    out = image;
    return true;
}

bool FunctionContaining(const ModuleImage& image, std::uint32_t rva, FunctionBounds& out) {
    // .pdata is sorted by begin address - the OS itself binary-searches it to
    // dispatch exceptions - so the entry that can cover `rva` is the last one
    // starting at or before it.
    std::size_t low = 0;
    std::size_t high = image.FunctionCount;
    while (low < high) {
        const std::size_t mid = low + (high - low) / 2;
        if (image.Functions[mid * 3] <= rva) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    if (low == 0) {
        return false;
    }
    const std::uint32_t begin = image.Functions[(low - 1) * 3];
    const std::uint32_t end = image.Functions[(low - 1) * 3 + 1];
    if (rva >= end || end <= begin) {
        return false;
    }
    out = {begin, end};
    return true;
}

}  // namespace mcht::builds
