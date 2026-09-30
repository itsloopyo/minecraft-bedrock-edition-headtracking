#pragma once

#include <cstdint>

#include "image_scan.h"

namespace mcht::builds {

struct ClientAccess {
    std::uint32_t LevelOwner = 0;
    std::uint32_t LevelReference = 0;
    std::uint32_t LocalPlayerGetter = 0;
    std::uint32_t LocalPlayerReference = 0;
    std::uint32_t MultiplayerSlot = 0;
    std::uint32_t PrimaryFlag = 0;
    std::uint32_t Game = 0;
    std::uint32_t ServerInterface = 0;
    std::uint32_t ServerSlot = 0;
};

struct LevelAccess {
    std::uint32_t DataReference = 0;
    std::uint32_t Data = 0;
    std::uint32_t Rules = 0;
    std::uint32_t MultiplayerFlag = 0;
    std::uint32_t PlayerList = 0;
};

bool ResolveClientAccess(const ModuleImage& image, const void* vtable, ClientAccess& out);
bool ResolveLevelAccess(const ModuleImage& image, const void* vtable,
                        const ClientAccess& client, LevelAccess& out);
bool ResolveServerAccess(const ModuleImage& image, const void* vtable,
                         std::uint32_t slot, std::uint32_t& member);

}  // namespace mcht::builds
