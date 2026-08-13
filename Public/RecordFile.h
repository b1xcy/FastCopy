#pragma once
#include <cstdio>
#include <string_view>

namespace RecordFile
{
	// Writes one path in FastCopy's record-file format: a size_t length followed
	// by that many wide characters, with '\' normalized to '/'.
	// Throws wil::ResultException if the write fails.
	void WriteEntry(FILE* file, std::wstring_view path);
}
