#pragma once

// Windows: an install keeps all the port writes beside it, in a folder
// "data" next to the executable (saves, settings, the shader cache, the
// added fighters' packs), so that nothing goes to the system drive's
// %APPDATA% when the game is installed elsewhere. `ttt2 --install` makes
// that folder; without it the port uses %APPDATA%\ttt2 and
// %LOCALAPPDATA%\ttt2 as before. Header-only: display_settings.cpp and
// keymap.cpp are also built without the rest of the runtime.

#ifdef _WIN32

#include <filesystem>
#include <string>

#include <windows.h>

namespace cafe::host {

// <folder of this executable>\data, or empty when there is none.
inline std::filesystem::path portable_data() {
    static const std::filesystem::path root = [] {
        std::wstring path(MAX_PATH, L'\0');
        for (;;) {
            const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (n == 0) return std::filesystem::path();
            if (n < path.size()) {
                path.resize(n);
                break;
            }
            path.resize(path.size() * 2);
        }
        const std::filesystem::path data = std::filesystem::path(path).parent_path() / "data";
        std::error_code ec;
        return std::filesystem::is_directory(data, ec) ? data : std::filesystem::path();
    }();
    return root;
}

} // namespace cafe::host

#endif
