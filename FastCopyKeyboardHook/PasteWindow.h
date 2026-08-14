#pragma once
#include "../Public/KeyboardHookSettings.h"

#include <Windows.h>

#include <functional>

// The hidden message-only window that receives hot-key and paste messages.
// Window lifecycle messages are handled here; everything else is forwarded
// to the handler.
class PasteWindow
{
public:
    using Handler = std::function<LRESULT(UINT message, WPARAM wParam, LPARAM lParam)>;

    PasteWindow(HINSTANCE instance, Handler handler);
    ~PasteWindow();

    PasteWindow(PasteWindow const&) = delete;
    PasteWindow& operator=(PasteWindow const&) = delete;

    HWND handle() const { return window_; }

private:
    static LRESULT CALLBACK StaticProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT Procedure(UINT message, WPARAM wParam, LPARAM lParam);

    HWND window_{};
    Handler handler_;
};
