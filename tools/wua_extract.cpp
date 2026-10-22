#include <zarchive/zarchivereader.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
#ifndef _WIN32
class Fd {
public:
    explicit Fd(int value = -1) : value_(value) {}
    ~Fd() { if (value_ >= 0) ::close(value_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : value_(std::exchange(other.value_, -1)) {}
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            if (value_ >= 0) ::close(value_);
            value_ = std::exchange(other.value_, -1);
        }
        return *this;
    }
    int get() const { return value_; }
    int release() { return std::exchange(value_, -1); }
private:
    int value_;
};

[[noreturn]] void system_error(const std::string& operation) {
    const int error = errno;
    throw std::runtime_error(operation + ": " + std::strerror(error));
}
#else
[[noreturn]] void system_error(const std::string& operation) {
    const DWORD error = GetLastError();
    char message[256] = "";
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, message,
                   sizeof message, nullptr);
    std::string text = message;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    throw std::runtime_error(operation + ": " + (text.empty() ? "error " + std::to_string(error) : text));
}

// Names Windows would read as something else: devices (CON, NUL, COM1...,
// with any extension), alternate data streams, trailing dots and spaces.
bool reserved_name(std::string_view name) {
    if (name.back() == '.' || name.back() == ' ') return true;
    std::string stem(name.substr(0, name.find('.')));
    for (char& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" || stem == "CONOUT$")
        return true;
    return stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '0' && stem[3] <= '9';
}
#endif

void validate_component(std::string_view name) {
    if (name.empty() || name == "." || name == "..")
        throw std::runtime_error("Archive contains an unsafe path component");
    for (const unsigned char c : name) {
        // Also reject controls so each manifest entry remains exactly one TSV row.
        if (c < 32 || c == 127 || c == '/' || c == '\\')
            throw std::runtime_error("Archive contains an unsafe path component");
#ifdef _WIN32
        if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            throw std::runtime_error("Archive contains an unsafe path component");
#endif
    }
#ifdef _WIN32
    if (reserved_name(name))
        throw std::runtime_error("Archive contains an unsafe path component");
#endif
}

struct Entry {
    std::string path;
    ZArchiveNodeHandle node;
    uint64_t size;
};

std::vector<Entry> inventory(ZArchiveReader& reader) {
    struct Directory { std::string path; ZArchiveNodeHandle node; unsigned depth; };
    const auto root = reader.LookUp("", false, true);
    if (root == ZARCHIVE_INVALID_NODE)
        throw std::runtime_error("Archive has no valid root directory");
    std::vector<Directory> pending{{"", root, 0}};
    std::unordered_set<ZArchiveNodeHandle> visited;
    std::unordered_set<std::string> paths;
    std::vector<Entry> files;
    while (!pending.empty()) {
        auto dir = std::move(pending.back());
        pending.pop_back();
        if (dir.depth > 256 || !visited.insert(dir.node).second)
            throw std::runtime_error("Archive has cyclic or excessively deep directories");
        const auto count = reader.GetDirEntryCount(dir.node);
        for (uint32_t i = 0; i < count; ++i) {
            ZArchiveReader::DirEntry entry{};
            if (!reader.GetDirEntry(dir.node, i, entry) || entry.isFile == entry.isDirectory)
                throw std::runtime_error("Invalid archive directory entry");
            validate_component(entry.name);
            std::string path = dir.path.empty() ? std::string(entry.name)
                                               : dir.path + "/" + std::string(entry.name);
            if (!paths.insert(path).second)
                throw std::runtime_error("Duplicate archive path: " + path);
            const auto node = reader.LookUp(path, entry.isFile, entry.isDirectory);
            if (node == ZARCHIVE_INVALID_NODE)
                throw std::runtime_error("Cannot look up archive entry: " + path);
            if (entry.isDirectory)
                pending.push_back({std::move(path), node, dir.depth + 1});
            else
                files.push_back({std::move(path), node, reader.GetFileSize(node)});
        }
    }
    std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b) { return a.path < b.path; });
    return files;
}

#ifdef _WIN32
// The archive's text is UTF-8.
fs::path utf8_path(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

// A folder that is a folder, not a link or junction to somewhere else.
void child_directory(const fs::path& path) {
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        system_error("Cannot create directory " + path.string());
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        system_error("Cannot safely open directory " + path.string());
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Cannot safely open directory " + path.string() + ": not a plain folder");
}

fs::path output_directory(const fs::path& output) {
    // Checks every ancestor below the drive, including ancestors of OUTPUT_DIR.
    const auto absolute = fs::absolute(output).lexically_normal();
    fs::path current = absolute.root_path();
    for (const auto& component : absolute.relative_path()) {
        if (component.empty()) continue;
        current /= component;
        child_directory(current);
    }
    return current;
}

#else
Fd child_directory(int parent, const std::string& name) {
    if (::mkdirat(parent, name.c_str(), 0755) != 0 && errno != EEXIST)
        system_error("Cannot create directory " + name);
    const int fd = ::openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        system_error("Cannot safely open directory " + name);
    return Fd(fd);
}

Fd output_directory(const fs::path& output) {
    // Traverse every ancestor with O_NOFOLLOW, including ancestors of OUTPUT_DIR.
    const auto absolute = fs::absolute(output).lexically_normal();
    Fd current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (current.get() < 0)
        system_error("Cannot open filesystem root");
    for (const auto& component : absolute.relative_path())
        current = child_directory(current.get(), component.string());
    return current;
}

#endif

bool analysis_file(const std::string& path) {
    // WUA paths are TITLEID_VERSION/code/... and TITLEID_VERSION/meta/....
    const auto first = path.find('/');
    if (first == std::string::npos) return false;
    const auto second = path.find('/', first + 1);
    if (second == std::string::npos) return false;
    const auto directory = path.substr(first + 1, second - first - 1);
    return directory == "code" || directory == "meta";
}

#ifdef _WIN32
void extract_file(ZArchiveReader& reader, const Entry& entry, const fs::path& root) {
    fs::path parent = root;
    const fs::path relative = utf8_path(entry.path);
    for (const auto& component : relative.parent_path()) {
        parent /= component;
        child_directory(parent);
    }
    const fs::path target = parent / relative.filename();
    // CREATE_NEW: existing files (and links) are never overwritten.
    const HANDLE output = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (output == INVALID_HANDLE_VALUE)
        system_error("Cannot create output file (existing files are never overwritten) " + entry.path);
    try {
        std::vector<unsigned char> buffer(1024 * 1024);
        uint64_t offset = 0;
        while (offset < entry.size) {
            const auto length = std::min<uint64_t>(buffer.size(), entry.size - offset);
            if (reader.ReadFromFile(entry.node, offset, length, buffer.data()) != length)
                throw std::runtime_error("Short archive read: " + entry.path);
            DWORD written = 0;
            if (!WriteFile(output, buffer.data(), static_cast<DWORD>(length), &written, nullptr))
                system_error("Cannot write " + entry.path);
            if (written != length) throw std::runtime_error("Short output write: " + entry.path);
            offset += length;
        }
    } catch (...) {
        CloseHandle(output);
        DeleteFileW(target.c_str());
        throw;
    }
    if (!CloseHandle(output)) {
        DeleteFileW(target.c_str());
        system_error("Cannot close output file " + entry.path);
    }
}
#else
void extract_file(ZArchiveReader& reader, const Entry& entry, int root) {
    const int duplicate = ::fcntl(root, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0) system_error("Cannot duplicate output directory handle");
    Fd parent(duplicate);
    const fs::path relative(entry.path);
    for (const auto& component : relative.parent_path())
        parent = child_directory(parent.get(), component.string());
    const auto name = relative.filename().string();
    Fd output(::openat(parent.get(), name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644));
    if (output.get() < 0)
        system_error("Cannot create output file (existing files are never overwritten) " + entry.path);
    try {
        std::array<unsigned char, 1024 * 1024> buffer{};
        uint64_t offset = 0;
        while (offset < entry.size) {
            const auto length = std::min<uint64_t>(buffer.size(), entry.size - offset);
            if (reader.ReadFromFile(entry.node, offset, length, buffer.data()) != length)
                throw std::runtime_error("Short archive read: " + entry.path);
            size_t written = 0;
            while (written < length) {
                const auto result = ::write(output.get(), buffer.data() + written, length - written);
                if (result < 0 && errno == EINTR) continue;
                if (result < 0) system_error("Cannot write " + entry.path);
                if (result == 0) throw std::runtime_error("Short output write: " + entry.path);
                written += static_cast<size_t>(result);
            }
            offset += length;
        }
        if (::close(output.release()) != 0)
            system_error("Cannot close output file " + entry.path);
    } catch (...) {
        ::unlinkat(parent.get(), name.c_str(), 0);
        throw;
    }
}
#endif
} // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    const bool list = command == "list";
    const bool analysis = command == "extract-analysis";
    const bool all = command == "extract-all";
    if ((!list && !analysis && !all) || argc != (list ? 3 : 4)) {
        std::cerr << "Usage:\n  wua_extract list ARCHIVE\n"
                  << "  wua_extract extract-analysis ARCHIVE OUTPUT_DIR\n"
                  << "  wua_extract extract-all ARCHIVE OUTPUT_DIR\n";
        return 2;
    }
    try {
        std::unique_ptr<ZArchiveReader> reader(ZArchiveReader::OpenFromFile(argv[2]));
        if (!reader) throw std::runtime_error("Cannot open WUA/ZArchive: " + std::string(argv[2]));
        const auto files = inventory(*reader);
        if (list) {
            std::cout << "path\tsize_bytes\n";
            for (const auto& entry : files)
                std::cout << entry.path << '\t' << entry.size << '\n';
        } else {
            auto root = output_directory(argv[3]);
            uint64_t bytes = 0;
            size_t count = 0;
            for (const auto& entry : files) {
                if (analysis && !analysis_file(entry.path)) continue;
#ifdef _WIN32
                extract_file(*reader, entry, root);
#else
                extract_file(*reader, entry, root.get());
#endif
                bytes += entry.size;
                ++count;
                std::cout << entry.path << '\n';
            }
            std::cerr << "Extracted " << count << " files (" << bytes << " bytes)\n";
            if (analysis && count == 0)
                throw std::runtime_error("No TITLEID_VERSION/code or /meta files found");
        }
        if (!std::cout) throw std::runtime_error("Cannot write standard output");
    } catch (const std::exception& error) {
        std::cerr << "wua_extract: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
