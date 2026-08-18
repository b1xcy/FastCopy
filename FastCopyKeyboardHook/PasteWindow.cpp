#include "PasteWindow.h"

#include <wil/result_macros.h>

PasteWindow::PasteWindow(HINSTANCE instance, Handler handler)
    : m_handler{ std::move(handler) }
{
    WNDCLASSW windowClass
    {
        .lpfnWndProc = &PasteWindow::windowProc,
        .hInstance = instance,
        .lpszClassName = KeyboardHookSettings::WindowClassName,
    };
    THROW_LAST_ERROR_IF(!RegisterClassW(&windowClass));

    m_window.reset(CreateWindowExW(
        0,
        KeyboardHookSettings::WindowClassName,
        nullptr,
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        instance,
        this));
    THROW_LAST_ERROR_IF(!m_window);
}

LRESULT CALLBACK PasteWindow::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_NCCREATE:
        {
            auto const create = reinterpret_cast<CREATESTRUCTW const*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        default:
        {
            if (auto self = reinterpret_cast<PasteWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA)); self && self->m_handler)
                return self->m_handler(message, wParam, lParam);
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }
}
