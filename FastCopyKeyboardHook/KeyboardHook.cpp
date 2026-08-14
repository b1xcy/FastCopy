#include "KeyboardHook.h"

KeyboardHook* KeyboardHook::s_instance = nullptr;

KeyboardHook::KeyboardHook(HINSTANCE instance, Callback callback)
    : hook_{ SetWindowsHookExW(WH_KEYBOARD_LL, &KeyboardHook::Procedure, instance, 0) },
      callback_{ std::move(callback) }
{
    s_instance = this;
}

KeyboardHook::~KeyboardHook()
{
    if (s_instance == this)
    {
        s_instance = nullptr;
    }
}

LRESULT CALLBACK KeyboardHook::Procedure(int code, WPARAM wParam, LPARAM lParam)
{
    return s_instance
        ? s_instance->callback_(code, wParam, lParam)
        : CallNextHookEx(nullptr, code, wParam, lParam);
}
