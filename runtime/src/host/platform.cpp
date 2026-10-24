#include "host/platform.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <timeapi.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

namespace cafe::host {

namespace fs = std::filesystem;

std::string utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

#ifdef _WIN32

namespace {

void set_errno_from_last_error() {
    switch (GetLastError()) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: errno = ENOENT; break;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: case ERROR_WRITE_PROTECT: errno = EACCES; break;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: errno = ENOSPC; break;
    case ERROR_TOO_MANY_OPEN_FILES: errno = EMFILE; break;
    default: errno = EIO; break;
    }
}

HANDLE handle_of(int fd) { return reinterpret_cast<HANDLE>(_get_osfhandle(fd)); }

// ReadFile and WriteFile take at most 4 GiB less one byte per call.
DWORD chunk(size_t size) { return static_cast<DWORD>(std::min<size_t>(size, 1u << 30)); }

fs::path environment(const wchar_t* variable) {
    const wchar_t* v = _wgetenv(variable);
    return v && *v ? fs::path(v) : fs::path();
}

} // namespace

int file_open(const fs::path& path, int flags, int mode) {
    return _wopen(path.c_str(), flags | _O_BINARY | _O_NOINHERIT,
                  (mode & 0200) ? (_S_IREAD | _S_IWRITE) : _S_IREAD);
}

int file_close(int fd) { return _close(fd); }

int64_t file_pread(int fd, void* buffer, size_t size, uint64_t offset) {
    OVERLAPPED at{};
    at.Offset = static_cast<DWORD>(offset);
    at.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD n = 0;
    if (!ReadFile(handle_of(fd), buffer, chunk(size), &n, &at)) {
        if (GetLastError() == ERROR_HANDLE_EOF) return 0;
        set_errno_from_last_error();
        return -1;
    }
    return n;
}

int64_t file_pwrite(int fd, const void* buffer, size_t size, uint64_t offset) {
    OVERLAPPED at{};
    at.Offset = static_cast<DWORD>(offset);
    at.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD n = 0;
    if (!WriteFile(handle_of(fd), buffer, chunk(size), &n, &at)) {
        set_errno_from_last_error();
        return -1;
    }
    return n;
}

int64_t file_write(int fd, const void* buffer, size_t size) {
    DWORD n = 0;
    if (!WriteFile(handle_of(fd), buffer, chunk(size), &n, nullptr)) {
        set_errno_from_last_error();
        return -1;
    }
    return n;
}

bool file_size(int fd, uint64_t& size) {
    LARGE_INTEGER s;
    if (!GetFileSizeEx(handle_of(fd), &s)) return false;
    size = static_cast<uint64_t>(s.QuadPart);
    return true;
}

int file_sync(int fd) {
    if (FlushFileBuffers(handle_of(fd))) return 0;
    set_errno_from_last_error();
    return -1;
}

void sync_folder(const fs::path&) {}

int64_t utc_offset(int64_t time) {
    const time_t t = static_cast<time_t>(time);
    struct tm local;
    if (localtime_s(&local, &t) != 0) return 0;
    // The local calendar time read as UTC, less the time itself.
    return static_cast<int64_t>(_mkgmtime(&local)) - time;
}

bool stdin_is_terminal() { return _isatty(_fileno(stdin)); }
bool stderr_is_terminal() { return _isatty(_fileno(stderr)); }

fs::path home_directory() {
    if (fs::path p = environment(L"USERPROFILE"); !p.empty()) return p;
    return fs::path("C:\\");
}

fs::path config_directory() {
    fs::path p = environment(L"APPDATA");
    return (p.empty() ? home_directory() / "AppData" / "Roaming" : p) / "ttt2";
}

fs::path data_directory() { return config_directory(); }
fs::path save_directory() { return data_directory() / "save"; }

fs::path cache_directory() {
    fs::path p = environment(L"LOCALAPPDATA");
    return (p.empty() ? home_directory() / "AppData" / "Local" : p) / "ttt2";
}

fs::path executable_path() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            return fs::path(path);
        }
        path.resize(path.size() * 2);
    }
}

fs::path readable_executable() { return executable_path(); }

void init_timing() { timeBeginPeriod(1); }

void sleep_until(std::chrono::steady_clock::time_point time) {
    using namespace std::chrono;
    // A high-resolution waitable timer (Windows 10 1803 and later) wakes
    // within a fraction of a millisecond; else the 1 ms system timer.
    thread_local const HANDLE timer = [] {
        HANDLE t = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        return t ? t : CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }();
    thread_local const nanoseconds slack = [] {
        HANDLE t = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (t) CloseHandle(t);
        return t ? nanoseconds(microseconds(700)) : nanoseconds(milliseconds(2));
    }();
    for (;;) {
        const auto left = time - steady_clock::now();
        if (left <= steady_clock::duration::zero()) return;
        if (left > slack && timer) {
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>(duration_cast<nanoseconds>(left - slack).count() / 100);
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, INFINITE);
                continue;
            }
        }
        std::this_thread::yield();
    }
}

void set_thread_name(const char* name) {
    // SetThreadDescription (Windows 10 1607 and later), looked up so older
    // systems still start.
    using Describe = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const auto describe = reinterpret_cast<Describe>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
    if (describe == nullptr) return;
    wchar_t wide[64];
    const int n = MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 64);
    if (n > 0) describe(GetCurrentThread(), wide);
}

#else

namespace {

// $XDG_*_HOME (absolute paths only, as the specification says), else
// ~/<fallback>.
fs::path xdg(const char* variable, const char* fallback) {
    const char* v = std::getenv(variable);
    return v && *v == '/' ? fs::path(v) : home_directory() / fallback;
}

} // namespace

int file_open(const fs::path& path, int flags, int mode) { return ::open(path.c_str(), flags | O_CLOEXEC, mode); }

int file_close(int fd) { return ::close(fd); }

int64_t file_pread(int fd, void* buffer, size_t size, uint64_t offset) {
    for (;;) {
        const ssize_t n = ::pread(fd, buffer, size, static_cast<off_t>(offset));
        if (n < 0 && errno == EINTR) continue;
        return n;
    }
}

int64_t file_pwrite(int fd, const void* buffer, size_t size, uint64_t offset) {
    for (;;) {
        const ssize_t n = ::pwrite(fd, buffer, size, static_cast<off_t>(offset));
        if (n < 0 && errno == EINTR) continue;
        return n;
    }
}

int64_t file_write(int fd, const void* buffer, size_t size) {
    for (;;) {
        const ssize_t n = ::write(fd, buffer, size);
        if (n < 0 && errno == EINTR) continue;
        return n;
    }
}

bool file_size(int fd, uint64_t& size) {
    struct stat st;
    if (::fstat(fd, &st) != 0) return false;
    size = static_cast<uint64_t>(st.st_size);
    return true;
}

int file_sync(int fd) { return ::fdatasync(fd); }

void sync_folder(const fs::path& folder) {
    if (const int fd = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC); fd >= 0) {
        ::syncfs(fd);
        ::close(fd);
    }
}

int64_t utc_offset(int64_t time) {
    const time_t t = static_cast<time_t>(time);
    struct tm local;
    localtime_r(&t, &local);
    return local.tm_gmtoff;
}

bool stdin_is_terminal() { return isatty(STDIN_FILENO); }
bool stderr_is_terminal() { return isatty(STDERR_FILENO); }

fs::path home_directory() {
    const char* h = std::getenv("HOME");
    return h && *h ? fs::path(h) : fs::path("/");
}

fs::path config_directory() { return xdg("XDG_CONFIG_HOME", ".config") / "ttt2"; }
fs::path data_directory() { return xdg("XDG_DATA_HOME", ".local/share") / "ttt2"; }
fs::path cache_directory() { return xdg("XDG_CACHE_HOME", ".cache") / "ttt2"; }
// (Not under $XDG_DATA_HOME: saves stay where earlier versions put them.)
fs::path save_directory() { return home_directory() / ".local/share/ttt2/save"; }

fs::path executable_path() {
    std::error_code ec;
    fs::path self = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : self;
}

fs::path readable_executable() { return "/proc/self/exe"; }

void init_timing() {}

void sleep_until(std::chrono::steady_clock::time_point time) { std::this_thread::sleep_until(time); }

void set_thread_name(const char* name) { pthread_setname_np(pthread_self(), name); }

#endif

} // namespace cafe::host
