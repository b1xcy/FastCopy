#include "KeyboardHookApp.h"
#include "ClipboardFileTransfer.h"
#include "ExplorerWindow.h"
#include "FastCopyLauncher.h"
#include "../Public/KeyboardHookSettings.h"

#include <wil/resource.h>
#include <wil/result_macros.h>

#include <ole2.h>

#include <string>

KeyboardHookApp::KeyboardHookApp(HINSTANCE instance)
    : window_{ instance, [this](UINT message, WPARAM wParam, LPARAM lParam)
               { return OnWindowMessage(message, wParam, lParam); } },
      pasteHotKey_{ window_.handle(), pasteHotKeyId, MOD_CONTROL | MOD_NOREPEAT, L'V' },
      keyboardHook_{ instance, [this](int code, WPARAM wParam, LPARAM lParam)
                     { return OnKeyboardEvent(code, wParam, lParam); } }
{
}

void KeyboardHookApp::ShowError(char const* message)
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

int KeyboardHookApp::Run()
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

    // RegisterHotKey gives us a message-queue path even when the low-level hook
    // cannot observe a particular desktop. The low-level hook runs in parallel
    // so Explorer's own Ctrl+V accelerator is explicitly suppressed.
    pasteHotKey_.Register();
    if (!pasteHotKey_.registered() && !keyboardHook_.installed())
    {
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

LRESULT KeyboardHookApp::OnWindowMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_HOTKEY:
        if (wParam == pasteHotKeyId && !pasteKeyGesture_)
        {
            RequestPaste(GetForegroundWindow());
        }
        return 0;
    case pasteMessage:
        try
        {
            HandlePaste(reinterpret_cast<HWND>(wParam));
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
        if (wParam == pasteHotKeyRestoreTimerId)
        {
            KillTimer(window_.handle(), pasteHotKeyRestoreTimerId);
            pasteHotKey_.Register();
        }
        return 0;
    default:
        return DefWindowProcW(window_.handle(), message, wParam, lParam);
    }
}

LRESULT KeyboardHookApp::OnKeyboardEvent(int code, WPARAM wParam, LPARAM lParam)
{
    if (code != HC_ACTION)
    {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    auto const event = reinterpret_cast<KBDLLHOOKSTRUCT const*>(lParam);
    auto const isReplayInput = (event->flags & LLKHF_INJECTED) != 0 &&
        event->dwExtraInfo == replayInputMarker;
    auto const keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    auto const keyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
    if (!isReplayInput && (keyDown || keyUp))
    {
        auto const pressed = keyDown;
        switch (event->vkCode)
        {
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_CONTROL:
            controlDown_ = pressed;
            break;
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_SHIFT:
            shiftDown_ = pressed;
            break;
        case VK_LMENU:
        case VK_RMENU:
        case VK_MENU:
            altDown_ = pressed;
            break;
        case VK_LWIN:
        case VK_RWIN:
            winDown_ = pressed;
            break;
        default:
            break;
        }
    }

    // Only non-replayed V key events are of interest.
    if (isReplayInput || event->vkCode != L'V')
    {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    // End an intercepted paste gesture on key-up.
    if (keyUp && interceptedPasteKey_)
    {
        interceptedPasteKey_ = false;
        pasteKeyGesture_ = false;
        return 1;
    }

    // Begin a paste gesture: Ctrl+V pressed in an Explorer window.
    auto const modifiersMatch = controlDown_ && !shiftDown_ && !altDown_ && !winDown_;
    if (keyDown && modifiersMatch && IsExplorerWindow(GetForegroundWindow()))
    {
        if (!interceptedPasteKey_)
        {
            interceptedPasteKey_ = true;
            pasteKeyGesture_ = true;
            RequestPaste(GetForegroundWindow());
        }
        return 1;
    }

    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void KeyboardHookApp::HandlePaste(HWND expectedExplorerWindow)
{
    pasteRequestQueued_ = false;
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

void KeyboardHookApp::RequestPaste(HWND foregroundWindow)
{
    if (pasteRequestQueued_)
    {
        return;
    }

    pasteRequestQueued_ = true;
    if (!PostMessageW(window_.handle(), pasteMessage, reinterpret_cast<WPARAM>(foregroundWindow), 0))
    {
        pasteRequestQueued_ = false;
    }
}

void KeyboardHookApp::ReplayPaste()
{
    if (pasteHotKey_.registered())
    {
        pasteHotKey_.Unregister();
        SetTimer(window_.handle(), pasteHotKeyRestoreTimerId, 250, nullptr);
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
        input[index].ki.dwExtraInfo = replayInputMarker;
    }
    SendInput(count, input, sizeof(INPUT));
}

void KeyboardHookApp::ClearMoveClipboard()
{
    if (OpenClipboard(window_.handle()))
    {
        EmptyClipboard();
        CloseClipboard();
    }
}
