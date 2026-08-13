#include "ClipboardFileTransfer.h"
#include "ExplorerWindow.h"
#include "FastCopyLauncher.h"
#include "../Public/KeyboardHookSettings.h"

#include <wil/resource.h>
#include <wil/result_macros.h>

#include <Windows.h>
#include <ole2.h>

#include <string>

namespace
{
    void ShowError(char const* message)
    {
        if (!message)
        {
            message = "Unknown error";
        }

        auto const length = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
        std::wstring wide(length > 0 ? length - 1 : 0, L'\0');
        if (length > 0)
        {
            MultiByteToWideChar(CP_UTF8, 0, message, -1, wide.data(), length);
        }
        MessageBoxW(nullptr, wide.c_str(), L"RoboCopyEx", MB_OK | MB_ICONERROR);
    }
}

class KeyboardHookApp
{
public:
    int Run(HINSTANCE instance)
    {
        if (!KeyboardHookSettings::IsEnabled())
        {
            return 0;
        }

        wil::unique_mutex singleton{ CreateMutexW(nullptr, FALSE, KeyboardHookSettings::SingletonName) };
        if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS)
        {
            return 0;
        }

        THROW_IF_FAILED(OleInitialize(nullptr));
        auto uninitializeOle = wil::scope_exit([] { OleUninitialize(); });

        WNDCLASSW windowClass{};
        windowClass.hInstance = instance;
        windowClass.lpfnWndProc = &KeyboardHookApp::StaticWindowProcedure;
        windowClass.lpszClassName = KeyboardHookSettings::WindowClassName;
        if (!RegisterClassW(&windowClass))
        {
            THROW_HR(HRESULT_FROM_WIN32(GetLastError()));
        }

        m_messageWindow = CreateWindowExW(
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
        if (!m_messageWindow)
        {
            THROW_HR(HRESULT_FROM_WIN32(GetLastError()));
        }

        s_instance = this;

        // RegisterHotKey gives us a message-queue path even when the low-level hook
        // cannot observe a particular desktop. The low-level hook runs in parallel
        // so Explorer's own Ctrl+V accelerator is explicitly suppressed.
        m_pasteHotKeyRegistered = RegisterPasteHotKey();
        m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, &KeyboardHookApp::StaticKeyboardProcedure, instance, 0);
        if (!m_pasteHotKeyRegistered && !m_keyboardHook)
        {
            return 1;
        }

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (m_pasteHotKeyRegistered)
        {
            UnregisterHotKey(m_messageWindow, PasteHotKeyId);
        }
        if (m_keyboardHook)
        {
            UnhookWindowsHookEx(m_keyboardHook);
        }
        return 0;
    }

private:
    static constexpr auto PasteMessage = WM_APP + 1;
    static constexpr int PasteHotKeyId = 1;
    static constexpr UINT_PTR PasteHotKeyRestoreTimerId = 1;
    static constexpr ULONG_PTR ReplayInputMarker = 0x52435856;

    static KeyboardHookApp* s_instance;

    HWND m_messageWindow{};
    HHOOK m_keyboardHook{};
    bool m_pasteHotKeyRegistered{};
    bool m_interceptedPasteKey{};
    bool m_pasteRequestQueued{};
    bool m_hookPasteGesture{};
    bool m_controlDown{};
    bool m_shiftDown{};
    bool m_altDown{};
    bool m_winDown{};

    bool RegisterPasteHotKey()
    {
        return RegisterHotKey(
            m_messageWindow,
            PasteHotKeyId,
            MOD_CONTROL | MOD_NOREPEAT,
            L'V') != FALSE;
    }

    void ReplayPaste()
    {
        if (m_pasteHotKeyRegistered)
        {
            UnregisterHotKey(m_messageWindow, PasteHotKeyId);
            m_pasteHotKeyRegistered = false;
            SetTimer(m_messageWindow, PasteHotKeyRestoreTimerId, 250, nullptr);
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

    void ClearMoveClipboard()
    {
        if (OpenClipboard(m_messageWindow))
        {
            EmptyClipboard();
            CloseClipboard();
        }
    }

    void HandlePaste(HWND expectedExplorerWindow)
    {
        m_pasteRequestQueued = false;
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
            ClearMoveClipboard();
        }
    }

    void RequestPaste(HWND foregroundWindow)
    {
        if (m_pasteRequestQueued)
        {
            return;
        }

        m_pasteRequestQueued = true;
        if (!PostMessageW(m_messageWindow, PasteMessage, reinterpret_cast<WPARAM>(foregroundWindow), 0))
        {
            m_pasteRequestQueued = false;
        }
    }

    LRESULT WindowProcedure(UINT message, WPARAM parameter, LPARAM lparam)
    {
        switch (message)
        {
        case WM_HOTKEY:
            if (parameter == PasteHotKeyId)
            {
                if (!m_hookPasteGesture)
                {
                    RequestPaste(GetForegroundWindow());
                }
            }
            return 0;
        case PasteMessage:
            try
            {
                HandlePaste(reinterpret_cast<HWND>(parameter));
            }
            catch (wil::ResultException const& e)
            {
                ShowError(e.what());
            }
            catch (std::exception const& e)
            {
                ShowError(e.what());
            }
            return 0;
        case WM_TIMER:
            if (parameter == PasteHotKeyRestoreTimerId)
            {
                KillTimer(m_messageWindow, PasteHotKeyRestoreTimerId);
                m_pasteHotKeyRegistered = RegisterPasteHotKey();
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
                DestroyWindow(m_messageWindow);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(m_messageWindow);
            return 0;
        default:
            return DefWindowProcW(m_messageWindow, message, parameter, lparam);
        }
    }

    LRESULT KeyboardProcedure(int code, WPARAM parameter, LPARAM data)
    {
        if (code != HC_ACTION)
        {
            return CallNextHookEx(m_keyboardHook, code, parameter, data);
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
                m_controlDown = pressed;
                break;
            case VK_LSHIFT:
            case VK_RSHIFT:
            case VK_SHIFT:
                m_shiftDown = pressed;
                break;
            case VK_LMENU:
            case VK_RMENU:
            case VK_MENU:
                m_altDown = pressed;
                break;
            case VK_LWIN:
            case VK_RWIN:
                m_winDown = pressed;
                break;
            default:
                break;
            }
        }
        auto const isPaste = event->vkCode == L'V';
        if (isReplayInput || !isPaste)
        {
            return CallNextHookEx(m_keyboardHook, code, parameter, data);
        }

        if (isPaste && keyUp && m_interceptedPasteKey)
        {
            m_interceptedPasteKey = false;
            m_hookPasteGesture = false;
            return 1;
        }

        auto const modifiersMatch = m_controlDown && !m_shiftDown && !m_altDown && !m_winDown;
        auto const foregroundWindow = GetForegroundWindow();
        if (!keyDown || !modifiersMatch || !IsExplorerWindow(foregroundWindow))
        {
            return CallNextHookEx(m_keyboardHook, code, parameter, data);
        }

        if (!m_interceptedPasteKey)
        {
            m_interceptedPasteKey = true;
            m_hookPasteGesture = true;
            RequestPaste(foregroundWindow);
        }
        return 1;
    }

    static LRESULT CALLBACK StaticWindowProcedure(HWND window, UINT message, WPARAM parameter, LPARAM lparam)
    {
        if (message == WM_NCCREATE)
        {
            auto const create = reinterpret_cast<CREATESTRUCTW const*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        auto const self = reinterpret_cast<KeyboardHookApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        return self ? self->WindowProcedure(message, parameter, lparam)
                    : DefWindowProcW(window, message, parameter, lparam);
    }

    static LRESULT CALLBACK StaticKeyboardProcedure(int code, WPARAM parameter, LPARAM data)
    {
        return s_instance
            ? s_instance->KeyboardProcedure(code, parameter, data)
            : CallNextHookEx(nullptr, code, parameter, data);
    }
};

KeyboardHookApp* KeyboardHookApp::s_instance = nullptr;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    try
    {
        KeyboardHookApp app;
        return app.Run(instance);
    }
    catch (wil::ResultException const& e)
    {
        ShowError(e.what());
        return 1;
    }
    catch (std::exception const& e)
    {
        ShowError(e.what());
        return 1;
    }
}
