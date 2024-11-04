cmake_minimum_required(VERSION 3.20)

set(VCPKG_LIBRARY_LINKAGE static)

# Force static runtime on Windows
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(blur LANGUAGES CXX)

# global settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
set(CMAKE_BINARY_DIR ${PROJECT_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE})

# packages
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(CLI11 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

# source files
file(GLOB_RECURSE COMMON_SOURCES 
    "src/common/*.cpp"
    "src/common/*.hpp"
    "src/common/*.h"
)

file(GLOB_RECURSE CLI_SOURCES 
    "src/cli/*.cpp"
    "src/cli/*.hpp"
    "src/cli/*.h"
)

file(GLOB_RECURSE GUI_SOURCES 
    "src/gui/*.cpp"
    "src/gui/*.hpp"
    "src/gui/*.h"
)

file(GLOB_RECURSE RESOURCES
    "resources/*"
)

# common settings
function(setup_target target)
    target_include_directories(${target} PRIVATE src)
    target_link_libraries(${target} PRIVATE
        # Gdiplus
        fmt::fmt
        nlohmann_json::nlohmann_json
        Boost::system Boost::filesystem
        CLI11::CLI11
    )
    target_compile_definitions(${target} PRIVATE 
        NOMINMAX
        FMT_HEADER_ONLY
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
    )
endfunction()

# cli
add_executable(blur-cli 
    ${COMMON_SOURCES}
    ${CLI_SOURCES}
    ${RESOURCES}
    src/cli/cli_pch.cpp
)
target_precompile_headers(blur-cli PRIVATE src/cli/cli_pch.h)
setup_target(blur-cli)

# gui
set(LAF_BACKEND "skia")
set(SKIA_DIR ${PROJECT_SOURCE_DIR}/dependencies/skia)
set(SKIA_LIBRARY_DIR ${SKIA_DIR}/out/Release-arm64)

set(LAF_WITH_EXAMPLES OFF) # disable examples
set(LAF_WITH_TESTS OFF) # disable tests

add_subdirectory(dependencies/laf)

add_executable(blur-gui WIN32
    ${COMMON_SOURCES}
    ${GUI_SOURCES}
    ${RESOURCES}
    src/gui/gui_pch.cpp
)
target_link_libraries(blur-gui PRIVATE
    laf-base
    laf-gfx
    laf-os
    ZLIB::ZLIB
)
target_precompile_headers(blur-gui PRIVATE src/gui/gui_pch.h)
set_target_properties(blur-gui PROPERTIES LINK_FLAGS "${LAF_BACKEND_LINK_FLAGS}")
set_target_properties(blur-gui PROPERTIES OUTPUT_NAME "blur")
setup_target(blur-gui)