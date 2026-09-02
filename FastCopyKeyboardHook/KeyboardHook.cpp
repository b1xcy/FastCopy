#include "KeyboardHook.h"
#include "KeyboardHookApp.h"

KeyboardHook* KeyboardHook::s_instance = nullptr;

KeyboardHook::KeyboardHook(HINSTANCE instance, KeyboardHookApp* owner)
    : m_owner{ owner },
      m_hook{ SetWindowsHookExW(WH_KEYBOARD_LL, &KeyboardHook::procedure, instance, 0) }
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

LRESULT CALLBACK KeyboardHook::procedure(int code, WPARAM wParam, LPARAM lParam)
{
    // Anything but HC_ACTION must be passed on untouched, and lParam only holds
    // a KBDLLHOOKSTRUCT for HC_ACTION.
    if (code == HC_ACTION && s_instance)
    {
        auto const& event = *reinterpret_cast<KBDLLHOOKSTRUCT const*>(lParam);
        switch (wParam)
        {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                if (s_instance->m_owner->onKeyDown(event))
                    return 1;
                break;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                if (s_instance->m_owner->onKeyUp(event))
                    return 1;
                break;
            default:
                break;
        }
    }

    return CallNextHookEx(nullptr, code, wParam, lParam);
}
