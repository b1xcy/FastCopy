#pragma once
#include "PasteHotKey.h"
#include "KeyboardHook.h"
#include "PasteWindow.h"

#include <Windows.h>

// Owns the paste integration: the hidden window, the Ctrl+V hot key and the
// low-level keyboard hook, plus the paste handling logic.
class KeyboardHookApp
{
public:
    KeyboardHookApp(HINSTANCE instance);
    int Run();
    static void ShowError(char const* message);

private:
    static constexpr auto pasteMessage = WM_APP + 1;
    static constexpr int pasteHotKeyId = 1;
    static constexpr UINT_PTR pasteHotKeyRestoreTimerId = 1;
    static constexpr ULONG_PTR replayInputMarker = 0x52435856;

    LRESULT OnWindowMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT OnKeyboardEvent(int code, WPARAM wParam, LPARAM lParam);
    void HandlePaste(HWND expectedExplorerWindow);
    void RequestPaste(HWND foregroundWindow);
    void ReplayPaste();
    void ClearMoveClipboard();

    PasteWindow window_;
    PasteHotKey pasteHotKey_;
    KeyboardHook keyboardHook_;
    bool interceptedPasteKey_{};
    bool pasteRequestQueued_{};
    bool pasteKeyGesture_{};
    bool controlDown_{};
    bool shiftDown_{};
    bool altDown_{};
    bool winDown_{};
};
