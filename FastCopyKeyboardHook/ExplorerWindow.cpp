#include "ExplorerWindow.h"

#include <objbase.h>
#include <ExDisp.h>
#include <ShlObj_core.h>
#include <ShObjIdl_core.h>
#include <Shlguid.h>
#include <Shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <string>

namespace
{
    using Microsoft::WRL::ComPtr;

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

    std::optional<std::filesystem::path> GetFolderFromView(IShellView* shellView)
    {
        ComPtr<IFolderView> folderView;
        if (FAILED(shellView->QueryInterface(IID_PPV_ARGS(&folderView))))
        {
            return std::nullopt;
        }

        ComPtr<IPersistFolder2> persistFolder;
        if (FAILED(folderView->GetFolder(IID_PPV_ARGS(&persistFolder))))
        {
            return std::nullopt;
        }

        PIDLIST_ABSOLUTE folderIdList{};
        if (FAILED(persistFolder->GetCurFolder(&folderIdList)))
        {
            return std::nullopt;
        }

        std::array<wchar_t, 32768> path{};
        auto const converted = SHGetPathFromIDListEx(
            folderIdList,
            path.data(),
            static_cast<DWORD>(path.size()),
            GPFIDL_DEFAULT);
        CoTaskMemFree(folderIdList);
        return converted ? std::optional{ std::filesystem::path{ path.data() } } : std::nullopt;
    }

    std::optional<std::filesystem::path> GetFolderFromLocation(IWebBrowser2* browser)
    {
        BSTR location{};
        if (FAILED(browser->get_LocationURL(&location)) || !location)
        {
            return std::nullopt;
        }

        std::array<wchar_t, 32768> path{};
        DWORD length = static_cast<DWORD>(path.size());
        auto const result = PathCreateFromUrlW(location, path.data(), &length, 0);
        SysFreeString(location);
        if (FAILED(result))
        {
            return std::nullopt;
        }

        return std::filesystem::path{ path.data() };
    }

}

bool IsExplorerWindow(HWND window)
{
    if (!window)
    {
        return false;
    }

    auto const className = GetWindowClass(window);
    return className == L"CabinetWClass" || className == L"ExploreWClass";
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

    ComPtr<IShellWindows> shellWindows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&shellWindows))))
    {
        return std::nullopt;
    }

    long count{};
    if (FAILED(shellWindows->get_Count(&count)))
    {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> locationFallback;
    for (long index = 0; index < count; ++index)
    {
        VARIANT itemIndex{};
        itemIndex.vt = VT_I4;
        itemIndex.lVal = index;

        ComPtr<IDispatch> dispatch;
        if (FAILED(shellWindows->Item(itemIndex, &dispatch)) || !dispatch)
        {
            continue;
        }

        ComPtr<IWebBrowser2> browser;
        if (FAILED(dispatch.As(&browser)))
        {
            continue;
        }

        SHANDLE_PTR browserWindowValue{};
        if (FAILED(browser->get_HWND(&browserWindowValue)) ||
            reinterpret_cast<HWND>(browserWindowValue) != expectedForegroundWindow)
        {
            continue;
        }

        if (!locationFallback)
        {
            locationFallback = GetFolderFromLocation(browser.Get());
        }

        ComPtr<IServiceProvider> serviceProvider;
        ComPtr<IShellBrowser> shellBrowser;
        ComPtr<IShellView> shellView;
        if (FAILED(browser.As(&serviceProvider)) ||
            FAILED(serviceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&shellBrowser))) ||
            FAILED(shellBrowser->QueryActiveShellView(&shellView)))
        {
            continue;
        }

        HWND viewWindow{};
        if (FAILED(shellView->GetWindow(&viewWindow)))
        {
            continue;
        }

        if (threadInfo.hwndFocus == viewWindow || IsChild(viewWindow, threadInfo.hwndFocus))
        {
            if (auto folder = GetFolderFromView(shellView.Get()))
            {
                return folder;
            }
        }

    }

    // Explorer versions with tabs can expose a browser location while the
    // active shell view does not support the older IFolderView interfaces.
    return locationFallback;
}
