#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace mcht::builds {

struct CodeMask {
    std::size_t Offset;
    std::size_t Size;
};

struct CodeContract {
    std::size_t Size;
    std::uint64_t Hash;
    std::initializer_list<CodeMask> Masks;
};

inline bool MatchesContract(const unsigned char* code, std::size_t available,
                            const CodeContract& contract) {
    if (available < contract.Size) {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < contract.Size; ++i) {
        unsigned char value = code[i];
        for (const CodeMask& mask : contract.Masks) {
            if (i >= mask.Offset && i - mask.Offset < mask.Size) {
                value = 0;
                break;
            }
        }
        hash = (hash ^ value) * 1099511628211ULL;
    }
    return hash == contract.Hash;
}

}  // namespace mcht::builds
