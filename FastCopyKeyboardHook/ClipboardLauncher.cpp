#include "ClipboardLauncher.h"
#include "ClipboardFileTransfer.h"
#include "../Public/FastCopyLauncher.h"
#include "../Public/RecordFile.h"

#include <objbase.h> // must precede wil/resource.h: the string wrappers are only defined once _OBJBASE_H_ is set
#include <wil/resource.h>
#include <wil/result_macros.h>

#include <ShlObj_core.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <format>

namespace
{
    std::filesystem::path GetRecordDirectory()
    {
        wil::unique_cotaskmem_string localAppData;
        THROW_IF_FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData));

        auto const result = std::filesystem::path{ localAppData.get() } / L"RoboCopyEx" / L"Records";
        std::filesystem::create_directories(result);
        return result;
    }

    std::filesystem::path WriteRecordFile(ClipboardFileTransfer const& transfer)
    {
        auto const directory = GetRecordDirectory();

        FILETIME timestamp{};
        GetSystemTimePreciseAsFileTime(&timestamp);
        ULARGE_INTEGER timestampValue{};
        timestampValue.LowPart = timestamp.dwLowDateTime;
        timestampValue.HighPart = timestamp.dwHighDateTime;
        static std::atomic_uint sequence{};

        auto const path = directory / std::format(
            L"{}{}-{}-{}.bin",
            transfer.move ? L'M' : L'C',
            timestampValue.QuadPart,
            GetCurrentProcessId(),
            sequence.fetch_add(1));

        FILE* file{};
        if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file)
        {
            THROW_HR(HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));
        }

        auto closeFile = wil::scope_exit([&] { if (file) { fclose(file); } });
        for (auto const& source : transfer.paths)
        {
            RecordFile::WriteEntry(file, source);
        }
        closeFile.release();
        if (fclose(file) != 0)
        {
            THROW_HR(HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));
        }

        return path;
    }
}

bool LaunchFastCopy(ClipboardFileTransfer const& transfer, std::filesystem::path const& destination)
{
    auto const recordPath = WriteRecordFile(transfer);

    try
    {
        ::LaunchFastCopy(destination.wstring(), recordPath.wstring());
        return true;
    }
    catch (...)
    {
        // Never leave a half-consumed record file behind when launching fails.
        std::error_code error;
        std::filesystem::remove(recordPath, error);
        throw;
    }
}
