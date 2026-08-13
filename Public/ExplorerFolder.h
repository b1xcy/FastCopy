#pragma once
#include <Windows.h>

#include <optional>
#include <string>

// Resolves the folder currently shown by the active tab of the foreground
// Explorer window (walking up through popup menus and owned/child windows).
// Returns nullopt when the foreground is not an Explorer window or the
// desktop (benign). Throws wil::ResultException on failure.
[[nodiscard]] std::optional<std::wstring> GetForegroundExplorerFolder();

// Resolves the folder shown by the active tab of the given Explorer frame
// window. Returns nullopt when no matching shell window is found (benign).
// Throws wil::ResultException on failure.
[[nodiscard]] std::optional<std::wstring> ResolveExplorerFolder(HWND frameWindow);

// Whether the window is the desktop (Progman or WorkerW). The desktop is a
// valid paste target even though it is not an Explorer frame window.
[[nodiscard]] bool IsDesktopWindow(HWND window);

// The user's desktop folder. Throws wil::ResultException on failure.
[[nodiscard]] std::wstring GetDesktopFolder();
