set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_FIXUP_ELF_RPATH ON)

include("${CMAKE_CURRENT_LIST_DIR}/blur-angle-dynamic.cmake")

# link the c++ runtime statically, so builds run on distros whose libstdc++ is older than the compiler's
set(
  VCPKG_LINKER_FLAGS
  "-static-libstdc++ -static-libgcc -Wl,--exclude-libs,libstdc++.a:libgcc.a"
)
