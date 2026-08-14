#include "PasteWindow.h"

#include <wil/result_macros.h>

PasteWindow::PasteWindow(HINSTANCE instance, Handler handler)
    : handler_{ std::move(handler) }
{
    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = &PasteWindow::StaticProcedure;
    windowClass.lpszClassName = KeyboardHookSettings::WindowClassName;
    if (!RegisterClassW(&windowClass))
    {
        THROW_HR(HRESULT_FROM_WIN32(GetLastError()));
    }

    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        KeyboardHookSettings::WindowClassName,
        L"",
        WS_POPUP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance,
        this);
    if (!window_)
    {
        THROW_HR(HRESULT_FROM_WIN32(GetLastError()));
    }
}

PasteWindow::~PasteWindow()
{
    if (window_)
    {
        DestroyWindow(window_);
    }
}

LRESULT PasteWindow::Procedure(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wParam)
        {
            DestroyWindow(window_);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    default:
        return handler_ ? handler_(message, wParam, lParam)
                        : DefWindowProcW(window_, message, wParam, lParam);
    }
}

LRESULT CALLBACK PasteWindow::StaticProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        auto const create = reinterpret_cast<CREATESTRUCTW const*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    auto const self = reinterpret_cast<PasteWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self ? self->Procedure(message, wParam, lParam)
                : DefWindowProcW(window, message, wParam, lParam);
}
