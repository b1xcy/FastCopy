#pragma once

#include <Windows.h>

namespace KeyboardHookSettings
{
    constexpr wchar_t RegistryPath[] = L"Software\\RoboCopyEx";
    constexpr wchar_t EnabledValueName[] = L"KeyboardHookEnabled";
    constexpr wchar_t WindowClassName[] = L"RoboCopyEx.KeyboardHook.Window";
    constexpr wchar_t SingletonName[] = L"Local\\RoboCopyEx.KeyboardHook.Singleton";

    inline bool IsEnabled()
    {
        DWORD value = 0;
        DWORD valueSize = sizeof(value);
        auto const result = RegGetValueW(
            HKEY_CURRENT_USER,
            RegistryPath,
            EnabledValueName,
            RRF_RT_REG_DWORD,
            nullptr,
            &value,
            &valueSize);
        if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
        {
            return true;
        }
        return result == ERROR_SUCCESS && value != 0;
    }

    inline bool SetEnabled(bool enabled)
    {
        HKEY key{};
        if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            RegistryPath,
            0,
            nullptr,
            0,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS)
        {
            return false;
        }

        DWORD const value = enabled ? 1 : 0;
        auto const result = RegSetValueExW(
            key,
            EnabledValueName,
            0,
            REG_DWORD,
            reinterpret_cast<BYTE const*>(&value),
            sizeof(value));
        RegCloseKey(key);
        return result == ERROR_SUCCESS;
    }
}
