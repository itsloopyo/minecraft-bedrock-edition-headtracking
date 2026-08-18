#include "code_resolver.h"

#include <windows.h>

#include <cstring>
#include <vector>

#include "cameraunlock/logging/file_log.h"
#include "common/memory_probe.h"
#include "image_scan.h"

namespace mcht::builds {
namespace {

using mcht::memory::AccessViolationFilter;

// InGamePlayScreen::_renderLevelPrep, spelled exactly as Bedrock's assert
// macros bake __FUNCSIG__ into .rdata. This is the one address found purely by
// name, and everything else hangs off it.
//
// The string is matched whole and must be NUL-terminated and NUL-preceded, so a
// longer signature that merely contains this one cannot answer for it.
constexpr char kRenderLevelPrepSignature[] =
    "virtual void __cdecl InGamePlayScreen::_renderLevelPrep(ScreenContext &, LevelRenderer &, "
    "Actor &)";

// EnTT hashes a component's type NAME at compile time, so this identifies
// MinecraftCamera::CameraComponent rather than any particular build of it. It
// is what tells the camera functions apart from the rest of the binary.
constexpr std::uint32_t kCameraComponentTypeHash = 0x4F6047C7;

// EnTT's hash for MinecraftCamera::RenderCameraComponent - the tag on the
// entity whose camera the renderer actually draws with. Both values here are
// FNV-1a of the type name, which is how EnTT derives them, so they are
// properties of the class rather than of the build.
//
// This one is what makes the getter findable. There are several functions that
// return "a CameraComponent"; only one looks the component up on the entity
// tagged as the RENDER camera, and that is the one setupCamera agrees with.
// Getting this wrong does not fail loudly: an earlier rule picked a getter that
// resolves a different camera entity, so the mod wrote a real transform into a
// real component every frame, the crosshair moved with it, and the view did not
// budge.
constexpr std::uint32_t kRenderCameraComponentTypeHash = 0x119E772B;

constexpr unsigned char kCallRel32 = 0xE8;

// The inner accessor walks the registry and returns the component, so it stays
// small. Its callers are wrappers.
constexpr std::uint32_t kMaxAccessorSize = 0x200;

// mov rax, [rcx] ; mov rax, [rax + disp32] - loading a vtable slot from the
// first argument, which is how a function taking an interface pointer starts.
constexpr unsigned char kMovRaxFromRcx[] = {0x48, 0x8B, 0x01};
constexpr unsigned char kMovRaxVtable[] = {0x48, 0x8B, 0x80};

// How far into a function the vtable dispatch may sit.
constexpr std::uint32_t kPrologueWindow = 0x40;

// The IClientInstance wrapper dispatches through a slot next to the ones the
// fairness gate already uses, which is what tells it apart from the other
// caller of the same accessor - that one dispatches through an unrelated
// interface, hundreds of bytes away in its vtable.
constexpr std::uint32_t kClientInstanceSlotWindow = 0x40;

// The crosshair pair, found together by the shape below. The renderer packs a
// hardcoded 16x16 into the rect it is about to blit, then hands that rect to
// the blit as its fourth argument:
//
//   movabs rax, 0x1000000010     48 B8 10 00 00 00 10 00 00 00
//   mov    [rbp + D], rax        48 89 85 <D>       ; rect.width / rect.height
//   ...
//   lea    r9,  [rbp + D-8]      4C 8D 8D <D-8>     ; &rect, fourth argument
//   call   <blit>                E8 <rel32>
//
// Both halves are needed: eleven functions in this image carry the constant,
// and it is the rect's USE that picks the renderer out of them. r9 is the
// fourth integer argument in the x64 calling convention, so that half does not
// depend on how the compiler allocated anything.
constexpr unsigned char kMovabs16x16[] = {0x48, 0xB8, 0x10, 0x00, 0x00, 0x00,
                                          0x10, 0x00, 0x00, 0x00};
constexpr unsigned char kMovRbpRax[] = {0x48, 0x89, 0x85};
constexpr unsigned char kLeaR9Rbp[] = {0x4C, 0x8D, 0x8D};

// The renderer sets up the other three arguments between the lea and the call,
// which is four or five instructions.
constexpr std::uint32_t kMaxLeaToCall = 0x30;

// Where rect.x sits relative to the packed width/height the movabs stores.
constexpr std::int32_t kRectOriginFromExtent = 8;

// A function that at least one of the scans flagged. Only a few hundred of the
// image's 900k functions ever land here.
struct Flagged {
    std::uint32_t Begin;
    std::uint32_t End;
    bool HasHash;
    bool HasRenderHash;
};

// REX.W-prefixed lea/mov whose ModRM selects [rip + disp32]. This is how
// Bedrock reaches every string and static it references.
bool IsRipRelativeLoad(const unsigned char* p) {
    return p[0] >= 0x48 && p[0] <= 0x4F && (p[1] == 0x8D || p[1] == 0x8B) &&
           (p[2] & 0xC7) == 0x05;
}

const unsigned char* FindBytes(const unsigned char* haystack, std::size_t haystackSize,
                               const void* needle, std::size_t needleSize) {
    if (needleSize == 0 || haystackSize < needleSize) {
        return nullptr;
    }
    const auto first = *static_cast<const unsigned char*>(needle);
    const unsigned char* cursor = haystack;
    std::size_t remaining = haystackSize - needleSize + 1;
    while (remaining > 0) {
        const auto hit = static_cast<const unsigned char*>(std::memchr(cursor, first, remaining));
        if (hit == nullptr) {
            return nullptr;
        }
        if (std::memcmp(hit, needle, needleSize) == 0) {
            return hit;
        }
        remaining -= static_cast<std::size_t>(hit - cursor) + 1;
        cursor = hit + 1;
    }
    return nullptr;
}

// Every .rdata address a reference to the signature could point at: the string
// itself, and any assert descriptor {const char* file, const char* funcsig}
// holding it.
//
// The descriptor indirection is not an optimisation. Virtual functions load the
// descriptor's address and never the string's, so a scan that only followed
// direct references would miss _renderLevelPrep entirely.
std::vector<std::uint32_t> SignatureTargets(const ModuleImage& image) {
    std::vector<std::uint32_t> targets;

    const unsigned char* const rdata = image.Base + image.RdataRva;
    const std::size_t length = std::strlen(kRenderLevelPrepSignature);

    const unsigned char* found = nullptr;
    std::size_t searched = 0;
    while (searched < image.RdataSize) {
        const unsigned char* const hit =
            FindBytes(rdata + searched, image.RdataSize - searched, kRenderLevelPrepSignature,
                      length);
        if (hit == nullptr) {
            break;
        }
        searched = static_cast<std::size_t>(hit - rdata) + 1;
        const bool startsHere = hit == rdata || hit[-1] == 0;
        const bool endsHere = hit + length < rdata + image.RdataSize && hit[length] == 0;
        if (!startsHere || !endsHere) {
            continue;
        }
        if (found != nullptr) {
            cameraunlock::logging::Line(
                "  the _renderLevelPrep signature appears more than once; not guessing.");
            return {};
        }
        found = hit;
    }
    if (found == nullptr) {
        return {};
    }

    const auto stringRva = static_cast<std::uint32_t>(found - image.Base);
    targets.push_back(stringRva);

    // The descriptor holds absolute addresses, which the loader has already
    // relocated, so what sits in memory is the string's runtime address.
    const auto stringAddress = reinterpret_cast<std::uintptr_t>(image.Base) + stringRva;
    for (std::size_t offset = 8; offset + 8 <= image.RdataSize; offset += 8) {
        std::uintptr_t candidate = 0;
        std::memcpy(&candidate, rdata + offset, sizeof(candidate));
        if (candidate != stringAddress) {
            continue;
        }
        // The slot before it is the __FILE__ pointer. Requiring it to point
        // into .rdata is what separates a descriptor from an ordinary pointer
        // table that happens to mention the string.
        std::uintptr_t file = 0;
        std::memcpy(&file, rdata + offset - 8, sizeof(file));
        const auto fileRva =
            static_cast<std::uintptr_t>(file - reinterpret_cast<std::uintptr_t>(image.Base));
        if (fileRva >= image.RdataRva && fileRva < image.RdataRva + image.RdataSize) {
            targets.push_back(static_cast<std::uint32_t>(image.RdataRva + offset - 8));
        }
    }
    return targets;
}

bool IsTarget(const std::vector<std::uint32_t>& targets, std::uint32_t rva) {
    for (const std::uint32_t target : targets) {
        if (target == rva) {
            return true;
        }
    }
    return false;
}

Flagged* FlaggedFor(std::vector<Flagged>& flagged, const ModuleImage& image, std::uint32_t rva) {
    FunctionBounds bounds;
    if (!FunctionContaining(image, rva, bounds)) {
        return nullptr;
    }
    for (Flagged& entry : flagged) {
        if (entry.Begin == bounds.Begin) {
            return &entry;
        }
    }
    flagged.push_back({bounds.Begin, bounds.End, false, false});
    return &flagged.back();
}

// Does this function index an element of 9*32 = 0x120 bytes, which is
// sizeof(MinecraftCamera::CameraComponent)? Used to confirm the getter, never
// to find it.
//
// Matched without pinning registers. The 20260806 build wrote this as
// `lea rax, [rdx + rdx*8]` / `shl eax, 5` and 20260812 writes the same thing as
// `lea rdi, [rcx + rcx*8]` / `shl edi, 5`; a byte pattern taken from one of
// them silently fails on the other, and picks a different function instead.
bool ComputesComponentStride(const ModuleImage& image, const FunctionBounds& bounds) {
    const unsigned char* const base = image.Base;
    bool sawLea = false;
    bool sawShift = false;

    for (std::uint32_t rva = bounds.Begin; rva + 4 <= bounds.End; ++rva) {
        const unsigned char* const p = base + rva;

        // lea r64, [rX + rX*8]: REX.W, 8D, ModRM(mod=00, rm=100 -> SIB),
        // SIB(scale=3, index == base).
        if (p[0] >= 0x48 && p[0] <= 0x4F && p[1] == 0x8D && (p[2] & 0xC7) == 0x04) {
            const unsigned char sib = p[3];
            if ((sib >> 6) == 3 && ((sib >> 3) & 7) == (sib & 7)) {
                sawLea = true;
            }
        }

        // shl r32, 5: C1 /4 ib with a register operand.
        if (p[0] == 0xC1 && (p[1] & 0xC0) == 0xC0 && (p[1] & 0x38) == 0x20 && p[2] == 0x05) {
            sawShift = true;
        }
    }
    return sawLea && sawShift;
}

// One pass over .text answering all three questions at once. .text is 240 MB
// here, so a second pass is a second few hundred milliseconds of startup.
void ScanText(const ModuleImage& image, const std::vector<std::uint32_t>& targets,
              std::vector<std::uint32_t>& referencingOut, std::vector<Flagged>& flaggedOut,
              std::vector<std::uint32_t>& rectHitsOut) {
    const unsigned char* const text = image.Base + image.TextRva;
    std::vector<std::uint32_t> hashHits;
    std::vector<std::uint32_t> renderHashHits;
    std::vector<std::uint32_t> referenceHits;

    // Every candidate is discriminated by its first byte before anything wider
    // is loaded. At 240 MB the difference between one byte compare and three
    // unaligned loads per position is most of this function's cost.
    constexpr auto kHashFirstByte = static_cast<unsigned char>(kCameraComponentTypeHash & 0xFF);
    constexpr auto kRenderHashFirstByte =
        static_cast<unsigned char>(kRenderCameraComponentTypeHash & 0xFF);

    for (std::uint32_t i = 0; i + sizeof(kMovabs16x16) <= image.TextSize; ++i) {
        const unsigned char* const p = text + i;
        const unsigned char lead = p[0];

        if (lead == kHashFirstByte || lead == kRenderHashFirstByte) {
            std::uint32_t immediate = 0;
            std::memcpy(&immediate, p, sizeof(immediate));
            if (immediate == kCameraComponentTypeHash) {
                hashHits.push_back(image.TextRva + i);
            } else if (immediate == kRenderCameraComponentTypeHash) {
                renderHashHits.push_back(image.TextRva + i);
            }
            continue;
        }

        if (lead < 0x48 || lead > 0x4F) {
            continue;
        }

        if (IsRipRelativeLoad(p)) {
            std::int32_t displacement = 0;
            std::memcpy(&displacement, p + 3, sizeof(displacement));
            const auto target = static_cast<std::uint32_t>(image.TextRva + i + 7 + displacement);
            if (IsTarget(targets, target)) {
                referenceHits.push_back(image.TextRva + i);
            }
        }

        if (lead == kMovabs16x16[0] &&
            std::memcmp(p, kMovabs16x16, sizeof(kMovabs16x16)) == 0) {
            rectHitsOut.push_back(image.TextRva + i);
        }
    }

    // Attributing hits to functions is a binary search each, so it is done here
    // on a few hundred hits rather than inside the loop on 240 million bytes.
    for (const std::uint32_t hit : hashHits) {
        if (Flagged* const entry = FlaggedFor(flaggedOut, image, hit)) {
            entry->HasHash = true;
        }
    }
    for (const std::uint32_t hit : renderHashHits) {
        if (Flagged* const entry = FlaggedFor(flaggedOut, image, hit)) {
            entry->HasRenderHash = true;
        }
    }
    for (const std::uint32_t hit : referenceHits) {
        FunctionBounds bounds;
        if (!FunctionContaining(image, hit, bounds)) {
            continue;
        }
        bool seen = false;
        for (const std::uint32_t existing : referencingOut) {
            seen = seen || existing == bounds.Begin;
        }
        if (!seen) {
            referencingOut.push_back(bounds.Begin);
        }
    }
}

const Flagged* FindFlagged(const std::vector<Flagged>& flagged, std::uint32_t begin) {
    for (const Flagged& entry : flagged) {
        if (entry.Begin == begin) {
            return &entry;
        }
    }
    return nullptr;
}

// setupCamera is the first thing _renderLevelPrep does with the camera, and the
// earliest function it calls that carries the CameraComponent type hash.
std::uint32_t FindCameraSetup(const ModuleImage& image, std::uint32_t renderLevelPrep,
                              const std::vector<Flagged>& flagged) {
    FunctionBounds bounds;
    if (!FunctionContaining(image, renderLevelPrep, bounds)) {
        return 0;
    }
    const unsigned char* const base = image.Base;
    for (std::uint32_t rva = bounds.Begin; rva + 5 <= bounds.End; ++rva) {
        if (base[rva] != kCallRel32) {
            continue;
        }
        std::int32_t displacement = 0;
        std::memcpy(&displacement, base + rva + 1, sizeof(displacement));
        const auto target = static_cast<std::uint32_t>(rva + 5 + displacement);
        const Flagged* const entry = FindFlagged(flagged, target);
        if (entry != nullptr && entry->HasHash) {
            cameraunlock::logging::Line("  setupCamera called from _renderLevelPrep+0x%X.",
                                        rva - bounds.Begin);
            return target;
        }
    }
    return 0;
}

// The one function that looks a CameraComponent up on the entity tagged as the
// RENDER camera: it carries both type hashes and indexes a 0x120-byte element.
// It takes the registry, so the mod cannot call it directly.
std::uint32_t FindRenderCameraAccessor(const ModuleImage& image,
                                       const std::vector<Flagged>& flagged) {
    std::uint32_t found = 0;
    for (const Flagged& entry : flagged) {
        if (!entry.HasHash || !entry.HasRenderHash ||
            entry.End - entry.Begin >= kMaxAccessorSize) {
            continue;
        }
        const FunctionBounds bounds{entry.Begin, entry.End};
        if (!ComputesComponentStride(image, bounds)) {
            continue;
        }
        if (found != 0) {
            cameraunlock::logging::Line(
                "  more than one render-camera accessor matched; not guessing.");
            return 0;
        }
        found = entry.Begin;
    }
    return found;
}

// The vtable slot a function dispatches through on its first argument, or zero.
std::uint32_t DispatchSlotOf(const ModuleImage& image, const FunctionBounds& bounds) {
    const unsigned char* const base = image.Base;
    const std::uint32_t limit =
        bounds.End < bounds.Begin + kPrologueWindow ? bounds.End : bounds.Begin + kPrologueWindow;
    for (std::uint32_t rva = bounds.Begin; rva + 10 <= limit; ++rva) {
        if (std::memcmp(base + rva, kMovRaxFromRcx, sizeof(kMovRaxFromRcx)) != 0 ||
            std::memcmp(base + rva + 3, kMovRaxVtable, sizeof(kMovRaxVtable)) != 0) {
            continue;
        }
        std::uint32_t slot = 0;
        std::memcpy(&slot, base + rva + 6, sizeof(slot));
        return slot;
    }
    return 0;
}

// The accessor's IClientInstance* wrapper, which is what the mod can actually
// call. Its other caller reaches the same accessor through an unrelated
// interface, so the two are told apart by which vtable they dispatch through:
// the wrapper's slot sits beside the ones the fairness gate already uses.
std::uint32_t FindRenderCameraGetter(const ModuleImage& image, std::uint32_t accessor,
                                     std::uint32_t clientInstanceSlot) {
    const unsigned char* const text = image.Base + image.TextRva;
    std::uint32_t found = 0;

    for (std::uint32_t i = 0; i + 5 <= image.TextSize; ++i) {
        if (text[i] != kCallRel32) {
            continue;
        }
        std::int32_t displacement = 0;
        std::memcpy(&displacement, text + i + 1, sizeof(displacement));
        const auto target = static_cast<std::uint32_t>(image.TextRva + i + 5 + displacement);
        if (target != accessor) {
            continue;
        }

        FunctionBounds bounds;
        if (!FunctionContaining(image, image.TextRva + i, bounds)) {
            continue;
        }
        const std::uint32_t slot = DispatchSlotOf(image, bounds);
        const std::uint32_t distance = slot > clientInstanceSlot ? slot - clientInstanceSlot
                                                                 : clientInstanceSlot - slot;
        if (slot == 0 || distance > kClientInstanceSlotWindow) {
            continue;
        }
        if (found != 0 && found != bounds.Begin) {
            cameraunlock::logging::Line(
                "  more than one IClientInstance wrapper matched; not guessing.");
            return 0;
        }
        found = bounds.Begin;
    }
    return found;
}

std::int32_t ReadDisplacement(const unsigned char* at) {
    std::int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

// Both crosshair addresses, or neither. Best-effort by contract: the caller
// carries on without them, and the crosshair simply stays centred.
void FindCrosshairPair(const ModuleImage& image, const std::vector<std::uint32_t>& rectHits,
                       ResolvedCode& out) {
    const unsigned char* const base = image.Base;
    std::uint32_t renderer = 0;
    std::uint32_t blit = 0;

    for (const std::uint32_t hit : rectHits) {
        const std::uint32_t store = hit + sizeof(kMovabs16x16);
        if (std::memcmp(base + store, kMovRbpRax, sizeof(kMovRbpRax)) != 0) {
            continue;
        }
        const std::int32_t rectSlot =
            ReadDisplacement(base + store + sizeof(kMovRbpRax)) - kRectOriginFromExtent;

        FunctionBounds bounds;
        if (!FunctionContaining(image, hit, bounds)) {
            continue;
        }

        for (std::uint32_t rva = bounds.Begin; rva + 7 <= bounds.End; ++rva) {
            if (std::memcmp(base + rva, kLeaR9Rbp, sizeof(kLeaR9Rbp)) != 0 ||
                ReadDisplacement(base + rva + sizeof(kLeaR9Rbp)) != rectSlot) {
                continue;
            }
            const std::uint32_t limit = rva + 7 + kMaxLeaToCall;
            for (std::uint32_t probe = rva + 7; probe + 5 <= bounds.End && probe < limit;
                 ++probe) {
                if (base[probe] != kCallRel32) {
                    continue;
                }
                const auto target =
                    static_cast<std::uint32_t>(probe + 5 + ReadDisplacement(base + probe + 1));
                if (renderer != 0 && (renderer != bounds.Begin || blit != target)) {
                    cameraunlock::logging::Line(
                        "  more than one crosshair renderer matched; leaving it centred.");
                    return;
                }
                renderer = bounds.Begin;
                blit = target;
                break;
            }
            break;
        }
    }

    if (renderer == 0 || blit == 0) {
        return;
    }
    out.HudCursorRender = renderer;
    out.UiBlit = blit;
}

bool Resolve(ResolvedCode& out, std::uint32_t clientInstanceSlot) {
    ModuleImage image;
    if (!MapRunningImage(image)) {
        cameraunlock::logging::Line("Could not read Minecraft.Windows.exe section headers.");
        return false;
    }

    const std::vector<std::uint32_t> targets = SignatureTargets(image);
    if (targets.empty()) {
        cameraunlock::logging::Line(
            "  could not find InGamePlayScreen::_renderLevelPrep by name in this build.");
        return false;
    }

    std::vector<std::uint32_t> referencing;
    std::vector<Flagged> flagged;
    std::vector<std::uint32_t> rectHits;
    ScanText(image, targets, referencing, flagged, rectHits);

    if (referencing.size() != 1) {
        cameraunlock::logging::Line(
            "  %zu functions reference the _renderLevelPrep signature; expected exactly one.",
            referencing.size());
        return false;
    }
    out.RenderLevelPrep = referencing[0];
    out.CameraSetup = FindCameraSetup(image, out.RenderLevelPrep, flagged);

    const std::uint32_t accessor = FindRenderCameraAccessor(image, flagged);
    if (accessor != 0) {
        out.GetRenderCameraComponent = FindRenderCameraGetter(image, accessor, clientInstanceSlot);
        cameraunlock::logging::Line("  render-camera accessor at 0x%08X.", accessor);
    }
    FindCrosshairPair(image, rectHits, out);

    cameraunlock::logging::Line(
        "  _renderLevelPrep=0x%08X setupCamera=0x%08X getRenderCameraComponent=0x%08X",
        out.RenderLevelPrep, out.CameraSetup, out.GetRenderCameraComponent);
    cameraunlock::logging::Line("  hudCursorRender=0x%08X uiBlit=0x%08X", out.HudCursorRender,
                                out.UiBlit);
    return out.Complete();
}

}  // namespace

bool ResolveCode(ResolvedCode& out, std::uint32_t clientInstanceSlot) {
    cameraunlock::logging::Line("Recovering camera addresses from the running image...");
    ResolvedCode resolved;
    bool ok = false;
    // The scan walks section headers and .pdata that the loader owns. A fault
    // there means the image is not the shape this reads, which is a reason to
    // stay dormant rather than to take the process down.
    __try {
        ok = Resolve(resolved, clientInstanceSlot);
    } __except (AccessViolationFilter(GetExceptionCode())) {
        cameraunlock::logging::Line("  faulted while scanning the image.");
        return false;
    }
    if (!ok) {
        return false;
    }
    out = resolved;
    return true;
}

}  // namespace mcht::builds
