#include "ExplorerFolderResolver.h"
#include "../Public/ShellWindows.h"

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result_macros.h>

#include <objbase.h>
#include <ShlObj_core.h>
#include <Shlwapi.h>

#include <array>

std::optional<std::filesystem::path> ExplorerFolderResolver::Resolve(HWND frameWindow, GUITHREADINFO const& threadInfo)
{
    ShellWindows shellWindows;
    auto const count = shellWindows.Count();

    std::optional<std::filesystem::path> locationFallback;
    for (long index = 0; index < count; ++index)
    {
        VARIANT itemIndex{};
        itemIndex.vt = VT_I4;
        itemIndex.lVal = index;

        auto browser = shellWindows.Item(itemIndex);
        if (browser.HWND() != frameWindow)
        {
            continue;
        }

        if (!locationFallback)
        {
            locationFallback = GetFolderFromLocation(browser.Get());
        }

        wil::com_ptr<IServiceProvider> serviceProvider;
        wil::com_ptr<IShellBrowser> shellBrowser;
        wil::com_ptr<IShellView> shellView;
        if (FAILED(browser.Get()->QueryInterface(IID_PPV_ARGS(serviceProvider.put()))) ||
            FAILED(serviceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(shellBrowser.put()))) ||
            FAILED(shellBrowser->QueryActiveShellView(shellView.put())))
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
            if (auto folder = GetFolderFromView(shellView.get()))
            {
                return folder;
            }
        }
    }

    return locationFallback;
}

std::optional<std::filesystem::path> ExplorerFolderResolver::GetFolderFromView(IShellView* shellView)
{
    wil::com_ptr<IFolderView> folderView;
    THROW_IF_FAILED(shellView->QueryInterface(IID_PPV_ARGS(folderView.put())));

    wil::com_ptr<IPersistFolder2> persistFolder;
    THROW_IF_FAILED(folderView->GetFolder(IID_PPV_ARGS(persistFolder.put())));

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

std::optional<std::filesystem::path> ExplorerFolderResolver::GetFolderFromLocation(IWebBrowser2* browser)
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
