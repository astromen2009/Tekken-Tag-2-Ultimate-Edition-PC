# Copies the libraries a Windows executable needs (SDL, shaderc, OpenSSL,
# the C++ runtime...) beside it, so it starts from its folder and
# `ttt2 --install` takes them along. Windows' own libraries and the Vulkan
# loader, which comes with the GPU driver, stay where they are.
#
#   cmake -DEXECUTABLE=<exe> -DDIRECTORIES=<dir;...> -DOBJDUMP=<objdump> -P copy_dlls.cmake

set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM windows+pe)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OBJDUMP}")

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${EXECUTABLE}"
    DIRECTORIES ${DIRECTORIES}
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-" "^vulkan-1\\.dll$"
    POST_EXCLUDE_REGEXES "[/\\\\][Ss]ystem32[/\\\\]" "[/\\\\][Ww]indows[/\\\\]")

get_filename_component(destination "${EXECUTABLE}" DIRECTORY)
foreach(library IN LISTS resolved)
    file(COPY "${library}" DESTINATION "${destination}")
endforeach()

# Windows' own libraries are not found in DIRECTORIES when cross-compiling.
foreach(library IN LISTS unresolved)
    string(TOLOWER "${library}" name)
    if(NOT name MATCHES "^(kernel32|user32|gdi32|advapi32|shell32|ole32|oleaut32|shlwapi|ws2_32|crypt32|bcrypt|ucrtbase|version|imm32|setupapi|winmm|cfgmgr32|hid|dwmapi|uxtheme|msvcrt|ntdll|comdlg32|winspool|imagehlp|dbghelp|powrprof|userenv|rpcrt4|secur32|ncrypt|iphlpapi|dxgi|d3d11|d3d12|dinput8|dsound|xinput1_4)\\.dll$"
       AND NOT name MATCHES "^(api-ms-|ext-ms-)")
        message(WARNING "copy_dlls: ${library} not found in ${DIRECTORIES}")
    endif()
endforeach()
