#include "gui.h"

#define GL_SILENCE_DEPRECATION
#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <glfw/glfw3.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#define GLFW_NATIVE_INCLUDE_NONE
#include <GLFW/glfw3native.h>

#include "drawing/drawing.h"

#include "resource.h"

#include <dwmapi.h>

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

void add_file(std::filesystem::path path) {
    std::wcout << path << std::endl;

    rendering.queue_render(std::make_shared<c_render>(path));
}

void drop_callback(GLFWwindow* window, int count, const char** paths) {
    for (int i = 0; i < count; i++) {
        std::wstring path_str = helpers::towstring(paths[i]).c_str();

        add_file(std::filesystem::path(path_str));
    }
}

WNDPROC o_wnd_proc;
static LRESULT CALLBACK wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static POINTS start_drag_pos;
    switch (msg)
    {
    case WM_MOUSEMOVE:
        // dragging window
        if (wParam == MK_LBUTTON) {
            POINTS p = MAKEPOINTS(lParam);
            RECT rect;
            GetWindowRect(hWnd, &rect);

            rect.left += p.x - start_drag_pos.x;
            rect.top += p.y - start_drag_pos.y;

            SetWindowPos(hWnd, HWND_TOPMOST, rect.left, rect.top, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOZORDER);
        }
        
        break;
    case WM_LBUTTONDOWN:
        start_drag_pos = MAKEPOINTS(lParam);
        break;
    }

    return CallWindowProc(o_wnd_proc, hWnd, msg, wParam, lParam);
}

void gui::run() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return;

    // GL 3.0 + GLSL 130
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // glfwWindowHint(GLFW_DECORATED, GLFW_FALSE); // remove window border
    // glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE); // transparent window

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(w, h, "blur", nullptr, nullptr);
    if (window == nullptr)
        return;

    HWND hwnd = glfwGetWin32Window(window);
    SetClassLongPtr(hwnd, GCLP_HICON, (LONG_PTR)LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1)));

    BOOL value = TRUE;
    ::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    glfwSetDropCallback(window, drop_callback);

    drawing::init(window);

    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    o_wnd_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtr((HWND)main_viewport->PlatformHandleRaw, GWLP_WNDPROC, (LONG_PTR)wnd_proc));

    while (!glfwWindowShouldClose(window))
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();

        glfwGetWindowSize(window, &w, &h);

        drawing::imgui.begin();

        const static int spacing = 5;
        const static int pad = 25;
        int y = 0;
        auto draw_str_temp = [&y](std::string text, s_colour colour = s_colour::white()) {
            const auto font = fonts::mono;

            const int available_width = w - pad * 2;
            drawing::clip_string(text, font, available_width);

            drawing::string(s_point(pad, pad + y), 0, colour, font, text);

            y += font.calc_size(text).h + spacing;
        };

        draw_str_temp("blur");
        draw_str_temp("drop a file...");

        if (!rendering.queue.empty()) {
            for (const auto [i, render] : helpers::enumerate(rendering.queue)) {
                bool current = render == rendering.current_render;

                std::string name_str = helpers::tostring(render->get_video_name());
                draw_str_temp(name_str, s_colour::white(current ? 255 : 100));

                if (current) {
                    auto render_status = render->get_status();

                    if (render_status.init) {
                        float progress = render_status.current_frame / (float)render_status.total_frames;

                        auto current_time = std::chrono::high_resolution_clock::now();
                        std::chrono::duration<double> frame_duration = current_time - render_status.start_time;
                        double elapsed_time = frame_duration.count();

                        double calced_fps = render_status.current_frame / elapsed_time;

                        draw_str_temp(fmt::format("{:.1f}% complete ({}/{}, {:.2f} fps)\n", progress * 100, render_status.current_frame, render_status.total_frames, calced_fps));
                    }
                }
            }
        }

        drawing::imgui.end();
        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    open = false;
    rendering.stop_rendering();
}
