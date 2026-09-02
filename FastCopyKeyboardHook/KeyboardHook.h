#pragma once
#include <wil/resource.h>

#include <Windows.h>

class KeyboardHookApp;

// Installs a WH_KEYBOARD_LL hook and removes it on destruction (RAII).
// It owns the single switch over the key events: hook plumbing is handled here,
// key presses and releases call straight into the owner's handler for each.
class KeyboardHook
{
public:
    KeyboardHook(HINSTANCE instance, KeyboardHookApp* owner);
    ~KeyboardHook();

    KeyboardHook(KeyboardHook const&) = delete;
    KeyboardHook& operator=(KeyboardHook const&) = delete;

    bool installed() const { return m_hook != nullptr; }

private:
    static LRESULT CALLBACK procedure(int code, WPARAM wParam, LPARAM lParam);

    static KeyboardHook* s_instance;

    // Declared before m_hook so the owner is set before the hook can deliver an
    // event, and still set while the hook is being removed.
    KeyboardHookApp* m_owner{};
    wil::unique_hhook m_hook;
};
