if(PORT STREQUAL "angle")
  # keep ANGLE as a dynamic library since SDL needs it to be
  set(VCPKG_LIBRARY_LINKAGE dynamic)
else()
  set(VCPKG_LIBRARY_LINKAGE static)
endif()
