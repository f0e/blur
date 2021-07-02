#include "includes.h"

#include <objidl.h>
#include <gdiplus.h>

// http://playtechs.blogspot.com/2007/10/forcing-window-to-maintain-particular.html
void c_preview::resize(int edge, RECT& rect) {
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

LRESULT CALLBACK c_preview::wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg)  {
    case WM_DESTROY: {
        PostQuitMessage(0);

        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Gdiplus::Graphics graphics(hdc);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        auto draw_text = [&](const std::wstring& text) {
            auto tahoma = Gdiplus::FontFamily(L"Tahoma");
            Gdiplus::Font font(&tahoma, 12, Gdiplus::FontStyle::FontStyleBold);
            Gdiplus::SolidBrush brush(Gdiplus::Color::Black);
            Gdiplus::Status st = graphics.DrawString(text.data(), -1, &font, Gdiplus::PointF(5, 5), &brush);
        };

        if (!preview.drawing) {
            // background
            FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));

            // text
            draw_text(L"waiting...");
        }
        else if (preview.preview_disabled) {
            // background
            FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));

            // text
            draw_text(L"preview disabled");
        } else {
            // set up image
            Gdiplus::Rect image_rect(0, 0, width, height);

            Gdiplus::Image image(helpers::towstring(preview.preview_filename).data());

            preview.window_ratio_x = image.GetWidth();
            preview.window_ratio_y = image.GetHeight();

            // draw image
            graphics.DrawImage(&image, image_rect);

            if (!preview.drew_image) {
                preview.drew_image = true;

                // force resize to fit aspect ratio
                RECT window_rc;
                GetWindowRect(hwnd, &window_rc);

                preview.resize(WMSZ_BOTTOMRIGHT, window_rc);

                SetWindowPos(hwnd, NULL, window_rc.left, window_rc.top, window_rc.right - window_rc.left, window_rc.bottom - window_rc.top, NULL);
            }
        }

        EndPaint(hwnd, &ps);

        return 0;
    }
    case WM_SIZING: {
        if (preview.drew_image) {
            preview.resize(int(wparam), *reinterpret_cast<LPRECT>(lparam));

            return TRUE;
        }
    }
    case WM_ERASEBKGND:
        if (preview.drew_image) {
            // prevent flickering by not clearing background
            return 0;
        }
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

void c_preview::create_window() {
    // create window class
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = L"blur preview";

    if (!RegisterClassEx(&wc))
        throw std::exception("failed to initialise preview");

    // aspect ratio handling
    RECT rect = { 0, 0, preview.preview_width, preview.preview_height };
    AdjustWindowRect(&rect, NULL, FALSE);
    g_window_adjust_x = (rect.right - rect.left) - preview.preview_width;
    g_window_adjust_y = (rect.bottom - rect.top) - preview.preview_height;

    // create window
    preview.hwnd = ::CreateWindow(wc.lpszClassName, L"blur preview", WS_OVERLAPPEDWINDOW, NULL, NULL, preview.preview_width, preview.preview_height, NULL, NULL, wc.hInstance, NULL);
    if (preview.hwnd == NULL)
        throw std::exception("failed to initialise preview");

    ShowWindow(preview.hwnd, SW_SHOW);

    // initialise gdiplus
    ULONG_PTR token;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, NULL);

    // window loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // shutdown gdiplus
    Gdiplus::GdiplusShutdown(token);
}

void c_preview::watch_preview() {
    std::time_t last_update_time = std::time(NULL);

    while (preview.preview_open) {
        if (std::filesystem::exists(preview.preview_filename) && preview.hwnd != NULL) {
            auto write_time = std::filesystem::last_write_time(preview.preview_filename);
            std::time_t write_time_t = helpers::to_time_t(write_time);
            
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

void c_preview::start(const std::string& filename) {
    preview_disabled = false;
    preview_open = true;
    preview_filename = filename;

    if (!window_thread_ptr) {
        window_thread_ptr = std::unique_ptr<std::thread>(new std::thread(&c_preview::create_window, this));
        window_thread_ptr->detach();
    }
    else if (preview.hwnd) {
        ShowWindow(preview.hwnd, SW_SHOWNORMAL);
    }

    if (!watch_thread_ptr) {
        watch_thread_ptr = std::unique_ptr<std::thread>(new std::thread(&c_preview::watch_preview, this));
        watch_thread_ptr->detach();
    }
}

void c_preview::stop() {
    preview_open = false;
    
    // close window
    SendMessage(preview.hwnd, WM_CLOSE, NULL, NULL);
}

void c_preview::disable() {
    if (!preview_open)
        return;

    preview_disabled = true;
    RedrawWindow(preview.hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE);
    ShowWindow(preview.hwnd, SW_MINIMIZE);
}