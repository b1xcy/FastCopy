#include "ExplorerFolder.h"

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result_macros.h>

#include <ExDisp.h>
#include <ShlObj_core.h>
#include <Shlwapi.h>

#include <cwchar>

namespace
{
	constexpr wchar_t ExplorerFrameClass[] = L"CabinetWClass";
	constexpr wchar_t ExplorerLegacyFrameClass[] = L"ExploreWClass";
	constexpr wchar_t DesktopWindowClass[] = L"Progman";
	constexpr wchar_t DesktopWorkerClass[] = L"WorkerW";

	bool IsExplorerFrame(HWND window)
	{
		wchar_t className[256]{};
		if (!GetClassNameW(window, className, ARRAYSIZE(className)))
		{
			return false;
		}
		return std::wcscmp(className, ExplorerFrameClass) == 0 ||
			std::wcscmp(className, ExplorerLegacyFrameClass) == 0;
	}

	// Whether `candidate` is the given window itself or one of its children
	// (e.g. the shell view window of the active tab).
	bool IsChildOrSelf(HWND window, HWND candidate)
	{
		return candidate && (candidate == window || IsChild(window, candidate));
	}

	// Whether `candidate` is a popup (e.g. the open context menu, class #32768)
	// owned by the given window; the keyboard focus sits on the menu while it
	// is open, so the menu's owner identifies the Explorer window it belongs to.
	bool IsOwnedByFrame(HWND frame, HWND candidate)
	{
		for (auto current = candidate; current; current = GetWindow(current, GW_OWNER))
		{
			if (current == frame)
			{
				return true;
			}
		}
		return false;
	}

	std::optional<std::wstring> GetFolderFromView(IShellView* shellView)
	{
		wil::com_ptr<IFolderView> folderView;
		THROW_IF_FAILED(shellView->QueryInterface(IID_PPV_ARGS(folderView.put())));

		wil::com_ptr<IPersistFolder2> persistFolder;
		THROW_IF_FAILED(folderView->GetFolder(IID_PPV_ARGS(persistFolder.put())));

		wil::unique_cotaskmem_ptr<ITEMIDLIST> pidl;
		THROW_IF_FAILED(persistFolder->GetCurFolder(wil::out_param(pidl)));

		wchar_t path[MAX_PATH]{};
		if (!SHGetPathFromIDListEx(pidl.get(), path, MAX_PATH, GPFIDL_DEFAULT))
		{
			// Virtual folder (This PC, libraries, ...) without a filesystem path.
			return std::nullopt;
		}
		return std::wstring{ path };
	}

	std::optional<std::wstring> GetFolderFromLocation(IWebBrowser2* browser)
	{
		wil::unique_bstr location;
		if (FAILED(browser->get_LocationURL(location.put())) || !location)
		{
			return std::nullopt;
		}

		wchar_t path[MAX_PATH]{};
		DWORD length = MAX_PATH;
		if (FAILED(PathCreateFromUrlW(location.get(), path, &length, 0)))
		{
			// Not a file:// URL (e.g. This PC, libraries, search results).
			return std::nullopt;
		}
		return std::wstring{ path };
	}
}

bool IsDesktopWindow(HWND window)
{
	wchar_t className[256]{};
	if (!GetClassNameW(window, className, ARRAYSIZE(className)))
	{
		return false;
	}
	return std::wcscmp(className, DesktopWindowClass) == 0 ||
		std::wcscmp(className, DesktopWorkerClass) == 0;
}

std::wstring GetDesktopFolder()
{
	wil::unique_cotaskmem_string path;
	THROW_IF_FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path));
	return std::wstring{ path.get() };
}

std::optional<std::wstring> GetForegroundExplorerFolder()
{
	// The foreground window may be the Explorer frame itself, a popup menu
	// (owned by the frame) or a child window; walk up until we reach the frame.
	// The desktop is a paste target of its own and needs no window resolution.
	auto window = GetForegroundWindow();
	for (int depth = 0; window && depth < 16; ++depth)
	{
		if (IsDesktopWindow(window))
		{
			return GetDesktopFolder();
		}

		if (IsExplorerFrame(window))
		{
			return ResolveExplorerFolder(window);
		}

		if (auto const owner = GetWindow(window, GW_OWNER))
		{
			window = owner;
		}
		else
		{
			window = GetParent(window);
		}
	}
	return std::nullopt;
}

std::optional<std::wstring> ResolveExplorerFolder(HWND frameWindow)
{
	wil::com_ptr<IShellWindows> shellWindows;
	THROW_IF_FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(shellWindows.put())));

	long count{};
	THROW_IF_FAILED(shellWindows->get_Count(&count));

	// The keyboard focus lives on the Explorer thread; while the classic
	// context menu is open it sits on the menu instead of on the shell view.
	GUITHREADINFO threadInfo{ sizeof(threadInfo) };
	auto const frameThread = GetWindowThreadProcessId(frameWindow, nullptr);
	auto const hasThreadInfo = GetGUIThreadInfo(frameThread, &threadInfo) != FALSE;

	std::optional<std::wstring> locationFallback;
	for (long index = 0; index < count; ++index)
	{
		VARIANT itemIndex{};
		itemIndex.vt = VT_I4;
		itemIndex.lVal = index;

		wil::com_ptr<IDispatch> dispatch;
		THROW_IF_FAILED(shellWindows->Item(itemIndex, &dispatch));
		if (!dispatch)
		{
			continue;
		}

		wil::com_ptr<IWebBrowser2> browser;
		if (FAILED(dispatch->QueryInterface(IID_PPV_ARGS(browser.put()))))
		{
			continue;
		}

		SHANDLE_PTR browserWindow{};
		THROW_IF_FAILED(browser->get_HWND(&browserWindow));
		VARIANT_BOOL visible{};
		THROW_IF_FAILED(browser->get_Visible(&visible));
		if (reinterpret_cast<HWND>(browserWindow) != frameWindow || !visible)
		{
			continue;
		}

		// Remember the location of the first matching tab as a last resort:
		// tabbed Explorer versions expose a browser location even when the
		// active shell view does not support the older IFolderView interfaces.
		if (!locationFallback)
		{
			locationFallback = GetFolderFromLocation(browser.get());
		}

		wil::com_ptr<IServiceProvider> serviceProvider;
		wil::com_ptr<IShellBrowser> shellBrowser;
		wil::com_ptr<IShellView> shellView;
		if (FAILED(browser->QueryInterface(IID_PPV_ARGS(serviceProvider.put()))) ||
			FAILED(serviceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(shellBrowser.put()))) ||
			FAILED(shellBrowser->QueryActiveShellView(shellView.put())))
		{
			// The entry is not an Explorer tab (e.g. an old browser window);
			// skip it instead of failing the whole resolution.
			continue;
		}

		HWND viewWindow{};
		if (FAILED(shellView->GetWindow(&viewWindow)))
		{
			continue;
		}

		// This is the active tab only when the keyboard focus is on its view
		// (or one of its children), or on a popup menu owned by its frame.
		if (!hasThreadInfo ||
			(!IsChildOrSelf(viewWindow, threadInfo.hwndFocus) &&
				!IsOwnedByFrame(frameWindow, threadInfo.hwndFocus)))
		{
			continue;
		}

		if (auto folder = GetFolderFromView(shellView.get()))
		{
			return folder;
		}
	}

	return locationFallback;
}
