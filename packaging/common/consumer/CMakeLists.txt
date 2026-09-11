# External consumer for the packaging lifecycle checks.
#
# This project is configured from a directory OUTSIDE the GlyphaStore checkout,
# against an installed package only. It must therefore resolve everything through
# the installed CMake package and the installed pkg-config file; any include or
# link path pointing back into the source tree is a contamination bug and is
# refused by engineering/tools/assert_consumer_isolation.py.
cmake_minimum_required(VERSION 3.25)

project(GlyphaStorePackagedConsumer LANGUAGES C CXX)

find_package(GlyphaStore CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLYPHASTORE_ABI REQUIRED IMPORTED_TARGET glyphastore-abi)

add_executable(glyphastore_daemon_smoke daemon_smoke.cpp)
target_link_libraries(glyphastore_daemon_smoke PRIVATE GlyphaStore::client)
target_compile_features(glyphastore_daemon_smoke PRIVATE cxx_std_23)

# Linked through pkg-config on purpose: it proves the installed .pc file resolves
# its own prefix without help from the build tree.
add_executable(glyphastore_abi_smoke abi_smoke.c)
target_link_libraries(glyphastore_abi_smoke PRIVATE PkgConfig::GLYPHASTORE_ABI)
set_target_properties(glyphastore_abi_smoke PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

enable_testing()
add_test(NAME glyphastore_abi_smoke COMMAND glyphastore_abi_smoke)
