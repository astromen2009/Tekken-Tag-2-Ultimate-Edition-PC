#pragma once

// What differs between the host systems the port runs on (Linux and
// Windows): files by descriptor with positioned reads and writes, where the
// port keeps its files, and the running executable.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace cafe::host {

// ---------------------------------------------------------------- files
// Descriptors as open(2) gives them, binary and not inherited by child
// processes. `flags` are the O_* flags of <fcntl.h>; failures set errno.
int file_open(const std::filesystem::path& path, int flags, int mode = 0644);
int file_close(int fd);
// Reads or writes at `offset` without moving a shared position (pread(2),
// pwrite(2)): at most `size` bytes, 0 at the end of the file, -1 on errors.
int64_t file_pread(int fd, void* buffer, size_t size, uint64_t offset);
int64_t file_pwrite(int fd, const void* buffer, size_t size, uint64_t offset);
// Appends at the descriptor's position (write(2)).
int64_t file_write(int fd, const void* buffer, size_t size);
bool file_size(int fd, uint64_t& size);
// The file's data on disk (fdatasync(2)).
int file_sync(int fd);
// The data of the file system holding `folder` on disk, renames included
// (syncfs(2)); does nothing where files are synced one by one.
void sync_folder(const std::filesystem::path& folder);

// How far local time is ahead of UTC at `time` (tm_gmtoff), in seconds.
int64_t utc_offset(int64_t time);

bool stdin_is_terminal();
bool stderr_is_terminal();

// ---------------------------------------------------------------- places
// Linux: $HOME, $XDG_CONFIG_HOME/ttt2, $XDG_DATA_HOME/ttt2, $XDG_CACHE_HOME/ttt2.
// Windows: %USERPROFILE%, %APPDATA%\ttt2 (configuration and data) and
// %LOCALAPPDATA%\ttt2 (cache).
std::filesystem::path home_directory();
std::filesystem::path config_directory();
std::filesystem::path data_directory();
std::filesystem::path cache_directory();
// The game's saves: ~/.local/share/ttt2/save, %APPDATA%\ttt2\save.
std::filesystem::path save_directory();

// This executable; empty if unknown. `readable` is a path that still reads
// the running program when its file has been replaced (a rebuild during an
// install): /proc/self/exe on Linux, where it differs.
std::filesystem::path executable_path();
std::filesystem::path readable_executable();

// A path as UTF-8 text (what SDL takes, and what the port prints).
std::string utf8(const std::filesystem::path& path);

// ---------------------------------------------------------------- time
// Waits as precise as Linux's: Windows' timer otherwise ends sleeps on its
// 15.6 ms ticks, which the GPU's polls (20 us), the sound mixer's 3 ms
// frames and the frame pacing cannot have. init_timing (at start-up) sets
// the system timer to 1 ms; sleep_until waits on a high-resolution timer and
// spins the last fraction of a millisecond. Linux: std::this_thread's.
void init_timing();
void sleep_until(std::chrono::steady_clock::time_point time);
template <typename Rep, typename Period>
void sleep_for(std::chrono::duration<Rep, Period> duration) {
    sleep_until(std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration));
}

// The calling thread's name, for debuggers and profilers.
void set_thread_name(const char* name);

} // namespace cafe::host
