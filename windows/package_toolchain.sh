#!/bin/bash
# Copies the compiler the Windows kit builds the game program with into
# KIT/toolchain, from MSYS2's CLANG64 environment (run it in that shell):
# Clang and LLD with the DLLs they need, the MinGW headers and libraries,
# libc++, compiler-rt, and the import libraries of the libraries the game
# program links (SDL3, the Vulkan loader, shaderc, OpenSSL, zlib, zstd).
# The packages' files only, without documentation, so the kit's compiler is
# the one the kit's runtime was built with.
#
#   windows/package_toolchain.sh KIT

set -euo pipefail
kit="$1"
dest="$kit/toolchain"
prefix=mingw-w64-clang-x86_64
packages=(clang lld libc++ libunwind compiler-rt crt headers winpthreads
          sdl3 shaderc vulkan-loader openssl zlib zstd)

# The packages with everything they need, MSYS2's own (msys) ones left out.
list=$(for p in "${packages[@]}"; do pactree -u -l "$prefix-$p"; done | grep "^$prefix-" | sort -u)
echo "toolchain packages:" $list

rm -rf "$dest"
mkdir -p "$dest"
for package in $list; do
    pacman -Qlq "$package" | while read -r file; do
        [[ -f "$file" ]] || continue
        relative="${file#/clang64/}"
        [[ "$relative" == "$file" ]] && continue
        case "$relative" in
            share/*|lib/cmake/*|lib/pkgconfig/*|*.pdb|lib/python*|etc/*|libexec/*) continue ;;
            bin/*.dll) ;;
            # Of the programs, the compiler and the linker only.
            bin/*) [[ "${relative#bin/}" =~ ^(clang|clang\+\+|clang-[0-9]+|ld\.lld|lld)\.exe$ ]] || continue ;;
        esac
        mkdir -p "$dest/$(dirname "$relative")"
        cp "$file" "$dest/$relative"
    done
done
# clang++ as a program of its own name (MSYS2 may link it to clang).
[[ -f "$dest/bin/clang++.exe" ]] || cp "$dest/bin/clang.exe" "$dest/bin/clang++.exe"
[[ -f "$dest/bin/ld.lld.exe" ]] || cp "$dest/bin/lld.exe" "$dest/bin/ld.lld.exe"
du -sh "$dest"
