#include "ExplorerWindow.h"
#include "ExplorerFolder.h"
#include "ShellWindows.h"
#include <objbase.h>
#include <wil/resource.h>
#include <wil/result_macros.h>
#include <ShlObj_core.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <string>

static std::array<wchar_t, 256> GetWindowClass(HWND window)
{
    std::array<wchar_t, 256> buffer{};
    GetClassNameW(window, buffer.data(), static_cast<int>(buffer.size()));
    return buffer;
}

static bool IsTextEntryWindow(HWND window)
{
    auto className = GetWindowClass(window);
    std::ranges::transform(className, className.begin(), [](wchar_t value) { return std::towlower(value); });
    auto const* text = className.data();
    return std::wcsstr(text, L"edit") != nullptr ||
        std::wcsstr(text, L"richedit") != nullptr ||
        std::wcscmp(text, L"combobox") == 0;
}

// Which folder the Explorer frame is showing. Enumeration failures throw
// (wil::ResultException) and are caught at the outer boundary; entries that are not
// this frame are skipped, and a window whose active view does not support the older
// IFolderView interfaces falls back to the browser location URL.
static std::optional<std::filesystem::path> ResolveActiveTabFolder(HWND frameWindow, GUITHREADINFO const& threadInfo)
{
    ShellWindows shellWindows;
    auto const count = shellWindows.Count();

    std::optional<std::filesystem::path> locationFallback;
    for (long index = 0; index < count; ++index)
    {
        wil::unique_variant itemIndex;
        itemIndex.vt = VT_I4;
        itemIndex.lVal = index;

        auto browser = shellWindows.Item(itemIndex);
        if (browser.HWND() != frameWindow)
        {
            continue;
        }

        if (!locationFallback)
        {
            locationFallback = ExplorerFolder::FromWebBrowser(browser.Get());
        }

        auto const shellView = ExplorerFolder::ActiveView(browser.Get());
        HWND viewWindow{};
        if (!shellView || FAILED(shellView->GetWindow(&viewWindow)))
        {
            continue;
        }

        // Tabs of one window share the frame, so the focused view is what picks out the
        // tab the user is actually looking at.
        if (threadInfo.hwndFocus == viewWindow || IsChild(viewWindow, threadInfo.hwndFocus))
        {
            if (auto folder = ExplorerFolder::FromShellView(shellView.get()))
            {
                return folder;
            }
        }
    }

    return locationFallback;
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

    return ResolveActiveTabFolder(expectedForegroundWindow, threadInfo);
}
