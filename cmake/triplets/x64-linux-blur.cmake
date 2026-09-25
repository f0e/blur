set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_FIXUP_ELF_RPATH ON)

include("${CMAKE_CURRENT_LIST_DIR}/blur-angle-dynamic.cmake")

# link the c++ runtime statically, so builds run on distros whose libstdc++ is older than the compiler's
set(
  VCPKG_LINKER_FLAGS
  "-static-libstdc++ -static-libgcc -Wl,--exclude-libs,libstdc++.a:libgcc.a"
)

# the port turns these off so the build machine can't change what SDL supports, but a desktop window needs them.
# libdecor draws title bars on wayland desktops that leave it to apps (gnome), xcursor gives themed cursors, xrandr
# gives the refresh rate vsync timing uses, and xinput2, xfixes and xsync give smooth scrolling, pointer confinement
# and clean resizing on x11. SDL loads them at runtime so none are needed to run, and fails to configure if their
# headers are missing rather than leaving them out
if(PORT STREQUAL "sdl3")
  set(
    VCPKG_CMAKE_CONFIGURE_OPTIONS
    -DSDL_WAYLAND_LIBDECOR=ON
    -DSDL_X11_XCURSOR=ON
    -DSDL_X11_XFIXES=ON
    -DSDL_X11_XINPUT=ON
    -DSDL_X11_XRANDR=ON
    -DSDL_X11_XSYNC=ON
  )
endif()
