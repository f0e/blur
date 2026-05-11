#pragma once

// NOLINTEND(misc-include-cleaner)
#include <common/common_pch.h> // still need to include here cause cmake cant inherit common projects pch + have project-specific pch

#include "render/primitives/color.h"
#include "render/primitives/point.h"
#include "render/primitives/rect.h"
#include "render/primitives/size.h"
#include "render/primitives/primitives_impl.h"

#include <glad/glad.h>

#include <SDL3/SDL.h>
// #include <SDL3/SDL_opengl.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_surface.h>

#define IMGUI_USER_CONFIG "gui/render/imconfig.h"

#include "gui_utils.h"
