#pragma once
#include <objbase.h>
#include <ExDisp.h>
#include <ShlObj_core.h>

#include <filesystem>
#include <optional>

// Resolves the folder of the active Explorer tab through the shared
// ShellWindows/WebBrowser2 wrappers. Enumeration failures throw
// (wil::ResultException) so callers catch them at the outer boundary;
// individual entries that are not Explorer tabs are skipped, and a window
// whose active view does not support the older IFolderView interfaces
// falls back to the browser location URL.
class ExplorerFolderResolver
{
public:
    std::optional<std::filesystem::path> Resolve(HWND frameWindow, GUITHREADINFO const& threadInfo);

private:
    static std::optional<std::filesystem::path> GetFolderFromView(IShellView* shellView);
    static std::optional<std::filesystem::path> GetFolderFromLocation(IWebBrowser2* browser);
};
