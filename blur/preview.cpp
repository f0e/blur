#include "preview.h"

#include <objidl.h>
#include <gdiplus.h>

#include <thread>
#include <filesystem>

#include "console.h"

c_preview preview;

bool drew_image = false;
// const int offset_x = 100;
// const int offset_y = 100;

using namespace Gdiplus;
#pragma comment (lib, "Gdiplus.lib")

std::wstring towstring(const std::string& v) {
    std::wstring out(v.size() + 1, L'\0');

    int size = MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, &out[0], out.size());

    out.resize(size - 1);
    return out;
}

// http://playtechs.blogspot.com/2007/10/forcing-window-to-maintain-particular.html
int window_ratio_x;
int window_ratio_y;
int g_window_adjust_x;
int g_window_adjust_y;
void resize(int edge, RECT& rect) {
    int size_x_desired = (rect.right - rect.left) - g_window_adjust_x;
    int size_y_desired = (rect.bottom - rect.top) - g_window_adjust_y;

    switch (edge) {
    case WMSZ_BOTTOM:
    case WMSZ_TOP: {
        int size_x = g_window_adjust_x + (size_y_desired * window_ratio_x) / window_ratio_y;
        rect.left = (rect.left + rect.right) / 2 - size_x / 2;
        rect.right = rect.left + size_x;

        break;
    }
    case WMSZ_BOTTOMLEFT: {
        int size_x, size_y;

        if (size_x_desired * window_ratio_y > size_y_desired * window_ratio_x) {
            size_x = rect.right - rect.left;
            size_y = g_window_adjust_y + ((size_x - g_window_adjust_x) * window_ratio_y) / window_ratio_x;
        }
        else {
            size_y = rect.bottom - rect.top;
            size_x = g_window_adjust_x + ((size_y - g_window_adjust_y) * window_ratio_x) / window_ratio_y;
        }

        rect.left = rect.right - size_x;
        rect.bottom = rect.top + size_y;

        break;
    }
    case WMSZ_BOTTOMRIGHT: {
        int size_x, size_y;

        if (size_x_desired * window_ratio_y > size_y_desired * window_ratio_x) {
            size_x = rect.right - rect.left;
            size_y = g_window_adjust_y + ((size_x - g_window_adjust_x) * window_ratio_y) / window_ratio_x;
        }
        else {
            size_y = rect.bottom - rect.top;
            size_x = g_window_adjust_x + ((size_y - g_window_adjust_y) * window_ratio_x) / window_ratio_y;
        }

        rect.right = rect.left + size_x;
        rect.bottom = rect.top + size_y;

        break;
    }
    case WMSZ_LEFT:
    case WMSZ_RIGHT: {
        int size_y = g_window_adjust_y + (size_x_desired * window_ratio_y) / window_ratio_x;
        rect.top = (rect.top + rect.bottom) / 2 - size_y / 2;
        rect.bottom = rect.top + size_y;

        break;
    }
    case WMSZ_TOPLEFT: {
        int size_x, size_y;

        if (size_x_desired * window_ratio_y > size_y_desired * window_ratio_x) {
            size_x = rect.right - rect.left;
            size_y = g_window_adjust_y + ((size_x - g_window_adjust_x) * window_ratio_y) / window_ratio_x;
        }
        else {
            size_y = rect.bottom - rect.top;
            size_x = g_window_adjust_x + ((size_y - g_window_adjust_y) * window_ratio_x) / window_ratio_y;
        }

        rect.left = rect.right - size_x;
        rect.top = rect.bottom - size_y;

        break;
    }
    case WMSZ_TOPRIGHT: {
        int size_x, size_y;

        if (size_x_desired * window_ratio_y > size_y_desired * window_ratio_x) {
            size_x = rect.right - rect.left;
            size_y = g_window_adjust_y + ((size_x - g_window_adjust_x) * window_ratio_y) / window_ratio_x;
        }
        else {
            size_y = rect.bottom - rect.top;
            size_x = g_window_adjust_x + ((size_y - g_window_adjust_y) * window_ratio_x) / window_ratio_y;
        }

        rect.right = rect.left + size_x;
        rect.top = rect.bottom - size_y;

        break;
    }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static float aspect_ratio = 1.f;

    switch (uMsg)  {
    case WM_DESTROY: {
        PostQuitMessage(0);

        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Graphics graphics(hdc);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        if (!preview.drawing) {
            // background
            FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));

            // text
            Font font(&FontFamily(L"Tahoma"), 12, FontStyle::FontStyleBold);
            SolidBrush brush(Color::Black);
            Status st = graphics.DrawString(L"waiting...", -1, &font, PointF(5, 5), &brush);
        }
        else {
            // set up image
            Rect image_rect(0, 0, width, height);

            Image image(towstring(preview.preview_filename).data());

            window_ratio_x = image.GetWidth();
            window_ratio_y = image.GetHeight();

            // draw image
            graphics.DrawImage(&image, image_rect);

            if (!drew_image) {
                drew_image = true;

                // force resize to fit aspect ratio
                RECT window_rc;
                GetWindowRect(hwnd, &window_rc);

                resize(WMSZ_BOTTOMRIGHT, window_rc);

                SetWindowPos(hwnd, NULL, window_rc.left, window_rc.top, window_rc.right - window_rc.left, window_rc.bottom - window_rc.top, NULL);
            }
        }

        EndPaint(hwnd, &ps);

        return 0;
    }
    case WM_SIZING: {
        if (drew_image) {
            resize(int(wParam), *reinterpret_cast<LPRECT>(lParam));

            return TRUE;
        }
    }
    case WM_GETMINMAXINFO: {
        // if (drew_image) {
        //     MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
        //     info->ptMinTrackSize.y = ((info->ptMinTrackSize.x - g_window_adjust_x) * window_ratio_y) / window_ratio_x + g_window_adjust_y;
        // 
        //     break;
        // }
    }
    case WM_ERASEBKGND:
        if (drew_image) {
            // prevent flickering by not clearing background
            return 0;
        }
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

bool create_window() {
    // create window class
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = L"blur preview";

    if (!RegisterClassEx(&wc))
        throw std::exception("failed to initialise preview");

    // aspect ratio shit
    RECT rect = { 0, 0, preview.preview_width, preview.preview_height };
    AdjustWindowRect(&rect, NULL, FALSE);
    g_window_adjust_x = (rect.right - rect.left) - preview.preview_width;
    g_window_adjust_y = (rect.bottom - rect.top) - preview.preview_height;

    // // make preview start a bit offset from console
    // RECT console_rect = console.get_console_position();
    // int x = console_rect.left + offset_x;
    // int y = console_rect.top + offset_y;

    // create window
    preview.hwnd = ::CreateWindow(wc.lpszClassName, L"blur preview", WS_OVERLAPPEDWINDOW, NULL, NULL, preview.preview_width, preview.preview_height, NULL, NULL, wc.hInstance, NULL);

    if (preview.hwnd == NULL)
        throw std::exception("failed to initialise preview");

    ShowWindow(preview.hwnd, SW_SHOW);

    ULONG_PTR token;
    GdiplusStartupInput input = { 0 };
    input.GdiplusVersion = 1;
    GdiplusStartup(&token, &input, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(token);

    return true;
}

template <typename TP>
std::time_t to_time_t(TP tp) {
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now()
        + system_clock::now());
    return system_clock::to_time_t(sctp);
}

void start_watch() {
    std::time_t last_update_time = std::time(NULL);

    while (true) {
        if (std::filesystem::exists(preview.preview_filename) && preview.hwnd != NULL) {
            auto write_time = std::filesystem::last_write_time(preview.preview_filename);
            std::time_t write_time_t = to_time_t(write_time);
            
            // check if the image has been modified
            if (std::difftime(write_time_t, last_update_time) > 0) {
                if (!preview.drawing)
                    preview.drawing = true;

                // redraw with new image
                RedrawWindow(preview.hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE);

                last_update_time = write_time_t;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}

std::unique_ptr<std::thread> window_thread_ptr;
std::unique_ptr<std::thread> watch_thread_ptr;
void c_preview::start(std::string_view filename) {
    preview_filename = filename;

    window_thread_ptr = std::unique_ptr<std::thread>(new std::thread(&create_window));
    window_thread_ptr->detach();

    watch_thread_ptr = std::unique_ptr<std::thread>(new std::thread(&start_watch));
    watch_thread_ptr->detach();
}

void c_preview::stop() {
    window_thread_ptr->join();
    watch_thread_ptr->join();
}