// Loader for Minecraft: Bedrock Edition.
//
// Every other mod in this catalogue is loaded by dropping a proxy DLL next to
// the game EXE. Bedrock cannot be loaded that way: it installs under
// C:\Program Files\WindowsApps, owned by TrustedInstaller and signature-checked
// as a package, so nothing can be added beside the executable. What Bedrock
// does allow is ordinary injection - its manifest declares
// Windows.FullTrustApplication with the runFullTrust capability, so the game
// runs as a normal full-trust desktop process rather than in an AppContainer
// sandbox.
//
// So this launcher takes the loader's place: it activates the Store package,
// waits for the game process, and injects the mod DLL. Run it instead of the
// game's own shortcut.

#include <windows.h>

#include <psapi.h>
#include <shobjidl_core.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include "common/bounds.h"
#include "common/module_path.h"

namespace {

// Release and Preview ship as separate packages with separate identities.
constexpr wchar_t kReleaseAumid[] = L"Microsoft.MinecraftUWP_8wekyb3d8bbwe!Game";
constexpr wchar_t kPreviewAumid[] = L"Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe!Game";
constexpr wchar_t kGameProcess[] = L"Minecraft.Windows.exe";
constexpr wchar_t kModDll[] = L"MinecraftHeadTracking.dll";

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitBadArgs = 2;

struct Options {
    bool attach = false;      // Inject into an already-running game instead of launching.
    bool preview = false;     // Target the Preview package.
    std::wstring dllPath;     // Defaults to the mod DLL beside this executable.
    DWORD waitSeconds = 120;  // How long to wait for the game to come up.
};

void Fail(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::fputs("ERROR: ", stderr);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
    va_end(args);
}

// Every PID whose image name matches. EnumProcesses cannot report truncation,
// so the buffer is grown until it comes back short: a full one means the list
// was cut, and the game sitting past the cut would be reported as "not
// running".
std::vector<DWORD> FindGameProcesses() {
    std::vector<DWORD> pids(4096);
    DWORD needed = 0;
    for (;;) {
        if (!EnumProcesses(pids.data(), static_cast<DWORD>(pids.size() * sizeof(DWORD)),
                           &needed)) {
            return {};
        }
        if (needed < pids.size() * sizeof(DWORD)) {
            break;
        }
        pids.resize(pids.size() * 2);
    }

    std::vector<DWORD> found;
    const DWORD count = needed / sizeof(DWORD);
    for (DWORD i = 0; i < count; ++i) {
        const HANDLE process =
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!process) {
            continue;
        }
        wchar_t name[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, name, &size)) {
            const wchar_t* leaf = std::wcsrchr(name, L'\\');
            leaf = leaf ? leaf + 1 : name;
            if (_wcsicmp(leaf, kGameProcess) == 0) {
                found.push_back(pids[i]);
            }
        }
        CloseHandle(process);
    }
    return found;
}

// The one match, or 0 when there is more than one. Bedrock only ever runs one
// instance, so more than one means something is wrong and we say so rather
// than guessing which to inject: picking one would mean loading a DLL into a
// process we have not identified beyond its file name.
DWORD SoleGameProcess(const std::vector<DWORD>& matches) {
    if (matches.size() > 1) {
        Fail("%zu processes are named %S. Close all but the one you want to mod and try again.",
             matches.size(), kGameProcess);
        return 0;
    }
    return matches.empty() ? 0 : matches[0];
}

// The game once it appears, or 0. Both ways of failing are reported here, so
// the caller adds nothing: a crowd of processes is not "the game did not
// start", and saying both would contradict itself.
DWORD WaitForGameProcess(DWORD timeoutSeconds) {
    const ULONGLONG deadline = GetTickCount64() + timeoutSeconds * 1000ULL;
    while (GetTickCount64() < deadline) {
        const std::vector<DWORD> matches = FindGameProcesses();
        if (!matches.empty()) {
            // Returns rather than polling on, so the too-many message is said
            // once instead of on every poll for the whole timeout.
            return SoleGameProcess(matches);
        }
        Sleep(250);
    }
    Fail("%S did not start within %lu seconds", kGameProcess, timeoutSeconds);
    return 0;
}

bool IsModuleLoaded(HANDLE process, const wchar_t* moduleName) {
    std::vector<HMODULE> modules(2048);
    DWORD needed = 0;
    if (!EnumProcessModulesEx(process, modules.data(),
                              static_cast<DWORD>(modules.size() * sizeof(HMODULE)), &needed,
                              LIST_MODULES_ALL)) {
        return false;
    }
    const DWORD count = needed / sizeof(HMODULE);
    for (DWORD i = 0; i < count && i < modules.size(); ++i) {
        wchar_t name[MAX_PATH];
        if (GetModuleBaseNameW(process, modules[i], name, MAX_PATH) &&
            _wcsicmp(name, moduleName) == 0) {
            return true;
        }
    }
    return false;
}

// How long to give LoadLibraryW inside the game. Generous: it runs the mod's
// DllMain, which spawns the bootstrap thread and returns, but the loader lock
// may be held by the game's own startup for a while first.
constexpr DWORD kRemoteLoadTimeoutMs = 30000;

// Runs LoadLibraryW(remotePath) on a thread inside the target. kernel32 is
// mapped at the same address in every process in a session, so our own
// LoadLibraryW pointer is valid there.
//
// `remotePathIsFree` says whether the page holding the path may be released.
// It is cleared only when a remote thread is left running: LoadLibraryW is
// still reading that string, so freeing it would fault inside Minecraft rather
// than here. One leaked page in a process we have just failed to mod is the
// cheaper end of that trade.
bool CallRemoteLoadLibrary(HANDLE process, void* remotePath, const std::wstring& dllPath,
                           bool& remotePathIsFree) {
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    const auto loadLibrary =
        reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
    if (!loadLibrary) {
        Fail("could not resolve LoadLibraryW");
        return false;
    }

    const HANDLE thread =
        CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr);
    if (!thread) {
        Fail("CreateRemoteThread failed (Win32 %lu)", GetLastError());
        return false;
    }

    // Checked, because a timeout leaves GetExitCodeThread reporting
    // STILL_ACTIVE - a non-zero value that reads exactly like the HMODULE a
    // successful load returns.
    const bool finished = WaitForSingleObject(thread, kRemoteLoadTimeoutMs) == WAIT_OBJECT_0;
    DWORD exitCode = 0;
    if (finished) {
        GetExitCodeThread(thread, &exitCode);
    }
    CloseHandle(thread);

    if (!finished) {
        remotePathIsFree = false;
        Fail("LoadLibraryW did not finish inside the game within %lu seconds",
             kRemoteLoadTimeoutMs / 1000);
        return false;
    }

    // The remote thread returns the HMODULE LoadLibraryW produced, truncated
    // to 32 bits. Zero means the load genuinely failed; non-zero is only a
    // hint, so the caller confirms against the module list.
    if (exitCode == 0) {
        Fail("LoadLibraryW rejected %S inside the game process", dllPath.c_str());
        return false;
    }
    return true;
}

// Writes the DLL path into the target and loads it from there. The allocation
// is the caller's to release, which is why it is not made here.
bool LoadModuleInProcess(HANDLE process, const std::wstring& dllPath, void* remotePath,
                         SIZE_T pathBytes, bool& remotePathIsFree) {
    if (!WriteProcessMemory(process, remotePath, dllPath.c_str(), pathBytes, nullptr)) {
        Fail("WriteProcessMemory failed (Win32 %lu)", GetLastError());
        return false;
    }
    if (!CallRemoteLoadLibrary(process, remotePath, dllPath, remotePathIsFree)) {
        return false;
    }
    if (!IsModuleLoaded(process, kModDll)) {
        Fail("%S did not appear in the game's module list after injection", kModDll);
        return false;
    }
    return true;
}

bool InjectDll(DWORD pid, const std::wstring& dllPath) {
    const HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                           PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                       FALSE, pid);
    if (!process) {
        Fail("could not open Minecraft process %lu (Win32 %lu). Is it running as a different user?",
             pid, GetLastError());
        return false;
    }

    bool ok = false;
    if (IsModuleLoaded(process, kModDll)) {
        std::printf("Mod is already loaded in pid %lu; nothing to do.\n", pid);
        ok = true;
    } else {
        const SIZE_T pathBytes = (dllPath.size() + 1) * sizeof(wchar_t);
        void* const remotePath =
            VirtualAllocEx(process, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remotePath) {
            Fail("VirtualAllocEx failed (Win32 %lu)", GetLastError());
        } else {
            bool remotePathIsFree = true;
            ok = LoadModuleInProcess(process, dllPath, remotePath, pathBytes, remotePathIsFree);
            if (remotePathIsFree) {
                VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
            }
        }
    }

    CloseHandle(process);
    return ok;
}

bool ActivatePackage(const wchar_t* aumid, DWORD& pidOut) {
    IApplicationActivationManager* manager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER,
                                  IID_PPV_ARGS(&manager));
    if (FAILED(hr)) {
        Fail("could not create the app activation manager (HRESULT 0x%08lX)", hr);
        return false;
    }

    DWORD pid = 0;
    hr = manager->ActivateApplication(aumid, nullptr, AO_NONE, &pid);
    manager->Release();

    if (FAILED(hr)) {
        Fail("could not launch %S (HRESULT 0x%08lX). Is Minecraft installed from the Microsoft Store?",
             aumid, hr);
        return false;
    }
    pidOut = pid;
    return true;
}

bool ParseArgs(int argc, wchar_t** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--attach") {
            options.attach = true;
        } else if (arg == L"--preview") {
            options.preview = true;
        } else if (arg == L"--dll" && i + 1 < argc) {
            options.dllPath = argv[++i];
        } else if (arg == L"--wait" && i + 1 < argc) {
            // Unchecked, a negative value wraps into a deadline decades away
            // and the launcher waits for a game that never comes instead of
            // reporting it.
            unsigned long seconds = 0;
            if (!mcht::bounds::ParseWaitSeconds(argv[++i], seconds)) {
                Fail("--wait must be a whole number of seconds between %lu and %lu",
                     mcht::bounds::kMinWaitSeconds, mcht::bounds::kMaxWaitSeconds);
                return false;
            }
            options.waitSeconds = static_cast<DWORD>(seconds);
        } else {
            Fail("unrecognised argument \"%S\"", arg.c_str());
            std::fputs("Usage: MinecraftHeadTrackingLauncher [--attach] [--preview] "
                       "[--dll <path>] [--wait <seconds>]\n", stderr);
            return false;
        }
    }
    if (options.dllPath.empty()) {
        options.dllPath = mcht::paths::DirectoryOfModule(nullptr) + kModDll;
    }
    return true;
}

// The game to inject into, launching it first unless it is already up, or 0
// when it could not be reached. Every failure has already been reported.
DWORD ResolveTargetProcess(const Options& options) {
    const std::vector<DWORD> matches = FindGameProcesses();
    if (!matches.empty()) {
        // SoleGameProcess has already said why it refused a crowd; saying
        // "Minecraft is not running" on top of that would contradict it.
        const DWORD running = SoleGameProcess(matches);
        if (running && !options.attach) {
            std::printf("Minecraft is already running (pid %lu); attaching to it.\n", running);
        }
        return running;
    }
    if (options.attach) {
        Fail("--attach was given but Minecraft is not running");
        return 0;
    }

    const wchar_t* aumid = options.preview ? kPreviewAumid : kReleaseAumid;
    std::printf("Launching %S ...\n", aumid);
    DWORD helperPid = 0;
    if (!ActivatePackage(aumid, helperPid)) {
        return 0;
    }
    // ActivateApplication returns the launch helper's pid; the game itself is
    // a separate process it spawns, so wait for that.
    std::printf("Waiting for %S (up to %lus) ...\n", kGameProcess, options.waitSeconds);
    return WaitForGameProcess(options.waitSeconds);
}

// Injecting before the process has finished its own initialisation can
// deadlock on the loader lock, so wait until it is pumping messages.
void WaitUntilGameIsIdle(DWORD pid, DWORD waitSeconds) {
    const HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) {
        return;
    }
    WaitForInputIdle(process, waitSeconds * 1000);
    CloseHandle(process);
}

// Runs with COM already initialised, which is what ActivatePackage needs.
int Run(const Options& options) {
    const DWORD pid = ResolveTargetProcess(options);
    if (!pid) {
        return kExitError;
    }

    std::printf("Game process: pid %lu\n", pid);
    WaitUntilGameIsIdle(pid, options.waitSeconds);

    std::printf("Injecting %S ...\n", options.dllPath.c_str());
    if (!InjectDll(pid, options.dllPath)) {
        return kExitError;
    }

    std::printf("Head tracking loaded. See MinecraftHeadTracking.log beside the DLL.\n");
    return kExitOk;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    if (!ParseArgs(argc, argv, options)) {
        return kExitBadArgs;
    }

    std::printf("MinecraftHeadTracking launcher %s (%s)\n", MCHT_MOD_VERSION, MCHT_GIT_SHA);

    if (GetFileAttributesW(options.dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Fail("mod DLL not found at %S", options.dllPath.c_str());
        return kExitError;
    }

    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comInit)) {
        Fail("CoInitializeEx failed (HRESULT 0x%08lX)", comInit);
        return kExitError;
    }

    const int exitCode = Run(options);

    CoUninitialize();
    return exitCode;
}
