#include "ExplorerWindow.h"
#include "../Public/ExplorerFolder.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <string>

namespace
{
    std::wstring GetWindowClass(HWND window)
    {
        std::array<wchar_t, 256> buffer{};
        auto const length = GetClassNameW(window, buffer.data(), static_cast<int>(buffer.size()));
        return length > 0 ? std::wstring{ buffer.data(), static_cast<size_t>(length) } : std::wstring{};
    }

    bool IsTextEntryWindow(HWND window)
    {
        auto className = GetWindowClass(window);
        std::ranges::transform(className, className.begin(), [](wchar_t value) { return std::towlower(value); });
        return className.find(L"edit") != std::wstring::npos ||
            className.find(L"richedit") != std::wstring::npos ||
            className == L"combobox";
    }
}

bool IsExplorerWindow(HWND window)
{
    if (!window)
    {
        return false;
    }

    if (IsDesktopWindow(window))
    {
        return true;
    }

    auto const className = GetWindowClass(window);
    return className == L"CabinetWClass" || className == L"ExploreWClass";
}

std::optional<std::filesystem::path> GetExplorerFolder(HWND expectedForegroundWindow)
{
    // The foreground must still be the Explorer window that captured the paste
    // key, because the request is processed asynchronously via PostMessage.
    if (GetForegroundWindow() != expectedForegroundWindow || !IsExplorerWindow(expectedForegroundWindow))
    {
        return std::nullopt;
    }

    // Do not hijack Ctrl+V while the user is typing in an edit control
    // (address bar, rename box, ...).
    GUITHREADINFO threadInfo{ sizeof(threadInfo) };
    auto const foregroundThread = GetWindowThreadProcessId(expectedForegroundWindow, nullptr);
    if (!GetGUIThreadInfo(foregroundThread, &threadInfo) || IsTextEntryWindow(threadInfo.hwndFocus))
    {
        return std::nullopt;
    }

    if (IsDesktopWindow(expectedForegroundWindow))
    {
        return std::filesystem::path{ GetDesktopFolder() };
    }

    if (auto folder = ResolveExplorerFolder(expectedForegroundWindow))
    {
        return std::filesystem::path{ *folder };
    }
    return std::nullopt;
}
