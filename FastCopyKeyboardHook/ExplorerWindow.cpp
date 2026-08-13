#include "ExplorerWindow.h"

#include <objbase.h>

#include <wil/resource.h>
#include <wil/result_macros.h>

#include <ExDisp.h>
#include <ShlObj_core.h>
#include <ShObjIdl_core.h>
#include <Shlguid.h>
#include <Shlwapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <string>

namespace
{
    using Microsoft::WRL::ComPtr;

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

    // Encapsulates the COM types used to resolve the folder of the active
    // Explorer window. COM failures throw (wil::ResultException) so callers
    // catch them at the outer boundary; entries that are not Explorer tabs
    // are skipped, and a window without a matching shell view reports nullopt.
    class ExplorerFolderResolver
    {
    public:
        ExplorerFolderResolver()
        {
            THROW_IF_FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&m_shellWindows)));
        }

        std::optional<std::filesystem::path> Resolve(HWND frameWindow, GUITHREADINFO const& threadInfo)
        {
            long count{};
            THROW_IF_FAILED(m_shellWindows->get_Count(&count));

            std::optional<std::filesystem::path> locationFallback;
            for (long index = 0; index < count; ++index)
            {
                VARIANT itemIndex{};
                itemIndex.vt = VT_I4;
                itemIndex.lVal = index;

                ComPtr<IDispatch> dispatch;
                THROW_IF_FAILED(m_shellWindows->Item(itemIndex, &dispatch));
                if (!dispatch)
                {
                    continue;
                }

                ComPtr<IWebBrowser2> browser;
                if (FAILED(dispatch.As(&browser)))
                {
                    continue;
                }

                SHANDLE_PTR browserWindowValue{};
                THROW_IF_FAILED(browser->get_HWND(&browserWindowValue));
                if (reinterpret_cast<HWND>(browserWindowValue) != frameWindow)
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

            return locationFallback;
        }

    private:
        ComPtr<IShellWindows> m_shellWindows;

        static std::optional<std::filesystem::path> GetFolderFromView(IShellView* shellView)
        {
            ComPtr<IFolderView> folderView;
            THROW_IF_FAILED(shellView->QueryInterface(IID_PPV_ARGS(&folderView)));

            ComPtr<IPersistFolder2> persistFolder;
            THROW_IF_FAILED(folderView->GetFolder(IID_PPV_ARGS(&persistFolder)));

            wil::unique_cotaskmem_ptr<ITEMIDLIST> pidl;
            THROW_IF_FAILED(persistFolder->GetCurFolder(wil::out_param(pidl)));

            std::array<wchar_t, 32768> path{};
            auto const converted = SHGetPathFromIDListEx(
                pidl.get(),
                path.data(),
                static_cast<DWORD>(path.size()),
                GPFIDL_DEFAULT);
            return converted ? std::optional{ std::filesystem::path{ path.data() } } : std::nullopt;
        }

        static std::optional<std::filesystem::path> GetFolderFromLocation(IWebBrowser2* browser)
        {
            wil::unique_bstr location;
            THROW_IF_FAILED(browser->get_LocationURL(location.put()));
            if (!location)
            {
                return std::nullopt;
            }

            std::array<wchar_t, 32768> path{};
            DWORD length = static_cast<DWORD>(path.size());
            auto const result = PathCreateFromUrlW(location.get(), path.data(), &length, 0);
            return FAILED(result) ? std::nullopt : std::optional{ std::filesystem::path{ path.data() } };
        }
    };
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
        PWSTR desktopPath{};
        if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath)))
        {
            return std::nullopt;
        }
        std::filesystem::path result{ desktopPath };
        CoTaskMemFree(desktopPath);
        return result;
    }

    ExplorerFolderResolver resolver;
    return resolver.Resolve(expectedForegroundWindow, threadInfo);
}
