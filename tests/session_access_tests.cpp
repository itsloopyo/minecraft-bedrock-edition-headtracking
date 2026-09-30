#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "builds/code_contract.h"
#include "builds/session_access.h"

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
}

int main() {
    using namespace mcht::builds;
    std::array<unsigned char, 8> bytes{1, 2, 3, 4, 5, 6, 7, 8};
    const CodeContract contract{8, 0x147c623dc0db4c6dULL, {{2, 4}}};
    Check(MatchesContract(bytes.data(), bytes.size(), contract), "Original contract rejected");
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] ^= 0xff;
        Check(MatchesContract(bytes.data(), bytes.size(), contract) == (i >= 2 && i < 6),
              "Mutation outside relocation mask accepted, or relocation rejected");
        bytes[i] ^= 0xff;
    }
    Check(!MatchesContract(bytes.data(), 7, contract), "Truncated function accepted");

    std::array<unsigned char, 512> memory{};
    ModuleImage image;
    image.Base = memory.data();
    image.TextSize = 128;
    image.RdataRva = 256;
    image.RdataSize = 256;
    ClientAccess client;
    LevelAccess level;
    std::uint32_t member = 0;
    Check(!ResolveClientAccess(image, memory.data() + 256, client), "Empty vtable accepted");
    Check(!ResolveLevelAccess(image, memory.data() + 256, client, level), "Empty level accepted");
    Check(!ResolveClientAccess(image, memory.data(), client), "Code used as a vtable");
    Check(!ResolveServerAccess(image, memory.data() + 256, 1, member), "Unaligned slot accepted");

    // cmp qword ptr [rcx+disp32], 0; setne al; ret, assembled for this test.
    const unsigned char getter[]{0x48, 0x83, 0xb9, 0x34, 0x12, 0, 0, 0, 0x0f, 0x95, 0xc0, 0xc3};
    std::memcpy(memory.data(), getter, sizeof(getter));
    const unsigned char* target = memory.data();
    std::memcpy(memory.data() + 256, &target, sizeof(target));
    Check(ResolveServerAccess(image, memory.data() + 256, 0, member) && member == 0x1234,
          "Validated member was not extracted");
    memory[3] = 0x78;
    Check(ResolveServerAccess(image, memory.data() + 256, 0, member) && member == 0x1278,
          "Compatible field movement rejected");
    memory[9] = 0x94;
    Check(!ResolveServerAccess(image, memory.data() + 256, 0, member), "Inverted predicate accepted");
    memory[9] = 0x95;
    image.TextSize = 11;
    Check(!ResolveServerAccess(image, memory.data() + 256, 0, member), "Function crosses code boundary");
    target = memory.data() + 128;
    std::memcpy(memory.data() + 256, &target, sizeof(target));
    Check(!ResolveServerAccess(image, memory.data() + 256, 0, member), "Non-code target accepted");
    std::puts("Session access contract tests passed.");
}
