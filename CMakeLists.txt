# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(RALLYBOX_PRODUCT_NAME "RallyBox-Dashboard")

file(READ "${CMAKE_CURRENT_LIST_DIR}/VERSION" RALLYBOX_PROJECT_VERSION_RAW)
string(STRIP "${RALLYBOX_PROJECT_VERSION_RAW}" RALLYBOX_PROJECT_VERSION)
if(NOT RALLYBOX_PROJECT_VERSION)
    set(RALLYBOX_PROJECT_VERSION "0.0.0")
endif()
set(PROJECT_VER "${RALLYBOX_PROJECT_VERSION}")
add_compile_definitions(RALLYBOX_FW_VERSION="${RALLYBOX_PROJECT_VERSION}")
add_compile_definitions(RALLYBOX_PRODUCT_NAME="${RALLYBOX_PRODUCT_NAME}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    ./components/bsp_extra
    )

add_compile_options(-Wno-format)
project(${RALLYBOX_PRODUCT_NAME})
