#include "session_access.h"

#include <cstring>

#include "code_contract.h"

namespace mcht::builds {
namespace {

// Only relocations and fields extracted below are masked. The called entity
// helpers have their own contracts; masking a call target alone is not enough.
const CodeContract kLevel{47, 0x3acd56aad7b1e696ULL, {{3, 4}, {10, 4}}};
const CodeContract kLocalPlayer{87, 0xa66e3607c2d23fd2ULL,
    {{7, 4}, {22, 4}, {32, 4}, {51, 4}, {70, 4}, {82, 4}}};
const CodeContract kWeakEntity{256, 0xfe4db127b5ef943eULL, {{220, 4}, {242, 4}}};
const CodeContract kPlayerEntity{376, 0x841b3657905f72a4ULL, {}};
const CodeContract kRemote{92, 0x53c760045ca93042ULL,
    {{14, 4}, {20, 4}, {34, 4}, {43, 4}, {58, 4}, {65, 1}, {69, 1}, {73, 1}, {76, 4}}};
const CodeContract kInGame{86, 0x8889bdcf1f3399deULL,
    {{14, 4}, {20, 4}, {42, 4}, {51, 4}, {61, 4}, {68, 4}}};
const CodeContract kPrimary{11, 0x4a17aedd2de72a2cULL, {{2, 4}}};
const CodeContract kLevelData{161, 0xab584ecc93ba3da1ULL,
    {{11, 4}, {21, 4}, {33, 4}, {40, 4}, {47, 4}, {53, 4}, {58, 4}, {80, 4},
     {92, 4}, {104, 4}, {111, 4}, {118, 4}, {124, 4}, {129, 4}, {151, 4}}};
const CodeContract kRules{31, 0x5f914f31bbfa6b0eULL, {{10, 4}, {16, 4}}};
const CodeContract kMultiplayer{32, 0x98f9a84821db8f08ULL, {{10, 4}, {16, 4}, {23, 4}}};
const CodeContract kRoster{131, 0x3e11f9cff4d6f4edULL, {{3, 4}, {122, 4}}};
const CodeContract kLocalServer{12, 0xcce9eb1b49a58148ULL, {{3, 4}}};

bool InRange(std::uintptr_t address, std::size_t bytes, std::uintptr_t start,
             std::size_t size) {
    return address >= start && bytes <= size && address - start <= size - bytes;
}

std::uint32_t Read32(const unsigned char* address) {
    std::uint32_t value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

bool IsCode(const ModuleImage& image, const unsigned char* code, const CodeContract& contract) {
    return InRange(reinterpret_cast<std::uintptr_t>(code), contract.Size,
                   reinterpret_cast<std::uintptr_t>(image.Base) + image.TextRva, image.TextSize)
        && MatchesContract(code, contract.Size, contract);
}

const unsigned char* Virtual(const ModuleImage& image, const void* vtable, std::uint32_t slot) {
    const auto address = reinterpret_cast<std::uintptr_t>(vtable) + slot;
    if (slot % sizeof(void*) != 0 ||
        !InRange(address, sizeof(void*), reinterpret_cast<std::uintptr_t>(image.Base) +
                 image.RdataRva, image.RdataSize)) {
        return nullptr;
    }
    const unsigned char* result;
    std::memcpy(&result, reinterpret_cast<const void*>(address), sizeof(result));
    return result;
}

const unsigned char* FindVirtual(const ModuleImage& image, const void* vtable,
                                  const CodeContract& contract) {
    const unsigned char* found = nullptr;
    for (std::uint32_t slot = 0; slot < 0x1000; slot += sizeof(void*)) {
        const unsigned char* code = Virtual(image, vtable, slot);
        if (!IsCode(image, code, contract)) {
            continue;
        }
        if (found != nullptr && found != code) {
            return nullptr;
        }
        found = code;
    }
    return found;
}

const unsigned char* CallTarget(const unsigned char* code, std::size_t displacement) {
    std::int32_t value;
    std::memcpy(&value, code + displacement, sizeof(value));
    return reinterpret_cast<const unsigned char*>(
        reinterpret_cast<std::uintptr_t>(code) + displacement + sizeof(value) + value);
}

bool ResolveData(const ModuleImage& image, const void* vtable, std::uint32_t slot,
                  std::uint32_t& reference, std::uint32_t& data) {
    const unsigned char* getter = Virtual(image, vtable, slot);
    if (!IsCode(image, getter, kLevelData) || Read32(getter + 11) != Read32(getter + 80)) {
        return false;
    }
    reference = Read32(getter + 11);
    data = Read32(getter + 151);
    return true;
}

}  // namespace

bool ResolveClientAccess(const ModuleImage& image, const void* vtable, ClientAccess& out) {
    const auto level = FindVirtual(image, vtable, kLevel);
    const auto local = FindVirtual(image, vtable, kLocalPlayer);
    const auto remote = FindVirtual(image, vtable, kRemote);
    if (!level || !local || !remote ||
        !IsCode(image, CallTarget(local, 32), kWeakEntity) ||
        !IsCode(image, CallTarget(local, 51), kPlayerEntity)) {
        return false;
    }
    const auto inGame = Virtual(image, vtable, Read32(remote + 14));
    const auto primary = Virtual(image, vtable, Read32(remote + 34));
    if (!IsCode(image, inGame, kInGame) || !IsCode(image, primary, kPrimary) ||
        Virtual(image, vtable, Read32(inGame + 42)) != level || remote[65] != remote[69]) {
        return false;
    }
    out.LevelOwner = Read32(level + 3);
    out.LevelReference = Read32(level + 10);
    out.LocalPlayerGetter = static_cast<std::uint32_t>(local - image.Base);
    out.LocalPlayerReference = Read32(local + 22);
    out.MultiplayerSlot = Read32(inGame + 61);
    out.PrimaryFlag = Read32(primary + 2);
    out.Game = Read32(remote + 58);
    out.ServerInterface = remote[65];
    out.ServerSlot = remote[73];
    return true;
}

bool ResolveLevelAccess(const ModuleImage& image, const void* vtable,
                        const ClientAccess& client, LevelAccess& out) {
    const auto multiplayer = Virtual(image, vtable, client.MultiplayerSlot);
    const auto roster = FindVirtual(image, vtable, kRoster);
    if (!IsCode(image, multiplayer, kMultiplayer) || !roster ||
        !ResolveData(image, vtable, Read32(multiplayer + 10), out.DataReference, out.Data)) {
        return false;
    }
    bool found = false;
    for (std::uint32_t slot = 0; slot < 0x1000; slot += sizeof(void*)) {
        const auto rules = Virtual(image, vtable, slot);
        if (!IsCode(image, rules, kRules)) {
            continue;
        }
        std::uint32_t reference, data;
        if (!ResolveData(image, vtable, Read32(rules + 10), reference, data) ||
            reference != out.DataReference || data != out.Data) {
            continue;
        }
        const auto member = Read32(rules + 22);
        if (found && member != out.Rules) {
            return false;
        }
        out.Rules = member;
        found = true;
    }
    out.MultiplayerFlag = Read32(multiplayer + 23);
    out.PlayerList = Read32(roster + 3);
    return found;
}

bool ResolveServerAccess(const ModuleImage& image, const void* vtable,
                         std::uint32_t slot, std::uint32_t& member) {
    const auto getter = Virtual(image, vtable, slot);
    if (!IsCode(image, getter, kLocalServer)) {
        return false;
    }
    member = Read32(getter + 3);
    return true;
}

}  // namespace mcht::builds
