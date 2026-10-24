# Writes the Windows kit's kit.ini (cmake -P, from CMakeLists.txt): what
# ttt2-setup passes to the kit's compiler. ${KIT} stands for the kit's folder.
#
# compile    for each generated file, as the ttt2 target compiles them
# link       after the generated objects: the manifest, the runtime archive
#            as a whole, the other archives, the libraries by name
#
# It is written once the kit is known to work: the generated code's stub
# (STUB_SOURCE) compiled with these lines by this build's compiler (CXX),
# linked into a program (CHECK) that starts with the DLLs copied beside it.
# Those DLLs are the kit's dlls/, which ttt2-setup puts beside the game
# program.

set(compile_arguments -std=gnu++20 ${COMPILE} -I\${KIT}/include)
list(JOIN compile_arguments " " compile)

list(POP_FRONT ARCHIVES runtime)
set(link_arguments -fuse-ld=lld \${KIT}/lib/ttt2_manifest.o
    -Wl,--whole-archive \${KIT}/lib/${runtime} -Wl,--no-whole-archive)
foreach(archive IN LISTS ARCHIVES)
    list(APPEND link_arguments \${KIT}/lib/${archive})
endforeach()
separate_arguments(libraries UNIX_COMMAND "${LIBRARIES}")
list(APPEND link_arguments ${libraries})
list(JOIN link_arguments " " link)

string(REPLACE "\${KIT}" "${KIT}" compile_here "${compile_arguments}")
string(REPLACE "\${KIT}" "${KIT}" link_here "${link_arguments}")
set(stub_object ${KIT}/lib/generated_stub.o)
# (Its source too, for ttt2-setup --check: names of functions only.)
configure_file(${STUB_SOURCE} ${KIT}/lib/generated_stub.cpp COPYONLY)
execute_process(COMMAND ${CXX} ${CXX_FLAGS} ${compile_here} -c ${STUB_SOURCE} -o ${stub_object} RESULT_VARIABLE failed)
if(failed)
    message(FATAL_ERROR "kit.ini: the generated code's stub does not compile")
endif()
get_filename_component(check_dir ${CHECK} DIRECTORY)
file(MAKE_DIRECTORY ${check_dir})
execute_process(COMMAND ${CXX} ${CXX_FLAGS} -o ${CHECK} ${stub_object} ${link_here} RESULT_VARIABLE failed)
if(failed)
    message(FATAL_ERROR "kit.ini: the kit does not link with the generated code's stub")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -DEXECUTABLE=${CHECK} "-DDIRECTORIES=${DLL_DIRS}" -DOBJDUMP=${OBJDUMP}
    -P ${COPY_DLLS} RESULT_VARIABLE failed)
if(failed)
    message(FATAL_ERROR "kit.ini: cannot gather the game program's DLLs")
endif()
file(REMOVE_RECURSE ${KIT}/dlls)
file(GLOB dlls ${check_dir}/*.dll)
file(COPY ${dlls} DESTINATION ${KIT}/dlls)
if(CMAKE_HOST_WIN32)
    execute_process(COMMAND ${CHECK} --help RESULT_VARIABLE failed OUTPUT_QUIET ERROR_QUIET)
    if(failed)
        message(FATAL_ERROR "kit.ini: the program linked with the generated code's stub does not start (${CHECK} --help)")
    endif()
endif()

file(CONFIGURE OUTPUT ${KIT}/kit.ini CONTENT [[
# How ttt2-setup compiles and links the game program (made by
# windows/kit_ini.cmake). ${KIT} is this folder.
version = @VERSION@
compile = @compile@
link = @link@
]] @ONLY)
