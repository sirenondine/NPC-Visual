# Cross-compile Windows x64 SKSE plugins on Linux with clang-cl + lld-link.
# The MSVC CRT and Windows SDK come from `xwin splat`; clang-cl, lld-link and llvm-rc
# find them through the INCLUDE and LIB environment variables, which
# tools/build-local.sh exports for every configure and build.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_AR llvm-lib)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)

# Static CRT everywhere (vcpkg ports included): SKSE plugins link /MT, and this toolchain
# replaces vcpkg's own windows.cmake, which would otherwise map VCPKG_CRT_LINKAGE for us.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
