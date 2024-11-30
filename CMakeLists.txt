cmake_minimum_required(VERSION 3.20)

set(VCPKG_LIBRARY_LINKAGE static)
if (WIN32)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
endif()

# Force static runtime on Windows
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(blur LANGUAGES CXX)

# global settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
    ${PROJECT_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
set(CMAKE_BINARY_DIR ${PROJECT_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE})

# packages
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(CLI11 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

# source files
file(
  GLOB_RECURSE
  COMMON_SOURCES
  "src/common/*.cpp"
  "src/common/*.hpp"
  "src/common/*.h")

file(
  GLOB_RECURSE
  CLI_SOURCES
  "src/cli/*.cpp"
  "src/cli/*.hpp"
  "src/cli/*.h")

file(
  GLOB_RECURSE
  GUI_SOURCES
  "src/gui/*.cpp"
  "src/gui/*.hpp"
  "src/gui/*.h")

file(GLOB_RECURSE RESOURCES "resources/*")

# common settings
function(setup_target target)
  target_include_directories(${target} PRIVATE src)
  target_link_libraries(
    ${target}
    PRIVATE nlohmann_json::nlohmann_json
            Boost::system
            Boost::filesystem
            Threads::Threads)
  target_compile_definitions(
    ${target}
    PRIVATE NOMINMAX
            BOOST_FILESYSTEM_NO_LIB BOOST_FILESYSTEM_STATIC_LINK=1 CLI11_COMPILE
            $<$<CONFIG:Debug>:_DEBUG>
            $<$<CONFIG:Release>:NDEBUG>)
  if (WIN32)
    set(${target}
      resources/resources_win32.rc)
  endif()
endfunction()

# cli
add_executable(
  blur-cli
  ${COMMON_SOURCES}
  ${CLI_SOURCES}
  ${RESOURCES}
  src/cli/cli_pch.cpp)
target_link_libraries(
  blur-cli
  PRIVATE CLI11::CLI11)
target_precompile_headers(blur-cli PRIVATE src/cli/cli_pch.h)
setup_target(blur-cli)

# gui
set(LAF_BACKEND "skia")
set(SKIA_DIR ${PROJECT_SOURCE_DIR}/dependencies/skia CACHE PATH "Path to Skia library directory")

# download platform specific skia to dependencies/skia
include(FetchContent)

if(UNIX AND NOT APPLE)
  set(SKIA_REPOSITORY
      https://github.com/aseprite/skia/releases/download/m102-861e4743af/Skia-Linux-Release-x64-libstdc++.zip
  )
elseif(APPLE)
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64" OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    set(SKIA_REPOSITORY
        https://github.com/aseprite/skia/releases/download/m102-861e4743af/Skia-macOS-Release-arm64.zip
    )
  elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64" OR CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
    set(SKIA_REPOSITORY
        https://github.com/aseprite/skia/releases/download/m102-861e4743af/Skia-macOS-Release-x64.zip
    )
  else()
    message(FATAL_ERROR "Unsupported macOS architecture: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
  endif()
elseif(WIN32)
  set(SKIA_REPOSITORY
      https://github.com/aseprite/skia/releases/download/m102-861e4743af/Skia-Windows-Release-x64.zip
  )
endif()

FetchContent_Declare(skia
  URL ${SKIA_REPOSITORY}
  SOURCE_DIR ${SKIA_DIR}
  DOWNLOAD_EXTRACT_TIMESTAMP true
)

FetchContent_GetProperties(skia)
if(NOT skia_POPULATED)
  FetchContent_MakeAvailable(skia)
endif()

set(LAF_WITH_EXAMPLES OFF) # disable examples
set(LAF_WITH_TESTS OFF) # disable tests

add_subdirectory(dependencies/laf)

add_executable(
  blur-gui WIN32
  ${COMMON_SOURCES}
  ${GUI_SOURCES}
  ${RESOURCES}
  src/gui/gui_pch.cpp)
target_link_libraries(
  blur-gui
  PRIVATE laf-base
          laf-gfx
          laf-os
          ZLIB::ZLIB)
target_precompile_headers(blur-gui PRIVATE src/gui/gui_pch.h)

if(APPLE)
    target_link_libraries(blur-gui PRIVATE
        "-framework CoreVideo"
        "-framework CoreGraphics"
    )

    # set(MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/src/Info.plist")

    # set_target_properties(blur-gui PROPERTIES
    #   MACOSX_BUNDLE TRUE
    #   MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/src/Info.plist
    # )

    # Specify the .icns file as the app icon
    set(MACOSX_BUNDLE_ICON_FILE blur.icns)

    # Ensure the .icns file is included in the app bundle's Resources folder
    set_source_files_properties(resources/blur.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    # Link the .icns file to the target
    target_sources(blur-gui PRIVATE resources/blur.icns)
endif()

set_target_properties(blur-gui PROPERTIES LINK_FLAGS
                                          "${LAF_BACKEND_LINK_FLAGS}")
set_target_properties(blur-gui PROPERTIES OUTPUT_NAME "blur")
setup_target(blur-gui)
