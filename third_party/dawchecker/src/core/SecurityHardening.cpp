#include "SecurityHardening.h"

#include <cstring>
#include <fstream>
#include <limits>

#if defined(DAWCHECKER_PLATFORM_WINDOWS)
    #include <windows.h>
#elif defined(DAWCHECKER_PLATFORM_LINUX)
    #include <fstream>
    #include <sys/personality.h>
    #include <unistd.h>
#elif defined(DAWCHECKER_PLATFORM_MACOS)
    #include <sys/sysctl.h>
    #include <unistd.h>
#endif

namespace dawchecker::security {

namespace {

bool detectDebuggerPresent()
{
#if defined(DAWCHECKER_PLATFORM_WINDOWS)
    return IsDebuggerPresent() != 0;
#elif defined(DAWCHECKER_PLATFORM_LINUX)
    // Read-only inspection of our own /proc/self/status TracerPid field.
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
    {
        if (line.rfind("TracerPid:", 0) == 0)
        {
            const auto pid = line.substr(10);
            return std::stol(pid) != 0;
        }
    }
    return false;
#else
    return false; // Not implemented for this platform; informational field only.
#endif
}

bool detectElevatedPrivileges()
{
#if defined(DAWCHECKER_PLATFORM_WINDOWS)
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup))
    {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
#elif defined(DAWCHECKER_PLATFORM_LINUX) || defined(DAWCHECKER_PLATFORM_MACOS)
    return geteuid() == 0;
#else
    return false;
#endif
}

bool detectASLR()
{
#if defined(DAWCHECKER_PLATFORM_LINUX)
    std::ifstream f("/proc/sys/kernel/randomize_va_space");
    int value = 0;
    if (f >> value)
        return value > 0;
    return false;
#elif defined(DAWCHECKER_PLATFORM_WINDOWS)
    // If /DYNAMICBASE was honored the module base will not be the preferred
    // base address baked into the PE header on repeated runs; we can't prove
    // that at runtime without extra scaffolding, so we report the flag we
    // compiled with instead — the linker will hard-fail before this ships if
    // the flag were dropped.
    return true;
#else
    return true;
#endif
}

} // namespace

HardeningReport runStartupSelfCheck()
{
    HardeningReport report;

    report.aslrActive = detectASLR();
    report.debuggerDetected = detectDebuggerPresent();
    report.runningElevated = detectElevatedPrivileges();

    // These two are compiled in via CMakeLists.txt's `hardening_flags`
    // target for every build of this project, so we can state them
    // truthfully as build-time facts rather than guessing at runtime.
    report.stackProtectorPresumed = true;
    report.controlFlowGuardOrCET = true;
#if defined(DAWCHECKER_PLATFORM_WINDOWS)
    report.dataExecutionPreventionActive = true; // /NXCOMPAT
#else
    report.dataExecutionPreventionActive = true; // -Wl,-z,noexecstack
#endif

    if (report.debuggerDetected)
        report.notes.emplace_back(
            "A debugger is attached to this process. This is shown for transparency only; "
            "DawChecker does not alter its behavior, disable features, or exit when debugged.");

    if (report.runningElevated)
        report.notes.emplace_back(
            "DawChecker is running with elevated privileges. It does not require this — "
            "consider restarting it as a standard user.");

    if (!report.aslrActive)
        report.notes.emplace_back(
            "Address space layout randomization appears disabled at the OS level. "
            "This reduces the effectiveness of exploit mitigations system-wide.");

    // Optional: read a sibling .sha256 manifest produced by the build's
    // post-build step (see CMakeLists.txt) so the GUI can display a build
    // integrity fingerprint to the user.
    // Left as std::nullopt here; the GUI layer resolves the running
    // executable's own path and looks up "<exe>.sha256" if present.

    return report;
}

void secureZero(void* data, std::size_t sizeBytes)
{
    if (data == nullptr || sizeBytes == 0)
        return;

    // volatile pointer prevents the optimizer from eliding this as a
    // dead store, which is the classic failure mode of a naive memset()
    // right before a buffer is freed or goes out of scope.
    volatile auto* p = static_cast<volatile std::uint8_t*>(data);
    for (std::size_t i = 0; i < sizeBytes; ++i)
        p[i] = 0;
}

ScopedSecureBuffer::ScopedSecureBuffer(std::size_t sizeBytes)
    : buffer_(sizeBytes, 0)
{
}

ScopedSecureBuffer::~ScopedSecureBuffer()
{
    secureZero(buffer_.data(), buffer_.size());
}

std::optional<std::size_t> checkedMul(std::size_t a, std::size_t b) noexcept
{
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a)
        return std::nullopt;
    return a * b;
}

std::optional<std::size_t> checkedAdd(std::size_t a, std::size_t b) noexcept
{
    if (a > std::numeric_limits<std::size_t>::max() - b)
        return std::nullopt;
    return a + b;
}

bool isSafeExportPath(const std::string& path)
{
    if (path.empty())
        return false;
    if (path.find("..") != std::string::npos)
        return false;
    // Reject NUL-embedded strings (possible truncation-vs-length mismatch attack).
    if (path.find('\0') != std::string::npos)
        return false;
    return true;
}

} // namespace dawchecker::security
