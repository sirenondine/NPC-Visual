# vcpkg triplet: static Windows x64 libraries built on Linux with clang-cl + xwin.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
# Empty VCPKG_CMAKE_SYSTEM_NAME is how vcpkg marks a Windows target (VCPKG_TARGET_IS_WINDOWS).
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../clang-cl-xwin.cmake")
set(VCPKG_ENV_PASSTHROUGH INCLUDE LIB)
# Ports whose cmake_minimum_required predates 3.15 (miniz) leave CMP0091 OLD, which ignores
# CMAKE_MSVC_RUNTIME_LIBRARY and builds /MD; the link then misses dllimport CRT symbols.
# Has to come in on the command line: the toolchain file loads after cmake_minimum_required.
list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_POLICY_DEFAULT_CMP0091=NEW)
