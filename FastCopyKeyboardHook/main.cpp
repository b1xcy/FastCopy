#include "ClipboardFileTransfer.h"
#include "ExplorerWindow.h"
#include "FastCopyLauncher.h"
#include "../Public/KeyboardHookSettings.h"

#include <Windows.h>
#include <ole2.h>

namespace
{
    constexpr auto PasteMessage = WM_APP + 1;
    constexpr int PasteHotKeyId = 1;
    constexpr UINT_PTR PasteHotKeyRestoreTimerId = 1;
    constexpr ULONG_PTR ReplayInputMarker = 0x52435856;
    HWND messageWindow{};
    HHOOK keyboardHook{};
    bool pasteHotKeyRegistered{};
    bool interceptedPasteKey{};
    bool pasteRequestQueued{};
    bool hookPasteGesture{};
    bool controlDown{};
    bool shiftDown{};
    bool altDown{};
    bool winDown{};

    bool RegisterPasteHotKey()
    {
        return RegisterHotKey(
            messageWindow,
            PasteHotKeyId,
            MOD_CONTROL | MOD_NOREPEAT,
            L'V') != FALSE;
    }

    void ReplayPaste()
    {
        if (pasteHotKeyRegistered)
        {
            UnregisterHotKey(messageWindow, PasteHotKeyId);
            pasteHotKeyRegistered = false;
            SetTimer(messageWindow, PasteHotKeyRestoreTimerId, 250, nullptr);
        }

        INPUT input[4]{};
        UINT count{};
        auto const controlIsDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        if (!controlIsDown)
        {
            input[count].type = INPUT_KEYBOARD;
            input[count++].ki.wVk = VK_CONTROL;
        }

        input[count].type = INPUT_KEYBOARD;
        input[count++].ki.wVk = L'V';
        input[count].type = INPUT_KEYBOARD;
        input[count].ki.wVk = L'V';
        input[count++].ki.dwFlags = KEYEVENTF_KEYUP;

        if (!controlIsDown)
        {
            input[count].type = INPUT_KEYBOARD;
            input[count].ki.wVk = VK_CONTROL;
            input[count++].ki.dwFlags = KEYEVENTF_KEYUP;
        }

        for (UINT index = 0; index < count; ++index)
        {
            input[index].ki.dwExtraInfo = ReplayInputMarker;
        }
        SendInput(count, input, sizeof(INPUT));
    }

    void ClearMoveClipboard(HWND owner)
    {
        if (OpenClipboard(owner))
        {
            EmptyClipboard();
            CloseClipboard();
        }
    }

    void HandlePaste(HWND expectedExplorerWindow)
    {
        pasteRequestQueued = false;
        auto const destination = GetExplorerFolder(expectedExplorerWindow);
        auto const transfer = destination ? ClipboardFileTransfer::Read() : std::nullopt;
        if (!destination || !transfer || !LaunchFastCopy(*transfer, *destination))
        {
            // Never replay into a different foreground window if focus changed while this
            // asynchronous request was queued.
            if (GetForegroundWindow() == expectedExplorerWindow)
            {
                ReplayPaste();
            }
            return;
        }

        if (transfer->move)
        {
            ClearMoveClipboard(messageWindow);
        }
    }

    void RequestPaste(HWND foregroundWindow)
    {
        if (pasteRequestQueued)
        {
            return;
        }

        pasteRequestQueued = true;
        if (!PostMessageW(messageWindow, PasteMessage, reinterpret_cast<WPARAM>(foregroundWindow), 0))
        {
            pasteRequestQueued = false;
        }
    }

    LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM parameter, LPARAM lparam)
    {
        switch (message)
        {
        case WM_HOTKEY:
            if (parameter == PasteHotKeyId)
            {
                if (!hookPasteGesture)
                {
                    RequestPaste(GetForegroundWindow());
                }
            }
            return 0;
        case PasteMessage:
            HandlePaste(reinterpret_cast<HWND>(parameter));
            return 0;
        case WM_TIMER:
            if (parameter == PasteHotKeyRestoreTimerId)
            {
                KillTimer(window, PasteHotKeyRestoreTimerId);
                pasteHotKeyRegistered = RegisterPasteHotKey();
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (parameter)
            {
                DestroyWindow(window);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        default:
            return DefWindowProcW(window, message, parameter, lparam);
        }
    }

    LRESULT CALLBACK KeyboardProcedure(int code, WPARAM parameter, LPARAM data)
    {
        if (code != HC_ACTION)
        {
            return CallNextHookEx(keyboardHook, code, parameter, data);
        }

        auto const event = reinterpret_cast<KBDLLHOOKSTRUCT const*>(data);
        auto const isReplayInput = (event->flags & LLKHF_INJECTED) != 0 &&
            event->dwExtraInfo == ReplayInputMarker;
        auto const keyDown = parameter == WM_KEYDOWN || parameter == WM_SYSKEYDOWN;
        auto const keyUp = parameter == WM_KEYUP || parameter == WM_SYSKEYUP;
        if (!isReplayInput && (keyDown || keyUp))
        {
            auto const pressed = keyDown;
            switch (event->vkCode)
            {
            case VK_LCONTROL:
            case VK_RCONTROL:
            case VK_CONTROL:
                controlDown = pressed;
                break;
            case VK_LSHIFT:
            case VK_RSHIFT:
            case VK_SHIFT:
                shiftDown = pressed;
                break;
            case VK_LMENU:
            case VK_RMENU:
            case VK_MENU:
                altDown = pressed;
                break;
            case VK_LWIN:
            case VK_RWIN:
                winDown = pressed;
                break;
            default:
                break;
            }
        }
        auto const isPaste = event->vkCode == L'V';
        if (isReplayInput || !isPaste)
        {
            return CallNextHookEx(keyboardHook, code, parameter, data);
        }

        if (isPaste && keyUp && interceptedPasteKey)
        {
            interceptedPasteKey = false;
            hookPasteGesture = false;
            return 1;
        }

        auto const modifiersMatch = controlDown && !shiftDown && !altDown && !winDown;
        auto const foregroundWindow = GetForegroundWindow();
        if (!keyDown || !modifiersMatch || !IsExplorerWindow(foregroundWindow))
        {
            return CallNextHookEx(keyboardHook, code, parameter, data);
        }

        if (!interceptedPasteKey)
        {
            interceptedPasteKey = true;
            hookPasteGesture = true;
            RequestPaste(foregroundWindow);
        }
        return 1;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    if (!KeyboardHookSettings::IsEnabled())
    {
        return 0;
    }

    auto const singleton = CreateMutexW(nullptr, FALSE, KeyboardHookSettings::SingletonName);
    if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (singleton)
        {
            CloseHandle(singleton);
        }
        return 0;
    }

    auto const comResult = OleInitialize(nullptr);
    if (FAILED(comResult))
    {
        CloseHandle(singleton);
        return 1;
    }

    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.lpszClassName = KeyboardHookSettings::WindowClassName;
    if (!RegisterClassW(&windowClass))
    {
        OleUninitialize();
        CloseHandle(singleton);
        return 1;
    }

    messageWindow = CreateWindowExW(
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
        nullptr);
    if (!messageWindow)
    {
        OleUninitialize();
        CloseHandle(singleton);
        return 1;
    }

    // RegisterHotKey gives us a message-queue path even when the low-level hook
    // cannot observe a particular desktop. The low-level hook runs in parallel
    // so Explorer's own Ctrl+V accelerator is explicitly suppressed.
    pasteHotKeyRegistered = RegisterPasteHotKey();
    keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProcedure, instance, 0);
    if (!pasteHotKeyRegistered && !keyboardHook)
    {
        DestroyWindow(messageWindow);
        OleUninitialize();
        CloseHandle(singleton);
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (pasteHotKeyRegistered)
    {
        UnregisterHotKey(messageWindow, PasteHotKeyId);
    }
    if (keyboardHook)
    {
        UnhookWindowsHookEx(keyboardHook);
    }
    OleUninitialize();
    CloseHandle(singleton);
    return 0;
}
