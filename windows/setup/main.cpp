// ttt2-setup: the Windows installer. It builds the game program on the
// player's PC from their own copy of the game, with the kit it is part of
// (windows/CMakeLists.txt), and installs the game with that program:
//
// choose     the game's .wua (or title folder) and where to install it
// check      the processor, the kit, free space
// extract    code/Tekken.rpx out of the game; its SHA-256 must be v16's
// recompile  cafe-recomp translates it to C++
// compile    the kit's Clang compiles that, as many files at a time as the
//            processor and memory allow (an interrupted build resumes)
// link       with the kit's runtime into ttt2.exe, its DLLs beside it
// install    ttt2.exe --install copies the game, itself and its DLLs and
//            adds the Start menu shortcut
//
//   ttt2-setup.exe [GAME [FOLDER]] [--keep-work]
//                              (GAME may be the install folder itself: an
//                              update rebuilds the program from it, without
//                              the .wua; %APPDATA%\ttt2\setup.ini then says
//                              which kit built it)
//   ttt2-setup.exe --check     compiles and links the kit's stub of the
//                              generated code into a program that starts:
//                              the kit works on this PC (no game needed)
//
// Without arguments the game and the folder are chosen in dialogs. Texts are
// in Spanish on a Spanish Windows, else in English.

#include "wua.h"

#include <openssl/evp.h>

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace wua = cafe::wua;

constexpr const wchar_t* kTitle = L"Tekken Tag Tournament 2";
// The executable the runtime is written for: TEKKEN TAG 2 Wii U EDITION,
// Europe, update 16 (cafe-recomp refuses any other).
constexpr const char* kRpxSha256 = "fc0270465d384386f67804e48716e30bfbe85a5a1004e3a6bfe3ac75f9de5019";
// The build's folder holds up to 0.54 GB of C++, 0.1 GB of objects and the
// 0.13 GB program; one compile takes up to 1.1 GB of memory.
constexpr uint64_t kWorkBytes = 1'100'000'000;
constexpr uint64_t kCompileBytes = 1'100'000'000;

// ---------------------------------------------------------------- text

bool g_spanish = false;
bool g_quiet = false; // --check: no message boxes (it also runs unattended)
const wchar_t* tr(const wchar_t* english, const wchar_t* spanish) { return g_spanish ? spanish : english; }

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring format(const wchar_t* pattern, ...) {
    wchar_t text[2048];
    va_list args;
    va_start(args, pattern);
    _vsnwprintf(text, sizeof text / sizeof *text - 1, pattern, args);
    va_end(args);
    text[sizeof text / sizeof *text - 1] = L'\0';
    return text;
}

std::wstring gigabytes(uint64_t bytes) { return format(L"%.1f GB", static_cast<double>(bytes) / 1e9); }

// The console window shows the progress; a redirected output gets UTF-8.
void print(const std::wstring& text) {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    if (!WriteConsoleW(out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)) {
        const std::string bytes = narrow(text);
        std::fwrite(bytes.data(), 1, bytes.size(), stdout);
        std::fflush(stdout);
    }
}
void line(const std::wstring& text) { print(text + L"\n"); }

void step(const std::wstring& text) { line(L"\n== " + text); }

struct Failure {
    std::wstring text;
};
[[noreturn]] void fail(const std::wstring& text) { throw Failure{text}; }

void show_error(const std::wstring& text) {
    line(L"\nERROR: " + text);
    if (!g_quiet) MessageBoxW(nullptr, text.c_str(), kTitle, MB_OK | MB_ICONERROR);
}

// ---------------------------------------------------------------- choosing

// The Windows file dialog: a .wua, or a folder (`folder`). None if cancelled.
std::optional<fs::path> choose(bool folder, const wchar_t* title, const fs::path& start) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        fail(tr(L"Cannot open the file dialog.", L"No se puede abrir el diálogo de archivos."));
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
    dialog->SetTitle(title);
    if (!folder) {
        const COMDLG_FILTERSPEC filters[] = {{tr(L"Wii U game archive (*.wua)", L"Juego de Wii U (*.wua)"), L"*.wua"},
                                             {tr(L"All files", L"Todos los archivos"), L"*.*"}};
        dialog->SetFileTypes(2, filters);
    }
    IShellItem* start_item = nullptr;
    if (!start.empty() &&
        SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_PPV_ARGS(&start_item)))) {
        dialog->SetFolder(start_item);
        start_item->Release();
    }
    std::optional<fs::path> chosen;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                chosen = fs::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return chosen;
}

fs::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    fs::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path))) out = path;
    CoTaskMemFree(path);
    return out;
}

// ---------------------------------------------------------------- the kit

struct Kit {
    fs::path folder;
    std::map<std::string, std::string> values;

    std::vector<std::wstring> arguments(const std::string& key) const {
        const auto it = values.find(key);
        if (it == values.end()) fail(format(tr(L"The installer is damaged: kit.ini has no %hs.", L"El instalador está dañado: a kit.ini le falta %hs."), key.c_str()));
        std::vector<std::wstring> out;
        std::istringstream in(it->second);
        const std::string kit = narrow(folder.generic_wstring());
        for (std::string word; in >> word;) {
            for (size_t at; (at = word.find("${KIT}")) != std::string::npos;) word.replace(at, 6, kit);
            out.push_back(wide(word));
        }
        return out;
    }
};

Kit load_kit() {
    wchar_t self[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, self, sizeof self / sizeof *self);
    Kit kit;
    kit.folder = fs::path(std::wstring(self, n)).parent_path();
    std::ifstream in(kit.folder / "kit.ini");
    if (!in) fail(tr(L"The installer is damaged: kit.ini is missing. Download it again.",
                     L"El instalador está dañado: falta kit.ini. Vuelve a descargarlo."));
    for (std::string text; std::getline(in, text);) {
        if (text.empty() || text[0] == '#') continue;
        const size_t equals = text.find(" = ");
        if (equals != std::string::npos) kit.values[text.substr(0, equals)] = text.substr(equals + 3);
    }
    for (const char* file : {"bin/cafe-recomp.exe", "toolchain/bin/clang++.exe", "lib/libttt2_runtime.a"}) {
        std::error_code ec;
        if (!fs::exists(kit.folder / file, ec)) {
            fail(format(tr(L"The installer is damaged: %hs is missing. Download it again.",
                           L"El instalador está dañado: falta %hs. Vuelve a descargarlo."), file));
        }
    }
    return kit;
}

// ---------------------------------------------------------------- processes

// A command line as CommandLineToArgvW reads it back.
std::wstring quote(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    return out + L"\"";
}

// Starts a program: its output into `log` (hidden), or into this console.
HANDLE start(const std::vector<std::wstring>& arguments, const fs::path* log, DWORD priority = 0) {
    std::wstring command;
    for (const std::wstring& argument : arguments) command += (command.empty() ? L"" : L" ") + quote(argument);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    HANDLE output = INVALID_HANDLE_VALUE;
    if (log) {
        SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
        output = CreateFileW(log->c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) fail(format(tr(L"Cannot write %ls.", L"No se puede escribir %ls."), log->c_str()));
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = output;
        startup.hStdError = output;
        startup.hStdInput = nullptr;
    }
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, log != nullptr,
                                        priority | (log ? CREATE_NO_WINDOW : 0), nullptr, nullptr, &startup, &process);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (!started) fail(format(tr(L"Cannot start %ls (error %lu).", L"No se puede iniciar %ls (error %lu)."), arguments[0].c_str(), GetLastError()));
    CloseHandle(process.hThread);
    return process.hProcess;
}

DWORD wait(HANDLE process) {
    WaitForSingleObject(process, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return code;
}

// The end of a log, for a message.
std::wstring tail(const fs::path& log) {
    std::ifstream in(log, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.size() > 1500) text = "..." + text.substr(text.size() - 1500);
    return wide(text);
}

// ---------------------------------------------------------------- steps

void check_processor() {
    __builtin_cpu_init();
    if (!__builtin_cpu_supports("avx2") || !__builtin_cpu_supports("fma") || !__builtin_cpu_supports("bmi2")) {
        fail(tr(L"This processor lacks AVX2 and FMA, which the game program needs (Intel Haswell, AMD Excavator or newer).",
                L"Este procesador no tiene AVX2 y FMA, que el programa del juego necesita (Intel Haswell, AMD Excavator o más nuevo)."));
    }
}

std::string sha256(const std::vector<uint8_t>& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    EVP_Digest(data.data(), data.size(), digest, &length, EVP_sha256(), nullptr);
    std::string hex;
    for (unsigned int i = 0; i < length; ++i) {
        char byte[3];
        std::snprintf(byte, sizeof byte, "%02x", digest[i]);
        hex += byte;
    }
    return hex;
}

// code/Tekken.rpx of a .wua, or of a title folder (or an install's game/).
std::vector<uint8_t> read_rpx(const fs::path& game) {
    std::error_code ec;
    if (fs::is_directory(game, ec)) {
        for (const fs::path& file : {game / "code" / "Tekken.rpx", game / "game" / "code" / "Tekken.rpx"}) {
            std::ifstream in(file, std::ios::binary);
            if (in) return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
        fail(format(tr(L"%ls holds no code\\Tekken.rpx.", L"%ls no contiene code\\Tekken.rpx."), game.c_str()));
    }
    std::unique_ptr<ZArchiveReader> archive(ZArchiveReader::OpenFromFile(game));
    if (!archive) fail(format(tr(L"Cannot open %ls as a .wua archive.", L"No se puede abrir %ls como archivo .wua."), game.c_str()));
    const std::vector<std::string> layers = wua::game_layers(*archive);
    const ZArchiveNodeHandle node = layers.empty() ? ZARCHIVE_INVALID_NODE : wua::find(*archive, layers, "code/Tekken.rpx");
    if (node == ZARCHIVE_INVALID_NODE) {
        fail(format(tr(L"%ls holds no Tekken Tag Tournament 2 (code/Tekken.rpx).",
                       L"%ls no contiene Tekken Tag Tournament 2 (code/Tekken.rpx)."), game.c_str()));
    }
    std::vector<uint8_t> data(archive->GetFileSize(node));
    if (archive->ReadFromFile(node, 0, data.size(), data.data()) != data.size()) {
        fail(format(tr(L"Cannot read code/Tekken.rpx from %ls.", L"No se puede leer code/Tekken.rpx de %ls."), game.c_str()));
    }
    return data;
}

// The generated files, as cafe-recomp lists them in sources.cmake.
std::vector<fs::path> generated_sources(const fs::path& generated) {
    std::ifstream in(generated / "sources.cmake");
    std::vector<fs::path> sources;
    for (std::string text; std::getline(in, text);) {
        const size_t at = text.find("${CMAKE_CURRENT_LIST_DIR}/");
        if (at == std::string::npos) continue;
        std::string name = text.substr(at + 26);
        while (!name.empty() && (name.back() == ')' || name.back() == '\r' || name.back() == ' ')) name.pop_back();
        sources.push_back(generated / name);
    }
    if (sources.empty()) fail(tr(L"cafe-recomp wrote no files.", L"cafe-recomp no generó archivos."));
    return sources;
}

unsigned compile_jobs() {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof memory;
    GlobalMemoryStatusEx(&memory);
    const uint64_t by_memory = memory.ullAvailPhys > kCompileBytes ? memory.ullAvailPhys / kCompileBytes : 1;
    return static_cast<unsigned>(std::clamp<uint64_t>(std::min<uint64_t>(system.dwNumberOfProcessors, by_memory), 1, 64));
}

void compile(const Kit& kit, const std::vector<fs::path>& sources, const fs::path& objects, const fs::path& logs) {
    const fs::path compiler = kit.folder / "toolchain" / "bin" / "clang++.exe";
    const std::vector<std::wstring> options = kit.arguments("compile");
    fs::create_directories(objects);
    fs::create_directories(logs);
    // Unchanged files compiled before (an interrupted build) are kept.
    std::vector<size_t> pending;
    for (size_t i = 0; i < sources.size(); ++i) {
        std::error_code ec;
        const fs::path object = objects / sources[i].filename().replace_extension(".o");
        if (fs::exists(object, ec) && fs::last_write_time(object, ec) >= fs::last_write_time(sources[i], ec)) continue;
        pending.push_back(i);
    }
    const unsigned jobs = compile_jobs();
    line(format(tr(L"%zu files to compile, %u at a time (this takes a while)", L"%zu archivos que compilar, %u a la vez (tarda un rato)"),
                pending.size(), jobs));
    struct Running {
        HANDLE process;
        size_t index;
    };
    std::vector<Running> running;
    size_t next = 0, done = sources.size() - pending.size();
    const auto started = GetTickCount64();
    while (next < pending.size() || !running.empty()) {
        while (next < pending.size() && running.size() < jobs) {
            const size_t i = pending[next++];
            const fs::path object = objects / sources[i].filename().replace_extension(".o");
            std::vector<std::wstring> arguments{compiler.wstring()};
            arguments.insert(arguments.end(), options.begin(), options.end());
            arguments.insert(arguments.end(), {L"-c", sources[i].wstring(), L"-o", object.wstring()});
            const fs::path log = logs / sources[i].filename().replace_extension(".log");
            running.push_back({start(arguments, &log, BELOW_NORMAL_PRIORITY_CLASS), i});
        }
        std::vector<HANDLE> handles;
        for (const Running& r : running) handles.push_back(r.process);
        const DWORD which = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
        if (which >= WAIT_OBJECT_0 + handles.size()) {
            fail(format(tr(L"Cannot wait for the compiler (error %lu).", L"No se puede esperar al compilador (error %lu)."),
                        GetLastError()));
        }
        const size_t at = which - WAIT_OBJECT_0;
        const Running finished = running[at];
        running.erase(running.begin() + static_cast<std::ptrdiff_t>(at));
        if (wait(finished.process) != 0) {
            for (const Running& r : running) {
                TerminateProcess(r.process, 1);
                wait(r.process);
            }
            fs::remove(objects / sources[finished.index].filename().replace_extension(".o"));
            fail(format(tr(L"Compiling %ls failed:\n\n%ls", L"Falló la compilación de %ls:\n\n%ls"),
                        sources[finished.index].filename().c_str(),
                        tail(logs / sources[finished.index].filename().replace_extension(".log")).c_str()));
        }
        ++done;
        const double minutes = static_cast<double>(GetTickCount64() - started) / 60000.0;
        print(format(tr(L"\r  %zu / %zu compiled (%.0f min)   ", L"\r  %zu / %zu compilados (%.0f min)   "), done,
                     sources.size(), minutes));
    }
    line(L"");
}

void link(const Kit& kit, const std::vector<fs::path>& sources, const fs::path& objects, const fs::path& program,
          const fs::path& logs) {
    // The objects in a response file: the command line has room for 32K characters.
    const fs::path response = objects / "objects.rsp";
    {
        std::ofstream out(response, std::ios::binary);
        for (const fs::path& source : sources) {
            out << '"' << narrow((objects / source.filename().replace_extension(".o")).generic_wstring()) << "\"\n";
        }
    }
    std::vector<std::wstring> arguments{(kit.folder / "toolchain" / "bin" / "clang++.exe").wstring(), L"-o",
                                        program.wstring(), L"@" + response.wstring()};
    const std::vector<std::wstring> options = kit.arguments("link");
    arguments.insert(arguments.end(), options.begin(), options.end());
    const fs::path log = logs / "link.log";
    if (wait(start(arguments, &log)) != 0) {
        fail(format(tr(L"Linking the game program failed:\n\n%ls", L"Falló el enlazado del programa del juego:\n\n%ls"), tail(log).c_str()));
    }
    for (const auto& entry : fs::directory_iterator(kit.folder / "dlls")) {
        fs::copy_file(entry.path(), program.parent_path() / entry.path().filename(), fs::copy_options::overwrite_existing);
    }
    const fs::path check = logs / "check.log";
    if (wait(start({program.wstring(), L"--help"}, &check)) != 0) {
        fail(format(tr(L"The game program does not start:\n\n%ls", L"El programa del juego no arranca:\n\n%ls"), tail(check).c_str()));
    }
}

// Where the last install went (runtime/src/install.cpp records it).
std::optional<fs::path> installed_folder() {
    std::ifstream in(known_folder(FOLDERID_RoamingAppData) / "ttt2" / "installed");
    std::string text;
    if (!in || !std::getline(in, text) || text.empty()) return std::nullopt;
    return fs::path(std::u8string(text.begin(), text.end()));
}

// --check: the kit's toolchain compiles, links and starts a program made of
// the runtime and the generated code's stub.
int check(const Kit& kit) {
    const fs::path work = known_folder(FOLDERID_LocalAppData) / "ttt2" / "setup-check";
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work / "generated");
    fs::copy_file(kit.folder / "lib" / "generated_stub.cpp", work / "generated" / "generated_stub.cpp");
    const std::vector<fs::path> sources{work / "generated" / "generated_stub.cpp"};
    step(tr(L"Compiling", L"Compilando"));
    compile(kit, sources, work / "objects", work / "logs");
    step(tr(L"Linking", L"Enlazando"));
    const fs::path program = work / "program" / "ttt2.exe";
    fs::create_directories(program.parent_path());
    link(kit, sources, work / "objects", program, work / "logs");
    line(tr(L"the kit works: the program links and starts", L"el kit funciona: el programa enlaza y arranca"));
    fs::remove_all(work, ec);
    return 0;
}

int run(const std::vector<std::wstring>& args) {
    SetConsoleTitleW(tr(L"Tekken Tag Tournament 2 - installer", L"Tekken Tag Tournament 2 - instalador"));
    line(tr(L"Tekken Tag Tournament 2 Ultimate Edition - Windows installer",
            L"Tekken Tag Tournament 2 Ultimate Edition - instalador para Windows"));
    bool keep_work = false, checking = false;
    std::vector<std::wstring> positional;
    for (const std::wstring& arg : args) {
        if (arg == L"--keep-work") keep_work = true;
        else if (arg == L"--check") checking = g_quiet = true;
        else positional.push_back(arg);
    }

    const Kit kit = load_kit();
    if (checking) return check(kit);
    check_processor();

    fs::path game, folder;
    if (positional.size() >= 1) {
        game = positional[0];
    } else {
        MessageBoxW(nullptr,
                    tr(L"This installs Tekken Tag Tournament 2 on this PC from your own copy of the game:\n\n"
                       L"TEKKEN TAG TOURNAMENT 2 Wii U EDITION, Europe, with update v16, as a .wua archive "
                       L"(Cemu makes one from an installed title).\n\n"
                       L"Choose the .wua, then the folder to install into (about 17 GB). The game program is then "
                       L"built for this PC, which takes some minutes.",
                       L"Esto instala Tekken Tag Tournament 2 en este PC a partir de tu propia copia del juego:\n\n"
                       L"TEKKEN TAG TOURNAMENT 2 Wii U EDITION, versión europea, con la actualización v16, como archivo .wua "
                       L"(Cemu lo crea a partir de un juego instalado).\n\n"
                       L"Elige el .wua y después la carpeta donde instalarlo (unos 17 GB). Luego se compila el programa del "
                       L"juego para este PC, lo que tarda unos minutos."),
                    kTitle, MB_OK | MB_ICONINFORMATION);
        const auto chosen = choose(false, tr(L"Choose your Tekken Tag Tournament 2 .wua", L"Elige tu .wua de Tekken Tag Tournament 2"),
                                   known_folder(FOLDERID_Downloads));
        if (!chosen) return 1;
        game = *chosen;
    }
    // Before anything long: the game must be the one the port is made from.
    step(tr(L"Checking the game", L"Comprobando el juego"));
    const std::vector<uint8_t> rpx = read_rpx(game);
    if (sha256(rpx) != kRpxSha256) {
        fail(tr(L"This is not the supported game: it must be TEKKEN TAG TOURNAMENT 2 Wii U EDITION, Europe (WUP-P-AKNP), "
                L"with update v16 (in the archive, or merged into it).",
                L"Este no es el juego compatible: tiene que ser TEKKEN TAG TOURNAMENT 2 Wii U EDITION, versión europea "
                L"(WUP-P-AKNP), con la actualización v16 (dentro del archivo o integrada en él)."));
    }
    line(tr(L"  the game is the supported one (EU, v16)", L"  el juego es el compatible (EU, v16)"));

    if (positional.size() >= 2) {
        folder = positional[1];
    } else {
        fs::path start = known_folder(FOLDERID_Profile);
        if (const auto previous = installed_folder()) start = previous->parent_path();
        const auto chosen = choose(true, tr(L"Choose where to install Tekken Tag Tournament 2 (about 17 GB)",
                                            L"Elige dónde instalar Tekken Tag Tournament 2 (unos 17 GB)"), start);
        if (!chosen) return 1;
        folder = *chosen;
    }

    const fs::path work = known_folder(FOLDERID_LocalAppData) / "ttt2" / "setup-work";
    fs::create_directories(work);
    std::error_code ec;
    const fs::space_info space = fs::space(work, ec);
    if (!ec && space.available < kWorkBytes) {
        fail(format(tr(L"Building needs %ls free on %ls (%ls free).", L"La compilación necesita %ls libres en %ls (hay %ls)."),
                    gigabytes(kWorkBytes).c_str(), work.root_path().c_str(), gigabytes(space.available).c_str()));
    }
    // Objects compiled by another version of the kit are compiled again.
    {
        const std::string version = kit.values.count("version") ? kit.values.at("version") : "";
        std::ifstream in(work / "kit-version");
        std::string previous;
        std::getline(in, previous);
        if (previous != version) fs::remove_all(work / "objects", ec);
        std::ofstream(work / "kit-version") << version << '\n';
    }
    // The PC stays awake while it builds and copies.
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

    step(tr(L"Translating the game's code to C++", L"Traduciendo el código del juego a C++"));
    const fs::path rpx_file = work / "Tekken.rpx", generated = work / "generated", logs = work / "logs";
    {
        std::ofstream out(rpx_file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(rpx.data()), static_cast<std::streamsize>(rpx.size()));
        if (!out) fail(format(tr(L"Cannot write %ls.", L"No se puede escribir %ls."), rpx_file.c_str()));
    }
    if (wait(start({(kit.folder / "bin" / "cafe-recomp.exe").wstring(), rpx_file.wstring(), generated.wstring()}, nullptr)) != 0) {
        fail(tr(L"Translating the game's code failed (see the window's text).",
                L"Falló la traducción del código del juego (mira el texto de la ventana)."));
    }
    const std::vector<fs::path> sources = generated_sources(generated);

    step(tr(L"Compiling the game program for this PC", L"Compilando el programa del juego para este PC"));
    compile(kit, sources, work / "objects", logs);

    step(tr(L"Linking the game program", L"Enlazando el programa del juego"));
    const fs::path program = work / "program" / "ttt2.exe";
    fs::create_directories(program.parent_path());
    link(kit, sources, work / "objects", program, logs);
    line(tr(L"  the game program starts", L"  el programa del juego arranca"));

    step(tr(L"Installing the game (copying about 16 GB)", L"Instalando el juego (copiando unos 16 GB)"));
    if (wait(start({program.wstring(), L"--install", game.wstring(), folder.wstring()}, nullptr)) != 0) {
        fail(tr(L"Installing failed (see the window's text).", L"Falló la instalación (mira el texto de la ventana)."));
    }
    SetThreadExecutionState(ES_CONTINUOUS);
    if (!keep_work) fs::remove_all(work, ec);
    // What built the installed program, for the launcher's updates (windows/launcher): the kit's version and the
    // game it was built from.
    {
        std::ofstream out(known_folder(FOLDERID_RoamingAppData) / "ttt2" / "setup.ini", std::ios::binary);
        out << "built = " << (kit.values.count("version") ? kit.values.at("version") : "") << '\n'
            << "game = " << narrow(fs::absolute(game).wstring()) << '\n';
    }

    const std::optional<fs::path> installed = installed_folder();
    const fs::path executable = installed ? *installed / "Tekken Tag Tournament 2.exe" : fs::path();
    const std::wstring done =
        format(tr(L"Tekken Tag Tournament 2 is installed%ls%ls.\n\nIt is in the Start menu. Play now?",
                  L"Tekken Tag Tournament 2 está instalado%ls%ls.\n\nLo tienes en el menú Inicio. ¿Jugar ahora?"),
               installed ? tr(L" in ", L" en ") : L"", installed ? installed->c_str() : L"");
    line(L"\n" + done);
    if (MessageBoxW(nullptr, done.c_str(), kTitle, MB_YESNO | MB_ICONINFORMATION) == IDYES && fs::exists(executable, ec)) {
        ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr, installed->c_str(), SW_SHOWNORMAL);
    }
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    g_spanish = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<std::wstring> args(argv + 1, argv + argc);
    try {
        return run(args);
    } catch (const Failure& failure) {
        show_error(failure.text);
    } catch (const std::exception& e) {
        show_error(wide(e.what()));
    }
    return 1;
}
