# header-only library
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO powerof3/CLibUtil
    REF e1eabe6a063a3abcd3e5b1e739e6ca842fc9a67c
    SHA512 278bcf1d5e4c140c907784f94fdcf7f0819e8f71dd2c67df6318c99ff9521042b9e13617f62c46db1e597c8a64c07f06f49d689cf60fb530f9521ac5fcf7d12f
    HEAD_REF master
)

# Install codes
# Upstream's folder is CLIBUtil, includes say ClibUtil: install under the include
# spelling so case-sensitive hosts (the local Linux build) find it too.
file(INSTALL ${SOURCE_PATH}/include/CLIBUtil/ DESTINATION ${CURRENT_PACKAGES_DIR}/include/ClibUtil)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
