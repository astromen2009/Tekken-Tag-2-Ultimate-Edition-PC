# The Windows installer's kit: what ttt2-setup.exe builds the game program
# with on the player's PC from their own .wua (the game's code cannot be
# shipped, only translated there). Nothing of the game is in the kit:
#
# kit/ttt2-setup.exe         the installer the player runs (setup/main.cpp)
# kit/kit.ini                how to compile and link (kit_ini.cmake)
# kit/bin/cafe-recomp.exe    the translator, with the DLLs it needs
# kit/include/cafe/          the headers the generated code includes
# kit/lib/                   the runtime and main.cpp (linked whole: the
#                            runtime's imports must replace the generated
#                            weak defaults), the archives it uses, the
#                            program's manifest, and the generated code's
#                            stub (for the check that the kit links)
# kit/dlls/                  the DLLs the game program needs beside it
# kit/toolchain/             Clang, LLD and the MinGW headers and libraries
#                            (copied from MSYS2 by package_toolchain.sh)
#
# Built with the ttt2_windows_kit target; .github/workflows/windows-installer.yml
# adds the toolchain and makes TTT2-Setup.exe with Inno Setup (installer.iss).

set(TTT2_WINDOWS_KIT_DIR ${CMAKE_BINARY_DIR}/windows-kit)
set(kit ${TTT2_WINDOWS_KIT_DIR})
# The release's version, the Linux launcher's (launcher/builder).
file(STRINGS ${CMAKE_SOURCE_DIR}/launcher/builder/CMakeLists.txt version_line
    REGEX "^set\\(TTT2_LAUNCHER_VERSION [0-9.]+\\)")
string(REGEX REPLACE ".* ([0-9.]+)\\)" "\\1" TTT2_WINDOWS_VERSION "${version_line}")
set(TTT2_WINDOWS_VERSION ${TTT2_WINDOWS_VERSION} PARENT_SCOPE)
# The libraries the game program links by name, as MSYS2's CLANG64 packages
# (and so the kit's toolchain) have them.
set(TTT2_WINDOWS_LIBRARIES "-lSDL3 -lvulkan-1 -lshaderc_shared -lcrypto -lz -lzstd -lpthread -lole32 -luuid -lshell32"
    CACHE STRING "Windows kit: the libraries the game program links")

# main.cpp and the runtime's objects in the archive the program is linked
# from as a whole.
add_library(ttt2_windows_main OBJECT ${CMAKE_SOURCE_DIR}/runtime/src/main.cpp)
target_link_libraries(ttt2_windows_main PRIVATE cafe_runtime)
target_compile_options(ttt2_windows_main PRIVATE -Wall -Wextra -Wpedantic)
add_library(ttt2_windows_runtime STATIC $<TARGET_OBJECTS:cafe_runtime> $<TARGET_OBJECTS:ttt2_windows_main>)
set_target_properties(ttt2_windows_runtime PROPERTIES
    OUTPUT_NAME ttt2_runtime ARCHIVE_OUTPUT_DIRECTORY ${kit}/lib LINKER_LANGUAGE CXX)

set(kit_files)
# The static archives cafe_runtime links, by their names in kit/lib.
set(archive_targets cafe_rpx cafe_title zarchive amd_addrlib cafe_latte)
set(kit_archives libttt2_runtime.a)
set(kit_archive_files ${kit}/lib/libttt2_runtime.a)
foreach(target IN LISTS archive_targets)
    set(name lib${target}.a)
    add_custom_command(OUTPUT ${kit}/lib/${name}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${kit}/lib/${name}
        DEPENDS ${target} VERBATIM)
    list(APPEND kit_files ${kit}/lib/${name})
    list(APPEND kit_archives ${name})
    list(APPEND kit_archive_files ${kit}/lib/${name})
endforeach()

file(GLOB headers CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/runtime/include/cafe/*.h)
foreach(header IN LISTS headers)
    get_filename_component(name ${header} NAME)
    add_custom_command(OUTPUT ${kit}/include/cafe/${name}
        COMMAND ${CMAKE_COMMAND} -E copy ${header} ${kit}/include/cafe/${name}
        DEPENDS ${header} VERBATIM)
    list(APPEND kit_files ${kit}/include/cafe/${name})
endforeach()

# The program's manifest (UTF-8 code page, long paths, DPI awareness) as an
# object to link.
add_custom_command(OUTPUT ${kit}/lib/ttt2_manifest.o
    COMMAND ${CMAKE_RC_COMPILER} -O coff -i ttt2.rc -o ${kit}/lib/ttt2_manifest.o
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/runtime/windows
    DEPENDS ${CMAKE_SOURCE_DIR}/runtime/windows/ttt2.rc ${CMAKE_SOURCE_DIR}/runtime/windows/ttt2.manifest VERBATIM)
list(APPEND kit_files ${kit}/lib/ttt2_manifest.o)

# The DLLs are taken from the compiler's folder (MSYS2's bin/) and the
# packages' folders.
get_filename_component(compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(dll_dirs "${compiler_bin}" ${TTT2_DLL_DIRS})
foreach(prefix IN LISTS CMAKE_PREFIX_PATH)
    list(APPEND dll_dirs "${prefix}/bin")
endforeach()

add_custom_command(OUTPUT ${kit}/bin/cafe-recomp.exe
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:cafe-recomp> ${kit}/bin/cafe-recomp.exe
    COMMAND ${CMAKE_COMMAND} -DEXECUTABLE=${kit}/bin/cafe-recomp.exe "-DDIRECTORIES=${dll_dirs}"
        "-DOBJDUMP=${CMAKE_OBJDUMP}" -P ${CMAKE_SOURCE_DIR}/runtime/windows/copy_dlls.cmake
    DEPENDS cafe-recomp VERBATIM)
list(APPEND kit_files ${kit}/bin/cafe-recomp.exe)

# The generated code's stub: what the kit's archives use of it, empty
# (launcher/builder/kit_stub.cmake, shared with Linux's kit).
set(stub_source ${CMAKE_CURRENT_BINARY_DIR}/generated_stub.cpp)
add_custom_command(OUTPUT ${stub_source}
    COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} "-DARCHIVES=${kit_archive_files}" -DOUTPUT=${stub_source}
        -P ${CMAKE_SOURCE_DIR}/launcher/builder/kit_stub.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/launcher/builder/kit_stub.cmake ttt2_windows_runtime ${kit_archive_files} VERBATIM)

# (A cross build's target and flags, for the check.)
separate_arguments(cxx_flags UNIX_COMMAND "${CMAKE_CXX_FLAGS}")
if(CMAKE_CXX_COMPILER_TARGET)
    list(PREPEND cxx_flags --target=${CMAKE_CXX_COMPILER_TARGET})
endif()

# kit.ini, once the stub compiled and linked with its lines starts; the
# check program's DLLs are the game program's (kit/dlls).
add_custom_command(OUTPUT ${kit}/kit.ini ${kit}/lib/generated_stub.o
    COMMAND ${CMAKE_COMMAND} -DKIT=${kit} -DVERSION=${TTT2_WINDOWS_VERSION} "-DLIBRARIES=${TTT2_WINDOWS_LIBRARIES}"
        "-DCOMPILE=${TTT2_GENERATED_OPTIONS}" "-DARCHIVES=${kit_archives}"
        -DCXX=${CMAKE_CXX_COMPILER} "-DCXX_FLAGS=${cxx_flags}" -DSTUB_SOURCE=${stub_source}
        -DCHECK=${CMAKE_CURRENT_BINARY_DIR}/check/ttt2.exe "-DDLL_DIRS=${dll_dirs}" -DOBJDUMP=${CMAKE_OBJDUMP}
        -DCOPY_DLLS=${CMAKE_SOURCE_DIR}/runtime/windows/copy_dlls.cmake
        -P ${CMAKE_CURRENT_SOURCE_DIR}/kit_ini.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/kit_ini.cmake ${stub_source} ttt2_windows_runtime ${kit_archive_files}
        ${kit}/lib/ttt2_manifest.o ${kit}/bin/cafe-recomp.exe VERBATIM)
list(APPEND kit_files ${kit}/kit.ini ${kit}/lib/generated_stub.o)

find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
# The installer itself: ZArchive reads the .wua, OpenSSL checks the game's
# executable.
add_executable(ttt2-setup setup/main.cpp ${CMAKE_SOURCE_DIR}/runtime/src/wua.cpp setup/ttt2-setup.rc)
target_include_directories(ttt2-setup PRIVATE ${CMAKE_SOURCE_DIR}/runtime/src)
target_compile_features(ttt2-setup PRIVATE cxx_std_20)
target_link_libraries(ttt2-setup PRIVATE ZArchive::zarchive OpenSSL::Crypto ole32 uuid shell32)
target_compile_options(ttt2-setup PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(ttt2-setup PRIVATE -municode)
set_target_properties(ttt2-setup PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${kit})
add_custom_command(TARGET ttt2-setup POST_BUILD
    COMMAND ${CMAKE_COMMAND} "-DEXECUTABLE=$<TARGET_FILE:ttt2-setup>" "-DDIRECTORIES=${dll_dirs}"
        "-DOBJDUMP=${CMAKE_OBJDUMP}" -P ${CMAKE_SOURCE_DIR}/runtime/windows/copy_dlls.cmake
    VERBATIM)

add_custom_target(ttt2_windows_kit ALL DEPENDS ${kit_files} ttt2_windows_runtime ttt2-setup)
