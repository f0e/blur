workspace "blur"
   configurations { "Debug", "Release", "ReleaseWithDebugInfo" }
	architecture "x86_64"

   -- basic project settings
   language "C++"
   cppdialect "C++latest"
   targetdir "$(SolutionDir)bin/$(ProjectName)/$(Configuration)/"
   objdir "!$(SolutionDir)build/$(ProjectName)/$(Configuration)/" --use the "!" prefix to force a specific directory using msvs's provided environment variables instead of premake tokens
   characterset "Unicode"
   staticruntime "On"

   -- build
   rtti "On"

   flags { "MultiProcessorCompile" }
   defines { "NOMINMAX", "FMT_HEADER_ONLY" }

   -- vc++ directories
   includedirs {
      "src"
   }

   files {
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
   pchheader "cli_pch.h"
   pchsource "src/cli/cli_pch.cpp"
   forceincludes { "src/cli/cli_pch.h" }

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
   pchheader "gui_pch.h"
   pchsource "src/gui/gui_pch.cpp"
   forceincludes { "src/gui/gui_pch.h" }

   -- import gui files
   files {
      "src/gui/**.cpp",
      "src/gui/**.h", "src/gui/**.hpp",
   }

   -- linker
   links {
      "opengl32",
      "glfw3",
      "Dwmapi"
   }

-- fix vcpkg in visual studio (set triplet)
filter "action:vs*"
   local function vcpkg(prj)
         premake.w('<VcpkgTriplet Condition="\'$(Platform)\'==\'x64\'">x64-windows-static</VcpkgTriplet>')
         premake.w('<VcpkgEnabled>true</VcpkgEnabled>')
   end

   require('vstudio')
   local vs = premake.vstudio.vc2010
   premake.override(premake.vstudio.vc2010.elements, "globals", function(base, prj)
         local calls = base(prj)
         table.insertafter(calls, vs.globals, vcpkg)
         return calls
   end)