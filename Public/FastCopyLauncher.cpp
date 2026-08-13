#include "FastCopyLauncher.h"

#include <wil/result_macros.h>

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <format>
#include <string>

#pragma comment(lib, "shell32.lib")

void LaunchFastCopy(std::wstring_view destination, std::wstring_view recordFile)
{
	std::wstring destinationText{ destination };
	std::wstring recordText{ recordFile };
	std::replace(destinationText.begin(), destinationText.end(), L'\\', L'/');
	std::replace(recordText.begin(), recordText.end(), L'\\', L'/');

	auto const cmd = std::format(LR"(fastcopy://"{}"|"{}")", destinationText, recordText);
#if (defined _DEBUG) || (defined DEBUG)
	OutputDebugString(cmd.data());
#endif

	AllowSetForegroundWindow(ASFW_ANY);
	auto const result = reinterpret_cast<INT_PTR>(ShellExecuteW(
		nullptr,
		L"open",
		cmd.c_str(),
		nullptr,
		nullptr,
		SW_SHOWNORMAL));
	if (result <= 32)
	{
		THROW_HR(HRESULT_FROM_WIN32(GetLastError()));
	}
}
