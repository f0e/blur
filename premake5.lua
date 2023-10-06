workspace "blur"
   configurations { "Debug", "Release", "ReleaseWithDebugInfo" }
   system "windows"
	architecture "x86_64"

   -- basic project settings
   language "C++"
   cppdialect "C++latest"
   targetdir "$(SolutionDir)bin/$(ProjectName)/$(Configuration)/"
   objdir "!$(SolutionDir)build/$(ProjectName)/$(Configuration)/" --use the "!" prefix to force a specific directory using msvs's provided environment variables instead of premake tokens
   characterset "Unicode"

   -- build
   rtti "On"

   flags { "MultiProcessorCompile" }
   defines { "NOMINMAX", "WIN32_LEAN_AND_MEAN", "FMT_HEADER_ONLY" }

   -- vc++ directories
   includedirs {
      "lib",
      "src"
   }

   libdirs {
      "lib",
   }

   files {
      "lib/**.h", "lib/**.hpp",
      
      "src/common/**.cpp",
      "src/common/**.h", "src/common/**.hpp",
      
      "resources/**"
   }

   -- linker
   links {
      "Gdiplus"
   }

-- configurations
filter "configurations:Debug"
   symbols "Full"
   defines { "_CONSOLE", "_DEBUG" }

filter "configurations:Release"
   symbols "Off"
   optimize "On"
   defines { "NDEBUG" }
   flags { "LinkTimeOptimization" }

filter "configurations:ReleaseWithDebugInfo"
   symbols "Full"
   optimize "On"
   defines { "NDEBUG" }
   flags { "LinkTimeOptimization" }

-- projects
project "blur-cli"
   -- basic project settings
   kind "ConsoleApp"
   location "src/cli"
   targetname "blur-cli"

   defines { "BLUR_CLI" }

   -- pch
   pchheader "pch.h"
   pchsource "src/cli/pch.cpp"
   forceincludes { "src/cli/pch.h" }

   -- import cli files
   files {
      "src/cli/**.cpp",
      "src/cli/**.h", "src/cli/**.hpp",
   }

project "blur-gui"
   -- basic project settings
   kind "WindowedApp"
   location "src/gui"
   targetname "blur"

   defines { "BLUR_GUI" }

   -- pch
   pchheader "pch.h"
   pchsource "src/gui/pch.cpp"
   forceincludes { "src/gui/pch.h" }

   -- import gui files
   files {
      "src/gui/**.cpp",
      "src/gui/**.h", "src/gui/**.hpp",

      "lib/imgui/**.cpp",
   }

   -- vc++ directories
   includedirs {
      "lib/glfw/include",
      "lib/freetype/include"
   }

   libdirs {
      "lib/glfw/win64",
      "lib/freetype/win64"
   }

   -- linker
   links {
      "opengl32",
      "glfw3",
      "freetype",
      "Dwmapi"
   }
