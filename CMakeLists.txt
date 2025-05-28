cmake_minimum_required(VERSION 3.20)

set(VCPKG_LIBRARY_LINKAGE static)
if(WIN32)
  set(VCPKG_TARGET_TRIPLET
      "x64-windows-static"
      CACHE STRING "")
endif()

# Force static runtime on Windows
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(blur LANGUAGES C CXX)

# global settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
    ${PROJECT_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE})
set(CMAKE_BINARY_DIR ${PROJECT_SOURCE_DIR}/build/${CMAKE_BUILD_TYPE})

# packages
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(CLI11 CONFIG REQUIRED)
find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)
find_package(SDL3_image CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# source files
file(GLOB_RECURSE COMMON_SOURCES "src/common/*.cpp")
file(GLOB_RECURSE CLI_SOURCES "src/cli/*.cpp")
file(GLOB_RECURSE GUI_SOURCES "src/gui/*.cpp")
file(GLOB_RECURSE CLI_TEST_SOURCES "tests/cli/*.cpp")
list(FILTER CLI_TEST_SOURCES EXCLUDE REGEX
     "^.*/tests/plot_weighting_functions/.*$")

# Create CLI sources without main for tests
set(CLI_SOURCES_NO_MAIN ${CLI_SOURCES})
list(FILTER CLI_SOURCES_NO_MAIN EXCLUDE REGEX ".*main\\.cpp$")

# collect imgui sources
set(IMGUI_SOURCES
    dependencies/imgui/imgui.cpp
    dependencies/imgui/imgui_demo.cpp
    dependencies/imgui/imgui_draw.cpp
    dependencies/imgui/imgui_tables.cpp
    dependencies/imgui/imgui_widgets.cpp
    dependencies/imgui/backends/imgui_impl_sdl3.cpp
    dependencies/imgui/backends/imgui_impl_opengl3.cpp
    dependencies/imgui/misc/freetype/imgui_freetype.cpp)

file(GLOB_RECURSE RESOURCES "resources/*")

# common
add_library(blur-common STATIC ${COMMON_SOURCES})
target_include_directories(blur-common PUBLIC src)
target_link_libraries(
  blur-common
  PUBLIC nlohmann_json::nlohmann_json
         cpr::cpr
         Boost::system
         Boost::filesystem
         Threads::Threads
         tl::expected
         spdlog::spdlog)
target_precompile_headers(blur-common PUBLIC src/common/common_pch.h)
target_compile_definitions(
  blur-common PUBLIC NOMINMAX BOOST_FILESYSTEM_NO_LIB
                     BOOST_FILESYSTEM_STATIC_LINK=1 SPDLOG_NO_EXCEPTIONS)

# common settings
function(setup_target target)
  target_include_directories(${target} PRIVATE src)
  target_link_libraries(
    ${target}
    PRIVATE nlohmann_json::nlohmann_json
            cpr::cpr
            Boost::system
            Boost::filesystem
            Threads::Threads
            tl::expected
            spdlog::spdlog)
  target_compile_definitions(${target} PRIVATE NOMINMAX BOOST_FILESYSTEM_NO_LIB
                                               BOOST_FILESYSTEM_STATIC_LINK=1)
  if(WIN32)
    set(${target} resources/resources_win32.rc)
  endif()
  if(APPLE)
    target_link_libraries(${target} PRIVATE "-framework CoreFoundation")

    get_target_property(is_bundle ${target} MACOSX_BUNDLE)

    if(is_bundle)
      set(BUILD_RESOURCES_DIR
          "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources")
    else()
      set(BUILD_RESOURCES_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()
  else()
    set(BUILD_RESOURCES_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
  endif()

  # copy vapoursynth scripts
  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BUILD_RESOURCES_DIR}/lib
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/src/vapoursynth" ${BUILD_RESOURCES_DIR}/lib)
endfunction()

if(CMAKE_BUILD_TYPE STREQUAL Debug)
  add_definitions(-DDEBUGMODE -D_DEBUG)
else()
  add_definitions(-DNDEBUG)
endif()

# cli
add_executable(blur-cli ${CLI_SOURCES} ${RESOURCES})
target_link_libraries(blur-cli PRIVATE blur-common CLI11::CLI11)
target_precompile_headers(blur-cli PRIVATE src/cli/cli_pch.h)
setup_target(blur-cli)

# gui
add_executable(blur WIN32 ${GUI_SOURCES} ${IMGUI_SOURCES} ${RESOURCES})

# name the executable blur-gui on windows to stop nvidia from detecting it as a
# game (blur 2010)
if(WIN32)
  set_target_properties(blur PROPERTIES OUTPUT_NAME "blur-gui")
endif()

# imgui
target_include_directories(blur
                           PRIVATE ${PROJECT_SOURCE_DIR}/dependencies/imgui)
target_compile_definitions(blur PRIVATE IMGUI_IMPL_OPENGL_LOADER_CUSTOM)

# stb
target_include_directories(blur PRIVATE ${PROJECT_SOURCE_DIR}/dependencies/stb)

# mpv
find_package(PkgConfig REQUIRED)
pkg_search_module(MPV REQUIRED mpv>=0.33.0)
target_include_directories(blur PRIVATE ${MPV_INCLUDE_DIRS})
target_link_libraries(blur PRIVATE ${MPV_LIBRARIES})
target_link_directories(blur PRIVATE ${MPV_LIBRARY_DIRS})

target_link_libraries(
  blur PRIVATE blur-common SDL3::SDL3 SDL3_image::SDL3_image Freetype::Freetype
               glad::glad)

if(APPLE)
  # Disable precompiled headers for the .mm file and .cpp file
  set_source_files_properties(src/gui/os/desktop_notification_mac.mm
                              PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

  # Set Objective-C++ compilation for both files
  set_source_files_properties(src/gui/os/desktop_notification_mac.mm
                              PROPERTIES COMPILE_FLAGS "-x objective-c++")

  # Add the .mm file for macOS notifications
  target_sources(blur PRIVATE src/gui/os/desktop_notification_mac.mm)

  target_link_libraries(
    blur
    PRIVATE "-framework CoreVideo" "-framework CoreGraphics"
            "-framework Foundation" "-framework AppKit"
            "-framework UserNotifications")

  set(MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/src/Info.plist")

  set_target_properties(
    blur
    PROPERTIES MACOSX_BUNDLE TRUE
               MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/src/Info.plist
               MACOSX_BUNDLE_BUNDLE_NAME "Blur"
               MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
               MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
               MACOSX_BUNDLE_GUI_IDENTIFIER "com.f0e.blur"
               OUTPUT_NAME "Blur"
               XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
               "${CMAKE_SOURCE_DIR}/src/Blur.entitlements"
               XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
               XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES"
               XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS "YES"
               XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "--deep"
               XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES")

  # Specify the .icns file as the app icon
  set(MACOSX_BUNDLE_ICON_FILE blur.icns)

  # Ensure the .icns file is included in the app bundle's Resources folder
  set_source_files_properties(${CMAKE_SOURCE_DIR}/resources/blur.icns
                              PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

  # Link the .icns file to the target
  target_sources(blur PRIVATE ${CMAKE_SOURCE_DIR}/resources/blur.icns)

  # Add a post-build step that will run after the bundle is created
  add_custom_command(
    TARGET blur
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Copying resources to bundle..."
    # Make sure the destination directories exist
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:blur>/Contents/Resources/"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_BUNDLE_DIR:blur>/Contents/Resources/vapoursynth"
    # Copy ci/out directory if it exists
    COMMAND ${CMAKE_COMMAND} -E echo "Copying ci/out files..."
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/ci/out"
            "$<TARGET_BUNDLE_DIR:blur>/Contents/Resources/"
    # Copy vapoursynth scripts
    COMMAND ${CMAKE_COMMAND} -E echo "Copying vapoursynth files..."
    COMMAND
      ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/src/vapoursynth"
      "$<TARGET_BUNDLE_DIR:blur>/Contents/Resources/lib"
    # Verify the copy worked
    COMMAND ${CMAKE_COMMAND} -E echo "Resource copying complete"
    # The VERBATIM flag ensures command arguments are correctly escaped
    VERBATIM)
elseif(UNIX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(Systemd IMPORTED_TARGET GLOBAL libsystemd>=239)

  find_package(sdbus-c++ REQUIRED)

  target_link_libraries(blur PRIVATE X11 Xext Xrandr SDBusCpp::sdbus-c++)
elseif(WIN32)
  find_package(cppwinrt CONFIG REQUIRED)

  target_link_libraries(blur PRIVATE Shcore Microsoft::CppWinRT windowsapp)
endif()

target_precompile_headers(blur PRIVATE src/gui/gui_pch.h)
setup_target(blur)

# tests
enable_testing()
add_executable(blur-tests ${CLI_SOURCES_NO_MAIN} ${CLI_TEST_SOURCES})
target_link_libraries(blur-tests PRIVATE blur-common GTest::gtest
                                         GTest::gtest_main CLI11::CLI11)
target_precompile_headers(blur-tests PRIVATE tests/cli/cli_test_pch.h)
setup_target(blur-tests)

include(CTest)
include(GoogleTest)
gtest_discover_tests(blur-tests)
