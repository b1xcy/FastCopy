#include "RecordFile.h"

#include <wil/result_macros.h>

#include <algorithm>
#include <string>

namespace RecordFile
{
	void WriteEntry(FILE* file, std::wstring_view path)
	{
		std::wstring normalized{ path };
		std::replace(normalized.begin(), normalized.end(), L'\\', L'/');

		auto const length = normalized.size();
		if (fwrite(&length, sizeof(length), 1, file) != 1 ||
			fwrite(normalized.data(), sizeof(wchar_t), length, file) != length)
		{
			THROW_HR(HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));
		}
	}
}
