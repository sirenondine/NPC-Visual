# Local clang-cl/xwin builds only (preset "local"). CommonLib uses nothing from DirectXTK
# but SimpleMath, and the full library compiles its shaders with fxc via CompileShaders.cmd,
# which cannot run on Linux. Same source as the official port at our vcpkg baseline.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Microsoft/DirectXTK
    REF jul2025
    SHA512 b91bed656306e9fdcba5a0de20c32c53f3783a51fe9283c4ee04362941e4e039787ca050c7a7effb35a30a274b4b3235738cb73acdc0e31aba714ad87bc7dbfe
    HEAD_REF main
)

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}/simplemath")
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}/simplemath")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH share/directxtk)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
