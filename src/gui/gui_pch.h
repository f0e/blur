#pragma once

#include <common/common_pch.h>

// blur
#include "drawing/datatypes/datatypes.h">

// dependencies
#define GL_SILENCE_DEPRECATION
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <glfw/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#define GLFW_NATIVE_INCLUDE_NONE
#include <glfw/glfw3native.h>

// resources
#include "../resources/resource.h"