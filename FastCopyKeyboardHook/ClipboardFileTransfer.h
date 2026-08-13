#pragma once

#include <objbase.h>

#include <wil/resource.h>

#include <optional>
#include <vector>

struct ClipboardFileTransfer
{
    // Shell item display names are CoTaskMem strings; keep them as-is instead
    // of copying into std::wstring (wil::unique_cotaskmem_string owns the memory).
    std::vector<wil::unique_cotaskmem_string> paths;
    bool move{};

    static std::optional<ClipboardFileTransfer> Read();
};
