workspace "blur"
   configurations { "Debug", "Release", "ReleaseWithDebugInfo" }
   system "windows"
	architecture "x86_64"

   project "blur"
      -- specify physical files paths to import
      files
      {
         "blur/**.cpp",
         "blur/**.h", "blur/**.hpp",
         
         "blur/**.rc", "blur/**.ico",
      }
      
      -- basic project settings
      language "C++"
      cppdialect "C++latest"
      kind "WindowedApp"
      location "blur"
      targetname "blur"
      targetdir "$(SolutionDir)bin/$(Configuration)/"
      objdir "!$(SolutionDir)build/$(Configuration)/" --use the "!" prefix to force a specific directory using msvs's provided environment variables instead of premake tokens
      characterset "Unicode"

      -- pch
      pchheader "pch.h"
      pchsource "blur/pch.cpp"

      -- vs-specific options
      filter "action:vs*"
         buildoptions { "/FI pch.h" }

      -- vc++ directories
      includedirs {
         "$(ProjectDir)",
         "$(ProjectDir)deps",
         "$(ProjectDir)deps/glfw/include",
         "$(ProjectDir)deps/freetype/include"
      }

      libdirs {
         "$(ProjectDir)deps",
         "$(ProjectDir)deps/glfw/win64",
         "$(ProjectDir)deps/freetype/win64"
      }

      -- build
      rtti "On"

      flags { "MultiProcessorCompile" }
      defines { "NOMINMAX", "WIN32_LEAN_AND_MEAN", "FMT_HEADER_ONLY" }

      -- linker
      links {
         "opengl32",
         "glfw3",
         "Gdiplus",
         "freetype",
         "Dwmapi"
      }

      -- configuration specific settings
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
