#include "layout_resolver.h"

#include <windows.h>

#include <cstring>

#include "cameraunlock/logging/file_log.h"
#include "common/memory_probe.h"
#include "image_scan.h"

namespace mcht::builds {
namespace {

using mcht::memory::AccessViolationFilter;

// setupCamera reads nine floats through the component and takes one address.
// The cap is what keeps this allocation-free; overflowing it means the function
// is not the shape this reads and the resolve fails rather than truncating.
constexpr std::size_t kMaxAccesses = 64;

constexpr unsigned char kCallRel32 = 0xE8;

// The first two integer argument registers. The client instance is loaded
// straight into one of them on its way to the getter.
constexpr unsigned kRcx = 1;
constexpr unsigned kRdx = 2;

// How far into the function the prologue may park `this`.
constexpr std::uint32_t kPrologueWindow = 0x60;

// The half-angle the projection multiplies the field of view by before taking
// its tangent. Matching the constant is what identifies which of the four
// floats read together is the field of view.
constexpr float kHalfAngle = 0.5f;

// How far past the tangent call the aspect ratio's multiply may sit.
constexpr std::uint32_t kMaxCallToScale = 0x30;

bool IsRex(unsigned char b) { return b >= 0x40 && b <= 0x4F; }
bool IsRexW(unsigned char b) { return IsRex(b) && (b & 0x08) != 0; }

// REX.R and REX.B each supply the high bit of a register number, so a register
// is four bits wide and xmm10 is not xmm2 with a prefix ignored.
unsigned RexR(unsigned char b) { return IsRex(b) ? (b >> 2) & 1 : 0; }
unsigned RexB(unsigned char b) { return IsRex(b) ? b & 1 : 0; }

// A [base + displacement] operand. Every read this file matches takes that
// form, so the SIB, rip-relative and register-direct encodings are rejected
// rather than decoded: matching them would mean reading a displacement that is
// not a struct offset.
struct MemOperand {
    unsigned Reg;
    unsigned Base;
    std::int32_t Displacement;
    std::uint32_t Length;  // ModRM byte through the end of the operand
};

bool DecodeMemOperand(const unsigned char* modrm, unsigned rexR, unsigned rexB, MemOperand& out) {
    const unsigned mod = modrm[0] >> 6;
    const unsigned rm = modrm[0] & 7;
    if (mod == 3 || rm == 4 || (mod == 0 && rm == 5)) {
        return false;
    }
    out.Reg = ((modrm[0] >> 3) & 7) | (rexR << 3);
    out.Base = rm | (rexB << 3);
    if (mod == 0) {
        out.Displacement = 0;
        out.Length = 1;
    } else if (mod == 1) {
        out.Displacement = static_cast<std::int8_t>(modrm[1]);
        out.Length = 2;
    } else {
        std::memcpy(&out.Displacement, modrm + 1, sizeof(std::int32_t));
        out.Length = 5;
    }
    return true;
}

// One field read, and where the instruction after it starts. The follow-on
// address is what lets the projection reads be told apart by what is done with
// them rather than by which offset they happen to carry.
struct Access {
    std::uint32_t Site;
    std::int32_t Displacement;
    unsigned Reg;
    unsigned Base;
    std::uint32_t Next;
};

struct AccessList {
    Access Items[kMaxAccesses];
    std::size_t Count = 0;
    bool Overflowed = false;

    void Add(const Access& access) {
        if (Count == kMaxAccesses) {
            Overflowed = true;
            return;
        }
        Items[Count++] = access;
    }
};

// F3 [REX] 0F 10 /r - movss xmm, [base + disp]
bool MatchMovssLoad(const unsigned char* base, std::uint32_t rva, Access& out) {
    const unsigned char* const p = base + rva;
    if (p[0] != 0xF3) {
        return false;
    }
    const unsigned prefix = IsRex(p[1]) ? 1u : 0u;
    const unsigned char rex = prefix ? p[1] : 0;
    const unsigned char* const opcode = p + 1 + prefix;
    if (opcode[0] != 0x0F || opcode[1] != 0x10) {
        return false;
    }
    MemOperand mem;
    if (!DecodeMemOperand(opcode + 2, RexR(rex), RexB(rex), mem)) {
        return false;
    }
    out = {rva, mem.Displacement, mem.Reg, mem.Base, rva + 3 + prefix + mem.Length};
    return true;
}

// REX.W 8D /r - lea r64, [base + disp]
bool MatchLea(const unsigned char* base, std::uint32_t rva, Access& out) {
    const unsigned char* const p = base + rva;
    if (!IsRexW(p[0]) || p[1] != 0x8D) {
        return false;
    }
    MemOperand mem;
    if (!DecodeMemOperand(p + 2, RexR(p[0]), RexB(p[0]), mem)) {
        return false;
    }
    out = {rva, mem.Displacement, mem.Reg, mem.Base, rva + 2 + mem.Length};
    return true;
}

// REX.W 8B /r - mov r64, [base + disp]
bool MatchMovLoad(const unsigned char* base, std::uint32_t rva, Access& out) {
    const unsigned char* const p = base + rva;
    if (!IsRexW(p[0]) || p[1] != 0x8B) {
        return false;
    }
    MemOperand mem;
    if (!DecodeMemOperand(p + 2, RexR(p[0]), RexB(p[0]), mem)) {
        return false;
    }
    out = {rva, mem.Displacement, mem.Reg, mem.Base, rva + 2 + mem.Length};
    return true;
}

// The register the CameraComponent pointer ends up in.
//
// The registry walk finishes by scaling the component index by the class size:
// `lea rX, [rI + rI*8]` then `shl eX, 5` is index * 0x120, and the `add` that
// applies it names the pointer. The three are matched by shape rather than by
// byte pattern because the register allocation differs between builds that
// compile the same source - 20260806 used rdx/rax where 20260812 used rcx/rdi.
//
// Zero when nothing matched or when two different registers did, either of
// which means the component pointer is not identified and nothing may be read
// through it.
unsigned FindComponentRegister(const ModuleImage& image, const FunctionBounds& bounds) {
    const unsigned char* const base = image.Base;
    unsigned found = 0;
    bool ambiguous = false;

    for (std::uint32_t rva = bounds.Begin; rva + 16 <= bounds.End; ++rva) {
        const unsigned char* const p = base + rva;
        if (!IsRexW(p[0]) || p[1] != 0x8D || (p[2] & 0xC7) != 0x04) {
            continue;
        }
        const unsigned char sib = p[3];
        if ((sib >> 6) != 3 || ((sib >> 3) & 7) != (sib & 7)) {
            continue;
        }
        const unsigned scaled = ((p[2] >> 3) & 7) | (RexR(p[0]) << 3);

        std::uint32_t afterShift = 0;
        for (std::uint32_t probe = rva + 4; probe + 3 <= bounds.End && probe < rva + 24; ++probe) {
            const unsigned prefix = IsRex(base[probe]) ? 1u : 0u;
            const unsigned char* const shl = base + probe + prefix;
            if (shl[0] != 0xC1 || (shl[1] & 0xC0) != 0xC0 || (shl[1] & 0x38) != 0x20 ||
                shl[2] != 0x05) {
                continue;
            }
            if (((shl[1] & 7) | (RexB(base[probe]) << 3)) != scaled) {
                continue;
            }
            afterShift = probe + prefix + 3;
            break;
        }
        if (afterShift == 0) {
            continue;
        }

        for (std::uint32_t probe = afterShift; probe + 3 <= bounds.End && probe < afterShift + 24;
             ++probe) {
            const unsigned char* const add = base + probe;
            if (!IsRexW(add[0]) || add[1] != 0x01 || (add[2] & 0xC0) != 0xC0) {
                continue;
            }
            if ((((add[2] >> 3) & 7) | (RexR(add[0]) << 3)) != scaled) {
                continue;
            }
            const unsigned component = (add[2] & 7) | (RexB(add[0]) << 3);
            ambiguous = ambiguous || (found != 0 && found != component);
            found = component;
            break;
        }
    }

    if (ambiguous) {
        cameraunlock::logging::Line(
            "  the component pointer lands in more than one register; not guessing.");
        return 0;
    }
    return found;
}

// The register the prologue parks `this` in, which is where the client
// instance is loaded from. Callers that never move it keep it in rcx.
unsigned FindThisRegister(const ModuleImage& image, const FunctionBounds& bounds) {
    const unsigned char* const base = image.Base;
    const std::uint32_t limit =
        bounds.End < bounds.Begin + kPrologueWindow ? bounds.End : bounds.Begin + kPrologueWindow;
    for (std::uint32_t rva = bounds.Begin; rva + 3 <= limit; ++rva) {
        const unsigned char* const p = base + rva;
        // REX.W 89 /r with a register destination - mov r64, rcx.
        if (!IsRexW(p[0]) || p[1] != 0x89 || (p[2] & 0xC0) != 0xC0) {
            continue;
        }
        if ((((p[2] >> 3) & 7) | (RexR(p[0]) << 3)) != kRcx) {
            continue;
        }
        return (p[2] & 7) | (RexB(p[0]) << 3);
    }
    return kRcx;
}

// The client instance's offset within LevelRendererPlayer: a pointer member
// loaded out of `this` into an argument register and handed straight to a
// call. setupCamera does this twice, at both of the points it needs the
// camera, and the two must agree - a build where they do not is one where this
// rule no longer identifies the member.
std::uint32_t FindClientInstanceOffset(const ModuleImage& image, const FunctionBounds& bounds,
                                       unsigned thisRegister) {
    const unsigned char* const base = image.Base;
    std::uint32_t found = 0;
    unsigned sites = 0;

    for (std::uint32_t rva = bounds.Begin; rva + 12 <= bounds.End; ++rva) {
        Access access;
        if (!MatchMovLoad(base, rva, access) || access.Base != thisRegister ||
            (access.Reg != kRcx && access.Reg != kRdx)) {
            continue;
        }
        if (base[access.Next] != kCallRel32) {
            continue;
        }
        if (found != 0 && found != static_cast<std::uint32_t>(access.Displacement)) {
            cameraunlock::logging::Line(
                "  the renderer's client instance is loaded from two different offsets; not "
                "guessing.");
            return 0;
        }
        found = static_cast<std::uint32_t>(access.Displacement);
        ++sites;
    }

    if (found != 0) {
        cameraunlock::logging::Line(
            "  client instance at renderer +0x%X, from %u call site%s.", found, sites,
            sites == 1 ? "" : "s that agree");
    }
    return found;
}

// Every field read through the component, in address order.
void CollectAccesses(const ModuleImage& image, const FunctionBounds& bounds, unsigned component,
                     AccessList& floats, AccessList& addresses) {
    const unsigned char* const base = image.Base;
    for (std::uint32_t rva = bounds.Begin; rva + 8 <= bounds.End; ++rva) {
        Access access;
        if (MatchMovssLoad(base, rva, access) && access.Base == component) {
            floats.Add(access);
        }
        if (MatchLea(base, rva, access) && access.Base == component) {
            addresses.Add(access);
        }
    }
}

// The orientation quaternion: the lowest offset read as four consecutive
// floats. Reading all four is what separates the pose from the loose floats
// beside it, which are read singly.
std::uint32_t FindOrientation(const AccessList& floats) {
    std::uint32_t found = 0;
    for (std::size_t i = 0; i < floats.Count; ++i) {
        const std::int32_t candidate = floats.Items[i].Displacement;
        bool complete = true;
        for (std::int32_t lane = 1; lane < 4; ++lane) {
            bool seen = false;
            for (std::size_t j = 0; j < floats.Count; ++j) {
                seen = seen || floats.Items[j].Displacement == candidate + lane * 4;
            }
            complete = complete && seen;
        }
        if (!complete) {
            continue;
        }
        const auto offset = static_cast<std::uint32_t>(candidate);
        if (found == 0 || offset < found) {
            found = offset;
        }
    }
    return found;
}

// F3 [REX] 0F 59 /r with a rip-relative operand - mulss xmm, [rip + disp32].
// Returns the constant's address and where the instruction ends.
bool MatchMulssRipConstant(const unsigned char* base, std::uint32_t rva, unsigned reg,
                           std::uint32_t& constantOut, std::uint32_t& nextOut) {
    const unsigned char* const p = base + rva;
    if (p[0] != 0xF3) {
        return false;
    }
    const unsigned prefix = IsRex(p[1]) ? 1u : 0u;
    const unsigned char rex = prefix ? p[1] : 0;
    const unsigned char* const opcode = p + 1 + prefix;
    if (opcode[0] != 0x0F || opcode[1] != 0x59 || (opcode[2] & 0xC7) != 0x05) {
        return false;
    }
    if ((((opcode[2] >> 3) & 7) | (RexR(rex) << 3)) != reg) {
        return false;
    }
    std::int32_t displacement = 0;
    std::memcpy(&displacement, opcode + 3, sizeof(displacement));
    nextOut = rva + 3 + prefix + 5;
    constantOut = static_cast<std::uint32_t>(nextOut + displacement);
    return true;
}

// F3 [REX] 0F 59 /r between two registers - mulss xmmDest, xmmSource.
bool MatchMulssRegisters(const unsigned char* base, std::uint32_t rva, unsigned& destOut,
                         unsigned& sourceOut) {
    const unsigned char* const p = base + rva;
    if (p[0] != 0xF3) {
        return false;
    }
    const unsigned prefix = IsRex(p[1]) ? 1u : 0u;
    const unsigned char rex = prefix ? p[1] : 0;
    const unsigned char* const opcode = p + 1 + prefix;
    if (opcode[0] != 0x0F || opcode[1] != 0x59 || (opcode[2] & 0xC0) != 0xC0) {
        return false;
    }
    destOut = ((opcode[2] >> 3) & 7) | (RexR(rex) << 3);
    sourceOut = (opcode[2] & 7) | (RexB(rex) << 3);
    return true;
}

// The two projection inputs, told apart by what the code does with them rather
// than by where they sit.
//
// setupCamera builds the projection as 1/(aspect * tan(fov/2)) and 1/tan(fov/2),
// so the field of view is the float halved and handed to the tangent, and the
// aspect ratio is the float that tangent's result is then multiplied by. The
// four floats read in that block are otherwise indistinguishable: two of them
// are the near and far planes, read the same way, one instruction apart.
//
// The half-angle constant is checked rather than assumed, because it is what
// makes "this float is an angle" evidence instead of a guess.
void FindProjectionFields(const ModuleImage& image, const FunctionBounds& bounds,
                          const AccessList& floats, std::uint32_t& aspectOut,
                          std::uint32_t& fovOut) {
    const unsigned char* const base = image.Base;

    for (std::size_t i = 0; i < floats.Count; ++i) {
        const Access& fov = floats.Items[i];
        std::uint32_t constant = 0;
        std::uint32_t afterMultiply = 0;
        if (!MatchMulssRipConstant(base, fov.Next, fov.Reg, constant, afterMultiply)) {
            continue;
        }
        if (constant < image.RdataRva ||
            constant + sizeof(float) > image.RdataRva + image.RdataSize) {
            continue;
        }
        float half = 0.0f;
        std::memcpy(&half, base + constant, sizeof(half));
        if (half != kHalfAngle) {
            continue;
        }
        // call qword ptr [rip + disp32] - the tangent, reached through the
        // import table, so there is no name to match and the surrounding
        // arithmetic is the identification.
        if (base[afterMultiply] != 0xFF || base[afterMultiply + 1] != 0x15) {
            continue;
        }

        const std::uint32_t limit = afterMultiply + 6 + kMaxCallToScale;
        for (std::uint32_t rva = afterMultiply + 6; rva + 5 <= bounds.End && rva < limit; ++rva) {
            unsigned dest = 0;
            unsigned source = 0;
            if (!MatchMulssRegisters(base, rva, dest, source) || source != fov.Reg) {
                continue;
            }
            // The aspect ratio is whatever was last loaded into the register
            // the tangent is being scaled into.
            for (std::size_t j = 0; j < floats.Count; ++j) {
                const Access& candidate = floats.Items[j];
                if (candidate.Reg == dest && candidate.Site < rva) {
                    aspectOut = static_cast<std::uint32_t>(candidate.Displacement);
                }
            }
            break;
        }
        fovOut = static_cast<std::uint32_t>(fov.Displacement);
        return;
    }
}

bool Resolve(std::uint32_t cameraSetupRva, ResolvedLayout& out) {
    ModuleImage image;
    if (!MapRunningImage(image)) {
        cameraunlock::logging::Line("  could not read the image's section headers.");
        return false;
    }
    FunctionBounds bounds;
    if (!FunctionContaining(image, cameraSetupRva, bounds)) {
        cameraunlock::logging::Line("  setupCamera has no .pdata entry; cannot read its code.");
        return false;
    }

    out.ClientInstance =
        FindClientInstanceOffset(image, bounds, FindThisRegister(image, bounds));

    const unsigned component = FindComponentRegister(image, bounds);
    if (component == 0) {
        cameraunlock::logging::Line(
            "  could not follow setupCamera to the camera component; nothing read.");
        return false;
    }

    AccessList floats;
    AccessList addresses;
    CollectAccesses(image, bounds, component, floats, addresses);
    if (floats.Overflowed || addresses.Overflowed) {
        cameraunlock::logging::Line(
            "  setupCamera reads more camera fields than this recognises; not guessing.");
        return false;
    }

    out.Orientation = FindOrientation(floats);
    FindProjectionFields(image, bounds, floats, out.AspectRatio, out.FieldOfView);

    // The post-view transform is the only field setupCamera takes the ADDRESS
    // of, because it is the only one handed to the matrix helpers rather than
    // loaded. More than one would mean that is no longer true.
    if (addresses.Count == 1) {
        out.PostViewTransform = static_cast<std::uint32_t>(addresses.Items[0].Displacement);
    } else if (addresses.Count > 1) {
        cameraunlock::logging::Line(
            "  setupCamera takes the address of %zu camera fields; expected exactly one.",
            addresses.Count);
    }

    cameraunlock::logging::Line(
        "  camera layout: clientInstance=0x%X orientation=0x%X aspect=0x%X fov=0x%X "
        "postViewTransform=0x%X",
        out.ClientInstance, out.Orientation, out.AspectRatio, out.FieldOfView,
        out.PostViewTransform);
    return out.Complete();
}

}  // namespace

bool ResolveLayout(std::uint32_t cameraSetupRva, ResolvedLayout& out) {
    cameraunlock::logging::Line("Recovering the camera layout from setupCamera...");
    ResolvedLayout resolved;
    bool ok = false;
    // The walk reads .pdata and instruction bytes the loader owns. A fault
    // there means the image is not the shape this reads, which is a reason to
    // stay dormant rather than to take the process down.
    __try {
        ok = Resolve(cameraSetupRva, resolved);
    } __except (AccessViolationFilter(GetExceptionCode())) {
        cameraunlock::logging::Line("  faulted while reading setupCamera.");
        return false;
    }
    if (!ok) {
        return false;
    }
    out = resolved;
    return true;
}

}  // namespace mcht::builds
