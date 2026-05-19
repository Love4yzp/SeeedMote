# SeeedMote v2 — gateway_basic_esp32s3 ESP-IDF project root.
#
# Required by ESP-IDF / PlatformIO. Pin PROJECT_VER so configure does not
# depend on git_describe (PIO ships ESP-IDF as a flat tarball with no git
# metadata for component submodules — without this pin, CMake configure
# aborts with "fatal: not a git repository").

cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gateway_basic_esp32s3)
