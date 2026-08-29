if(PORT STREQUAL "angle")
  # SDL loads EGL dynamically, so ANGLE must remain a shared library. Keep the
  # rest of Blur's vcpkg dependencies statically linked.
  set(VCPKG_LIBRARY_LINKAGE dynamic)
else()
  set(VCPKG_LIBRARY_LINKAGE static)
endif()
