#include "KeyboardHookApp.h"
#include "ClipboardFileTransfer.h"
#include "ExplorerWindow.h"
#include "FastCopyLauncher.h"

#include <wil/resource.h>
#include <wil/result_macros.h>

#include <ole2.h>

#include <string>

KeyboardHookApp::KeyboardHookApp(HINSTANCE instance)
    : m_window{ instance, this },
      m_pasteHotKey{ m_window.Handle(), pasteHotKeyId, MOD_CONTROL | MOD_NOREPEAT, L'V' },
      m_keyboardHook{ instance, this }
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
    THROW_IF_FAILED(OleInitialize(nullptr));
    auto uninitializeOle = wil::scope_exit([] { OleUninitialize(); });

    // RegisterHotKey gives us a message-queue path even when the low-level hook
    // cannot observe a particular desktop. The low-level hook runs in parallel
    // so Explorer's own Ctrl+V accelerator is explicitly suppressed.
    auto const registered = m_pasteHotKey.Register();
    if (!registered && !m_keyboardHook.installed())
        return 1;

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

LRESULT KeyboardHookApp::onHotKey(int hotKeyId)
{
    if (hotKeyId == pasteHotKeyId && !m_pasteKeyGesture)
    {
        requestPaste(GetForegroundWindow());
    }
    return 0;
}

LRESULT KeyboardHookApp::onPasteRequested(HWND expectedExplorerWindow)
{
    try
    {
        handlePaste(expectedExplorerWindow);
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
}

LRESULT KeyboardHookApp::onTimer(UINT_PTR timerId)
{
    if (timerId == pasteHotKeyRestoreTimerId)
    {
        KillTimer(m_window.Handle(), pasteHotKeyRestoreTimerId);
        m_pasteHotKey.Register();
    }
    return 0;
}

bool KeyboardHookApp::isReplayInput(KBDLLHOOKSTRUCT const& event)
{
    return (event.flags & LLKHF_INJECTED) != 0 && event.dwExtraInfo == replayInputMarker;
}

void KeyboardHookApp::updateModifierState(DWORD virtualKey, bool pressed)
{
    switch (virtualKey)
    {
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_CONTROL:
            m_controlDown = pressed;
            return;
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_SHIFT:
            m_shiftDown = pressed;
            return;
        case VK_LMENU:
        case VK_RMENU:
        case VK_MENU:
            m_altDown = pressed;
            return;
        case VK_LWIN:
        case VK_RWIN:
            m_winDown = pressed;
            return;
        default:
            return;
    }
}

bool KeyboardHookApp::onKeyDown(KBDLLHOOKSTRUCT const& event)
{
    // Only non-replayed V key events are of interest.
    if (isReplayInput(event))
    {
        return false;
    }

    updateModifierState(event.vkCode, true);
    if (event.vkCode != L'V')
    {
        return false;
    }

    // Begin a paste gesture: Ctrl+V pressed in an Explorer window.
    auto const modifiersMatch = m_controlDown && !m_shiftDown && !m_altDown && !m_winDown;
    if (!modifiersMatch || !IsExplorerWindow(GetForegroundWindow()))
    {
        return false;
    }

    // Auto-repeat while the key is held reaches here again; suppress it without
    // queueing a second paste.
    if (!m_interceptedPasteKey)
    {
        m_interceptedPasteKey = true;
        m_pasteKeyGesture = true;
        requestPaste(GetForegroundWindow());
    }
    return true;
}

bool KeyboardHookApp::onKeyUp(KBDLLHOOKSTRUCT const& event)
{
    if (isReplayInput(event))
    {
        return false;
    }

    updateModifierState(event.vkCode, false);
    if (event.vkCode != L'V')
    {
        return false;
    }

    // End an intercepted paste gesture on key-up.
    if (!m_interceptedPasteKey)
    {
        return false;
    }

    m_interceptedPasteKey = false;
    m_pasteKeyGesture = false;
    return true;
}

void KeyboardHookApp::handlePaste(HWND expectedExplorerWindow)
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
            replayPaste();
        }
        return;
    }

    if (transfer->move)
    {
        clearMoveClipboard();
    }
}

void KeyboardHookApp::requestPaste(HWND foregroundWindow)
{
    if (m_pasteRequestQueued)
    {
        return;
    }

    m_pasteRequestQueued = true;
    if (!PostMessageW(m_window.Handle(), pasteMessage, reinterpret_cast<WPARAM>(foregroundWindow), 0))
    {
        m_pasteRequestQueued = false;
    }
}

void KeyboardHookApp::replayPaste()
{
    if (m_pasteHotKey.Unregister())
        SetTimer(m_window.Handle(), pasteHotKeyRestoreTimerId, 250, nullptr);

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

void KeyboardHookApp::clearMoveClipboard()
{
    if (OpenClipboard(m_window.Handle()))
    {
        EmptyClipboard();
        CloseClipboard();
    }
}
