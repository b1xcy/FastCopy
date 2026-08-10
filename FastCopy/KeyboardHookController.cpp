#include "pch.h"
#include "KeyboardHookController.h"
#include "../Public/KeyboardHookSettings.h"

#include <filesystem>

bool KeyboardHookController::IsEnabled()
{
    return KeyboardHookSettings::IsEnabled();
}

void KeyboardHookController::SetEnabled(bool enabled)
{
    if (!KeyboardHookSettings::SetEnabled(enabled))
    {
        return;
    }

    enabled ? Start() : Stop();
}

void KeyboardHookController::Start()
{
    if (!IsEnabled())
    {
        return;
    }

    std::wstring modulePath(32768, L'\0');
    auto const length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length == modulePath.size())
    {
        return;
    }
    modulePath.resize(length);

    auto const hookPath = std::filesystem::path{ modulePath }.parent_path() / L"FastCopyKeyboardHook.exe";
    if (!std::filesystem::exists(hookPath))
    {
        return;
    }

    STARTUPINFOW startupInfo{ sizeof(startupInfo) };
    PROCESS_INFORMATION processInfo{};
    if (CreateProcessW(
        hookPath.c_str(),
        nullptr,
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        hookPath.parent_path().c_str(),
        &startupInfo,
        &processInfo))
    {
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }
}

void KeyboardHookController::Stop()
{
    auto const window = FindWindowW(KeyboardHookSettings::WindowClassName, nullptr);
    if (window)
    {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
}
