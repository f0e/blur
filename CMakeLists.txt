cmake_minimum_required(VERSION 3.20)
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
find_package(cxxopts CONFIG REQUIRED)

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
        cxxopts::cxxopts
    )
    target_compile_definitions(${target} PRIVATE 
        NOMINMAX 
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

# # gui
# add_executable(blur-gui WIN32
#     ${COMMON_SOURCES}
#     ${GUI_SOURCES}
#     ${RESOURCES}
#     src/gui/gui_pch.cpp
# )
# target_precompile_headers(blur-gui PRIVATE src/gui/gui_pch.h)
# target_link_libraries(blur-gui PRIVATE opengl32 glfw3 Dwmapi)
# set_target_properties(blur-gui PROPERTIES OUTPUT_NAME "blur")
# setup_target(blur-gui)