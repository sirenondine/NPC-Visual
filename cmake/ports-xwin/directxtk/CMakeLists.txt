cmake_minimum_required(VERSION 3.21)
project(DirectXTK LANGUAGES CXX)

find_package(directxmath CONFIG REQUIRED)

set(DXTK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
add_library(DirectXTK STATIC "${DXTK_ROOT}/Src/SimpleMath.cpp")
target_compile_features(DirectXTK PUBLIC cxx_std_17)
target_include_directories(DirectXTK
    PUBLIC
        $<BUILD_INTERFACE:${DXTK_ROOT}/Inc>
        $<INSTALL_INTERFACE:include/directxtk>
    PRIVATE
        "${DXTK_ROOT}/Src")
target_link_libraries(DirectXTK PUBLIC Microsoft::DirectXMath)

install(TARGETS DirectXTK EXPORT directxtk-targets ARCHIVE DESTINATION lib)
install(FILES "${DXTK_ROOT}/Inc/SimpleMath.h" "${DXTK_ROOT}/Inc/SimpleMath.inl" DESTINATION include/directxtk)
install(EXPORT directxtk-targets FILE directxtk-targets.cmake NAMESPACE Microsoft:: DESTINATION share/directxtk)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/directxtk-config.cmake"
    "include(CMakeFindDependencyMacro)\nfind_dependency(directxmath CONFIG)\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/directxtk-targets.cmake\")\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/directxtk-config.cmake" DESTINATION share/directxtk)
