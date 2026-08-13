#include "FastCopyLauncher.h"

#include <ShlObj_core.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <format>
#include <string>

namespace
{
    std::optional<std::filesystem::path> GetRecordDirectory()
    {
        PWSTR localAppData{};
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData)))
        {
            return std::nullopt;
        }

        auto const result = std::filesystem::path{ localAppData } / L"RoboCopyEx" / L"Records";
        CoTaskMemFree(localAppData);

        std::error_code error;
        std::filesystem::create_directories(result, error);
        return error ? std::nullopt : std::optional{ result };
    }

    std::optional<std::filesystem::path> WriteRecordFile(ClipboardFileTransfer const& transfer)
    {
        auto const directory = GetRecordDirectory();
        if (!directory)
        {
            return std::nullopt;
        }

        FILETIME timestamp{};
        GetSystemTimePreciseAsFileTime(&timestamp);
        ULARGE_INTEGER timestampValue{};
        timestampValue.LowPart = timestamp.dwLowDateTime;
        timestampValue.HighPart = timestamp.dwHighDateTime;
        static std::atomic_uint sequence{};

        auto const path = *directory / std::format(
            L"{}{}-{}-{}.bin",
            transfer.move ? L'M' : L'C',
            timestampValue.QuadPart,
            GetCurrentProcessId(),
            sequence.fetch_add(1));

        FILE* file{};
        if (_wfopen_s(&file, path.c_str(), L"wb") != 0)
        {
            return std::nullopt;
        }

        bool succeeded = true;
        for (auto const& source : transfer.paths)
        {
            std::wstring normalized{ source.get() };
            std::ranges::replace(normalized, L'\\', L'/');
            auto const length = normalized.size();
            succeeded = fwrite(&length, sizeof(length), 1, file) == 1 &&
                fwrite(normalized.data(), sizeof(wchar_t), length, file) == length;
            if (!succeeded)
            {
                break;
            }
        }
        succeeded = fclose(file) == 0 && succeeded;

        if (!succeeded)
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            return std::nullopt;
        }

        return path;
    }

}

bool LaunchFastCopy(ClipboardFileTransfer const& transfer, std::filesystem::path const& destination)
{
    auto const recordPath = WriteRecordFile(transfer);
    if (!recordPath)
    {
        return false;
    }

    auto destinationText = destination.wstring();
    auto recordText = recordPath->wstring();
    std::ranges::replace(destinationText, L'\\', L'/');
    std::ranges::replace(recordText, L'\\', L'/');

    auto const uri = std::format(LR"(fastcopy://"{}"|"{}")", destinationText, recordText);
    AllowSetForegroundWindow(ASFW_ANY);
    auto const launchResult = reinterpret_cast<INT_PTR>(ShellExecuteW(
        nullptr,
        L"open",
        uri.c_str(),
        nullptr,
        nullptr,
        SW_SHOWNORMAL));
    if (launchResult <= 32)
    {
        std::error_code error;
        std::filesystem::remove(*recordPath, error);
        return false;
    }

    return true;
}
