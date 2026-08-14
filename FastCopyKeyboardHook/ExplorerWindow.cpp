#include "ExplorerWindow.h"
#include "ExplorerFolderResolver.h"

#include <objbase.h>

#include <wil/resource.h>
#include <wil/result_macros.h>

#include <ShlObj_core.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <string>

namespace
{
    std::array<wchar_t, 256> GetWindowClass(HWND window)
    {
        std::array<wchar_t, 256> buffer{};
        GetClassNameW(window, buffer.data(), static_cast<int>(buffer.size()));
        return buffer;
    }

    bool IsTextEntryWindow(HWND window)
    {
        auto className = GetWindowClass(window);
        std::ranges::transform(className, className.begin(), [](wchar_t value) { return std::towlower(value); });
        auto const* text = className.data();
        return std::wcsstr(text, L"edit") != nullptr ||
            std::wcsstr(text, L"richedit") != nullptr ||
            std::wcscmp(text, L"combobox") == 0;
    }
}

bool IsExplorerWindow(HWND window)
{
    if (!window)
    {
        return false;
    }

    auto const className = GetWindowClass(window);
    // The desktop is a valid paste target even though it is not an Explorer frame.
    if (std::wcscmp(className.data(), L"Progman") == 0 || std::wcscmp(className.data(), L"WorkerW") == 0)
    {
        return true;
    }
    return std::wcscmp(className.data(), L"CabinetWClass") == 0 ||
        std::wcscmp(className.data(), L"ExploreWClass") == 0;
}

std::optional<std::filesystem::path> GetExplorerFolder(HWND expectedForegroundWindow)
{
    if (GetForegroundWindow() != expectedForegroundWindow || !IsExplorerWindow(expectedForegroundWindow))
    {
        return std::nullopt;
    }

    GUITHREADINFO threadInfo{ sizeof(threadInfo) };
    auto const foregroundThread = GetWindowThreadProcessId(expectedForegroundWindow, nullptr);
    if (!GetGUIThreadInfo(foregroundThread, &threadInfo) || IsTextEntryWindow(threadInfo.hwndFocus))
    {
        return std::nullopt;
    }

    // The desktop has no shell view to resolve; its folder is the user's desktop.
    auto const className = GetWindowClass(expectedForegroundWindow);
    if (std::wcscmp(className.data(), L"Progman") == 0 || std::wcscmp(className.data(), L"WorkerW") == 0)
    {
        wil::unique_cotaskmem_string desktopPath;
        THROW_IF_FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, wil::out_param(desktopPath)));
        return std::filesystem::path{ desktopPath.get() };
    }

    ExplorerFolderResolver resolver;
    return resolver.Resolve(expectedForegroundWindow, threadInfo);
}
