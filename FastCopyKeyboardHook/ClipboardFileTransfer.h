#pragma once

#include <optional>
#include <string>
#include <vector>

struct ClipboardFileTransfer
{
    std::vector<std::wstring> paths;
    bool move{};

    static std::optional<ClipboardFileTransfer> Read();
};
