#include "session_policy.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "builds/build_registry.h"
#include "builds/session_access.h"
#include "cameraunlock/logging/file_log.h"
#include "common/memory_probe.h"

namespace mcht::session {
namespace {

using mcht::memory::AccessViolationFilter;
using mcht::memory::IsReadable;

// Reading the whole chain a few times a second is plenty; a game rule cannot
// change faster than a packet arrives.
constexpr std::uint64_t kPollIntervalMs = 200;

// Consecutive polls that must agree before tracking is allowed to switch ON.
// Switching OFF is always immediate.
//
// This is asymmetric on purpose. It covers two different fail-open routes with
// the same mechanism:
//   * a level load, where the pointer chain resolves and unresolves as objects
//     appear, and the player list is briefly self-only because PlayerListPacket
//     has not arrived yet;
//   * a roster dip mid-fight, where an opponent leaves the tab list for a
//     moment because they went through a portal, respawned or relogged.
// Both look identical from here: a single poll saying "nobody else is around".
constexpr int kConfirmPolls = 5;

// A player list longer than this is not a player list.
constexpr std::uint64_t kMaxPlausiblePlayers = 4096;

// The variant tag a bool-valued game rule carries. Anything else means this is
// not the rule the profile says it is, so its value byte cannot be trusted.
constexpr unsigned char kBoolVariantTag = 1;

// How many polls a message may be re-tried for. An attempt is spent only when
// delivery could not happen at all - no local player yet, or the display call
// faulted - never on a delivered message, or the player would read the same
// chat line several times over.
constexpr int kAnnounceAttempts = 3;

enum class Reason { NotInWorld, Settling, PvpCombat, Allowed };

struct AccessCache {
    mcht::builds::ModuleImage Image;
    const void* ClientVtable = nullptr;
    const void* LevelVtable = nullptr;
    const void* ServerVtable = nullptr;
    mcht::builds::ClientAccess Client;
    mcht::builds::LevelAccess Level;
    std::uint32_t LocalServer = 0;
    bool ClientValid = false;
    bool LevelValid = false;
    bool ServerValid = false;
};

AccessCache g_access;

// Everything carried between polls. Grouped because leaving a world has to
// clear all of it at once: nothing polls in menus, so a counter left standing
// from the previous world would skip the confirmation delay - the only thing
// covering the join window - for every later world of the session.
struct PollState {
    std::uint64_t LastPollAt = 0;
    bool Allowed = false;
    Reason Announced = Reason::NotInWorld;
    int Confirmations = 0;

    // Last values logged, so the line is written on change rather than on
    // every poll.
    std::uint64_t LastPlayers = 0;
    bool LastPvp = false;
    bool LastRemote = false;

    int PendingAnnounce = 0;
    const char* PendingText = nullptr;

    void ResetForNewSession() {
        Allowed = false;
        Announced = Reason::NotInWorld;
        Confirmations = 0;
        LastPlayers = 0;
        LastPvp = false;
        LastRemote = false;
        // The repeat is undeliverable once the local player is gone, and left
        // standing it is delivered into the NEXT world instead: the first poll
        // there finds a local player and says "Head tracking active" while the
        // gate is still settling, or in a PvP world that is about to refuse.
        PendingAnnounce = 0;
        PendingText = nullptr;
    }
};

PollState g_poll;

// Virtual dispatch by byte offset into the vtable.
template <typename Fn>
Fn VirtualAt(void* object, std::uint32_t byteOffset) {
    if (object == nullptr || !IsReadable(object, sizeof(void*))) {
        return nullptr;
    }
    void** const vtable = *reinterpret_cast<void***>(object);
    if (vtable == nullptr || !IsReadable(vtable + byteOffset / sizeof(void*), sizeof(void*))) {
        return nullptr;
    }
    return reinterpret_cast<Fn>(vtable[byteOffset / sizeof(void*)]);
}

// Reads a pointer-sized member, or nothing when the slot is not there.
void* MemberPointer(void* object, std::uint32_t byteOffset) {
    if (object == nullptr) {
        return nullptr;
    }
    const auto slot = static_cast<unsigned char*>(object) + byteOffset;
    if (!IsReadable(slot, sizeof(void*))) {
        return nullptr;
    }
    return *reinterpret_cast<void* const*>(slot);
}

using ObjectGetter = void*(__fastcall*)(void*);
using DisplayMessageFn = void(__fastcall*)(void*, const std::string*,
                                           const std::optional<std::string>*);

// Client-side only: this appends to the local GUI message list. It sends no
// packet and other players never see it.
// Separated because a function holding C++ objects cannot also hold __try.
bool CallDisplay(DisplayMessageFn display, void* localPlayer, const std::string* message,
                 const std::optional<std::string>* filtered) {
    // Access violations only. A stale vtable offset lands us on the wrong
    // function and faults, which is this handler's whole purpose; a stack
    // overflow or a C++ exception the game threw belongs to the game, and
    // swallowing either here would hide a dead process or abandon an unwind
    // half done.
    __try {
        display(localPlayer, message, filtered);
        return true;
    } __except (AccessViolationFilter(GetExceptionCode())) {
        return false;
    }
}

// True when the line was handed to the game. False means nothing was shown, so
// the caller may try again on a later poll.
bool Tell(void* localPlayer, const char* text) {
    const auto& session = mcht::builds::ActiveProfile().Offsets.Session;
    if (!mcht::builds::KnownBuild() || session.LocalPlayerDisplayMessage == 0) {
        return false;
    }
    const auto display =
        VirtualAt<DisplayMessageFn>(localPlayer, session.LocalPlayerDisplayMessage);
    if (display == nullptr) {
        cameraunlock::logging::Line("[message] no display function at LocalPlayer vtable +0x%X.",
                                    session.LocalPlayerDisplayMessage);
        return false;
    }
    const std::string message(text);
    const std::optional<std::string> filtered;
    if (!CallDisplay(display, localPlayer, &message, &filtered)) {
        cameraunlock::logging::Line("[message] display call faulted.");
        return false;
    }
    return true;
}

void Announce(void* localPlayer, Reason reason) {
    if (g_poll.PendingAnnounce > 0 && g_poll.PendingText != nullptr) {
        // Spend an attempt whether or not there is a player to tell, so a
        // message that can never be delivered stops being carried rather than
        // surfacing several worlds later.
        --g_poll.PendingAnnounce;
        if (localPlayer != nullptr && Tell(localPlayer, g_poll.PendingText)) {
            // Delivered. Anything still pending would be a duplicate line.
            g_poll.PendingAnnounce = 0;
            g_poll.PendingText = nullptr;
        }
    }

    if (reason == g_poll.Announced || reason == Reason::Settling) {
        return;
    }
    g_poll.Announced = reason;

    switch (reason) {
        case Reason::Allowed:
            cameraunlock::logging::Line("Head tracking active.");
            g_poll.PendingText = "Head tracking active.";
            g_poll.PendingAnnounce = kAnnounceAttempts;
            break;
        case Reason::PvpCombat:
            cameraunlock::logging::Line("Head tracking disabled: PvP with other players.");
            g_poll.PendingText =
                "Head tracking disabled: PvP is on and other players are here. "
                "It would let you look around without moving your aim.";
            g_poll.PendingAnnounce = kAnnounceAttempts;
            break;
        case Reason::NotInWorld:
            // Leaving a world is not worth a message, and there is nobody to
            // tell: the local player is gone by then.
            break;
        case Reason::Settling:
            break;
    }
}

// The objects the verdict is read from. Present only in a world that has
// finished joining.
struct WorldHandles {
    void* ClientInstance;
    void* Level;
    void* LocalPlayer;
    void* Data;
};

std::optional<bool> ReadBool(void* object, std::uint32_t offset) {
    if (object == nullptr) {
        return std::nullopt;
    }
    const auto at = static_cast<const unsigned char*>(object) + offset;
    if (!IsReadable(at, 1) || *at > 1) {
        return std::nullopt;
    }
    return *at != 0;
}

bool ResolveWorld(void* self, WorldHandles& out) {
    if (self == nullptr) {
        return false;
    }
    void* const clientInstance =
        MemberPointer(self, mcht::builds::ActiveLayout().ClientInstance);
    if (clientInstance == nullptr) {
        return false;
    }

    const void* clientVtable = MemberPointer(clientInstance, 0);
    if (!clientVtable) {
        return false;
    }
    if (!g_access.Image.Base && !mcht::builds::MapRunningImage(g_access.Image)) {
        return false;
    }
    if (clientVtable != g_access.ClientVtable) {
        g_access.ClientVtable = clientVtable;
        g_access.ClientValid = mcht::builds::ResolveClientAccess(
            g_access.Image, clientVtable, g_access.Client);
        g_access.LevelVtable = nullptr;
        g_access.ServerVtable = nullptr;
        cameraunlock::logging::Line("[session] client access %s.",
            g_access.ClientValid ? "validated" : "unsupported; tracking disabled before calling game functions");
    }
    if (!g_access.ClientValid) {
        return false;
    }
    const auto& access = g_access.Client;
    void* owner = MemberPointer(clientInstance, access.LevelOwner);
    void* reference = MemberPointer(owner, access.LevelReference);
    if (ReadBool(reference, 0x28) != true || ReadBool(MemberPointer(reference, 0x30), 0) != true) {
        return false;
    }
    void* level = MemberPointer(reference, 0x40);
    const void* levelVtable = MemberPointer(level, 0);
    if (!levelVtable) {
        return false;
    }
    if (levelVtable != g_access.LevelVtable) {
        g_access.LevelVtable = levelVtable;
        g_access.LevelValid = mcht::builds::ResolveLevelAccess(
            g_access.Image, levelVtable, access, g_access.Level);
        cameraunlock::logging::Line("[session] level access %s.",
            g_access.LevelValid ? "validated" : "unsupported; tracking disabled");
    }
    if (!g_access.LevelValid ||
        ReadBool(MemberPointer(level, g_access.Level.DataReference), 0) != true) {
        return false;
    }
    void* data = MemberPointer(level, g_access.Level.Data);
    if (!data) {
        return false;
    }

    // The local player only exists once StartGamePacket has been handled, and
    // that matters for correctness rather than tidiness: before it arrives the
    // pvp rule still holds its registration default of false, so reading it
    // early would report "no PvP" and switch tracking ON in a PvP session.
    const auto localReference = static_cast<const unsigned char*>(clientInstance) + access.LocalPlayerReference;
    if (!IsReadable(localReference, 24)) {
        return false;
    }
    const auto getLocalPlayer = reinterpret_cast<ObjectGetter>(
        const_cast<unsigned char*>(g_access.Image.Base) + access.LocalPlayerGetter);
    void* const localPlayer = getLocalPlayer(clientInstance);
    if (localPlayer == nullptr) {
        return false;
    }

    out = {clientInstance, level, localPlayer, data};
    return true;
}

// Size of the tab list, or nothing when it does not look like one. Includes
// the local player, so a solo session reads 1.
std::optional<std::uint64_t> ReadPlayerCount(void* level) {
    void* const playerList = MemberPointer(level, g_access.Level.PlayerList);
    if (playerList == nullptr) {
        return std::nullopt;
    }
    const auto sizeField = static_cast<unsigned char*>(playerList) + 0x10;
    if (!IsReadable(sizeField, sizeof(std::uint64_t))) {
        return std::nullopt;
    }
    const std::uint64_t players = *reinterpret_cast<const std::uint64_t*>(sizeField);
    if (players == 0 || players > kMaxPlausiblePlayers) {
        return std::nullopt;
    }
    return players;
}

// The `pvp` game rule, or nothing when the rule vector does not look the way
// the validated layout requires.
std::optional<bool> ReadPvpEnabled(void* data) {
    void* rules = static_cast<unsigned char*>(data) + g_access.Level.Rules;
    const auto begin = static_cast<unsigned char*>(MemberPointer(rules, 0x18));
    const auto end = reinterpret_cast<std::uintptr_t>(MemberPointer(rules, 0x20));
    const auto start = reinterpret_cast<std::uintptr_t>(begin);
    constexpr std::size_t stride = 0x118;
    if (!begin || end <= start || end - start > 1024 * stride || (end - start) % stride != 0 ||
        !IsReadable(begin, static_cast<std::size_t>(end - start))) {
        return std::nullopt;
    }
    std::optional<bool> found;
    for (std::size_t offset = 0; offset < end - start; offset += stride) {
        const auto rule = begin + offset;
        std::uint64_t length, capacity;
        std::memcpy(&length, rule + 0x20, sizeof(length));
        std::memcpy(&capacity, rule + 0x28, sizeof(capacity));
        if (length != 3 || capacity < length) {
            continue;
        }
        const auto name = capacity < 16 ? rule + 0x10 :
            static_cast<unsigned char*>(MemberPointer(rule, 0x10));
        if (!name || !IsReadable(name, 3) || std::memcmp(name, "pvp", 3) != 0) {
            continue;
        }
        if (found.has_value() || rule[8] != kBoolVariantTag || rule[4] > 1) {
            return std::nullopt;
        }
        found = rule[4] != 0;
    }
    return found;
}

std::optional<bool> ReadRemote(const WorldHandles& world) {
    const auto multiplayer = ReadBool(world.Data, g_access.Level.MultiplayerFlag);
    const auto secondary = ReadBool(world.ClientInstance, g_access.Client.PrimaryFlag);
    if (!multiplayer.has_value() || !secondary.has_value()) {
        return std::nullopt;
    }
    if (!*multiplayer) {
        return false;
    }
    if (*secondary) {
        return true;
    }
    void* game = MemberPointer(world.ClientInstance, g_access.Client.Game);
    if (!game) {
        return std::nullopt;
    }
    auto server = static_cast<unsigned char*>(game) + g_access.Client.ServerInterface;
    const void* vtable = MemberPointer(server, 0);
    if (!vtable) {
        return std::nullopt;
    }
    if (vtable != g_access.ServerVtable) {
        g_access.ServerVtable = vtable;
        g_access.ServerValid = mcht::builds::ResolveServerAccess(
            g_access.Image, vtable, g_access.Client.ServerSlot, g_access.LocalServer);
        cameraunlock::logging::Line("[session] server access %s.",
            g_access.ServerValid ? "validated" : "unsupported; tracking disabled");
    }
    if (!g_access.ServerValid || !IsReadable(server + g_access.LocalServer, sizeof(void*))) {
        return std::nullopt;
    }
    return MemberPointer(server, g_access.LocalServer) == nullptr;
}

// All three inputs are logged, not just the verdict. When the roster reads 1,
// `remote` is the only thing deciding the outcome, and a report saying
// "tracking stayed on" is unactionable without knowing which input said so.
void LogInputsOnChange(std::uint64_t players, bool pvpEnabled, bool remote) {
    if (players == g_poll.LastPlayers && pvpEnabled == g_poll.LastPvp &&
        remote == g_poll.LastRemote) {
        return;
    }
    g_poll.LastPlayers = players;
    g_poll.LastPvp = pvpEnabled;
    g_poll.LastRemote = remote;
    cameraunlock::logging::Line("[session] players=%llu pvp=%s remote=%s",
                                static_cast<unsigned long long>(players),
                                pvpEnabled ? "on" : "off", remote ? "yes" : "no");
}

Reason Evaluate(void* self, void** outLocalPlayer) {
    WorldHandles world;
    if (!ResolveWorld(self, world)) {
        return Reason::NotInWorld;
    }
    *outLocalPlayer = world.LocalPlayer;

    const std::optional<std::uint64_t> players = ReadPlayerCount(world.Level);
    if (!players.has_value()) {
        return Reason::NotInWorld;
    }

    const std::optional<bool> pvpEnabled = ReadPvpEnabled(world.Data);
    if (!pvpEnabled.has_value()) {
        return Reason::NotInWorld;
    }

    // The remote check is a second positive: its false also covers menus and a
    // LAN-hosted world, so it is never read as proof of safety on its own.
    const auto remote = ReadRemote(world);
    if (!remote.has_value()) {
        return Reason::NotInWorld;
    }

    LogInputsOnChange(*players, *pvpEnabled, *remote);

    if (!*pvpEnabled) {
        return Reason::Allowed;
    }
    return (*players >= 2 || *remote) ? Reason::PvpCombat : Reason::Allowed;
}

// A fault walking the pointer chain means the world is not in the shape the
// profile describes, which is exactly "not in a world" and closes the gate.
// Anything that is not a bad access is left to unwind: reporting a stack
// overflow as a quiet no-world verdict would hide it behind tracking that
// merely looks switched off.
Reason EvaluateGuarded(void* self, void** outLocalPlayer) {
    __try {
        return Evaluate(self, outLocalPlayer);
    } __except (AccessViolationFilter(GetExceptionCode())) {
        static ULONGLONG lastLog = 0;
        const auto now = GetTickCount64();
        if (now - lastLog >= 5000) {
            lastLog = now;
            cameraunlock::logging::Line("[session] game memory became unreadable; tracking disabled.");
        }
        return Reason::NotInWorld;
    }
}

}  // namespace

bool TrackingAllowed(void* self) {
    const std::uint64_t now = GetTickCount64();
    if (now - g_poll.LastPollAt < kPollIntervalMs) {
        return g_poll.Allowed;
    }
    g_poll.LastPollAt = now;

    void* localPlayer = nullptr;
    const Reason reason = EvaluateGuarded(self, &localPlayer);

    if (reason == Reason::NotInWorld) {
        // Includes every unreadable state, so this is also the reset for
        // leaving a world, and for any fault inside Evaluate.
        g_poll.ResetForNewSession();
        Announce(nullptr, Reason::NotInWorld);
        return false;
    }

    if (reason != Reason::Allowed) {
        // Anything that is not a clean allow takes effect at once and clears
        // the confirmation run. Turning off is never delayed.
        g_poll.Confirmations = 0;
        g_poll.Allowed = false;
        Announce(localPlayer, reason);
        return false;
    }

    if (g_poll.Confirmations < kConfirmPolls) {
        ++g_poll.Confirmations;
        g_poll.Allowed = false;
        Announce(localPlayer, Reason::Settling);
        return false;
    }

    g_poll.Allowed = true;
    Announce(localPlayer, Reason::Allowed);
    return true;
}

}  // namespace mcht::session
