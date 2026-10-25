#include "host/keymap.h"

#include <sys/stat.h>
#ifdef _WIN32
#include <wchar.h>
#include <windows.h>
#endif
#include "host/portable.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

namespace cafe::host::keymap {
namespace {

namespace fs = std::filesystem;

// A hand-edited file is a few hundred bytes; anything much larger is not
// one, and reading it must not hold up the window's input.
constexpr uint64_t kMaxFileSize = 64 * 1024;

bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && space(s.front())) s.remove_prefix(1);
    while (!s.empty() && space(s.back())) s.remove_suffix(1);
    return s;
}

// `#` starts a comment at the start of a line or after a space (a `#`
// inside a word is part of it).
std::string_view strip_comment(std::string_view line) {
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '#' && (i == 0 || space(line[i - 1]))) return line.substr(0, i);
    }
    return line;
}

std::optional<size_t> input_index(std::string_view id) {
    for (size_t i = 0; i < kInputCount; ++i) {
        if (kInputs[i].id == id) return i;
    }
    return std::nullopt;
}

std::string quoted(std::string_view s) { return "\"" + std::string(s) + "\""; }

// The key a value names, or why it names none.
struct Key {
    uint16_t key = 0;
    std::string error;
};

Key key_of(std::string_view value, const Resolver& resolve) {
    if (value.empty()) return {0, "no key"};
    if (std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        unsigned number = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
        if (error != std::errc{} || end != value.data() + value.size() || number < kFirstKey || number >= kKeyCount) {
            // Numbers are always scancodes; the digit keys have no other
            // name in SDL, so a bare `1` is the likeliest mistake.
            return {0, std::string(value) + " is not a key number (SDL scancodes are 4 to 511; the 1 key is 30)"};
        }
        return {static_cast<uint16_t>(number), {}};
    }
    const uint16_t key = resolve ? resolve(std::string(value)) : 0;
    if (key < kFirstKey || key >= kKeyCount) return {0, "unknown key " + quoted(value)};
    return {key, {}};
}

#ifndef _WIN32
fs::path home() {
    const char* h = std::getenv("HOME");
    return h && *h ? fs::path(h) : fs::path("/");
}
#endif

// stat(2) as both systems have it (Windows: the wide-path call, 64-bit sizes).
#ifdef _WIN32
using FileStat = struct _stat64;
int stat_file(const fs::path& file, FileStat& st) { return _wstat64(file.c_str(), &st); }
bool regular(const FileStat& st) { return (st.st_mode & _S_IFMT) == _S_IFREG; }
#else
using FileStat = struct stat;
int stat_file(const fs::path& file, FileStat& st) { return ::stat(file.c_str(), &st); }
bool regular(const FileStat& st) { return S_ISREG(st.st_mode); }
#endif

// Where the port's settings are: $XDG_CONFIG_HOME/ttt2 (default
// ~/.config/ttt2), %APPDATA%\ttt2 on Windows.
fs::path config_folder() {
#ifdef _WIN32
    if (fs::path portable = host::portable_data(); !portable.empty()) return portable;
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    if (appdata && *appdata) return fs::path(appdata) / "ttt2";
    const wchar_t* profile = _wgetenv(L"USERPROFILE");
    return fs::path(profile && *profile ? profile : L"C:\\") / "AppData" / "Roaming" / "ttt2";
#else
    const char* config = std::getenv("XDG_CONFIG_HOME");
    return (config && *config == '/' ? fs::path(config) : home() / ".config") / "ttt2";
#endif
}

} // namespace

bool reserved(uint16_t key) { return std::find(kReserved.begin(), kReserved.end(), key) != kReserved.end(); }

Keys default_keys() {
    Keys keys{};
    for (size_t i = 0; i < kInputCount; ++i) keys[i] = kInputs[i].default_key;
    return keys;
}

Parsed parse(std::string_view text, const Resolver& resolve) {
    Parsed parsed;
    std::array<size_t, kInputCount> set_on{}; // the line that gave each input its key
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3); // a byte order mark from an editor
    size_t number = 0;
    while (!text.empty()) {
        ++number;
        const size_t end = std::min(text.find('\n'), text.size());
        const std::string_view line = trim(strip_comment(text.substr(0, end)));
        text.remove_prefix(std::min(end + 1, text.size()));
        if (line.empty()) continue;

        const size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            parsed.warnings.push_back({number, "not \"input = key\": " + quoted(line)});
            continue;
        }
        const std::string_view id = trim(line.substr(0, equals));
        const std::string_view value = trim(line.substr(equals + 1));
        const std::optional<size_t> input = input_index(id);
        if (!input) {
            parsed.warnings.push_back({number, "unknown input " + quoted(id)});
            continue;
        }
        const Key key = key_of(value, resolve);
        if (!key.error.empty()) {
            parsed.warnings.push_back({number, std::string(id) + ": " + key.error});
            continue;
        }
        if (reserved(key.key)) {
            parsed.warnings.push_back({number, std::string(id) + ": " + quoted(value) + " is a reserved key"});
            continue;
        }
        // The earlier line is the one left out.
        if (set_on[*input] != 0) {
            parsed.warnings.push_back(
                {set_on[*input], std::string(id) + ": given again on line " + std::to_string(number)});
        } else {
            ++parsed.assigned;
        }
        set_on[*input] = number;
        parsed.keys[*input] = key.key;
    }
    std::stable_sort(parsed.warnings.begin(), parsed.warnings.end(),
                     [](const Warning& a, const Warning& b) { return a.line < b.line; });
    return parsed;
}

fs::path file_path() {
    return config_folder() / "keyboard.ini";
}

Loaded load(const fs::path& file, const Resolver& resolve) {
    Loaded loaded{default_keys(), {}};
    const std::string name = file.string();
    FileStat st{};
    if (stat_file(file, st) != 0) {
        const int error = errno;
        loaded.log.push_back(error == ENOENT || error == ENOTDIR
                                 ? "keyboard: defaults (no " + name + ")"
                                 : "keyboard: defaults (cannot read " + name + ": " + std::strerror(error) + ")");
        return loaded;
    }
    // Never open a pipe or a device: that could wait forever.
    if (!regular(st)) {
        loaded.log.push_back("keyboard: defaults (" + name + " is not a file)");
        return loaded;
    }
    if (static_cast<uint64_t>(st.st_size) > kMaxFileSize) {
        loaded.log.push_back("keyboard: defaults (" + name + " is larger than 64 KiB)");
        return loaded;
    }
    std::ifstream in(file, std::ios::binary);
    std::string text;
    try {
        if (in) text.assign(std::istreambuf_iterator<char>(in), {});
    } catch (const std::ios_base::failure& e) {
        // libstdc++'s file buffer throws on a failed read (EIO: a failing
        // disk, a network home gone) whatever the stream's exception mask.
        loaded.log.push_back("keyboard: defaults (cannot read " + name + ": " + e.code().message() + ")");
        return loaded;
    }
    if (!in.is_open() || in.bad()) {
        const int error = errno;
        loaded.log.push_back("keyboard: defaults (cannot read " + name + ": " + std::strerror(error) + ")");
        return loaded;
    }
    const Parsed parsed = parse(text, resolve);
    for (const Warning& warning : parsed.warnings) {
        loaded.log.push_back("keyboard: " + name + ":" + std::to_string(warning.line) + ": " + warning.text +
                             ", line ignored");
    }
    loaded.log.push_back("keyboard: " + std::to_string(parsed.assigned) + " of " + std::to_string(kInputCount) +
                         " keys from " + name);
    loaded.keys = parsed.keys;
    return loaded;
}

Watcher::Watcher(fs::path file, Resolver resolve) : file_(std::move(file)), resolve_(std::move(resolve)) {}

Watcher::Stamp Watcher::stamp() const {
#ifdef _WIN32
    // The volume and file index stand for the device and inode; the times
    // are in 100 ns ticks.
    const HANDLE handle = CreateFileW(file_.c_str(), FILE_READ_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info{};
    const bool got = GetFileInformationByHandle(handle, &info);
    CloseHandle(handle);
    if (!got) return {};
    const auto ticks = [](const FILETIME& t) {
        return static_cast<int64_t>(uint64_t{t.dwHighDateTime} << 32 | t.dwLowDateTime);
    };
    return {true, info.dwVolumeSerialNumber, uint64_t{info.nFileIndexHigh} << 32 | info.nFileIndexLow,
            uint64_t{info.nFileSizeHigh} << 32 | info.nFileSizeLow, ticks(info.ftLastWriteTime),
            ticks(info.ftCreationTime)};
#else
    FileStat st{};
    if (stat_file(file_, st) != 0) return {};
    const auto ns = [](const timespec& t) { return int64_t{t.tv_sec} * 1'000'000'000 + t.tv_nsec; };
    return {true, st.st_dev, st.st_ino, static_cast<uint64_t>(st.st_size), ns(st.st_mtim), ns(st.st_ctim)};
#endif
}

std::optional<Loaded> Watcher::reload() {
    // Stamped before reading: a write during the read shows as a change at
    // the next check, and the file is read again then.
    const Stamp now = stamp();
    if (read_ && *read_ == now) return std::nullopt;
    read_ = now;
    return load(file_, resolve_);
}

} // namespace cafe::host::keymap
