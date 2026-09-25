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

# the port turns these off, but a desktop app needs them (libdecor for wayland title bars, xrandr for refresh rate,
# etc). SDL loads them at runtime
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
