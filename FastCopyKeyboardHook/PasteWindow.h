#pragma once
#include "../Public/KeyboardHookSettings.h"

#include <Windows.h>
#include <wil/resource.h>
#include <functional>

// The message-only window (HWND_MESSAGE) that receives hot-key and paste
// messages. Window lifecycle messages are handled here; everything else is
// forwarded to the handler.
class PasteWindow
{
public:
    using Handler = std::function<LRESULT(UINT message, WPARAM wParam, LPARAM lParam)>;

    PasteWindow(HINSTANCE instance, Handler handler);

    PasteWindow(PasteWindow const&) = delete;
    PasteWindow& operator=(PasteWindow const&) = delete;

    HWND handle() const { return m_window.get(); }

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    wil::unique_hwnd m_window{};
    Handler m_handler;
};
