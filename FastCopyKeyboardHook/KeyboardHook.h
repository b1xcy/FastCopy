#pragma once
#include <wil/resource.h>

#include <Windows.h>

#include <functional>

// Installs a WH_KEYBOARD_LL hook and removes it on destruction (RAII).
// Incoming events are forwarded to the callback.
class KeyboardHook
{
public:
    using Callback = std::function<LRESULT(int code, WPARAM wParam, LPARAM lParam)>;

    KeyboardHook(HINSTANCE instance, Callback callback);
    ~KeyboardHook();

    KeyboardHook(KeyboardHook const&) = delete;
    KeyboardHook& operator=(KeyboardHook const&) = delete;

    bool installed() const { return hook_ != nullptr; }

private:
    static LRESULT CALLBACK Procedure(int code, WPARAM wParam, LPARAM lParam);

    static KeyboardHook* s_instance;

    wil::unique_hhook hook_;
    Callback callback_;
};
