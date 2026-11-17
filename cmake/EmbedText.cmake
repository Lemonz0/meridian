# Turns a text file into a C++ header with a NUL-terminated byte array.
# Usage: cmake -DIN=<file> -DOUT=<header> -DNAME=<identifier> -P EmbedText.cmake
# A byte array (not a string literal) because MSVC caps string literals at 64 KB.
file(READ "${IN}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
get_filename_component(name_only "${IN}" NAME)
file(WRITE "${OUT}.tmp" "#pragma once\n// Generated from ${name_only} by cmake/EmbedText.cmake. Do not edit.\ninline constexpr unsigned char ${NAME}[] = {${bytes}0x00};\n")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
